#include "zplayer.h"
#include "zprefs.h"
#include <algorithm>
#include <sstream>
#include "zmenu_theme.h"

int ZPlayer::MatchActionAt(int px,int py) const
{
    const int count=2;
    const int width=std::min(180,(init_w-32)/count);
    for(int slot=0;slot<count;++slot) {
        SDL_Rect r{(init_w-count*(width+8)+8)/2+slot*(width+8),init_h-52,width,30};
        if(px>=r.x && px<r.x+r.w && py>=r.y && py<r.y+r.h)
            return hosted_setup_available ? slot+1 : slot;
    }
    return -1;
}

void ZPlayer::ChooseMatchAction(int action)
{
    if(!show_match_summary || summary_continue_sent) return;
    if(action==0) { SendContinueAfterEnd(); return; }
    if(action==1 && !hosted_setup_available) {
        // Use the existing server-authorized reset/vote path for campaign,
        // legacy generated games, and matches hosted by somebody else.
        if(client_socket.SendMessage(RESET_MAP,nullptr,0))
            AddNewsEntry("Rematch requested. Server approval or a vote may be required (F1 yes / F2 no).");
        else AddNewsEntry("Could not request rematch: server disconnected. Continue or reconnect.");
        return;
    }
    if(!hosted_setup_available || (action!=1 && action!=2)) return;
    // Starting a fresh hosted match uses a snapshot of the setup that actually
    // created this session, rather than preferences edited since it started.
    const bool setup=hosted_setup_available;
    const auto recipe=hosted_setup;
    ReturnToMenu();
    if(setup) {
        client_socket.ClearConnection();
        CloseAllMenus();
        auto *menu=new GMMMultiplayerCreate(&recipe,action==1);
        AttachMainMenu(menu);
    }
}

bool ZPlayer::IsIdleUnit(ZObject *object) const
{
    if(!object || our_team<=NULL_TEAM || object->GetOwner()!=our_team
       || object->IsDestroyed() || object->IsMinion() || !object->CanMove()
       || !object->GetWayPointList().empty() || !object->GetWayPointDevList().empty()
       || object->GetAttackObject() || object->IsMoving()) return false;
    unsigned char type,id; object->GetObjectID(type,id);
    return type==ROBOT_OBJECT || type==VEHICLE_OBJECT;
}

void ZPlayer::SelectNextIdleUnit()
{
    ZObject *first=nullptr, *next=nullptr;
    for(auto *object:object_list) if(IsIdleUnit(object)) {
        int ref=object->GetRefID();
        if(!first || ref<first->GetRefID()) first=object;
        if(ref>last_idle_ref && (!next || ref<next->GetRefID())) next=object;
    }
    if(!next) next=first;
    if(!next) { AddNewsEntry("No idle mobile units"); return; }
    last_idle_ref=next->GetRefID();
    select_info.Clear(); select_info.selected_list.push_back(next);
    select_info.SetupGroupDetails(); DetermineCursor(); GiveHudSelected();
    int x,y; next->GetCenterCords(x,y); FocusCameraTo(x,y);
}

void ZPlayer::RenderSelectionReadout()
{
    if(show_match_summary || summary_pending || our_team==NULL_TEAM) return;
    int robots=0,vehicles=0,cannons=0,idle=0,factories=0;
    for(auto *object:object_list)if(object->GetOwner()==our_team&&object->ProducesUnits()&&!object->IsDestroyed()&&(object->GetBuildState()==BUILDING_SELECT||object->GetBuildState()==BUILDING_PAUSED))++factories;
    for(auto *object:object_list) if(IsIdleUnit(object)) ++idle;
    for(auto *object:select_info.selected_list) {
        if(!object || object->IsDestroyed()) continue;
        unsigned char type,id; object->GetObjectID(type,id);
        robots+=type==ROBOT_OBJECT; vehicles+=type==VEHICLE_OBJECT; cannons+=type==CANNON_OBJECT;
    }
    int total=robots+vehicles+cannons;
    string text=to_string(total)+" selected";
    if(total) text+=": "+to_string(robots)+" infantry, "+to_string(vehicles)+" vehicles, "+to_string(cannons)+" guns";
    text+=" | Idle: "+to_string(idle)+" ["+ZPrefs_KeyLabel(zod_keys[ZKEY_IDLE_UNIT])+"]";
    text+=" / Factories idle: "+to_string(factories)+" ["+ZPrefs_KeyLabel(zod_keys[ZKEY_IDLE_FACTORY])+"]";if(unit_limit_reached[our_team])text+=" / UNIT LIMIT";
    if(text!=selection_readout_text) {
        selection_readout_text=text;
        selection_readout_img=ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(text.c_str());
    }
    if(auto *s=selection_readout_img.GetBaseSurface()) {
        SDL_Rect background{6,6,s->w+12,s->h+8};
        ZSDL_FillSurfaceRect(&background,18,24,30);
        selection_readout_img.BlitSurface(NULL,12,10);
    }
}

void ZPlayer::lobby_countdown_event(ZPlayer *p,char *data,int size,int)
{
    if(size!=sizeof(int) || !data) return;
    int seconds; memcpy(&seconds,data,sizeof(seconds));
    if(seconds< -1 || seconds>3600) return;
    p->lobby_countdown=seconds;
}

void ZPlayer::cloud_required_event(ZPlayer *p,char *data,int size,int)
{
    int required=0;if(size!=sizeof(required)||!data)return;memcpy(&required,data,size);if(required!=0 && required!=1)return;
    p->managed_dedicated=true;if(required==0)return;
    const char *ticket=getenv("ZOD_JOIN_TICKET");
    if(ticket && strlen(ticket)==64) p->client_socket.SendMessage(CLOUD_TICKET,ticket,65);
    else {
        p->cloud_disconnect_reason="Cloud account required. Get a join ticket from the Zod website.";
        p->AddNewsEntry(p->cloud_disconnect_reason);
        for(auto *menu:p->gui_menu_list) if(auto *lobby=dynamic_cast<GMMLobby*>(menu)) lobby->SetError("Cloud login required. Get a join ticket from the Zod website.");
    }
}
void ZPlayer::cloud_result_event(ZPlayer *p,char *data,int size,int)
{
    int success=0;if(size!=sizeof(success)||!data)return;memcpy(&success,data,size);
    if(success==1) p->ProcessConnect();
    else {
        p->cloud_disconnect_reason="Cloud login failed. Get a fresh ticket from the Zod website.";
        p->AddNewsEntry(p->cloud_disconnect_reason);
        for(auto *menu:p->gui_menu_list) if(auto *lobby=dynamic_cast<GMMLobby*>(menu)) lobby->SetError(p->cloud_disconnect_reason);
    }
}

void ZPlayer::ReceiveObjective(const string &packet){
 std::istringstream input(packet.substr(11));string value;vector<string> fields;for(int i=0;i<5&&std::getline(input,value,'|');++i)fields.push_back(value);if(fields.size()!=5)return;
 auto number=[](const string&s,int &n){if(s.empty()||s.size()>10)return false;char *end=nullptr;long value=strtol(s.c_str(),&end,10);if(!end||*end||value< -1||value>1000000)return false;n=value;return true;};int step,total,time,x,y;
 if(!number(fields[0],step)||!number(fields[1],total)||!number(fields[2],time)||!number(fields[3],x)||!number(fields[4],y)||total>64||step<0||step>total)return;
 string text;std::getline(input,text);if(text.size()>512)return;if(objective_total&&step>objective_step){AddNewsEntry("Objective complete: "+objective_text);BattleFeedback("Objective complete: "+objective_text);}objective_step=step;objective_total=total;objective_remaining=time;objective_x=x;objective_y=y;objective_text=text;objective_received=ztime.ztime;
}
void ZPlayer::RenderObjectiveHUD(){
 if(!objective_total||objective_text.empty())return;auto *surface=ZSDL_Surface::GetMainSoftwareSurface();if(!surface)return;
 int remaining=objective_remaining<0?-1:std::max(0,objective_remaining-(int)std::max(0.0,ztime.ztime-objective_received));string text="Objective "+to_string(objective_step)+"/"+to_string(objective_total);if(remaining>=0)text+=" / "+to_string(remaining)+"s";text+=": "+objective_text;
 ZMenuTheme::panel(surface,8,46,std::min(560,init_w-120),34);ZMenuTheme::queueText(text.c_str(),18,56,12,{244,184,85,255},std::min(536,init_w-144));
 if(objective_x>=0&&objective_y>=0){int sx,sy;zmap.GetViewShift(sx,sy);int x=objective_x-sx,y=objective_y-sy;if(x>=0&&y>=0&&x<init_w-100&&y<init_h-36){ZMenuTheme::rect(surface,x-6,y-6,14,2,255,195,60);ZMenuTheme::rect(surface,x-6,y+6,14,2,255,195,60);ZMenuTheme::rect(surface,x-6,y-6,2,14,255,195,60);ZMenuTheme::rect(surface,x+6,y-6,2,14,255,195,60);}}
}

void ZPlayer::BattleFeedback(const string &text){if(text==battle_notice&&SDL_GetTicks()<battle_notice_until)return;battle_notice=text.substr(0,80);battle_notice_until=SDL_GetTicks()+6000;}
void ZPlayer::RenderBattleFeedback(){
    auto *surface=ZSDL_Surface::GetMainSoftwareSurface();if(!surface)return;
    if(!battle_notice.empty()&&SDL_GetTicks()<battle_notice_until){int width=std::min(560,init_w-120);ZMenuTheme::panel(surface,8,86,width,34);ZMenuTheme::queueText(battle_notice.c_str(),18,96,12,{255,215,130,255},width-20);}
    if(zod_team_markers){int sx,sy;zmap.GetViewShift(sx,sy);int count=0;for(auto *object:object_list){if(object->IsDestroyed()||object->IsMinion()||object->GetOwner()==NULL_TEAM)continue;int x,y;object->GetCenterCords(x,y);x-=sx;y-=sy;if(x<0||y<0||x>=init_w-100||y>=init_h-36)continue;string marker="T"+to_string(object->GetOwner());ZMenuTheme::rect(surface,x-8,y-24,22,12,10,17,23);ZMenuTheme::queueText(marker.c_str(),x-6,y-24,10,{255,255,255,255},20);if(++count>=100)break;}}
}
void ZPlayer::SelectNextIdleFactory(){ZObject *first=nullptr,*next=nullptr;for(auto *object:object_list){if(object->GetOwner()!=our_team||!object->ProducesUnits()||object->IsDestroyed()||(object->GetBuildState()!=BUILDING_SELECT&&object->GetBuildState()!=BUILDING_PAUSED))continue;if(!first||object->GetRefID()<first->GetRefID())first=object;if(object->GetRefID()>last_factory_ref&&(!next||object->GetRefID()<next->GetRefID()))next=object;}if(!next)next=first;if(!next){BattleFeedback("All factories are producing or ready to deploy.");return;}last_factory_ref=next->GetRefID();int x,y;next->GetCenterCords(x,y);FocusCameraTo(x,y);BattleFeedback(unit_limit_reached[our_team]?"Production blocked: unit limit reached. Free a unit slot.":"Idle factory selected: choose production or resume its queue.");select_info.Clear();select_info.selected_list.push_back(next);select_info.SetupGroupDetails();GiveHudSelected();}
