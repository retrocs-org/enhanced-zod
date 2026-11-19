#include "zhighscore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>

using namespace std;

static vector<ZHighScoreEntry> g_scores;
static bool g_loaded = false;

static const int MAX_KEPT = 200;

static string score_path()
{
	const char *home = getenv("HOME");
	if(!home || !home[0]) home = getenv("USERPROFILE");
	if(!home || !home[0]) return ".zod_highscores";
	return string(home) + "/.zod_highscores";
}

static string base_of(const string &f)
{
	size_t slash = f.find_last_of("/\\");
	return slash == string::npos ? f : f.substr(slash + 1);
}

static bool by_score(const ZHighScoreEntry &a, const ZHighScoreEntry &b)
{
	if(a.score != b.score) return a.score > b.score;
	return a.seconds < b.seconds;
}

static void load()
{
	g_scores.clear();
	g_loaded = true;

	FILE *fp = fopen(score_path().c_str(), "r");
	if(!fp) return;

	char line[1024];
	while(fgets(line, sizeof(line), fp))
	{
		//score \t seconds \t kills \t lost \t difficulty \t when \t map \t player
		ZHighScoreEntry e;
		char map[512] = "", player[256] = "";
		if(sscanf(line, "%d\t%d\t%d\t%d\t%d\t%lld\t%511[^\t]\t%255[^\r\n]",
			&e.score, &e.seconds, &e.kills, &e.lost, &e.difficulty, &e.when, map, player) >= 7)
		{
			e.map_file = map;
			e.player = player[0] ? player : "Player";
			g_scores.push_back(e);
		}
	}
	fclose(fp);

	stable_sort(g_scores.begin(), g_scores.end(), by_score);
}

static void save()
{
	FILE *fp = fopen(score_path().c_str(), "w");
	if(!fp) return;

	for(size_t i=0;i<g_scores.size() && (int)i<MAX_KEPT;i++)
	{
		const ZHighScoreEntry &e = g_scores[i];
		string player = e.player;
		for(size_t c=0;c<player.size();c++) if(player[c] == '\t' || player[c] == '\n') player[c] = ' ';
		fprintf(fp, "%d\t%d\t%d\t%d\t%d\t%lld\t%s\t%s\n",
			e.score, e.seconds, e.kills, e.lost, e.difficulty, e.when, e.map_file.c_str(), player.c_str());
	}
	fclose(fp);
}

int ZHighScore_Compute(double seconds, int kills, int lost, int difficulty)
{
	static const double mult[] = { 0.5, 1.0, 1.5, 2.0 };
	if(difficulty < 0) difficulty = 0;
	if(difficulty > 3) difficulty = 3;

	double time_bonus = 1200.0 - seconds;   //finish inside 20 minutes for a bonus
	if(time_bonus < 0) time_bonus = 0;

	double points = 1000.0 + kills * 25.0 - lost * 10.0 + time_bonus * 2.0;
	if(points < 100) points = 100;

	return (int)(points * mult[difficulty] + 0.5);
}

int ZHighScore_Record(const ZHighScoreEntry &entry)
{
	if(!g_loaded) load();

	ZHighScoreEntry e = entry;
	e.map_file = base_of(e.map_file);
	if(!e.when) e.when = (long long)time(NULL);

	g_scores.push_back(e);
	stable_sort(g_scores.begin(), g_scores.end(), by_score);

	int rank = 0;
	for(size_t i=0;i<g_scores.size();i++)
		if(g_scores[i].when == e.when && g_scores[i].map_file == e.map_file && g_scores[i].score == e.score)
		{
			rank = (int)i + 1;
			break;
		}

	if((int)g_scores.size() > MAX_KEPT) g_scores.resize(MAX_KEPT);
	save();

	return rank;
}

const vector<ZHighScoreEntry> &ZHighScore_All(bool reload)
{
	if(!g_loaded || reload) load();
	return g_scores;
}

const ZHighScoreEntry *ZHighScore_BestFor(const string &map_file)
{
	if(!g_loaded) load();
	for(size_t i=0;i<g_scores.size();i++)
		if(base_of(g_scores[i].map_file) == base_of(map_file)) return &g_scores[i];   //sorted: first hit is best
	return NULL;
}
