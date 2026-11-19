#ifndef _ZPREFS_H_
#define _ZPREFS_H_

#include <string>

// Player-local preferences, persisted to ~/.zod_prefs and editable from the
// in-game Options menu. Load order: defaults, then the prefs file, then the
// env vars (ZOD_CLASSIC_MOUSE / ZOD_SMOOTH) - so env always wins a launch.

// Classic (original-Z) mouse: left click orders and drops the selection,
// right click cancels. Off = RTS-style right-click orders.
extern bool zod_classic_mouse;

// Audio volume (a sound_setting index, 0..MAX_SOUND_SETTINGS-1) and game speed
// multiplier, remembered across sessions so the Options menu values stick (#73).
extern int zod_volume_setting;
extern int zod_video_volume;extern bool zod_video_subtitles;
extern int zod_effects_volume,zod_music_volume; // independent 0..100 gains
extern double zod_game_speed;
extern int zod_text_percent,zod_cursor_percent;
extern bool zod_team_markers,zod_reduced_motion;
extern double zod_menu_scale;   //#196: UI scale for menus + resume bar

// Bot AI difficulty (an index into the profiles in zbot.cpp). NORMAL == the
// original behavior, and is the default - so a regular campaign with no -D flag
// and no Options change plays exactly as before. Steers *decision quality*
// (think rate, how many units it orders, how fast, how aggressively), never unit
// stats. Read live by the bot thread.
enum bot_difficulty { BOT_DIFF_EASY, BOT_DIFF_NORMAL, BOT_DIFF_HARD, BOT_DIFF_EXPERT, MAX_BOT_DIFFICULTY };
extern int zod_bot_difficulty;
extern const char *bot_difficulty_name[MAX_BOT_DIFFICULTY];

//#75: max combat units per team. Adjustable in Options (UNIT_LIMIT_MIN..MAX);
//read at the start of each game, so a change takes effect from the next game.
extern int zod_max_units_per_team;

//#246: the player's display name, persisted so it's not "Player" for everyone in
//multiplayer. Editable in Options. Empty here means "unset" - main defaults it to
//the OS login name on first run; an explicit -n on the command line still wins.
extern std::string zod_player_name;

//front-end (Z-style start menu) settings
extern int  zod_gamma;          //-ZVIDEO_GAMMA_STEPS..+ZVIDEO_GAMMA_STEPS, 0 = neutral
extern bool zod_fullscreen;     //remembered window mode for the menu launch
extern bool zod_scenes;         //intro splash + mission briefing cards
extern bool zod_music;          //menu/planet music on
//which soundtrack: the original game's MIDI score (needs a SoundFont) or the
//engine's recorded tracks. MIDI falls back to recorded when it can't play.
enum music_style { MUSIC_STYLE_MIDI, MUSIC_STYLE_RECORDED, MAX_MUSIC_STYLES };
extern int zod_music_style;
extern bool zod_frontend;       //Z-style front-end (false = classic fenio menu)

//how the game picture is scaled to the window: smooth (linear), sharp (nearest)
//or pixel-perfect (whole-number scale, black bars when it does not fit)
enum picture_mode { PICTURE_SMOOTH, PICTURE_SHARP, PICTURE_PIXEL_PERFECT, MAX_PICTURE_MODES };
extern int zod_picture_mode;

//controller: how fast the left stick moves the cursor
enum stick_speed { STICK_SLOW, STICK_NORMAL, STICK_FAST, MAX_STICK_SPEEDS };
extern int zod_stick_speed;

//rebindable keys (printable ASCII; stored as the lowercase character)
enum zod_key_action
{
	ZKEY_PAN_UP, ZKEY_PAN_DOWN, ZKEY_PAN_LEFT, ZKEY_PAN_RIGHT,
	ZKEY_PAUSE, ZKEY_PLAYER_LIST, ZKEY_CHAT_HISTORY, ZKEY_SMOOTHING,
	ZKEY_IDLE_UNIT,ZKEY_IDLE_FACTORY,
	MAX_ZKEYS
};
extern int zod_keys[MAX_ZKEYS];
const char *ZPrefs_KeyActionName(int action);          //"Pan up", "Pause", ...
int  ZPrefs_KeyDefault(int action);
std::string ZPrefs_KeyLabel(int key);                  //"W", "Z", ...
bool ZPrefs_KeyIs(int typed, int action);              //does this typed character trigger the action?
int  ZPrefs_PanActionForKey(int keycode);              //ZKEY_PAN_* for a key-down/up keycode, -1 if none
void ZPrefs_ResetKeys();

void ZPrefs_ResetCategory(int category); // Graphics, Audio, Game, Controls, Media
void ZPrefs_Load();
void ZPrefs_Save();

#endif
