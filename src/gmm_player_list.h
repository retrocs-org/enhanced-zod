#ifndef _ZGMM_PLAYER_LIST_H_
#define _ZGMM_PLAYER_LIST_H_

#include "zgui_main_menu_base.h"

class GMMPlayerList : public ZGuiMainMenuBase
{
public:
	GMMPlayerList(bool staff=false);

	void Process();
    void SetFeedback(const string &status);
private:
	GMMWList player_list;
    bool staff=false;int selected=-1;GMMWTextEntry reason;Uint64 moderation_sent_at=0;bool moderation_pending=false;int duration_index=1;GMMWButton duration,kick,ban;GMMWLabel help;
	GMMWLabel players_on_label;
	GMMWLabel players_onnum_label;

	void SetupLayout1();

	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);

	void SetupList();
};

#endif
