#ifndef _ZMOVIE_H_
#define _ZMOVIE_H_

#include <string>
#include <vector>
struct ZSubtitleCue {double begin,end;std::string text;};
std::vector<ZSubtitleCue> ZMovie_LoadSubtitles(const std::string &path);
int ZMovie_PlayPortable(const std::string &path);

// Full-window video playback for the original Z cutscenes (intro, travel, win,
// lose, outro). The clips are NOT shipped with the engine: the user puts them in
// assets/videos/ (E_INTRO.mp4, E_ATRAV1.mp4, ...). A missing clip is simply
// skipped. Playback uses bundled FFmpeg decoding and SDL video/audio on desktop platforms.
// Adjacent SRT subtitles and saved video-volume controls are supported.

enum ZMovieResult
{
	ZMOVIE_MISSING = 0,   //no such clip / cannot play here - nothing happened
	ZMOVIE_FINISHED = 1,  //played to the end
	ZMOVIE_SKIPPED = 2    //the player pressed a key / clicked to skip it
};

// Play one file by full path. Blocks until it ends or is skipped.
// Space pauses, Escape/Enter/click skips, S toggles subtitles, Up/Down changes volume.
int ZMovie_Play(const std::string &path);

// Does assets/videos/<name>.mp4 exist?
bool ZMovie_HasClip(const std::string &name);

// Play assets/videos/<name>.mp4 (music is paused meanwhile, input flushed after).
int ZMovie_PlayClip(const std::string &name);

#endif
