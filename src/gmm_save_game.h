#ifndef _ZGMM_SAVE_GAME_H_
#define _ZGMM_SAVE_GAME_H_

#include "zgui_main_menu_base.h"
#include "zsavegame.h"

//In-game "Save Game" window: one button per save slot, showing what is in it.
class GMMSaveGame : public ZGuiMainMenuBase
{
public:
	GMMSaveGame();

	void Process();
    bool KeyPress(int key) override;
    void RenderContents(ZMap &map,SDL_Surface *dest) override;
private:
	GMMWButton slot_button[ZSAVE_USER_SLOTS];
GMMWButton save_button;int selected=0;
    ZSDL_Surface preview;std::string preview_path;
	double next_refresh_time;

	void UpdateButtonText();
	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);
};

#endif
