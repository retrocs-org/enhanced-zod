#ifndef _ZMUSIC_ENGINE_H_
#define _ZMUSIC_ENGINE_H_

#include "constants.h"
#include "zsdl.h"
#include "zmap.h"
#include "zobject.h"
#include "zmidi.h"

#include <vector>

using namespace std;

enum music_danger_level
{
	M_CALM, M_ATTACKING, M_FORT, M_MAX_DANGER_LEVELS
};

class d_level_start_info
{
public:
	d_level_start_info() {}
	d_level_start_info(double position_, double length_) 
	{
		position = position_;
		length = length_;
	}

	double position;
	double length;
};

class ZMusicEngine
{
public:
	ZMusicEngine() {}

	static void Init();

	static void SetMusicOn(bool music_on_) { sound_system_on = music_on_; }
	static void PlaySplashMusic();
	static void PlayMenuMusic();              //start menu (MIDI: the original options tune)
	static void PlayResultMusic(bool won);    //win / lose tune after a match (MIDI only)
	static void PlayPlanetMusic(int p_type_);
	//re-read zod_music_style and restart whatever is playing in the new style
	static void ApplyStyle();
	static void Stop();
	//true when the original MIDI score is what is actually playing
	static bool UsingMidi() { return use_midi; }
	static bool MidiPossible();

	static void Process(vector<ZObject*> &object_list, ZMap &tmap, int our_team, int fort_ref_id);
	static void SetDangerLevel(int d_level_);
	static void ResetMusic();
	static int GetDangerLevel() { return d_level; }

private:
	static void InitDLevelStarts();

	static bool sound_system_on;

	static ZAudio_Music *splash_music;
	static ZAudio_Music *planet_music[MAX_PLANET_TYPES];

	static bool playing_planet_music;
	static int d_level;
	static int p_type;
	static vector<d_level_start_info> d_level_start[MAX_PLANET_TYPES][M_MAX_DANGER_LEVELS];

	//original MIDI score
	enum current_tune { TUNE_NONE, TUNE_SPLASH, TUNE_MENU, TUNE_PLANET, TUNE_WIN, TUNE_LOSE };
	static bool use_midi;
	static int current;
	static ZMidiFile midi_splash, midi_menu, midi_win, midi_lose;
	static ZMidiFile midi_planet[MAX_PLANET_TYPES];
	static vector<d_level_start_info> d_level_start_midi[MAX_PLANET_TYPES][M_MAX_DANGER_LEVELS];
	static void InitMidiDLevelStarts();
	static void PlayMidi(ZMidiFile &f, double start, int loops, int fade_ms);

	static bool do_next_reset;
	static double next_reset_time;
	static double next_change_d_level_time;

};

#endif
