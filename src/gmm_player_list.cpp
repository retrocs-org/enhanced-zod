#include "gmm_player_list.h"

GMMPlayerList::GMMPlayerList(bool staff_,int role,const std::map<int,int> *roles_) : ZGuiMainMenuBase()
{
	staff=staff_;server_role=role;roles=roles_;menu_type = GMM_PLAYER_LIST;
	title = "Player List";
	w = staff?240:160;
	h = 118;

	SetupLayout1();
}

void GMMPlayerList::SetupLayout1()
{
	players_on_label.SetText("Players Online:");
	players_on_label.SetCoords(GMM_SIDE_MARGIN + 4, GMM_TITLE_MARGIN);
	players_on_label.SetDimensions(w - (GMM_SIDE_MARGIN * 2), 0);
	players_on_label.SetJustification(MMLABEL_NORMAL);
	AddWidget(&players_on_label);

	players_onnum_label.SetText("50");
	players_onnum_label.SetCoords(GMM_SIDE_MARGIN, GMM_TITLE_MARGIN);
	players_onnum_label.SetDimensions(w - (GMM_SIDE_MARGIN * 2), 0);
	players_onnum_label.SetJustification(MMLABEL_RIGHT);
	AddWidget(&players_onnum_label);

	player_list.SetCoords(GMM_SIDE_MARGIN, GMM_TITLE_MARGIN + MMLABEL_HEIGHT + 2);
	player_list.SetDimensions(w - (GMM_SIDE_MARGIN * 2), 118);
	player_list.SetVisibleEntries(8);
	AddWidget(&player_list);

	h = GMM_TITLE_MARGIN + player_list.GetHeight() + GMM_BOTTOM_MARGIN + MMLABEL_HEIGHT + 2;

	if(staff){int y=h-GMM_BOTTOM_MARGIN;help.SetText("Select a player and reason. Staff hierarchy is enforced.");help.SetCoords(GMM_SIDE_MARGIN,y);help.SetDimensions(w-2*GMM_SIDE_MARGIN,12);AddWidget(&help);y+=16;reason.SetMaxText(160);reason.SetCoords(GMM_SIDE_MARGIN,y);reason.SetDimensions(w-2*GMM_SIDE_MARGIN,16);AddWidget(&reason);y+=20;duration.SetType(MMGENERIC_BUTTON);duration.SetText("Ban duration: 1 hour");duration.SetCoords(GMM_SIDE_MARGIN,y);duration.SetDimensions(w-2*GMM_SIDE_MARGIN,GMMWBUTTON_HEIGHT);AddWidget(&duration);y+=GMMWBUTTON_HEIGHT+4;for(auto *b:{&kick,&ban}){b->SetType(MMGENERIC_BUTTON);b->SetDimensions((w-2*GMM_SIDE_MARGIN-4)/2,GMMWBUTTON_HEIGHT);AddWidget(b);}kick.SetText("Kick selected");ban.SetText("Ban selected");kick.SetCoords(GMM_SIDE_MARGIN,y);ban.SetCoords(w/2+2,y);kick_unavailable.SetCoords(GMM_SIDE_MARGIN,y+4);ban_unavailable.SetCoords(w/2+2,y+4);for(auto *label:{&kick_unavailable,&ban_unavailable}){label->SetDimensions((w-2*GMM_SIDE_MARGIN-4)/2,GMMWBUTTON_HEIGHT);AddWidget(label);}h=y+GMMWBUTTON_HEIGHT+GMM_BOTTOM_MARGIN;}
    //needed if w is ever changed / set
	UpdateDimensions();
}

bool GMMPlayerList::CanModerate() const {
 if(!staff||server_role==0||selected<0||(local_p_id&&selected==*local_p_id))return false;
 auto rank=[](int role){return role==2?3:role==1?2:role==3?1:0;};int target=0;if(roles){auto found=roles->find(selected);if(found!=roles->end())target=found->second;}
 return rank(server_role)>rank(target);
}
void GMMPlayerList::Process()
{
    if(moderation_pending&&SDL_GetTicks()-moderation_sent_at>70000)SetFeedback("expired: delivery not confirmed; check portal history");
    kick.SetActive(!moderation_pending&&CanModerate());ban.SetActive(!moderation_pending&&CanModerate());
    kick_unavailable.SetActive(staff&&(moderation_pending||!CanModerate()));ban_unavailable.SetActive(staff&&(moderation_pending||!CanModerate()));kick_unavailable.SetText(moderation_pending?"Kick: awaiting result":"Kick: unavailable");ban_unavailable.SetText(moderation_pending?"Ban: awaiting result":"Ban: unavailable");
    if(staff){const char *label=server_role==2?"Owner":server_role==1?"Administrator":server_role==3?"Moderator":"Player";title=string("Player List / ")+label;}
	SetupList();

	ProcessWidgets();
}

void GMMPlayerList::SetupList()
{
	char num_c[500];

	if(!player_info) return;

	player_list.GetEntryList().clear();

	//add the players
	for(vector<p_info>::iterator i=player_info->begin(); i!=player_info->end(); ++i)
	{
		if(i->mode == PLAYER_MODE)
		{int role=0;if(roles){auto r=roles->find(i->p_id);if(r!=roles->end())role=r->second;}string badge=role==2?"[Owner] ":role==1?"[Admin] ":role==3?"[Mod] ":"";
			player_list.GetEntryList().push_back(mmlist_entry((i->team==RED_TEAM?string("[R] "):i->team==BLUE_TEAM?string("[B] "):i->team==GREEN_TEAM?string("[G] "):i->team==YELLOW_TEAM?string("[Y] "):string("[S] "))+team_type_string[i->team] + ": " + badge+i->name, i->p_id, i->team));}

	}

	//set player online text
	sprintf(num_c, "%d", (int)player_list.GetEntryList().size());
	players_onnum_label.SetText(num_c);

	//add the bots
	//for(vector<p_info>::iterator i=player_info->begin(); i!=player_info->end(); ++i)
	//{
	//	if(i->mode == BOT_MODE && !i->ignored)
	//		player_list.GetEntryList().push_back(mmlist_entry((i->team==RED_TEAM?string("[R] "):i->team==BLUE_TEAM?string("[B] "):i->team==GREEN_TEAM?string("[G] "):i->team==YELLOW_TEAM?string("[Y] "):string("[S] "))+team_type_string[i->team] + ": " + i->name, i->p_id, i->team + MAX_TEAM_TYPES));
	//}

	//sort the list
	sort(player_list.GetEntryList().begin(), player_list.GetEntryList().end(), sort_mmlist_entry_func);

	for(auto &row:player_list.GetEntryList())if(row.ref_id==selected)row.state=MMLIST_PRESSED;
    player_list.CheckViewI();
}

void GMMPlayerList::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
 if(staff&&event_type==GMM_UNCLICK_EVENT&&event_widget==&duration){const int values[]={15,60,1440,10080};duration_index=(duration_index+1)%4;const char *labels[]={"15 min","1 hour","1 day","7 days"};duration.SetText(string("Ban duration: ")+labels[duration_index]);}
 if(event_widget==&player_list){int index=player_list.GetFirstSelected();if(index>=0&&index<(int)player_list.GetEntryList().size())selected=player_list.GetEntryList()[index].ref_id;}
 if(staff&&!moderation_pending&&event_type==GMM_UNCLICK_EVENT&&(event_widget==&kick||event_widget==&ban)){
  if(!CanModerate()||reason.GetText().find_first_not_of(" \t\r\n")==string::npos){help.SetText("Select a player and enter a reason first.");return;}
  gmm_flags.moderate_target=selected;const int minutes[]={15,60,1440,10080};gmm_flags.moderate_minutes=event_widget==&ban?minutes[duration_index]:0;moderation_pending=true;moderation_sent_at=SDL_GetTicks();gmm_flags.moderate_reason=reason.GetText().substr(0,160);help.SetText("Request queued; server checks your current permissions.");
 }
}

void GMMPlayerList::SetFeedback(const string &status){moderation_pending=status.compare(0,7,"pending")==0;help.SetText(status.substr(0,65));}
