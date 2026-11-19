#include "gmm_main_menu.h"

GMMMainMenu::GMMMainMenu(bool compact, bool allow_exit, bool single_player,bool editor_test) : ZGuiMainMenuBase()
{
	int i;

	menu_type = GMM_MAIN_MAIN;
    return_to_frontend=allow_exit;
	title = compact ? "Command" : "Battle menu";
	w = 164;
	h = 118;

	//#79: Play Campaign (button 0) is its own section at the top - a gap after it
	//separates the primary action from the rest.
	const int section_gap = 7;

	// Resume is the first action during play; existing action identifiers stay stable.
    if(!compact) {
        menu_button[GMMMM_RESUME_BUTTON].SetType(MMGENERIC_BUTTON);
        menu_button[GMMMM_RESUME_BUTTON].SetText("Resume battle");
        menu_button[GMMMM_RESUME_BUTTON].SetCoords(GMM_SIDE_MARGIN,GMM_TITLE_MARGIN);
        menu_button[GMMMM_RESUME_BUTTON].SetDimensions(w-GMM_SIDE_MARGIN*2,GMMWBUTTON_HEIGHT);
        menu_button[GMMMM_RESUME_BUTTON].SetGreen(true);
        AddWidget(&menu_button[GMMMM_RESUME_BUTTON]);
    }
    //setup buttons
	int y = GMM_TITLE_MARGIN + (compact?0:GMMWBUTTON_HEIGHT+7);
	for(i=0;i<GMMMM_RESUME_BUTTON;i++)
	{
        if(!compact && (i==GMMMM_SELECT_MAP_BUTTON || i==GMMMM_GENERATE_MAP_BUTTON || i==GMMMM_MULTIPLAYER_BUTTON)) continue;
        if(!compact && single_player && (i==GMMMM_CHANGE_TEAMS_BUTTON || i==GMMMM_MANAGE_BOTS_BUTTON || i==GMMMM_PLAYER_LIST_BUTTON)) continue;
		//#79: on the start menu (no game yet) hide the in-game/lobby admin entries
		if(compact && (i == GMMMM_CHANGE_TEAMS_BUTTON || i == GMMMM_MANAGE_BOTS_BUTTON || i == GMMMM_PLAYER_LIST_BUTTON))
			continue;
		if((i==GMMMM_EXIT_TO_MENU_BUTTON && compact) || (i==GMMMM_SAVE_GAME_BUTTON && (compact || !single_player || !allow_exit || editor_test)))
			continue;

		menu_button[i].SetType(MMGENERIC_BUTTON);
		menu_button[i].SetText(i==GMMMM_EXIT_TO_MENU_BUTTON&&editor_test?"Return to Editor":i==GMMMM_EXIT_TO_MENU_BUTTON && !allow_exit?"Leave match":gmm_main_menu_button_string[i]);
		menu_button[i].SetCoords(GMM_SIDE_MARGIN, y);
		menu_button[i].SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
		AddWidget(&menu_button[i]);

		y += GMMWBUTTON_HEIGHT + 3;
		if(i == GMMMM_SELECT_MAP_BUTTON) y += section_gap;   //section break after Play Campaign
	}

	//cheap hack for h
	h = y + GMM_BOTTOM_MARGIN;

	//needed if w is ever changed / set
	UpdateDimensions();
}

void GMMMainMenu::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
	int w_ref_id;

	if(!event_widget) return;

	w_ref_id = event_widget->GetRefID();

	switch(event_type)
	{
	case GMM_UNCLICK_EVENT:
		if(w_ref_id == menu_button[GMMMM_RESUME_BUTTON].GetRefID()) { killme=true; }
        else if(w_ref_id == menu_button[GMMMM_CHANGE_TEAMS_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_CHANGE_TEAMS;
		}
		else if(w_ref_id == menu_button[GMMMM_MANAGE_BOTS_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_MANAGE_BOTS;
		}
		else if(w_ref_id == menu_button[GMMMM_PLAYER_LIST_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_PLAYER_LIST;
		}
		else if(w_ref_id == menu_button[GMMMM_SELECT_MAP_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_SELECT_MAP;
		}
		else if(w_ref_id == menu_button[GMMMM_GENERATE_MAP_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_GENERATE_MAP;
		}
		else if(w_ref_id == menu_button[GMMMM_MULTIPLAYER_BUTTON].GetRefID())
		{
			//was dead; now opens the orchestrator match browser
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_MULTIPLAYER;
		}
		else if(w_ref_id == menu_button[GMMMM_OPTIONS_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_OPTIONS;
		}
		else if(w_ref_id == menu_button[GMMMM_SAVE_GAME_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_SAVE_GAME;
		}
		else if(w_ref_id == menu_button[GMMMM_EXIT_TO_MENU_BUTTON].GetRefID())
		{
			if(return_to_frontend) gmm_flags.exit_to_menu=true;
            else gmm_flags.leave_match=true;
		}
		else if(w_ref_id == menu_button[GMMMM_QUIT_GAME_BUTTON].GetRefID())
		{
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_WARNING;
			gmm_flags.warning_flags.text1 = "Are you sure you want";
			gmm_flags.warning_flags.text2 = "to quit the game?";
			gmm_flags.warning_flags.quit_game = true;
		}
		break;
	}
}
