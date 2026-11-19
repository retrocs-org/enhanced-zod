#include <cmath>
#include "gmm_options.h"
#include "zprefs.h"
#include "zvideo.h"
#include "zaudio.h"
#include "constants.h"	//#75: DEFAULT_MAX_UNITS_PER_TEAM + UNIT_LIMIT bounds

//defined in zobject.cpp
extern bool zod_render_smoothing;

GMMOptions::GMMOptions() : ZGuiMainMenuBase()
{
	menu_type = GMM_OPTIONS;
	title = "Options";
	w = 280;
	h = 118;

	SetupLayout1();AddWidget(&effects_slider);AddWidget(&music_slider);
    const char *names[]={"Graphics","Audio","Game","Controls","Access"};
    for(int i=0;i<5;++i) { category_button[i].SetType(MMGENERIC_BUTTON);category_button[i].SetText(names[i]);category_button[i].SetCoords(5+(i%3)*90,23+(i/3)*23);category_button[i].SetDimensions(86,20);AddWidget(&category_button[i]); }
    for(auto *button:{&picture_button,&stick_button,&fullscreen_button,&gamma_button,&text_size_button,&cursor_size_button,&team_marker_button,&motion_button}) {button->SetType(MMGENERIC_BUTTON);AddWidget(button);}defaults_button.SetType(MMGENERIC_BUTTON);defaults_button.SetText("Reset category defaults");AddWidget(&defaults_button);AddWidget(&help_label);Reflow();
}

void GMMOptions::SetupLayout1()
{
	int next_y;

	next_y = GMM_TITLE_MARGIN;

	//#246: editable player name. Click to focus, type to edit; persisted to prefs
	//and used as the multiplayer display name.
	name_entry.SetLabel("Name: ");
	name_entry.SetText(zod_player_name);
	name_entry.SetMaxText(16);
	name_entry.SetCoords(GMM_SIDE_MARGIN, next_y);
	name_entry.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWTEXTENTRY_HEIGHT);
	AddWidget(&name_entry);
	next_y += GMMWTEXTENTRY_HEIGHT + 7;

	//volume and game speed: click cycles to the next setting; the button text
	//shows the current value (refreshed each frame in Process from live state)
	volume_button.SetType(MMGENERIC_BUTTON);
	volume_button.SetText("Volume:");
	volume_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	volume_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&volume_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	speed_button.SetType(MMGENERIC_BUTTON);
	speed_button.SetText("Game Speed:");
	speed_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	speed_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&speed_button);
	next_y += GMMWBUTTON_HEIGHT + 7;

	//player preference toggles (persisted to ~/.zod_prefs)
	mouse_button.SetType(MMGENERIC_BUTTON);
	mouse_button.SetText("Mouse: Classic");
	mouse_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	mouse_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&mouse_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	smooth_button.SetType(MMGENERIC_BUTTON);
	smooth_button.SetText("Smoothing: On");
	smooth_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	smooth_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&smooth_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	//#difficulty: cycles the bot AI difficulty (persisted, read live by the bot)
	difficulty_button.SetType(MMGENERIC_BUTTON);
	difficulty_button.SetText("Difficulty: Normal");
	difficulty_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	difficulty_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&difficulty_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	max_units_button.SetType(MMGENERIC_BUTTON);	//#75
	max_units_button.SetText("Max Units: 100");
	max_units_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	max_units_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&max_units_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	menu_size_button.SetType(MMGENERIC_BUTTON);	//#196
	menu_size_button.SetText("Menu Size: 2x");
	menu_size_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	menu_size_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&menu_size_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	pause_button.SetType(MMGENERIC_BUTTON);
	pause_button.SetText("Pause Game");
	pause_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	pause_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&pause_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	reshuffle_button.SetType(MMGENERIC_BUTTON);
	reshuffle_button.SetText("Reshuffle Teams");
	reshuffle_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	reshuffle_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&reshuffle_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	reset_button.SetType(MMGENERIC_BUTTON);
	reset_button.SetText("Reset Map");
	reset_button.SetCoords(GMM_SIDE_MARGIN, next_y);
	reset_button.SetDimensions(w - (GMM_SIDE_MARGIN * 2), GMMWBUTTON_HEIGHT);
	AddWidget(&reset_button);
	next_y += GMMWBUTTON_HEIGHT + 1;

	h = next_y + GMM_BOTTOM_MARGIN;

	//needed if w is ever changed / set
	UpdateDimensions();
}

void GMMOptions::Reflow()
{
    vector<ZGMMWidget*> all={&name_entry,&volume_button,&speed_button,&mouse_button,&smooth_button,&menu_size_button,&difficulty_button,&max_units_button,&reshuffle_button,&reset_button,&effects_slider,&music_slider,&pause_button,&picture_button,&stick_button,&fullscreen_button,&gamma_button,&text_size_button,&cursor_size_button,&team_marker_button,&motion_button};
    for(auto *widget:all) widget->SetActive(false);
    vector<vector<ZGMMWidget*>> groups={{&fullscreen_button,&gamma_button,&picture_button,&smooth_button,&menu_size_button},{&effects_slider,&music_slider},{&name_entry,&speed_button,&difficulty_button,&max_units_button,&pause_button,&reshuffle_button,&reset_button},{&mouse_button,&stick_button},{&text_size_button,&cursor_size_button,&team_marker_button,&motion_button}};
    int y=76;
    for(auto *widget:groups[category]) { widget->SetActive(true);widget->SetCoords(10,y);int height=(widget==&effects_slider||widget==&music_slider)?34:18;widget->SetDimensions(w-20,height);y+=height+5; }
    for(int i=0;i<5;++i) category_button[i].SetGreen(category==i);
    preview_y=y;if(category==0)y+=36;defaults_button.SetCoords(10,y+2);defaults_button.SetDimensions(w-20,18);help_label.SetCoords(10,y+28);const char *help[]={"Filter: pixels. Motion: moving units.","Independent volume; Left/Right: 5%.","Unit limit takes effect next battle.","Tab selects; Enter changes; [ ] tabs.","Symbols supplement colour; text 100-120%."};help_label.SetText(help[category]);
    int nextHeight=std::max(160,y+46);this->y+=(h-nextHeight)/2;h=nextHeight;UpdateDimensions();
}

void GMMOptions::Process()
{
	//#246: reflect the current saved name when the field isn't being edited (so it
	//shows the startup default / any external change without clobbering live typing)
	if(!name_entry.IsSelected())
		name_entry.SetText(zod_player_name);

text_size_button.SetText("Text size: "+to_string(zod_text_percent)+"%");cursor_size_button.SetText("Battle cursor: "+to_string(zod_cursor_percent)+"%");team_marker_button.SetText(string("Team symbols: ")+(zod_team_markers?"On":"Off"));motion_button.SetText(string("Reduced motion: ")+(zod_reduced_motion?"On":"Off"));
	SetVolumeStatus();

	SetTimeStatuses();

	mouse_button.SetText(zod_classic_mouse ? "Mouse: Classic" : "Mouse: Modern");
	mouse_button.SetGreen(zod_classic_mouse);
	smooth_button.SetText(zod_render_smoothing ? "Motion smoothing: On" : "Motion smoothing: Off");
	smooth_button.SetGreen(zod_render_smoothing);

	{
		int d = (zod_bot_difficulty >= 0 && zod_bot_difficulty < MAX_BOT_DIFFICULTY) ? zod_bot_difficulty : BOT_DIFF_NORMAL;
		difficulty_button.SetText(string("Difficulty: ") + bot_difficulty_name[d]);
		difficulty_button.SetGreen(d != BOT_DIFF_NORMAL);
	}

	{
		char buf[32];
		snprintf(buf, sizeof(buf), "Max Units: %d", zod_max_units_per_team);	//#75
		max_units_button.SetText(buf);
		max_units_button.SetGreen(zod_max_units_per_team != DEFAULT_MAX_UNITS_PER_TEAM);
	}

	{
		char buf[32];
		snprintf(buf, sizeof(buf), "Menu Size: %gx", zod_menu_scale);	//#196
		menu_size_button.SetText(buf);
		menu_size_button.SetGreen(zod_menu_scale != 1.0);
	}

    const char *filters[]={"Smooth","Sharp","Pixel-perfect"};
    picture_button.SetText(string("Image filter: ")+filters[zod_picture_mode]);
    fullscreen_button.SetText(zod_fullscreen?"Fullscreen: On":"Fullscreen: Off");
    gamma_button.SetText("Gamma: "+to_string(zod_gamma));
    const char *speeds[]={"Slow","Normal","Fast"};stick_button.SetText(string("Controller speed: ")+speeds[zod_stick_speed]);
    ProcessWidgets();
}

void GMMOptions::SetTimeStatuses()
{
	if(!ztime) return;

	pause_button.SetGreen(ztime->IsPaused());

	char speed_str[64];
	sprintf(speed_str, "Game Speed: %.0lf%%", 100 * ztime->GameSpeed());
	speed_button.SetText(speed_str);
}

void GMMOptions::SetVolumeStatus()
{
	int si;

	if(!sound_setting) return;

	si = *sound_setting;

	if(si < 0) return;
	if(si >= MAX_SOUND_SETTINGS) return;

	volume_button.SetText("Volume: " + sound_setting_string[si]);
}

void GMMOptions::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
    if(event_type==GMM_UNCLICK_EVENT) {
        if(event_widget==&defaults_button){ZPrefs_ResetCategory(category==4?5:category);ZVideo_SetFullscreen(zod_fullscreen);ZVideo_SetGamma(zod_gamma);ZVideo_SetPictureMode(zod_picture_mode);ZAudio_ApplyVolumes();if(category==1)gmm_flags.set_music=true;if(category==2){gmm_flags.set_game_speed=true;gmm_flags.set_game_speed_value=1;}Reflow();return;}

        for(int i=0;i<5;++i) if(event_widget==&category_button[i]) { category=i;focus=0;Reflow();return; }
        if(event_widget==&text_size_button){zod_text_percent=zod_text_percent>=120?100:zod_text_percent+10;ZPrefs_Save();return;}
        if(event_widget==&cursor_size_button){zod_cursor_percent=zod_cursor_percent>=200?100:zod_cursor_percent+50;ZPrefs_Save();return;}
        if(event_widget==&team_marker_button){zod_team_markers=!zod_team_markers;ZPrefs_Save();return;}
        if(event_widget==&motion_button){zod_reduced_motion=!zod_reduced_motion;ZPrefs_Save();return;}
        if(event_widget==&stick_button) {zod_stick_speed=(zod_stick_speed+1)%MAX_STICK_SPEEDS;ZPrefs_Save();return;}
        if(event_widget==&fullscreen_button) {zod_fullscreen=!zod_fullscreen;ZVideo_SetFullscreen(zod_fullscreen);ZPrefs_Save();return;}
        if(event_widget==&gamma_button) {zod_gamma=zod_gamma>=ZVIDEO_GAMMA_STEPS?-ZVIDEO_GAMMA_STEPS:zod_gamma+1;ZVideo_SetGamma(zod_gamma);ZPrefs_Save();return;}
        if(event_widget==&picture_button) { zod_picture_mode=(zod_picture_mode+1)%MAX_PICTURE_MODES;ZVideo_SetPictureMode(zod_picture_mode);ZPrefs_Save();return; }
    }

	int w_ref_id;

	if(!event_widget) return;

	w_ref_id = event_widget->GetRefID();

	switch(event_type)
	{
	case GMM_KEYPRESS_EVENT:
		//#246: the name field was edited - persist it (used as the MP display name)
		if(w_ref_id == name_entry.GetRefID())
		{
			zod_player_name = name_entry.GetText();
			ZPrefs_Save();
		}
		break;

	case GMM_UNCLICK_EVENT:
		if(w_ref_id == reshuffle_button.GetRefID())
		{
			gmm_flags.reshuffle_teams = true;
		}
		else if(w_ref_id == reset_button.GetRefID())
		{
			//gmm_flags.reset_map = true;
			gmm_flags.open_main_menu = true;
			gmm_flags.open_main_menu_type = GMM_WARNING;
			gmm_flags.warning_flags.text1 = "Are you sure you want";
			gmm_flags.warning_flags.text2 = "to reset the map?";
			gmm_flags.warning_flags.reset_map = true;
		}
		else if(w_ref_id == pause_button.GetRefID())
		{
			gmm_flags.pause_game = true;
		}
		else if(w_ref_id == mouse_button.GetRefID())
		{
			zod_classic_mouse = !zod_classic_mouse;
			ZPrefs_Save();
		}
		else if(w_ref_id == difficulty_button.GetRefID())
		{
			//#difficulty: cycle Easy->Normal->Hard->Expert; the bot reads it live
			zod_bot_difficulty = (zod_bot_difficulty + 1) % MAX_BOT_DIFFICULTY;
			ZPrefs_Save();
		}
		else if(w_ref_id == max_units_button.GetRefID())
		{
			//#75: cycle the max units per team (wraps at the top); takes effect next game
			zod_max_units_per_team += UNIT_LIMIT_STEP;
			if(zod_max_units_per_team > UNIT_LIMIT_MAX) zod_max_units_per_team = UNIT_LIMIT_MIN;
			ZPrefs_Save();
		}
		else if(w_ref_id == smooth_button.GetRefID())
		{
			zod_render_smoothing = !zod_render_smoothing;
			ZPrefs_Save();
		}
		else if(w_ref_id == menu_size_button.GetRefID())
		{
			//#196: cycle the menu/UI scale (wraps). Persisted; applied to menus on
			//open and to this menu live. SetRenderScale auto-caps it to fit.
			//#207: capped at 2x - 3x was too large and caused graphical glitches.
			static const double sizes[] = { 1.0, 1.5, 2.0 };
			const int n = sizeof(sizes) / sizeof(sizes[0]);
			int cur = 0;
			for(int i = 0; i < n; i++) if(zod_menu_scale <= sizes[i] + 0.01) { cur = i; break; }
			zod_menu_scale = sizes[(cur + 1) % n];
			ZPrefs_Save();
			SetRenderScale(zod_menu_scale);   //re-scale this open menu immediately
		}
		else if(w_ref_id == volume_button.GetRefID())
		{
			//cycle to the next volume setting (wraps round)
			int si = sound_setting ? *sound_setting : 0;
			si = (si + 1) % MAX_SOUND_SETTINGS;
			gmm_flags.set_volume = true;
			gmm_flags.set_volume_value = si;
		}
		else if(w_ref_id == speed_button.GetRefID())
		{
			//find the current speed, advance to the next (wraps round)
			int cur = MAX_GMMOPTIONS_SPEED_SETTINGS - 1;
			for(int i=0;i<MAX_GMMOPTIONS_SPEED_SETTINGS;i++)
				if(ztime && ztime->GameSpeed() <= gmmoption_speed_setting_value[i] + 0.01)
					{ cur = i; break; }
			int next = (cur + 1) % MAX_GMMOPTIONS_SPEED_SETTINGS;
			gmm_flags.set_game_speed = true;
			gmm_flags.set_game_speed_value = gmmoption_speed_setting_value[next];
		}
		break;

	//#73: the mouse wheel nudges the value under the cursor up (wheel up) or
	//down (wheel down), clamped at the ends - so you can lower volume/speed,
	//not just cycle forward with a click.
	case GMM_WHEELUP_EVENT:
	case GMM_WHEELDOWN_EVENT:
		{
			int dir = (event_type == GMM_WHEELUP_EVENT) ? +1 : -1;

			if(w_ref_id == volume_button.GetRefID())
			{
				int si = sound_setting ? *sound_setting : 0;
				si += dir;
				if(si < 0) si = 0;
				if(si >= MAX_SOUND_SETTINGS) si = MAX_SOUND_SETTINGS - 1;
				gmm_flags.set_volume = true;
				gmm_flags.set_volume_value = si;
			}
			else if(w_ref_id == speed_button.GetRefID())
			{
				int cur = MAX_GMMOPTIONS_SPEED_SETTINGS - 1;
				for(int i=0;i<MAX_GMMOPTIONS_SPEED_SETTINGS;i++)
					if(ztime && ztime->GameSpeed() <= gmmoption_speed_setting_value[i] + 0.01)
						{ cur = i; break; }
				int next = cur + dir;
				if(next < 0) next = 0;
				if(next >= MAX_GMMOPTIONS_SPEED_SETTINGS) next = MAX_GMMOPTIONS_SPEED_SETTINGS - 1;
				gmm_flags.set_game_speed = true;
				gmm_flags.set_game_speed_value = gmmoption_speed_setting_value[next];
			}
			else if(w_ref_id == max_units_button.GetRefID())
			{
				//#75: wheel adjusts max units up/down, clamped at the ends
				zod_max_units_per_team += dir * UNIT_LIMIT_STEP;
				if(zod_max_units_per_team < UNIT_LIMIT_MIN) zod_max_units_per_team = UNIT_LIMIT_MIN;
				if(zod_max_units_per_team > UNIT_LIMIT_MAX) zod_max_units_per_team = UNIT_LIMIT_MAX;
				ZPrefs_Save();
			}
		}
		break;
	}
}

std::vector<ZGMMWidget*> GMMOptions::CategoryWidgets(){
 std::vector<std::vector<ZGMMWidget*>> groups={{&fullscreen_button,&gamma_button,&picture_button,&smooth_button,&menu_size_button},{&effects_slider,&music_slider},{&name_entry,&speed_button,&difficulty_button,&max_units_button,&pause_button,&reshuffle_button,&reset_button},{&mouse_button,&stick_button},{&text_size_button,&cursor_size_button,&team_marker_button,&motion_button}};return groups[category];
}
bool GMMOptions::KeyPress(int c){
 if(name_entry.IsSelected())return ZGuiMainMenuBase::KeyPress(c);
 if(c=='['||c==']'){category=(category+(c==']'?1:4))%5;focus=0;Reflow();return true;}
 auto widgets=CategoryWidgets();
 if(c==9||c==SDLK_DOWN||c==SDLK_UP){focus=(focus+(c==SDLK_UP?(int)widgets.size()-1:1))%widgets.size();help_label.SetText("Selected setting "+to_string(focus+1)+" / Enter changes");return true;}
 if(c==13||c==SDLK_LEFT||c==SDLK_RIGHT){
  if(category==4){if(focus==0)zod_text_percent=100+(zod_text_percent-100+(c==SDLK_LEFT?20:10))%30;if(focus==1)zod_cursor_percent=100+(zod_cursor_percent-100+(c==SDLK_LEFT?100:50))%150;if(focus==2)zod_team_markers=!zod_team_markers;if(focus==3)zod_reduced_motion=!zod_reduced_motion;ZPrefs_Save();}
  else if(category==1){int &value=focus?zod_music_volume:zod_effects_volume;value=std::max(0,std::min(100,value+(c==SDLK_LEFT?-5:5)));ZAudio_ApplyVolumes();ZPrefs_Save();}
  else HandleWidgetEvent(c==SDLK_LEFT?GMM_WHEELDOWN_EVENT:GMM_UNCLICK_EVENT,widgets[focus]);return true;
 }
 return ZGuiMainMenuBase::KeyPress(c);
}

void GMMOptions::RenderContents(ZMap &map,SDL_Surface *dest){
 auto focused=CategoryWidgets();for(auto *widget:focused)if(auto *button=dynamic_cast<GMMWButton*>(widget))button->SetGreen(widget==focused[focus]);
 ZGuiMainMenuBase::RenderContents(map,dest);if(category!=0)return;if(!dest)dest=ZSDL_Surface::GetMainSoftwareSurface();if(!dest)return;
 SDL_Surface *sample=SDL_CreateSurface(8,8,SDL_PIXELFORMAT_RGBA32);if(sample){for(int y=0;y<8;++y)for(int x=0;x<8;++x){SDL_Rect p{x,y,1,1};bool amber=(x+y)%2;SDL_FillSurfaceRect(sample,&p,SDL_MapSurfaceRGBA(sample,amber?244:74,amber?164:90,amber?65:104,255));}SDL_Rect dst{x+w-46,y+preview_y,28,28};SDL_BlitSurfaceScaled(sample,nullptr,dest,&dst,zod_picture_mode==PICTURE_SMOOTH?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);SDL_DestroySurface(sample);}
 float t=zod_reduced_motion?0:fmod(SDL_GetTicks()/1000.0,2.0),pos=zod_render_smoothing?t:floor(t*4)/4;SDL_Rect moving{x+10+(int)(pos*80),y+preview_y+10,8,8};SDL_FillSurfaceRect(dest,&moving,SDL_MapSurfaceRGBA(dest,244,164,65,255));
}
