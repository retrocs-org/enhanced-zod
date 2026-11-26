#ifndef _ZGMM_LOBBY_H_
#define _ZGMM_LOBBY_H_
#include "zgui_main_menu_base.h"
#include "zmpclient.h"

class GMMLobby : public ZGuiMainMenuBase
{
public:
    GMMLobby();
    void Process() override;
    void RenderContents(ZMap &map,SDL_Surface *dest) override;
    bool UnClick(int x,int y) override;
    void SetSession(const string &address, const string &map, const vector<int> &teams, bool loaded);
    void SetError(const string &message);
    void SetPauseConfirmed(bool confirmed) { pause_confirmed=confirmed; }
    void SetCountdown(int seconds) { countdown=seconds; }
private:
    friend class ZLobbyRegression;
    GMMWLabel session_label, map_label, status_label, hint_label, error_label;
    GMMWList player_list;
    GMMWButton auto_button,spectate_button;
    ZSDL_Surface preview_image;string preview_name;bool preview_loaded=false;
    GMMWLabel preview_label;
    GMMWButton team_button, bots_button, ready_button, leave_button;
    bool pause_confirmed=false, saw_paused=false, world_loaded=false, sticky_error=false;
    bool ready_pending=false, requested_ready=false;
    Uint64 ready_sent_at=0, opened_at=0;
    int countdown=-1,previous_countdown=-1;Uint64 cancelled_at=0;
    vector<int> fort_teams;
    void SetupLayout1();
    void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget) override;
    void RebuildPlayers();
    bool Playable(const p_info &player) const;
    p_info *LocalPlayer();
};
#endif
