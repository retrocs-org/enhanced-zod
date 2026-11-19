#include "gmm_multiplayer.h"
#include "zlan.h"
#include <algorithm>

GMMMultiplayer::GMMMultiplayer()
{
    menu_type=GMM_MULTIPLAYER; title="Multiplayer"; w=420; h=325;
    SetupLayout1(); Refresh();
}
void GMMMultiplayer::SetupLayout1()
{
    auto button=[&](GMMWButton &b,int px,int py,int width,const string &label) {
        b.SetType(MMGENERIC_BUTTON); b.SetCoords(px,py); b.SetDimensions(width,18); b.SetText(label); AddWidget(&b);
    };
    button(mode_button,10,24,190,"Nearby LAN"); button(refresh_button,210,24,200,"Refresh");
    match_list.SetCoords(10,48); match_list.SetVisibleEntries(7); match_list.SetDimensions(400,105); AddWidget(&match_list);
    status_label.SetCoords(10,162); AddWidget(&status_label);
    detail_label.SetCoords(10,178); AddWidget(&detail_label);
    button(join_button,10,198,400,"Join Selected Game");
    address_entry.SetCoords(10,224); address_entry.SetDimensions(400,GMMWTEXTENTRY_HEIGHT);
    address_entry.SetLabel("Address: "); address_entry.SetAddressMode(true); address_entry.SetMaxText(65); AddWidget(&address_entry);
    button(address_button,10,245,400,"Join by IP / hostname:port");
    button(create_button,10,270,190,"Create Game"); button(back_button,210,270,200,"Back");
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
        if(!result.ok && result.error.empty()) result.error="Match service is unreachable";
        return result;
    });
}
void GMMMultiplayer::ApplyRefresh(const RefreshResult &result)
{
    matches=result.matches; match_list.GetEntryList().clear();
    for(size_t i=0;i<matches.size();++i) {
        const auto &match=matches[i];
        string name=match.map; if(name.size()>4 && name.substr(name.size()-4)==".map") name.resize(name.size()-4);
        if(name.size()>25) name=name.substr(0,22)+"...";
        string line=name+"  "+to_string(std::max(0,match.players))+"/"+to_string(match.capacity)+"  "+match.host+":"+to_string(match.port);
        bool unavailable=match.started || (match.capacity>0 && match.players>=match.capacity);
        if(unavailable) line+=match.started ? " [playing]" : " [full]";
        mmlist_entry entry(line,(int)i,(int)i); entry.disabled=unavailable;
        match_list.GetEntryList().push_back(entry);
    }
    match_list.CheckViewI();
    if(!joining && !sticky_error) {
        status_label.SetText(!result.ok ? result.error : matches.empty() ? "No games found. Create one or enter an address." : to_string(matches.size())+" game(s) found");
    }
}
void GMMMultiplayer::SetError(const string &message)
{
    joining=false; join_delay=0; sticky_error=true; gmm_flags.join_match=false;
    status_label.SetText(message.substr(0,70));
    detail_label.SetText(message.size()>70 ? message.substr(70,70) : "Check the address and port, then retry.");
}
void GMMMultiplayer::QueueJoin(const MatchInfo &match)
{
    requested=match; joining=true; join_delay=2; sticky_error=false;
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
    if(joining) return true;
    bool enter=address_entry.IsSelected() && c==13;
    bool consumed=ZGuiMainMenuBase::KeyPress(c);
    if(enter) JoinAddress();
    return enter || consumed;
}
void GMMMultiplayer::Process()
{
    RefreshResult result; if(refresh_task.Poll(result)) ApplyRefresh(result);
    if(join_delay>0 && --join_delay==0) {
        gmm_flags.join_match=true; gmm_flags.join_host=requested.host; gmm_flags.join_port=requested.port;
        gmm_flags.join_team=NULL_TEAM;
    }
    ProcessWidgets();
}
void GMMMultiplayer::HandleWidgetEvent(int type,ZGMMWidget *widget)
{
    if(!widget || joining) return;
    if(type==GMM_CLICK_EVENT && widget==&match_list) {
        int index=match_list.GetGMMWFlags().mmlist_entry_selected;
        if(index>=0) {
            match_list.UnSelectAll(index);
            if(index<(int)match_list.GetEntryList().size()) {
                int ref=match_list.GetEntryList()[index].ref_id;
                if(ref>=0 && ref<(int)matches.size()) address_entry.SetText(matches[ref].host+":"+to_string(matches[ref].port));
            }
        }
        return;
    }
    if(type!=GMM_UNCLICK_EVENT) return;
    if(widget==&mode_button) {
        if(refresh_task.Pending()) return;
        lan_mode=!lan_mode; mode_button.SetText(lan_mode ? "Nearby LAN" : "Online games"); Refresh();
    } else if(widget==&refresh_button) Refresh();
    else if(widget==&create_button) { gmm_flags.open_main_menu=true; gmm_flags.open_main_menu_type=GMM_MULTIPLAYER_CREATE; }
    else if(widget==&back_button) { killme=true; }
    else if(widget==&address_button) JoinAddress();
    else if(widget==&join_button) {
        int selected=match_list.GetFirstSelected();
        if(selected<0) { SetError("Select a game first"); return; }
        auto &entry=match_list.GetEntryList()[selected];
        if(entry.disabled) { SetError("This game is full or already playing"); return; }
        if(entry.ref_id>=0 && entry.ref_id<(int)matches.size()) QueueJoin(matches[entry.ref_id]);
    }
}
