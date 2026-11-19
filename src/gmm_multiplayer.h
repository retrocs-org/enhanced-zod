#ifndef _ZGMM_MULTIPLAYER_H_
#define _ZGMM_MULTIPLAYER_H_
#include "zgui_main_menu_base.h"
#include "zmpclient.h"
#include "zmp_async.h"
class GMMMultiplayer : public ZGuiMainMenuBase
{
public:
    GMMMultiplayer();
    void Process() override;
    bool KeyPress(int c) override;
    bool UnClick(int px,int py) override { return joining && WithinDimensions(px,py) ? true : ZGuiMainMenuBase::UnClick(px,py); }
    void SetError(const string &error);
private:
    friend class ZLANRegression;
    GMMWList match_list;
    GMMWTextEntry address_entry;
    GMMWLabel status_label, detail_label;
    GMMWButton mode_button, join_button, address_button, create_button, refresh_button, back_button;
    vector<MatchInfo> matches;
    bool lan_mode=true, joining=false, sticky_error=false;
    int join_delay=0;
    MatchInfo requested;
    struct RefreshResult { bool ok=false; vector<MatchInfo> matches; string error; };
    ZMPAsync<RefreshResult> refresh_task;
    void SetupLayout1();
    void ApplyRefresh(const RefreshResult &result);
    void HandleWidgetEvent(int type,ZGMMWidget *widget) override;
    void Refresh();
    void JoinAddress();
    void QueueJoin(const MatchInfo &match);
};
#endif
