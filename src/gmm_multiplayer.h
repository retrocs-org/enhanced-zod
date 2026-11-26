#ifndef _ZGMM_MULTIPLAYER_H_
#define _ZGMM_MULTIPLAYER_H_
#include "zgui_main_menu_base.h"
#include "zmpclient.h"
#include "zmp_async.h"
#include "zportal.h"
class GMMMultiplayer : public ZGuiMainMenuBase
{
public:
    GMMMultiplayer();
    void Process() override;
    void RenderContents(ZMap &map, SDL_Surface *dest) override;
    bool KeyPress(int c) override;
    bool UnClick(int px,int py) override { return ZGuiMainMenuBase::UnClick(px,py); }
    void SetError(const string &error);
    void SetReconnectTarget(const string &host,int port,const string &id);
    ~GMMMultiplayer();
private:
    friend class ZLANRegression;
    GMMWList match_list;
    GMMWTextEntry address_entry,search_entry,region_entry;
    GMMWLabel status_label, detail_label,direct_help[4],availability_label,preview_hint;
    ZSDL_Surface preview_image;
    GMMWButton portal_button,creations_button;
    GMMWButton view_button,ping_button,players_button,state_button,favorite_button;
    int view=0,max_ping=0,players_filter=0,state_filter=0;
    string applied_search,applied_region;
    void Refilter();
    void SetBrowserMode(int mode);
    GMMWButton mode_button,lan_button,direct_button, join_button, address_button, create_button, refresh_button, back_button;
    vector<MatchInfo> matches,discovered;
    bool lan_mode=false,direct_mode=false, joining=false, sticky_error=false;
    int join_delay=0;
    MatchInfo requested,reconnect_target;bool has_reconnect_target=false;
    struct RefreshResult { bool ok=false; vector<MatchInfo> matches; string error; };
    ZMPAsync<RefreshResult> refresh_task;
    ZMPAsync<ZPortalResponse> auth_begin,auth_poll;
    bool open_approval=true;
    string auth_device,auth_url;Uint64 auth_poll_at=0,auth_deadline=0;
    void CancelAuthorization();void PollAuthorization();
    void SetupLayout1();
    void ApplyRefresh(const RefreshResult &result);
    void HandleWidgetEvent(int type,ZGMMWidget *widget) override;
    void Refresh();
    struct Compatibility {string address,version,error,map,hash,localHash;bool compatible=false;MapPreview preview;MatchInfo availability;};
    ZMPAsync<Compatibility> compatibility_task;string compatibility_address;
    void CheckCompatibility(const MatchInfo &match);
    void JoinAddress();
    void QueueJoin(const MatchInfo &match);
};
#endif
