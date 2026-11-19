#include "zmusic_engine.h"
#include "common.h"
#include "zprefs.h"

using namespace COMMON;

bool ZMusicEngine::sound_system_on = true;

ZAudio_Music *ZMusicEngine::splash_music;
ZAudio_Music *ZMusicEngine::planet_music[MAX_PLANET_TYPES];

bool ZMusicEngine::playing_planet_music = false;
int ZMusicEngine::d_level = M_CALM;
int ZMusicEngine::p_type = DESERT;
vector<d_level_start_info> ZMusicEngine::d_level_start[MAX_PLANET_TYPES][M_MAX_DANGER_LEVELS];

bool ZMusicEngine::use_midi = false;
int ZMusicEngine::current = ZMusicEngine::TUNE_NONE;
ZMidiFile ZMusicEngine::midi_splash, ZMusicEngine::midi_menu, ZMusicEngine::midi_win, ZMusicEngine::midi_lose;
ZMidiFile ZMusicEngine::midi_planet[MAX_PLANET_TYPES];
vector<d_level_start_info> ZMusicEngine::d_level_start_midi[MAX_PLANET_TYPES][M_MAX_DANGER_LEVELS];

bool ZMusicEngine::do_next_reset = false;
double ZMusicEngine::next_reset_time = 0;
double ZMusicEngine::next_change_d_level_time = 0;

void ZMusicEngine::Init()
{
	string filename;

	splash_music = ZSDL_LoadMusic("assets/sounds/ABATTLE.mp3");
	
	for(int i=0;i<MAX_PLANET_TYPES;i++)
	{
		filename = "assets/sounds/music_" + planet_type_string[i] + ".ogg";
		planet_music[i] = ZSDL_LoadMusic ( filename.c_str() );
	}

	//fixes
	if(!planet_music[DESERT]) planet_music[DESERT] = planet_music[ARCTIC];
	if(!planet_music[ARCTIC]) planet_music[ARCTIC] = planet_music[DESERT];

	if(!planet_music[VOLCANIC]) planet_music[VOLCANIC] = planet_music[CITY];
	if(!planet_music[CITY]) planet_music[CITY] = planet_music[VOLCANIC];

	InitDLevelStarts();

	//the original game's MIDI score: one track per planet, plus the title,
	//menu, victory and defeat tunes
	static const char *planet_midi[MAX_PLANET_TYPES] = { "AD1.MID", "AV1.MID", "AA1.MID", "AJ1.MID", "AC1.MID" };
	for(int i=0;i<MAX_PLANET_TYPES;i++)
		midi_planet[i].Load(ZSDL_DataPath(string("assets/sounds/") + planet_midi[i]));
	midi_splash.Load(ZSDL_DataPath("assets/sounds/ABATTLE.MID"));
	midi_menu.Load(ZSDL_DataPath("assets/sounds/AOPTIONS.MID"));
	midi_win.Load(ZSDL_DataPath("assets/sounds/AWIN.MID"));
	midi_lose.Load(ZSDL_DataPath("assets/sounds/ALOSE.MID"));

	InitMidiDLevelStarts();

	use_midi = (zod_music_style == MUSIC_STYLE_MIDI) && MidiPossible();
}

bool ZMusicEngine::MidiPossible()
{
	if(!ZAudio_MidiAvailable()) return false;
	for(int i=0;i<MAX_PLANET_TYPES;i++)
		if(!midi_planet[i].Loaded()) return false;
	return true;
}

//Section tables for the MIDI score. The planet tracks are written in 8-bar
//blocks that build up in intensity, so (as with the recorded tracks above) the
//calm, under-attack and fort-in-danger sections are ranges of blocks, each
//with a few block boundaries to enter at. Times are seconds into the MIDI.
void ZMusicEngine::InitMidiDLevelStarts()
{
	struct section { int planet; int level; double end; double in[6]; };

	const double B = 15.6098;   //one 8-bar block of the volcanic / city track (123 bpm)

	static const section table[] =
	{
		//desert + arctic (same structure; 118 -> 123 -> 121 bpm)
		{ DESERT, M_CALM,      178.99, { 0, 48.82, 81.36, 97.63, 113.90, 146.44 } },
		{ DESERT, M_ATTACKING, 273.97, { 178.99, 211.53, -1 } },
		{ DESERT, M_FORT,      370.50, { 273.97, 337.44, -1 } },

		{ VOLCANIC, M_CALM,      13 * B, { 0, 4 * B, 6 * B, 9 * B, 10 * B, -1 } },
		{ VOLCANIC, M_ATTACKING, 17 * B, { 13 * B, 15 * B, -1 } },
		{ VOLCANIC, M_FORT,      360.40, { 17 * B, 18 * B, 21 * B, -1 } },

		//jungle (126 bpm rising to 134)
		{ JUNGLE, M_CALM,      166.91, { 0, 45.72, 106.67, 121.91, -1 } },
		{ JUNGLE, M_ATTACKING, 225.54, { 166.91, 196.45, -1 } },
		{ JUNGLE, M_FORT,      341.80, { 225.54, 254.63, 283.29, -1 } },
	};

	for(size_t s=0; s<sizeof(table)/sizeof(table[0]); s++)
		for(int i=0; i<6 && table[s].in[i] >= 0; i++)
		{
			//stop a beat early so the cut lands before the next block's downbeat
			d_level_start_info info(table[s].in[i], table[s].end - 0.05 - table[s].in[i]);
			d_level_start_midi[table[s].planet][table[s].level].push_back(info);
		}

	for(int i=0; i<M_MAX_DANGER_LEVELS; i++)
	{
		d_level_start_midi[ARCTIC][i] = d_level_start_midi[DESERT][i];
		d_level_start_midi[CITY][i] = d_level_start_midi[VOLCANIC][i];
	}
}

void ZMusicEngine::PlayMidi(ZMidiFile &f, double start, int loops, int fade_ms)
{
	if(!ZSDL_GetMusicOn()) return;   //-s: all audio off

	vector<unsigned char> data = f.Slice(start);
	if(data.empty()) return;
	ZAudio_PlayMidi(&data[0], data.size(), loops, fade_ms);
}

void ZMusicEngine::PlayMenuMusic()
{
	if(!sound_system_on) return;

	if(use_midi && midi_menu.Loaded())
	{
		if(current != TUNE_MENU) PlayMidi(midi_menu, 0, -1, 0);
	}
	else if(current != TUNE_MENU && current != TUNE_SPLASH)
		ZSDL_PlayMusic(splash_music, -1);

	current = TUNE_MENU;
	playing_planet_music = false;
}

void ZMusicEngine::PlayResultMusic(bool won)
{
	if(!sound_system_on) return;

	//only the MIDI score has these; the recorded planet track just carries on
	if(!use_midi) return;

	ZMidiFile &f = won ? midi_win : midi_lose;
	if(!f.Loaded()) return;

	PlayMidi(f, 0, -1, 0);
	current = won ? TUNE_WIN : TUNE_LOSE;
	playing_planet_music = false;
	do_next_reset = false;
}

void ZMusicEngine::Stop()
{
	ZAudio_StopMusic();
	current = TUNE_NONE;
	playing_planet_music = false;
	do_next_reset = false;
}

void ZMusicEngine::ApplyStyle()
{
	bool want_midi = (zod_music_style == MUSIC_STYLE_MIDI) && MidiPossible();
	if(want_midi == use_midi) return;
	use_midi = want_midi;

	if(!sound_system_on) return;

	//restart the current tune in the other style
	int was = current;
	current = TUNE_NONE;
	switch(was)
	{
	case TUNE_SPLASH: PlaySplashMusic(); break;
	case TUNE_MENU: PlayMenuMusic(); break;
	case TUNE_PLANET: PlayPlanetMusic(p_type); break;
	case TUNE_WIN: case TUNE_LOSE: if(use_midi) PlayResultMusic(was == TUNE_WIN); else PlayPlanetMusic(p_type); break;
	}
}

void ZMusicEngine::InitDLevelStarts()
{
	double l1_end;
	double l2_end;
	double l3_end;
	double next_in;
	double next_end;
	int next_level;
	int next_planet;

	{
		next_planet = DESERT;
		l1_end = 180.23 - 1;
		l2_end = 275.87 - 1;
		l3_end = 371.47 - 1;

		{
			next_level = M_CALM;
			next_end = l1_end;
			next_in = 0;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 60;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 75;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 88.5;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 105;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 146.3;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_ATTACKING;
			next_end = l2_end;
			next_in = 180.23;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 210;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_FORT;
			next_end = l3_end;
			next_in = 275.87;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 335.77;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}
	}

	{
		next_planet = VOLCANIC;
		l1_end = 204.31 - 1;
		l2_end = 267.174 - 1;
		l3_end = 361.34 - 1;

		{
			next_level = M_CALM;
			next_end = l1_end;
			next_in = 0;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 62.68;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 94.73;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 141.93;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 157.15;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_ATTACKING;
			next_end = l2_end;
			next_in = 204.31;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 235.75;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_FORT;
			next_end = l3_end;
			next_in = 267.174;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 282.89;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 330.04;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}
	}

	{
		next_planet = JUNGLE;
		l1_end = 168.05 - 1;
		l2_end = 227.075 - 1;
		l3_end = 342.77 - 1;

		{
			next_level = M_CALM;
			next_end = l1_end;
			next_in = 0;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 46.03;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 107.4;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 122.74;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_ATTACKING;
			next_end = l2_end;
			next_in = 168.05;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 197.79;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}

		{
			next_level = M_FORT;
			next_end = l3_end;
			next_in = 227.075;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 256.37;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
			next_in = 288.83;
			d_level_start[next_planet][next_level].push_back(d_level_start_info(next_in, next_end - next_in));
		}
	}

	//copies
	for(int i=0; i<M_MAX_DANGER_LEVELS; i++)
	{
		d_level_start[ARCTIC][i] = d_level_start[DESERT][i];
		d_level_start[CITY][i] = d_level_start[VOLCANIC][i];
	}
}

void ZMusicEngine::PlaySplashMusic()
{
	if(!sound_system_on) return;

	if(use_midi && midi_splash.Loaded())
		PlayMidi(midi_splash, 0, -1, 0);
	else
		ZSDL_PlayMusic(splash_music, -1);

	current = TUNE_SPLASH;
	playing_planet_music = false;
}

void ZMusicEngine::PlayPlanetMusic(int p_type_)
{
	if(!sound_system_on) return;

	if(p_type_ < 0) return;
	if(p_type_ >= MAX_PLANET_TYPES) return;

	p_type = p_type_;

	if(use_midi)
		PlayMidi(midi_planet[p_type], 0, -1, 0);
	else
		ZSDL_PlayMusic(planet_music[p_type], -1);

	current = TUNE_PLANET;

	playing_planet_music = true;

	do_next_reset = false;

	d_level = M_CALM;
}

void ZMusicEngine::Process(vector<ZObject*> &object_list, ZMap &tmap, int our_team, int fort_ref_id)
{
	if(!playing_planet_music) return;

	double the_time = current_time();
	static double next_check_time = 0;
	ZObject *our_fort = NULL;

	//get fort
	our_fort = ZObject::GetObjectFromID(fort_ref_id, object_list);

	//see what our current d level is
	if(our_fort && the_time >= next_check_time)
	{
		next_check_time = the_time + 0.25;

		int new_d_level = M_CALM;
		static int last_new_d_level = M_CALM;
		
		if(our_fort->IsDestroyed())
		{
			new_d_level = M_CALM;
		}
		else
		{
			//anyone in our fort area?
			{
				int fx, fy;

				our_fort->GetCenterCords(fx, fy);

				for(vector<ZObject*>::iterator obj=object_list.begin(); obj!=object_list.end(); obj++)
				{
					unsigned char ot, oid;

					if((*obj)->GetOwner() == NULL_TEAM) continue;
					if((*obj)->GetOwner() == our_team) continue;

					(*obj)->GetObjectID(ot, oid);

					if(ot == BUILDING_OBJECT) continue;
					if(ot == MAP_ITEM_OBJECT) continue;

					int ox, oy;

					(*obj)->GetCenterCords(ox, oy);

					if(points_within_distance(fx, fy, ox, oy, 250))
					{
						new_d_level = M_FORT;
						break;
					}
				}
			}

			//are we under attack?
			if(new_d_level == M_CALM)
			{
				for(vector<ZObject*>::iterator obj=object_list.begin(); obj!=object_list.end(); obj++)
				{
					unsigned char ot, oid;

					if(!(*obj)->GetAttackObject()) continue;

					if((*obj)->GetAttackObject()->GetOwner() == our_team)
					{
						new_d_level = M_ATTACKING;
						break;
					}

					if((*obj)->GetOwner() == our_team && (*obj)->GetAttackObject()->GetOwner() != NULL_TEAM)
					{
						new_d_level = M_ATTACKING;
						break;
					}
				}
			}
		}
		

		if(last_new_d_level != new_d_level)
		{
			int inc_time;

			if(our_fort->IsDestroyed()) 
				inc_time = 15;
			else
				inc_time = 3;

			last_new_d_level = new_d_level;

			if(the_time + inc_time > next_change_d_level_time)
				next_change_d_level_time = the_time + inc_time;
		}

		if(the_time >= next_change_d_level_time)
			SetDangerLevel(new_d_level);
	}

	if(do_next_reset && the_time >= next_reset_time)
	{
		do_next_reset = false;

		ResetMusic();
	}
}

void ZMusicEngine::SetDangerLevel(int d_level_)
{
	if(d_level_ < 0) return;
	if(d_level_ >= M_MAX_DANGER_LEVELS) return;

	//if(current_time() < next_change_d_level_time) return;

	if(d_level_ == d_level) return;

	d_level = d_level_;

	ResetMusic();
}

void ZMusicEngine::ResetMusic()
{
	if(!playing_planet_music) return;

	if(d_level < 0) return;
	if(d_level >= M_MAX_DANGER_LEVELS) return;

	vector<d_level_start_info> &starts = use_midi ? d_level_start_midi[p_type][d_level] : d_level_start[p_type][d_level];

	//even got info for this level?
	if(!starts.size())
	{
		printf("ZMusicEngine::ResetMusic:No start information for this danger level\n");
		return;
	}

	int random_s;

	random_s = rand() % starts.size();

	//printf("ResetMusic:: p:%d d:%d ran:%d\n", p_type, d_level, random_s);

	//set the position
	if(use_midi)
	{
		//a MIDI can't be seeked cheaply: play a slice that starts there, with a
		//short fade so the change of section isn't a hard cut
		PlayMidi(midi_planet[p_type], starts[random_s].position, -1, 120);
	}
	else if(!ZAudio_SetMusicPosition(starts[random_s].position))
		printf("ZAudio_SetMusicPosition: %s\n", SDL_GetError());

	//tell the system when to reset again
	double the_time = current_time();
	do_next_reset = true;
	next_reset_time = the_time + starts[random_s].length;

	switch(d_level)
	{
	case M_CALM: next_change_d_level_time = the_time + 5; break;
	case M_ATTACKING: next_change_d_level_time = the_time + 7; break;
	case M_FORT: next_change_d_level_time = the_time + 3; break;
	default: next_change_d_level_time = the_time + 7; break;
	}
}
