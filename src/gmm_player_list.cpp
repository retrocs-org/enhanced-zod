#include "gmm_player_list.h"

GMMPlayerList::GMMPlayerList(bool staff_) : ZGuiMainMenuBase()
{
	staff=staff_;menu_type = GMM_PLAYER_LIST;
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

	if(staff){int y=h-GMM_BOTTOM_MARGIN;help.SetText("Select player; reason required. Ban: 60 min.");help.SetCoords(GMM_SIDE_MARGIN,y);help.SetDimensions(w-2*GMM_SIDE_MARGIN,12);AddWidget(&help);y+=16;reason.SetMaxText(160);reason.SetCoords(GMM_SIDE_MARGIN,y);reason.SetDimensions(w-2*GMM_SIDE_MARGIN,16);AddWidget(&reason);y+=20;for(auto *b:{&kick,&ban}){b->SetType(MMGENERIC_BUTTON);b->SetDimensions((w-2*GMM_SIDE_MARGIN-4)/2,GMMWBUTTON_HEIGHT);AddWidget(b);}kick.SetText("Kick selected");ban.SetText("Ban 60 min");kick.SetCoords(GMM_SIDE_MARGIN,y);ban.SetCoords(w/2+2,y);h=y+GMMWBUTTON_HEIGHT+GMM_BOTTOM_MARGIN;}
    //needed if w is ever changed / set
	UpdateDimensions();
}

void GMMPlayerList::Process()
{
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
			player_list.GetEntryList().push_back(mmlist_entry((i->team==RED_TEAM?string("[R] "):i->team==BLUE_TEAM?string("[B] "):i->team==GREEN_TEAM?string("[G] "):i->team==YELLOW_TEAM?string("[Y] "):string("[S] "))+team_type_string[i->team] + ": " + i->name, i->p_id, i->team));
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
 if(event_widget==&player_list){int index=player_list.GetFirstSelected();if(index>=0&&index<(int)player_list.GetEntryList().size())selected=player_list.GetEntryList()[index].ref_id;}
 if(staff&&event_type==GMM_UNCLICK_EVENT&&(event_widget==&kick||event_widget==&ban)){
  if(selected<0||reason.GetText().empty()){help.SetText("Select a player and enter a reason first.");return;}
  gmm_flags.moderate_target=selected;gmm_flags.moderate_minutes=event_widget==&ban?60:0;gmm_flags.moderate_reason=reason.GetText().substr(0,160);help.SetText("Request queued; server checks your current permissions.");
 }
}
