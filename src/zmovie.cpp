#include "zmovie.h"
#include "zsdl.h"
#include "zaudio.h"

#include <SDL3/SDL.h>
#include <cstdio>

using namespace std;

int ZMovie_Play(const string &path) {return ZMovie_PlayPortable(path); }

static string clip_path(const string &name)
{
	return ZSDL_DataPath("assets/videos/" + name + ".mp4");
}

bool ZMovie_HasClip(const string &name)
{
	FILE *f = fopen(clip_path(name).c_str(), "rb");
	if(!f) return false;
	fclose(f);
	return true;
}

int ZMovie_PlayClip(const string &name)
{
	if(!ZMovie_HasClip(name)) return ZMOVIE_MISSING;

	ZAudio_PauseMusic(true);
	int r = ZMovie_Play(clip_path(name));
	ZAudio_PauseMusic(false);

	//the key press / click that skipped it must not also act on the screen
	//underneath
	SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
	SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
	SDL_FlushEvents(SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_CANCELED);
	SDL_FlushEvents(SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_BUTTON_UP);
	return r;
}
