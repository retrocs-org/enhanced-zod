#ifndef _ZGMM_OPTIONS_H_
#define _ZGMM_OPTIONS_H_

#include "zgui_main_menu_base.h"
#include "gmmw_slider.h"
#include "zprefs.h"

#define MAX_GMMOPTIONS_SPEED_SETTINGS 7

const double gmmoption_speed_setting_value[MAX_GMMOPTIONS_SPEED_SETTINGS] =
{
	0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0
};

class GMMOptions : public ZGuiMainMenuBase
{
public:
	GMMOptions();

	void Process();
    bool KeyPress(int c) override;
    void RenderContents(ZMap &map,SDL_Surface *dest) override;
private:
	// volume and game speed use cycling BUTTONS, not radios: the radio widget's
	// art was never shipped, so the menu had no working volume/speed control
	// (issue #41); buttons are the reliable control this fork ships
	GMMWSlider effects_slider{"Sound effects",&zod_effects_volume},music_slider{"Music",&zod_music_volume};
    GMMWButton category_button[7],picture_button,stick_button,fullscreen_button,gamma_button;
    GMMWButton text_size_button,cursor_size_button,team_marker_button,motion_button;
    int category=0,focus=0,preview_y=0;
    GMMWSlider video_slider{"Video volume",&zod_video_volume};
    GMMWButton damage_alert_button,production_alert_button,alert_sound_button,alert_cooldown_button;
    GMMWButton preset_button,recording_button;
    GMMWButton subtitle_toggle_button,deadzone_button,edge_button,subtitle_size_button,subtitle_bg_button,profile_button;
    GMMWButton diagnostics_button;GMMWButton defaults_button;GMMWLabel help_label;
    std::vector<ZGMMWidget*> CategoryWidgets();
    void Reflow();
    GMMWTextEntry name_entry;	//#246: editable player name (persisted)
	GMMWButton volume_button;
	GMMWButton speed_button;
	GMMWButton mouse_button;
	GMMWButton smooth_button;
	GMMWButton menu_size_button;	//#196: cycles the menu/UI scale
	GMMWButton difficulty_button;	//#difficulty: cycles the bot AI difficulty
	GMMWButton max_units_button;	//#75: max units per team (takes effect next game)
	GMMWButton reshuffle_button;
	GMMWButton reset_button;
	GMMWButton pause_button;

	void SetupLayout1();

	void HandleWidgetEvent(int event_type, ZGMMWidget *event_widget);

	void SetVolumeStatus();
	void SetTimeStatuses();
};

#endif
