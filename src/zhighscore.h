#ifndef _ZHIGHSCORE_H_
#define _ZHIGHSCORE_H_

#include <string>
#include <vector>

// Local high-score table for the single-player front-end ("High Score" in the
// main menu). One line per won match is kept in ~/.zod_highscores; the menu
// shows the overall top list and the best result per campaign level.

struct ZHighScoreEntry
{
	std::string player;     //player name at the time
	std::string map_file;   //e.g. maps/p02_bb_orig07.map
	int score;
	int seconds;            //match length (game time)
	int kills;              //enemy units destroyed
	int lost;               //own units lost
	int difficulty;         //bot_difficulty index
	long long when;         //unix time

	ZHighScoreEntry() : score(0), seconds(0), kills(0), lost(0), difficulty(1), when(0) {}
};

// Points for a won match. Rewards kills and speed, penalises losses, and
// scales with bot difficulty (Easy x0.5 ... Expert x2).
int ZHighScore_Compute(double seconds, int kills, int lost, int difficulty);

// Record a won match. Returns the 1-based overall rank it reached (0 = none).
int ZHighScore_Record(const ZHighScoreEntry &entry);

// All entries, best score first.
const std::vector<ZHighScoreEntry> &ZHighScore_All(bool reload = false);

// Best entry for a map (NULL if never won).
const ZHighScoreEntry *ZHighScore_BestFor(const std::string &map_file);

#endif
