#include "zprefs.h"
#include <algorithm>
#include "constants.h"	//#75: DEFAULT_MAX_UNITS_PER_TEAM + UNIT_LIMIT bounds

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using namespace std;

bool zod_classic_mouse = true;

//#73: remembered Options values. Defaults match the in-code defaults - full
//volume (SOUND_100 == 4, see enum sound_setting in zsdl.h) and normal speed.
int zod_volume_setting = 4;
int zod_effects_volume=100,zod_music_volume=100;
int zod_video_volume=100;bool zod_video_subtitles=true;
double zod_game_speed = 1.0;

//#196: UI scale for menus + the resume bar. Default 2x (bigger is friendlier on
//both touch and modern high-res displays); auto-capped per menu so nothing clips.
double zod_menu_scale = 2.0;
int zod_text_percent=100,zod_cursor_percent=100;
bool zod_team_markers=false,zod_reduced_motion=false;
int zod_picture_mode = PICTURE_SMOOTH;
int zod_stick_speed = STICK_NORMAL;

static const struct { const char *pref; const char *name; int def; } g_key_table[MAX_ZKEYS] =
{
	{ "key_pan_up",       "Pan up",         'w' },
	{ "key_pan_down",     "Pan down",       's' },
	{ "key_pan_left",     "Pan left",       'a' },
	{ "key_pan_right",    "Pan right",      'd' },
	{ "key_pause",        "Pause / resume", 'z' },
	{ "key_player_list",  "Player list",    'p' },
	{ "key_chat_history", "Chat history",   'h' },
	{ "key_smoothing",    "Smoothing",      'y' },
    { "key_idle_unit",    "Next idle unit", 'i' },
    { "key_idle_factory", "Next idle factory", 'o' },
};
int zod_keys[MAX_ZKEYS] = { 'w', 's', 'a', 'd', 'z', 'p', 'h', 'y', 'i', 'o' };

const char *ZPrefs_KeyActionName(int a) { return (a >= 0 && a < MAX_ZKEYS) ? g_key_table[a].name : ""; }
int ZPrefs_KeyDefault(int a) { return (a >= 0 && a < MAX_ZKEYS) ? g_key_table[a].def : 0; }
void ZPrefs_ResetKeys() { for(int i=0;i<MAX_ZKEYS;i++) zod_keys[i] = g_key_table[i].def; }

std::string ZPrefs_KeyLabel(int key)
{
	std::string s;
	if(key >= 'a' && key <= 'z') s += (char)(key - 32);
	else if(key > 32 && key < 127) s += (char)key;
	else s = "?";
	return s;
}

bool ZPrefs_KeyIs(int typed, int action)
{
	if(action < 0 || action >= MAX_ZKEYS) return false;
	if(typed >= 'A' && typed <= 'Z') typed += 32;
	return typed == zod_keys[action];
}

int ZPrefs_PanActionForKey(int keycode)
{
	for(int a = ZKEY_PAN_UP; a <= ZKEY_PAN_RIGHT; a++)
		if(ZPrefs_KeyIs(keycode, a)) return a;
	return -1;
}

//bot AI difficulty - default NORMAL keeps the original behavior (opt-in)
int zod_bot_difficulty = BOT_DIFF_NORMAL;
const char *bot_difficulty_name[MAX_BOT_DIFFICULTY] = { "Easy", "Normal", "Hard", "Expert" };
int zod_max_units_per_team = DEFAULT_MAX_UNITS_PER_TEAM;	//#75

//#246: display name, persisted so multiplayer isn't "Player" vs "Player". Empty
//means unset (main fills it from the OS login on first run).
string zod_player_name = "";

int  zod_gamma = 0;
bool zod_fullscreen = false;
bool zod_scenes = true;
bool zod_music = true;
int  zod_music_style = MUSIC_STYLE_MIDI;
bool zod_frontend = true;

//defined in zobject.cpp (the render-smoothing master toggle)
extern bool zod_render_smoothing;

static string prefs_path()
{
	const char *home = getenv("HOME");
	if(!home || !home[0]) home = getenv("USERPROFILE"); //Windows
	if(!home || !home[0]) return ".zod_prefs";          //last resort: cwd

	return string(home) + "/.zod_prefs";
}

void ZPrefs_Load()
{
	FILE *fp = fopen(prefs_path().c_str(), "r");

	if(fp)
	{
		char line[256];

		while(fgets(line, sizeof(line), fp))
		{
			char *eq = strchr(line, '=');
			if(!eq) continue;
			*eq = 0;

			const char *key = line;
			bool value = (eq[1] == '1');

			if(!strcmp(key,"text_percent"))zod_text_percent=std::max(100,std::min(120,atoi(eq+1)));
            else if(!strcmp(key,"cursor_percent"))zod_cursor_percent=std::max(100,std::min(200,atoi(eq+1)));
            else if(!strcmp(key,"team_markers"))zod_team_markers=value;
            else if(!strcmp(key,"reduced_motion"))zod_reduced_motion=value;
            else if(!strcmp(key, "classic_mouse")) zod_classic_mouse = value;
			else if(!strcmp(key, "render_smoothing")) zod_render_smoothing = value;
			else if(!strcmp(key, "volume")) {zod_volume_setting=atoi(eq+1);zod_effects_volume=std::max(0,std::min(100,zod_volume_setting*25));}
            else if(!strcmp(key,"effects_volume"))zod_effects_volume=std::max(0,std::min(100,atoi(eq+1)));
            else if(!strcmp(key,"video_volume"))zod_video_volume=std::max(0,std::min(100,atoi(eq+1)));
            else if(!strcmp(key,"video_subtitles"))zod_video_subtitles=atoi(eq+1)!=0;
            else if(!strcmp(key,"music_volume"))zod_music_volume=std::max(0,std::min(100,atoi(eq+1)));
			else if(!strcmp(key, "game_speed")) zod_game_speed = atof(eq + 1);
			else if(!strcmp(key, "menu_scale"))
			{
				zod_menu_scale = atof(eq + 1);
				if(zod_menu_scale < 1.0) zod_menu_scale = 1.0;
				if(zod_menu_scale > 2.0) zod_menu_scale = 2.0;   //#207: cap at 2x (3x glitched)
			}
			else if(!strcmp(key, "bot_difficulty"))
			{
				zod_bot_difficulty = atoi(eq + 1);
				if(zod_bot_difficulty < 0 || zod_bot_difficulty >= MAX_BOT_DIFFICULTY)
					zod_bot_difficulty = BOT_DIFF_NORMAL;
			}
			else if(!strcmp(key, "max_units_per_team"))
			{
				zod_max_units_per_team = atoi(eq + 1);
				if(zod_max_units_per_team < UNIT_LIMIT_MIN) zod_max_units_per_team = UNIT_LIMIT_MIN;
				if(zod_max_units_per_team > UNIT_LIMIT_MAX) zod_max_units_per_team = UNIT_LIMIT_MAX;
			}
			else if(!strcmp(key, "gamma"))
			{
				zod_gamma = atoi(eq + 1);
				if(zod_gamma < -3) zod_gamma = -3;
				if(zod_gamma > 3) zod_gamma = 3;
			}
			else if(!strcmp(key, "fullscreen")) zod_fullscreen = value;
			else if(!strcmp(key, "scenes")) zod_scenes = value;
			else if(!strcmp(key, "music")) zod_music = value;
			else if(!strcmp(key, "music_style"))
			{
				zod_music_style = atoi(eq + 1);
				if(zod_music_style < 0 || zod_music_style >= MAX_MUSIC_STYLES) zod_music_style = MUSIC_STYLE_MIDI;
			}
			else if(!strcmp(key, "frontend")) zod_frontend = value;
			else if(!strcmp(key, "picture"))
			{
				zod_picture_mode = atoi(eq + 1);
				if(zod_picture_mode < 0 || zod_picture_mode >= MAX_PICTURE_MODES) zod_picture_mode = PICTURE_SMOOTH;
			}
			else if(!strcmp(key, "stick_speed"))
			{
				zod_stick_speed = atoi(eq + 1);
				if(zod_stick_speed < 0 || zod_stick_speed >= MAX_STICK_SPEEDS) zod_stick_speed = STICK_NORMAL;
			}
			else if(!strncmp(key, "key_", 4))
			{
				for(int a=0;a<MAX_ZKEYS;a++)
					if(!strcmp(key, g_key_table[a].pref))
					{
						int k = atoi(eq + 1);
						if(k > 32 && k < 127) zod_keys[a] = (k >= 'A' && k <= 'Z') ? k + 32 : k;
					}
			}
			else if(!strcmp(key, "player_name"))
			{
				char *v = eq + 1;
				size_t vl = strlen(v);
				while(vl && (v[vl-1] == '\n' || v[vl-1] == '\r')) v[--vl] = 0;
				zod_player_name = v;
			}
		}

		fclose(fp);
	}

	//env vars override the file for this launch
	{
		const char *e = getenv("ZOD_CLASSIC_MOUSE");
		if(e && e[0]) zod_classic_mouse = (e[0] != '0');
	}
	{
		const char *e = getenv("ZOD_SMOOTH");
		if(e && e[0] == '0') zod_render_smoothing = false;
	}
	{
		//ZOD_FRONTEND=0 brings back the classic in-game start menu
		const char *e = getenv("ZOD_FRONTEND");
		if(e && e[0]) zod_frontend = (e[0] != '0');
	}
}

void ZPrefs_Save()
{
	FILE *fp = fopen(prefs_path().c_str(), "w");

	if(!fp) return;

	fprintf(fp,"text_percent=%d\ncursor_percent=%d\nteam_markers=%d\nreduced_motion=%d\n",zod_text_percent,zod_cursor_percent,zod_team_markers,zod_reduced_motion);
	fprintf(fp, "classic_mouse=%d\n", zod_classic_mouse ? 1 : 0);
	fprintf(fp, "render_smoothing=%d\n", zod_render_smoothing ? 1 : 0);
	fprintf(fp,"video_volume=%d\nvideo_subtitles=%d\n",zod_video_volume,zod_video_subtitles);
	fprintf(fp,"volume=%d\neffects_volume=%d\nmusic_volume=%d\n",zod_volume_setting,zod_effects_volume,zod_music_volume);
	fprintf(fp, "game_speed=%g\n", zod_game_speed);
	fprintf(fp, "menu_scale=%g\n", zod_menu_scale);
	fprintf(fp, "bot_difficulty=%d\n", zod_bot_difficulty);
	fprintf(fp, "max_units_per_team=%d\n", zod_max_units_per_team);
	fprintf(fp, "gamma=%d\n", zod_gamma);
	fprintf(fp, "fullscreen=%d\n", zod_fullscreen ? 1 : 0);
	fprintf(fp, "scenes=%d\n", zod_scenes ? 1 : 0);
	fprintf(fp, "music=%d\n", zod_music ? 1 : 0);
	fprintf(fp, "music_style=%d\n", zod_music_style);
	fprintf(fp, "frontend=%d\n", zod_frontend ? 1 : 0);
	fprintf(fp, "picture=%d\n", zod_picture_mode);
	fprintf(fp, "stick_speed=%d\n", zod_stick_speed);
	for(int a=0;a<MAX_ZKEYS;a++) fprintf(fp, "%s=%d\n", g_key_table[a].pref, zod_keys[a]);
	if(zod_player_name.size()) fprintf(fp, "player_name=%s\n", zod_player_name.c_str());
	fclose(fp);
}

extern bool zod_render_smoothing;
void ZPrefs_ResetCategory(int category){
 switch(category){
 case 0:zod_fullscreen=false;zod_gamma=0;zod_picture_mode=PICTURE_SMOOTH;zod_render_smoothing=true;zod_menu_scale=2;break;
 case 1:zod_effects_volume=zod_music_volume=100;zod_volume_setting=4;zod_music=true;zod_music_style=MUSIC_STYLE_MIDI;break;
 case 2:zod_game_speed=1;zod_scenes=true;zod_bot_difficulty=BOT_DIFF_NORMAL;zod_max_units_per_team=DEFAULT_MAX_UNITS_PER_TEAM;break;
 case 3:zod_classic_mouse=true;zod_stick_speed=STICK_NORMAL;ZPrefs_ResetKeys();break;
 case 5:zod_text_percent=zod_cursor_percent=100;zod_team_markers=zod_reduced_motion=false;break;
 case 4:zod_scenes=true;zod_video_volume=100;zod_video_subtitles=true;break;
 }
 ZPrefs_Save();
}
