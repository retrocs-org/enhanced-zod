#ifndef _ZMPCLIENT_H_
#define _ZMPCLIENT_H_

#include <string>
#include <vector>
#include <cstdint>

// Client for the match orchestrator (see orchestrator/). Talks JSON/HTTP to it
// and hands the menus simple structs - the menus never touch HTTP or JSON. The
// orchestrator address comes from $ZOD_ORCHESTRATOR; if the default public
// service fails, Windows/macOS/Linux can host a local server instead. Set it
// to "local" to choose local hosting immediately.

struct MatchInfo
{
	std::string id;
	std::string name;
	std::string map;
	std::string host;
	int port;
	int players;
	bool lan = false, started = false, portal = false;
    int ping_ms=-1;
    std::string region="Unspecified",game_mode="Skirmish";
	int capacity;   //max humans = the map's player slots

	MatchInfo() : port(-1), players(-1), capacity(0) {}
};

// A map the orchestrator can spawn, with its player-slot count (from --map-info).
struct MapMeta
{
	std::string name;
	int players;

	MapMeta() : players(0) {}
};

// Settings are passed to the server process, never applied to the parent session.
struct MatchOptions
{
    int human_team = 1;
    int difficulty = 1;
    double game_speed = 1.0;
};
struct MapPreview
{
    bool valid = false;
    int width = 0, height = 0, terrain = 0;
    int preview_width = 0, preview_height = 0;
    std::string name, title, error,details;
    std::vector<int> teams;
    std::vector<std::uint32_t> pixels; //ARGB8888, with fort markers
};
class ZMap;
bool ZMP_ReadMapDataPreview(ZMap &map,MapPreview &out);
bool ZMP_ReadMapFilePreview(const std::string &path, MapPreview &out);
bool ZMP_ReadMapPreview(const std::string &name, MapPreview &out);
bool ZMP_ValidateSetup(const MapPreview &map, const std::vector<std::string> &bots,
                       const MatchOptions &options, std::string &error);

// GET /matches. Returns the live matches; ok=false on any transport/parse error.
bool ZMP_ListMatches(std::vector<MatchInfo> &out);

// POST /matches with {map, bots}. On success fills `out` (incl. host/port to
// join) and returns true.
bool ZMP_CreateMatch(const std::string &map, const std::vector<std::string> &bots, MatchInfo &out,
                     const MatchOptions &options = MatchOptions());

// POST /matchmake. Returns the open match to drop a "play with someone" joiner
// into (the orchestrator seeds one if needed). Fills `out` (host/port) on success.
bool ZMP_Matchmake(MatchInfo &out);

// GET /maps. The maps the orchestrator can spawn, each with its player-slot count
// (the create form needs the filenames; the count drives the "(NP)" label).
bool ZMP_ListMaps(std::vector<MapMeta> &out);

// Error detail for the last request on this thread.
std::string ZMP_LastError();

// The orchestrator address the menus would connect to (for display).
std::string ZMP_OrchestratorAddress();

#endif
