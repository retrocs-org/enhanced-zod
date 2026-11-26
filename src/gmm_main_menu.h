#ifndef _ZGMM_MAIN_MENU_H_
#define _ZGMM_MAIN_MENU_H_

#include "zgui_main_menu_base.h"

//#79: Play Campaign is the primary action, so it sits first as its own section
//(a gap separates it from the rest - see the layout in gmm_main_menu.cpp).
enum gmm_main_menu_button
{
	GMMMM_SELECT_MAP_BUTTON,
	GMMMM_GENERATE_MAP_BUTTON, GMMMM_MULTIPLAYER_BUTTON, GMMMM_CHANGE_TEAMS_BUTTON,
	GMMMM_MANAGE_BOTS_BUTTON, GMMMM_PLAYER_LIST_BUTTON, GMMMM_OPTIONS_BUTTON,
	GMMMM_SAVE_GAME_BUTTON,
	GMMMM_EXIT_TO_MENU_BUTTON,
	GMMMM_QUIT_GAME_BUTTON,
	GMMMM_RESUME_BUTTON,
	MAX_GMMMM_BUTTONS
};

const string gmm_main_menu_button_string[MAX_GMMMM_BUTTONS] =
{
	"Play Campaign",
	"Generate Map", "Server Browser", "Change Teams",
	"Manage Bots", "Player List", "Options", "Save Game", "Main Menu", "Quit Game", "Resume battle"
};

class GMMMainMenu : public ZGuiMainMenuBase
{
public:
	//#79: compact = the start menu (no game loaded) - hides the in-game/lobby
	//admin entries (Change Teams / Manage Bots / Player List) that don't apply yet.
	//allow_exit: show "Main Menu" (back to the Z-style front-end; local games only)
	GMMMainMenu(bool compact = false, bool allow_exit = false, bool single_player = false,bool editor_test=false,const string &portalId="",int role=0);
private:
    friend class ZPlayerUIRegression;
	bool return_to_frontend=false;GMMWButton portal_button;string portal_path;
	GMMWButton menu_button[MAX_GMMMM_BUTTONS];

	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);
};

#endif
