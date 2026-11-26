#ifndef _ZGMM_PLAYER_LIST_H_
#define _ZGMM_PLAYER_LIST_H_

#include "zgui_main_menu_base.h"
#include <map>

class GMMPlayerList : public ZGuiMainMenuBase
{
public:
	GMMPlayerList(bool staff=false,int role=1,const std::map<int,int> *roles=nullptr);
    void SetRole(int role){server_role=role;}

	void Process();
    void SetFeedback(const string &status);
private:
	GMMWList player_list;
    int server_role=0;const std::map<int,int> *roles=nullptr;
    bool CanModerate() const;
    bool staff=false;int selected=-1;GMMWTextEntry reason;Uint64 moderation_sent_at=0;bool moderation_pending=false;int duration_index=1;GMMWButton duration,kick,ban;GMMWLabel help,kick_unavailable,ban_unavailable;
	GMMWLabel players_on_label;
	GMMWLabel players_onnum_label;

	void SetupLayout1();

	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);

	void SetupList();
};

#endif
