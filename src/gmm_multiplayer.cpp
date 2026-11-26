#include "gmm_multiplayer.h"
#include "zlan.h"
#include "zserver_bookmarks.h"
#include "znetconnect.h"
#include "zasset_import.h"
#include <chrono>
#include <cctype>
#include <algorithm>
namespace {bool hexToken(const string &value,size_t length){return value.size()==length&&std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isxdigit(c);});}}

GMMMultiplayer::GMMMultiplayer()
{
    menu_type=GMM_MULTIPLAYER; title="Multiplayer"; w=420; h=430;
    SetupLayout1(); Refresh();
}
void GMMMultiplayer::SetupLayout1()
{
    auto button=[&](GMMWButton &b,int px,int py,int width,const string &label) {
        b.SetType(MMGENERIC_BUTTON); b.SetCoords(px,py); b.SetDimensions(width,18); b.SetText(label); AddWidget(&b);
    };
    button(mode_button,10,24,190,"Nearby LAN"); button(refresh_button,210,24,200,"Refresh");
    search_entry.SetCoords(10,48);search_entry.SetDimensions(400,GMMWTEXTENTRY_HEIGHT);search_entry.SetLabel("Map/name/mode: ");search_entry.SetMaxText(60);AddWidget(&search_entry);
    region_entry.SetCoords(10,70);region_entry.SetDimensions(400,GMMWTEXTENTRY_HEIGHT);region_entry.SetLabel("Region: ");region_entry.SetMaxText(32);AddWidget(&region_entry);
    button(view_button,10,92,126,"All servers");button(ping_button,144,92,126,"Any ping");button(players_button,278,92,132,"Any players");
    button(state_button,10,114,190,"Any game state");button(favorite_button,210,114,200,"Toggle favorite");
    match_list.SetCoords(10,140); match_list.SetVisibleEntries(7); match_list.SetDimensions(400,105); AddWidget(&match_list);
    status_label.SetCoords(10,254); AddWidget(&status_label);
    detail_label.SetCoords(10,270); AddWidget(&detail_label);
    button(join_button,10,290,400,"Join Selected Game");
    address_entry.SetCoords(10,316); address_entry.SetDimensions(400,GMMWTEXTENTRY_HEIGHT);
    address_entry.SetLabel("Address: "); address_entry.SetAddressMode(true); address_entry.SetMaxText(65); AddWidget(&address_entry);
    button(address_button,10,337,400,"Join by IP / hostname:port");
    button(create_button,10,362,190,"Create Game"); button(back_button,210,362,200,"Back");
    button(portal_button,10,388,190,"Friends / invitations");button(creations_button,210,388,200,"Community creations");
    UpdateDimensions();
}
void GMMMultiplayer::Refresh()
{
    if(refresh_task.Pending() || joining) return;
    sticky_error=false; status_label.SetText(lan_mode ? "Scanning nearby games..." : "Loading online games...");
    detail_label.SetText("Direct entry: IP:port, or IP with default port 2137");
    bool lan=lan_mode;
    refresh_task.Start([lan]() {
        RefreshResult result;
        result.ok=lan ? ZLAN_Discover(result.matches,result.error) : ZMP_ListMatches(result.matches);
        if(!result.ok && result.error.empty()) result.error=ZMP_LastError().empty()?"Match service is unreachable":ZMP_LastError();
        if(result.ok)for(size_t i=0;i<result.matches.size()&&i<32;++i){auto &m=result.matches[i];std::string error;auto start=std::chrono::steady_clock::now();auto connection=ZNetConnect(m.host,m.port,error,250);if(connection)m.ping_ms=(int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
        return result;
    });
}
void GMMMultiplayer::ApplyRefresh(const RefreshResult &result)
{
    discovered=result.matches;Refilter();
    match_list.CheckViewI();
    if(!joining && !sticky_error) {
        status_label.SetText(!result.ok ? result.error : matches.empty() ? "No games found. Create one or enter an address." : to_string(matches.size())+" game(s) found");
    }
}
void GMMMultiplayer::Refilter()
{
    applied_search=search_entry.GetText();applied_region=region_entry.GetText();
    auto lower=[](string text){for(char &c:text)c=(char)std::tolower((unsigned char)c);return text;};
    matches=discovered;auto entries=discovered;
    if(view){auto bookmarks=ZServerBookmarks(view==2);for(const auto &saved:bookmarks){auto found=std::find_if(matches.begin(),matches.end(),[&](const MatchInfo &m){return m.host==saved.host&&m.port==saved.port;});if(found==matches.end())entries.push_back(saved);}}
    match_list.GetEntryList().clear();
    // Keep the displayed indices bound to this filtered snapshot, including offline bookmarks.
    if(view){auto bookmarks=ZServerBookmarks(view==2);entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const MatchInfo &m){return std::none_of(bookmarks.begin(),bookmarks.end(),[&](const MatchInfo &b){return m.host==b.host&&m.port==b.port;});}),entries.end());}
    for(const auto &m:entries){
        if(!applied_search.empty()&&lower(m.name+" "+m.map+" "+m.game_mode+" "+m.host).find(lower(applied_search))==string::npos)continue;
        if(!applied_region.empty()&&lower(m.region).find(lower(applied_region))==string::npos)continue;
        if(max_ping&&(m.ping_ms<0||m.ping_ms>max_ping))continue;
        if(players_filter==1&&!(m.capacity>0&&m.players<m.capacity))continue;
        if(players_filter==2&&m.players<=0)continue;
        if(players_filter==3&&m.players!=0)continue;
        if((state_filter==1&&m.started)||(state_filter==2&&!m.started))continue;
        auto found=std::find_if(matches.begin(),matches.end(),[&](const MatchInfo &e){return e.host==m.host&&e.port==m.port;});
        if(found==matches.end()){matches.push_back(m);found=matches.end()-1;}
        int ref=(int)(found-matches.begin());string name=m.map.empty()?m.host:m.map;if(name.size()>22)name=name.substr(0,19)+"...";
        string line=(ZServerFavorite(m)?"* ":"")+name+" "+to_string(std::max(0,m.players))+"/"+to_string(m.capacity)+" "+(m.ping_ms<0?"?":to_string(m.ping_ms))+"ms"+(m.started?" [playing]":m.capacity>0&&m.players>=m.capacity?" [full]":"");
        match_list.GetEntryList().push_back(mmlist_entry(line,ref,ref));
    }
    match_list.CheckViewI();
}
void GMMMultiplayer::SetError(const string &message)
{
    CancelAuthorization();
    joining=false; join_delay=0; sticky_error=true; gmm_flags.join_match=false;
    status_label.SetText(message.substr(0,70));
    detail_label.SetText(message.size()>70 ? message.substr(70,70) : "Check the address and port, then retry.");
}
void GMMMultiplayer::QueueJoin(const MatchInfo &match)
{
    requested=match; joining=true; join_delay=match.portal?0:2; sticky_error=false;
    if(match.portal){
        if(match.id.empty()||match.id.size()>64||match.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=string::npos){SetError("Invalid portal server identifier. Refresh the directory.");return;}
        status_label.SetText("Requesting website approval...");detail_label.SetText("Your browser handles sign-in; the game never asks for your password.");
        auto id=match.id;auto host=match.host;int port=match.port;auth_begin.Start([id,host,port](){std::string error;if(!ZNetCheckVersion(host,port,error)){ZPortalResponse result;result.error=error;return result;}return ZPortal_Request("/api/connect/start","{\"server\":\""+id+"\"}");});return;
    }
    status_label.SetText("Connecting to "+match.host+":"+to_string(match.port)+"...");
    detail_label.SetText("Waiting for the host (up to 3 seconds)"); address_entry.SetSelected(false);
}
void GMMMultiplayer::JoinAddress()
{
    if(joining) return;
    MatchInfo match; string error;
    if(!ZMP_ParseAddress(address_entry.GetText(),match.host,match.port,error)) { SetError(error); return; }
    QueueJoin(match);
}
bool GMMMultiplayer::KeyPress(int c)
{
    if(joining){if(c==27){CancelAuthorization();killme=true;}return true;}
    bool enter=address_entry.IsSelected() && c==13;
    bool consumed=ZGuiMainMenuBase::KeyPress(c);
    if(enter) JoinAddress();
    return enter || consumed;
}
void GMMMultiplayer::Process()
{
    PollAuthorization();
    Compatibility check;if(compatibility_task.Poll(check)&&check.address==compatibility_address&&!joining){
        status_label.SetText(check.compatible?"Compatible engine: "+check.version:check.error.substr(0,70));
        detail_label.SetText(!ZAsset_RuntimeReady()?"Import original graphics and sounds before playing.":check.compatible?"Map: "+check.map.substr(0,32)+" / downloaded on join":"Install the same engine version; Refresh checks updated servers.");
    }
    RefreshResult result; if(refresh_task.Poll(result)) ApplyRefresh(result);
    if(applied_search!=search_entry.GetText()||applied_region!=region_entry.GetText())Refilter();
    if(join_delay>0 && --join_delay==0) {
        gmm_flags.join_match=true; gmm_flags.join_host=requested.host; gmm_flags.join_port=requested.port;
        gmm_flags.join_team=NULL_TEAM;
    }
    ProcessWidgets();
}
void GMMMultiplayer::HandleWidgetEvent(int type,ZGMMWidget *widget)
{
    if(!widget || (joining&&widget!=&back_button&&widget!=&portal_button)) return;
    if(type==GMM_CLICK_EVENT && widget==&match_list) {
        int index=match_list.GetGMMWFlags().mmlist_entry_selected;
        if(index>=0) {
            match_list.UnSelectAll(index);
            if(index<(int)match_list.GetEntryList().size()) {
                int ref=match_list.GetEntryList()[index].ref_id;
                if(ref>=0 && ref<(int)matches.size()){address_entry.SetText(matches[ref].host+":"+to_string(matches[ref].port));CheckCompatibility(matches[ref]);}
            }
        }
        return;
    }
    if(type!=GMM_UNCLICK_EVENT) return;
    if(widget==&portal_button)SDL_OpenURL((auth_url.empty()?ZPortal_URL()+"/community":auth_url).c_str());
    else if(widget==&creations_button)SDL_OpenURL((ZPortal_URL()+"/creations").c_str());
    else if(widget==&view_button){view=(view+1)%3;view_button.SetText(view==0?"All servers":view==1?"Favorites":"Recently joined");Refilter();}
    else if(widget==&ping_button){max_ping=max_ping==0?100:max_ping==100?200:max_ping==200?500:0;ping_button.SetText(max_ping?"Ping <= "+to_string(max_ping):"Any ping");Refilter();}
    else if(widget==&players_button){players_filter=(players_filter+1)%4;const char *labels[]={"Any players","Open slots","Populated","Empty"};players_button.SetText(labels[players_filter]);Refilter();}
    else if(widget==&state_button){state_filter=(state_filter+1)%3;state_button.SetText(state_filter==0?"Any game state":state_filter==1?"Lobby":"In battle");Refilter();}
    else if(widget==&favorite_button){int selected=match_list.GetFirstSelected();if(selected>=0){int ref=match_list.GetEntryList()[selected].ref_id;if(ref>=0&&ref<(int)matches.size())ZServerToggleFavorite(matches[ref]);Refilter();}}
    else if(widget==&mode_button) {
        if(refresh_task.Pending()) return;
        lan_mode=!lan_mode; mode_button.SetText(lan_mode ? "Nearby LAN" : "Online games"); Refresh();
    } else if(widget==&refresh_button) Refresh();
    else if(widget==&create_button) { gmm_flags.open_main_menu=true; gmm_flags.open_main_menu_type=GMM_MULTIPLAYER_CREATE; }
    else if(widget==&back_button) { CancelAuthorization();killme=true; }
    else if(widget==&address_button) JoinAddress();
    else if(widget==&join_button) {
        int selected=match_list.GetFirstSelected();
        if(selected<0) { SetError("Select a game first"); return; }
        auto &entry=match_list.GetEntryList()[selected];

        if(entry.ref_id>=0 && entry.ref_id<(int)matches.size()) QueueJoin(matches[entry.ref_id]);
    }
}

GMMMultiplayer::~GMMMultiplayer(){CancelAuthorization();}
void GMMMultiplayer::CancelAuthorization(){
    if(!auth_device.empty()){auto device=std::move(auth_device);ZMPAsync<ZPortalResponse> cancel;cancel.Start([device](){return ZPortal_Request("/api/connect/cancel","{\"device\":\""+device+"\"}");});}
    auth_url.clear();auth_deadline=0;auth_poll_at=0;
}
void GMMMultiplayer::PollAuthorization(){
    ZPortalResponse response;
    if(auth_begin.Poll(response)){
        if(!joining||killme)return;
        string device,code;
        if(!response.ok||response.status!=200||!ZPortal_String(response.body,"device",device)||!hexToken(device,64)||!ZPortal_String(response.body,"user_code",code)||!hexToken(code,12)){
            SetError(response.error.empty()?"Could not start website approval (HTTP "+to_string(response.status)+"). Retry from the server list.":response.error);return;
        }
        auth_device=device;auth_url=ZPortal_URL()+"/connect?code="+code;auth_poll_at=SDL_GetTicks()+2000;auth_deadline=SDL_GetTicks()+300000;
        status_label.SetText("Approve server connection in your browser. Code: "+code);
        detail_label.SetText("Return here after approval. Back cancels. Friends button reopens approval.");if(open_approval)SDL_OpenURL(auth_url.c_str());
    }
    if(!joining||killme||auth_device.empty())return;
    if(SDL_GetTicks()>=auth_deadline){SetError("Website approval expired. Select the server and try again.");return;}
    if(auth_poll.Poll(response)){
        auth_poll_at=SDL_GetTicks()+2000;
        if(response.status==429){auth_poll_at+=2000;return;}
        string state,ticket,server;
        if(!response.ok||response.status!=200||!ZPortal_String(response.body,"state",state)){
            SetError(response.error.empty()?"Website approval failed or expired. Select the server again.":response.error);return;
        }
        if(state=="approved"){
            if(!ZPortal_String(response.body,"ticket",ticket)||!hexToken(ticket,64)||!ZPortal_String(response.body,"server",server)||server!=requested.id){SetError("Invalid server authorization. Please retry.");return;}
            auth_device.clear();auth_url.clear();auth_deadline=0;gmm_flags.join_ticket=std::move(ticket);
            gmm_flags.join_match=true;gmm_flags.join_host=requested.host;gmm_flags.join_port=requested.port;gmm_flags.join_team=NULL_TEAM;
            status_label.SetText("Approved. Connecting to "+requested.name+"...");return;
        }
        if(state!="pending"){SetError("Connection approval was declined. Select the server to retry.");return;}
    }
    if(!auth_poll.Pending()&&SDL_GetTicks()>=auth_poll_at){auto device=auth_device;auth_poll.Start([device](){return ZPortal_Request("/api/connect/poll","{\"device\":\""+device+"\"}");});}
}

void GMMMultiplayer::CheckCompatibility(const MatchInfo &match){
 compatibility_task.Cancel();compatibility_address=match.host+":"+to_string(match.port);
 status_label.SetText("Map: "+(match.map.empty()?string("announced on joining"):match.map.substr(0,52)));
 detail_label.SetText("Checking engine compatibility before joining...");
 auto address=compatibility_address,host=match.host,map=match.map;int port=match.port;
 compatibility_task.Start([address,host,port,map](){Compatibility result;result.address=address;result.map=map;result.compatible=ZNetCheckVersion(host,port,result.error,2000,&result.version);return result;});
}
