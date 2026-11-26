#include "gmm_lobby.h"
#include <algorithm>

namespace {
string clip(const string &text,size_t length) { return text.size()>length ? text.substr(0,length-3)+"..." : text; }
string clean(string text) { for(char &c:text) if((unsigned char)c<32 || c==127) c=' '; return text; }
}
GMMLobby::GMMLobby()
{
    menu_type=GMM_LOBBY; title="Match lobby"; w=590; h=336;
    opened_at=SDL_GetTicks(); SetupLayout1();
}
void GMMLobby::SetupLayout1()
{
    auto label=[&](GMMWLabel &l,int y) { l.SetCoords(10,y); AddWidget(&l); };
    label(session_label,25); label(map_label,42); label(status_label,61);
    player_list.SetCoords(10,80); player_list.SetDimensions(400,130); player_list.SetVisibleEntries(8); AddWidget(&player_list);
    auto button=[&](GMMWButton &b,int x,int y,const string &text) {
        b.SetType(MMGENERIC_BUTTON); b.SetCoords(x,y); b.SetDimensions(196,20); b.SetText(text); AddWidget(&b);
    };
    button(team_button,10,216,"Choose next open team"); button(bots_button,214,216,"Bot changes (vote)");
    button(ready_button,10,240,"Waiting for assignment..."); button(leave_button,214,240,"Leave lobby");
    button(auto_button,10,264,"Auto assign open team");button(spectate_button,214,264,"Watch as spectator");
    label(hint_label,291); label(error_label,308);
    preview_label.SetCoords(424,200);AddWidget(&preview_label);
    session_label.SetText("Connecting to server..."); status_label.SetText("Loading map and players...");
    UpdateDimensions();
}
void GMMLobby::SetSession(const string &address,const string &map,const vector<int> &teams,bool loaded)
{
    if(map!=preview_name||!loaded){preview_image.Unload();preview_loaded=false;preview_name=map;}
    session_label.SetText("Server: "+clip(clean(address),65));
    map_label.SetText("Map: "+clip(clean(map.empty() ? "loading..." : map),68));
    fort_teams=teams; world_loaded=loaded;
}
void GMMLobby::SetError(const string &message)
{
    sticky_error=true; ready_pending=false;
    error_label.SetText(clip(clean(message),73));
}
p_info *GMMLobby::LocalPlayer()
{
    if(player_info && local_p_id) for(auto &p:*player_info) if(p.p_id==*local_p_id) return &p;
    return nullptr;
}
bool GMMLobby::Playable(const p_info &p) const
{
    return p.team>NULL_TEAM && p.team<MAX_TEAM_TYPES && std::find(fort_teams.begin(),fort_teams.end(),p.team)!=fort_teams.end();
}
void GMMLobby::RebuildPlayers()
{
    auto &rows=player_list.GetEntryList(); rows.clear();
    int humans=0,ready=0,bots=0;string waiting;
    if(previous_countdown>0&&countdown<0)cancelled_at=SDL_GetTicks();previous_countdown=countdown;
    for(int team:fort_teams) {
        bool occupied=false;
        if(player_info) for(const auto &p:*player_info) if(p.team==team && (p.mode==PLAYER_MODE || (p.mode==BOT_MODE && !p.ignored))) {
            string name=clip(clean(p.name.empty() ? "Unnamed player" : p.name),32);
            bool me=local_p_id && p.p_id==*local_p_id;
            string state=p.mode==BOT_MODE ? "BOT" : p.world_sync==0 ? "RECEIVING MAP" : p.ready ? "READY" : "NOT READY";
            rows.push_back(mmlist_entry(team_type_string[team]+" | "+name+(me ? " (you)" : "")+" | "+state,p.p_id,team));
            occupied=true;
            if(p.mode==PLAYER_MODE) { ++humans; ready+=p.ready;if(!p.ready){if(!waiting.empty())waiting+=", ";waiting+=clip(name,16);} } else ++bots;
        }
        if(!occupied) rows.push_back(mmlist_entry(team_type_string[team]+" | Open - waiting for player",-team,team));
    }
    if(player_info) for(const auto &p:*player_info) if(p.mode==PLAYER_MODE && !Playable(p))
        rows.push_back(mmlist_entry("Spectator | "+clip(clean(p.name),32)+(local_p_id && p.p_id==*local_p_id ? " (you)" : ""),p.p_id,NULL_TEAM));
    auto *me=LocalPlayer();
    if(ready_pending && me && me->ready==requested_ready) ready_pending=false;
    if(ready_pending && SDL_GetTicks()-ready_sent_at>5000) SetError("Ready change was not confirmed. Check connection and retry.");
    bool eligible=world_loaded && me && me->mode==PLAYER_MODE && Playable(*me);
    ready_button.SetText(!eligible ? "Waiting for playable team..." : ready_pending ? "Confirming ready state..." : me->ready ? (countdown>0?"Cancel countdown / Not ready":"Not ready") : "I'm ready");
    ready_button.SetGreen(eligible && me->ready);
    team_button.SetText(eligible ? string("You: ")+team_type_string[me->team]+" - change team" : "Choose next open team");
    if(!world_loaded || !me) status_label.SetText(transfer_progress.empty()?"Loading map and player assignment...":transfer_progress);
    else if(countdown>0) status_label.SetText("Starting in "+to_string(countdown)+"s - empty teams become bots");
    else if(countdown==0) status_label.SetText("All ready - starting match...");
    else if(humans<2 && !bots) status_label.SetText("Waiting for an opponent - invite a player or add a bot");
    else status_label.SetText("Waiting for ready: "+clip(waiting,45)+" ("+to_string(ready)+"/"+to_string(humans)+")");
    if(cancelled_at&&SDL_GetTicks()-cancelled_at<5000&&countdown<0)status_label.SetText("Countdown cancelled: a player or team is no longer ready.");
    hint_label.SetText(eligible ? "Not ready cancels countdown. Bot changes use a player vote." : "Spectators do not hold a team or block match startup.");
    if(public_server){bots_button.SetText((server_role==1||server_role==2)?"Admin: manage bots":"Bots: server admins only");hint_label.SetText(server_role==2?"Server owner: manage admins in My servers; match changes apply directly.":server_role==1?"Server administrator: match controls apply directly.":"Server owners/admins manage match settings. Players control teams and ready state.");}
    player_list.CheckViewI();
}
void GMMLobby::Process()
{
    if(ztime && world_loaded) {
        if(ztime->IsPaused()) saw_paused=true;
        else if(saw_paused || pause_confirmed) { killme=true; return; }
    }
    if((!world_loaded || !LocalPlayer()) && !sticky_error && SDL_GetTicks()-opened_at>10000)
        SetError("Map or player assignment did not arrive. Leave and retry.");
    RebuildPlayers(); ProcessWidgets();
}
void GMMLobby::HandleWidgetEvent(int type,ZGMMWidget *widget)
{
    if(type!=GMM_UNCLICK_EVENT || !widget) return;
    if(widget==&leave_button) { gmm_flags.leave_match=true; return; }
    auto *me=LocalPlayer();
    if(widget==&spectate_button){if(!world_loaded||!me||!ztime||!ztime->IsPaused())return;gmm_flags.change_team=true;gmm_flags.change_team_type=-1;return;}
    if(widget==&auto_button){
        if(!world_loaded||!me||ready_pending)return;
        for(int team:fort_teams){bool taken=false;for(const auto &p:*player_info)if(p.p_id!=me->p_id&&p.team==team&&(p.mode==PLAYER_MODE||(p.mode==BOT_MODE&&!p.ignored)))taken=true;
            if(!taken){gmm_flags.change_team=true;gmm_flags.change_team_type=team;return;}}
        SetError("All teams are occupied. Spectate or wait for a free team.");return;
    }
    if(widget==&ready_button) {
        if(!world_loaded || !me || me->mode!=PLAYER_MODE || !Playable(*me) || ready_pending) return;
        sticky_error=false; error_label.SetText(""); requested_ready=!me->ready;
        ready_pending=true; ready_sent_at=SDL_GetTicks();
        gmm_flags.toggle_ready=true; gmm_flags.ready_value=requested_ready;
    } else if(widget==&team_button) {
        if(!world_loaded || !me || ready_pending) return;
        for(int step=1;step<MAX_TEAM_TYPES;++step) {
            int team=(std::max(0,(int)me->team)+step-1)%(MAX_TEAM_TYPES-1)+1;
            if(std::find(fort_teams.begin(),fort_teams.end(),team)==fort_teams.end()) continue;
            bool taken=false;
            for(const auto &p:*player_info) if(p.p_id!=me->p_id && p.team==team && (p.mode==PLAYER_MODE || (p.mode==BOT_MODE && !p.ignored))) taken=true;
            if(!taken && team!=me->team) { gmm_flags.change_team=true; gmm_flags.change_team_type=team; error_label.SetText(""); return; }
        }
        SetError("No open fort team. A player must leave or a bot must be removed.");
    } else if(widget==&bots_button && world_loaded && (!public_server||server_role==1||server_role==2)) {
        gmm_flags.open_main_menu=true; gmm_flags.open_main_menu_type=GMM_MANAGE_BOTS;
    }
}

bool GMMLobby::UnClick(int px,int py)
{
    bool handled=ZGuiMainMenuBase::UnClick(px,py);
    if(killme) { killme=false; gmm_flags.leave_match=true; }
    return handled;
}

void GMMLobby::RenderContents(ZMap &map,SDL_Surface *dest){
    if(world_loaded&&!preview_loaded){MapPreview preview;if(ZMP_ReadMapDataPreview(map,preview)){auto *surface=SDL_CreateSurface(preview.preview_width,preview.preview_height,SDL_PIXELFORMAT_ARGB8888);if(surface){for(int row=0;row<surface->h;++row)memcpy((char*)surface->pixels+row*surface->pitch,preview.pixels.data()+row*surface->w,surface->w*4);preview_image.LoadBaseImage(surface);}preview_label.SetText(to_string(preview.width)+" x "+to_string(preview.height));preview_loaded=true;}}
    ZGuiMainMenuBase::RenderContents(map,dest);if(preview_image.GetBaseSurface())preview_image.BlitSurface(nullptr,x+424,y+80);
}
