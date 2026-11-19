#include "zmpclient.h"
#include "zhttp.h"
#include "zlocalserver.h"

#include "zmap.h"
#include "zasset_import.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <cerrno>
#include <cmath>
#include "zprefs.h"
#include "zskirmish.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

using namespace std;

// --- Local fallback -----------------------------------------------------------
// The default orchestrator is a public server that may be gone. When it can't be
// reached (and $ZOD_ORCHESTRATOR isn't set), Multiplayer still works on this
// machine: maps come from the local maps/ folder and "Create Game" starts a
// dedicated server (this same executable with -d) on a free port 2300-2399.
// Other computers on the LAN can join it by address.
static std::mutex g_local_mutex;
static thread_local string g_last_error;
string ZMP_LastError() { return g_last_error; }

static bool g_orch_down = false;          //remember a failed lookup for this session
static vector<MapMeta> g_local_maps;
static bool g_local_maps_read = false;

static bool local_mode_requested()
{
	const char *e = getenv("ZOD_ORCHESTRATOR");
	return e && strcmp(e, "local") == 0;
}

static bool orchestrator_overridden()
{
	const char *e = getenv("ZOD_ORCHESTRATOR");
	return e && e[0] && !local_mode_requested();
}

struct LocalMatch
{
	ZLocalServerProcess process;
	MatchInfo info;
};
static vector<LocalMatch> g_local_matches;
static bool g_atexit_set = false;

static void local_kill_all()
{
	std::lock_guard<std::mutex> lock(g_local_mutex);
	for(size_t i = 0; i < g_local_matches.size(); i++)
		g_local_matches[i].process.Stop();
}

static void local_reap()
{
	for(size_t i = 0; i < g_local_matches.size();)
	{
		if(!g_local_matches[i].process.Running())
			g_local_matches.erase(g_local_matches.begin() + i);
		else
			i++;
	}
}

static string self_exe_path()
{
	char buf[4096];
#ifdef _WIN32
	wchar_t path[32768];
	DWORD n = GetModuleFileNameW(nullptr, path, 32768);
	if(n && n < 32768) {
		int length = WideCharToMultiByte(CP_UTF8, 0, path, n, nullptr, 0, nullptr, nullptr);
		string result(length, '\0');
		if(length) WideCharToMultiByte(CP_UTF8, 0, path, n, &result[0], length, nullptr, nullptr);
		return result;
	}
#elif defined(__APPLE__)
	uint32_t n = sizeof(buf);
	if(_NSGetExecutablePath(buf, &n) == 0) return buf;
#else
	ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if(n > 0) { buf[n] = 0; return buf; }
#endif
	return "";
}

static bool port_is_free(int port)
{
#ifdef _WIN32
	WSADATA data;
	if(WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
	SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd == INVALID_SOCKET) { WSACleanup(); return false; }
	// SO_REUSEADDR on Windows can allow binding an occupied listener's port.
	BOOL exclusive = TRUE;
	setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
#else
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0) return false;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
	sockaddr_in a{};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons((unsigned short)port);
	bool ok = ::bind(fd, (sockaddr*)&a, sizeof(a)) == 0;
#ifdef _WIN32
	closesocket(fd); WSACleanup();
#else
	close(fd);
#endif
	return ok;
}

//player slots = distinct teams with a fort placed (same rule as --map-info)
static int map_player_slots(const string &path)
{
	ZMap m;
	if(!m.Read(path.c_str())) return 0;

	bool team_has_fort[MAX_TEAM_TYPES] = {false};
	for(vector<map_object>::iterator i = m.GetObjectList().begin(); i != m.GetObjectList().end(); ++i)
		if(i->object_type == BUILDING_OBJECT &&
			(i->object_id == FORT_FRONT || i->object_id == FORT_BACK) &&
			i->owner > NULL_TEAM && i->owner < MAX_TEAM_TYPES)
			team_has_fort[(int)i->owner] = true;

	int players = 0;
	for(int t = 0; t < MAX_TEAM_TYPES; t++)
		if(team_has_fort[t]) players++;
	return players;
}

bool ZMP_ReadMapPreview(const string &name, MapPreview &out)
{
    out = MapPreview(); out.name = name;
    string path;
    if(!ZSkirmish_MapPath(name,path)) { out.error="Invalid map filename"; return false; }
    bool valid=ZMP_ReadMapFilePreview(path,out);out.name=name;return valid;
}
bool ZMP_ReadMapFilePreview(const string &path,MapPreview &out) {
    out=MapPreview();out.name=path;
    ZMap map;
    if(!map.Read(path.c_str())) { out.error = "Map not installed locally"; return false; }
    auto &header = map.GetMapBasics();
    out.width = header.width; out.height = header.height; out.terrain = header.terrain_type;
    out.title = header.map_name;
    for(const auto &object : map.GetObjectList())
        if(object.object_type == BUILDING_OBJECT && (object.object_id == FORT_FRONT || object.object_id == FORT_BACK)
           && object.owner > NULL_TEAM && object.owner < MAX_TEAM_TYPES
           && find(out.teams.begin(), out.teams.end(), object.owner) == out.teams.end()) out.teams.push_back(object.owner);
    sort(out.teams.begin(), out.teams.end());
    if(out.teams.empty()) { out.error = "Map has no playable forts"; return false; }
    double scale = std::min(160.0 / out.width, 108.0 / out.height);
    out.preview_width = std::max(1, (int)(out.width * scale));
    out.preview_height = std::max(1, (int)(out.height * scale));
    out.pixels.assign(out.preview_width * out.preview_height, 0xff665544);
    SDL_Surface *palette = SDL_LoadBMP(ZSDL_DataPath("assets/planets/" + planet_type_string[out.terrain] + ".bmp").c_str());
    for(int y = 0; palette && y < out.preview_height; ++y)
        for(int x = 0; x < out.preview_width; ++x) {
            int tx = x * out.width / out.preview_width, ty = y * out.height / out.preview_height;
            int tile = map.GetTile((unsigned)(ty*out.width+tx)).tile;
            Uint8 r = 70, g = 70, b = 70, a;
            SDL_ReadSurfacePixel(palette,(tile%20)*16+(x*out.width*16/out.preview_width)%16,(tile/20)*16+(y*out.height*16/out.preview_height)%16,&r,&g,&b,&a);
            out.pixels[y * out.preview_width + x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    if(palette) SDL_DestroySurface(palette);
    auto pixel=[&](int x,int y,uint32_t color){if(x>=0&&y>=0&&x<out.preview_width&&y<out.preview_height)out.pixels[y*out.preview_width+x]=color;};
    for(auto &zone:map.GetZoneList()){int x=zone.x*out.preview_width/out.width,y=zone.y*out.preview_height/out.height,w=max(1,zone.w*out.preview_width/out.width),h=max(1,zone.h*out.preview_height/out.height);for(int dx=0;dx<w;++dx){pixel(x+dx,y,0xffc69a4a);pixel(x+dx,y+h-1,0xffc69a4a);}for(int dy=0;dy<h;++dy){pixel(x,y+dy,0xffc69a4a);pixel(x+w-1,y+dy,0xffc69a4a);}}
    const uint32_t colors[] = {0xffffffff,0xffff4040,0xff4488ff,0xff44dd66,0xffffdd44,0xffcc66dd,0xff44dddd,0xffeeeeee,0xff222222};
    int factories=0,forts=0;
    for(const auto &object:map.GetObjectList()){
        int cx=object.x*out.preview_width/out.width,cy=object.y*out.preview_height/out.height;
        bool fort=object.object_type==BUILDING_OBJECT&&(object.object_id==FORT_FRONT||object.object_id==FORT_BACK);
        bool factory=object.object_type==BUILDING_OBJECT&&(object.object_id==ROBOT_FACTORY||object.object_id==VEHICLE_FACTORY);
        forts+=fort;factories+=factory;if(!fort&&!factory&&object.object_type!=MAP_ITEM_OBJECT)continue;
        uint32_t color=fort?colors[max(0,min(8,(int)object.owner))]:factory?0xfff5e2a0:0xffeeeeee;int radius=fort?2:1;
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)if(fort||abs(dx)+abs(dy)<=radius)pixel(cx+dx,cy+dy,color);
    }
    out.details=to_string(forts)+" forts / "+to_string(factories)+" factories / "+to_string(map.GetZoneList().size())+" territories";
    out.valid = true; return true;
}

bool ZMP_ValidateSetup(const MapPreview &map, const vector<string> &bots, const MatchOptions &options, string &error)
{
    error.clear();
    if(options.human_team <= NULL_TEAM || options.human_team >= MAX_TEAM_TYPES) error = "Choose a human team";
    else if(options.difficulty < 0 || options.difficulty >= MAX_BOT_DIFFICULTY) error = "Invalid bot difficulty";
    else if(!std::isfinite(options.game_speed) || options.game_speed < 0.5 || options.game_speed > 2.0) error = "Game speed must be 50-200%";
    else if(map.valid && find(map.teams.begin(), map.teams.end(), options.human_team) == map.teams.end()) error = "Your team has no fort on this map";
    vector<string> used;
    for(const auto &bot : bots) {
        int team = 0;
        for(int t = 1; t < MAX_TEAM_TYPES; ++t) if(team_type_string[t] == bot) team = t;
        if(!team) error = "Invalid bot team";
        else if(team == options.human_team) error = "Your team cannot also be a bot";
        else if(map.valid && find(map.teams.begin(), map.teams.end(), team) == map.teams.end()) error = "Bot team has no fort on this map";
        else if(find(used.begin(), used.end(), bot) != used.end()) error = "Duplicate bot team";
        used.push_back(bot);
    }
    return error.empty();
}

static bool map_meta_less(const MapMeta &a, const MapMeta &b) { return a.name < b.name; }

static bool local_list_maps(vector<MapMeta> &out)
{
	out.clear();
	if(!g_local_maps_read)
	{
		vector<string> filenames;
        for(const std::string &folder:{std::string("maps"),ZAsset_Profile()+"content/maps"})
            SDL_EnumerateDirectory(folder.c_str(),[](void *data,const char*,const char *name){static_cast<vector<string>*>(data)->push_back(name);return SDL_ENUM_CONTINUE;},&filenames);
        sort(filenames.begin(),filenames.end());filenames.erase(unique(filenames.begin(),filenames.end()),filenames.end());
		for(const auto &name : filenames)
		{
			if(name.size() < 5 || name.substr(name.size() - 4) != ".map") continue;
			MapMeta map;
			map.name = name;
			map.players = map_player_slots("maps/" + name);
			if(map.players > 0) g_local_maps.push_back(map);
		}
		sort(g_local_maps.begin(), g_local_maps.end(), map_meta_less);
		g_local_maps_read = !g_local_maps.empty(); //allow retry if maps were unavailable
	}
	out = g_local_maps;
	return !out.empty();
}

static bool local_list_matches(vector<MatchInfo> &out)
{
	out.clear();
	local_reap();
	for(size_t i = 0; i < g_local_matches.size(); i++)
		out.push_back(g_local_matches[i].info);
	return true;
}

static bool local_create_match(const string &map, const vector<string> &bots, MatchInfo &out, const MatchOptions &options)
{
    string map_path;
    if(!ZSkirmish_MapPath(map,map_path)) { g_last_error="Invalid map filename"; return false; }
	const int capacity = map_player_slots(map_path);
	if(capacity <= 0) { g_last_error = "Map is missing or invalid"; return false; }
	string exe = self_exe_path();
	if(exe.empty()) return false;

	local_reap();

	int port = -1;
	for(int p = 2300; p <= 2399; p++)
	{
		bool used = false;
		for(size_t i = 0; i < g_local_matches.size(); i++)
			if(g_local_matches[i].info.port == p) used = true;
		if(!used && port_is_free(p)) { port = p; break; }
	}
	if(port < 0) { g_last_error = "No free local server ports (2300-2399)"; return false; }

	string map_arg = map_path;
	vector<string> args;
	args.push_back(exe);
	args.push_back("-d");
	args.push_back("-m");
	args.push_back(map_arg);
	for(size_t i = 0; i < bots.size(); i++)
	{
		args.push_back("-b");
		args.push_back(bots[i]);
	}

	char portbuf[16];
	snprintf(portbuf, sizeof(portbuf), "%d", port);
	LocalMatch lm;
    args.push_back("-D"); args.push_back(to_string(options.difficulty));
	if(!ZLocalServer_Start(args, port, lm.process, g_last_error, {"ZOD_GAMESPEED=" + to_string(options.game_speed)})) return false;
	lm.info.id = string("local-") + portbuf;
	lm.info.name = map;
	lm.info.map = map;
	lm.info.host = "127.0.0.1";
	lm.info.port = port;
	lm.info.players = 0;
	lm.info.capacity = capacity;
	out = lm.info;
	g_local_matches.push_back(std::move(lm));

	if(!g_atexit_set) { g_atexit_set = true; atexit(local_kill_all); }

	return true;
}

// --- tiny JSON readers, just enough for the orchestrator's flat, controlled
// responses (no nesting beyond a string array, no escaped quotes in values). Not
// a general JSON parser. ---

// Split a top-level array "[ {..}, {..} ]" into its object substrings.
static vector<string> json_split_array(const string &s)
{
	vector<string> out;
	size_t i = s.find('[');
	if(i == string::npos) return out;

	int depth = 0;
	bool in_str = false;
	size_t obj_start = string::npos;

	for(i++; i < s.size(); i++)
	{
		char c = s[i];
		if(in_str) { if(c == '"') in_str = false; continue; }
		if(c == '"') { in_str = true; continue; }

		if(c == '{') { if(depth == 0) obj_start = i; depth++; }
		else if(c == '}') { if(--depth == 0 && obj_start != string::npos) { out.push_back(s.substr(obj_start, i - obj_start + 1)); obj_start = string::npos; } }
		else if(c == ']' && depth == 0) break;
	}
	return out;
}

// "key":"value" -> value
static bool json_str(const string &obj, const string &key, string &out)
{
	string pat = "\"" + key + "\"";
	size_t k = obj.find(pat);
	if(k == string::npos) return false;
	size_t colon = obj.find(':', k + pat.size());
	if(colon == string::npos) return false;
	size_t q1 = obj.find('"', colon + 1);
	if(q1 == string::npos) return false;
	size_t q2 = obj.find('"', q1 + 1);
	if(q2 == string::npos) return false;
	out = obj.substr(q1 + 1, q2 - q1 - 1);
	return true;
}

// "key": <number> -> int
static bool json_int(const string &obj, const string &key, int &out)
{
	string pat = "\"" + key + "\"";
	size_t k = obj.find(pat);
	if(k == string::npos) return false;
	size_t colon = obj.find(':', k + pat.size());
	if(colon == string::npos) return false;
	size_t p = colon + 1;
	while(p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) p++;
	if(p >= obj.size()) return false;
	out = atoi(obj.c_str() + p);
	return true;
}


static void orchestrator_target(string &host, int &port)
{
	// Default to the public PoC server so Multiplayer works out of the box.
	// Override with ZOD_ORCHESTRATOR=host[:port] (e.g. 127.0.0.1:8080 for a local
	// orchestrator).
	host = "z.0f.ee";
	port = 8080;

	const char *e = getenv("ZOD_ORCHESTRATOR");
	if(e && e[0])
	{
		string s = e;
		size_t c = s.find(':');
		if(c == string::npos) host = s;
		else { host = s.substr(0, c); port = atoi(s.c_str() + c + 1); }
	}
}

string ZMP_OrchestratorAddress()
{
	if(local_mode_requested()) return "local hosting";
	string h;
	int p;
	orchestrator_target(h, p);
	char buf[160];
	snprintf(buf, sizeof(buf), "%s:%d", h.c_str(), p);
	return buf;
}

bool ZMP_ListMatches(vector<MatchInfo> &out)
{
	std::lock_guard<std::mutex> lock(g_local_mutex);
	g_last_error.clear();
	out.clear();

	string host;
	int port;
	orchestrator_target(host, port);

	if(local_mode_requested() || (g_orch_down && !orchestrator_overridden())) return local_list_matches(out);

	ZHttpResponse r = ZHttp_Get(host, port, "/matches");
	if(!r.ok || r.status != 200)
	{
		if(orchestrator_overridden()) return false;
		g_orch_down = true;
		return local_list_matches(out);
	}

	vector<string> objs = json_split_array(r.body);
	for(size_t i = 0; i < objs.size(); i++)
	{
		MatchInfo m;
		json_str(objs[i], "id", m.id);
		json_str(objs[i], "name", m.name);
		json_str(objs[i], "map", m.map);
		json_str(objs[i], "host", m.host);
		json_int(objs[i], "port", m.port);
		json_int(objs[i], "players", m.players);
		json_int(objs[i], "capacity", m.capacity);
        size_t started=objs[i].find("\"started\"");
        if(started!=string::npos) {
            size_t colon=objs[i].find(':',started+9);
            size_t value=colon==string::npos ? string::npos : objs[i].find_first_not_of(" \r\n\t",colon+1);
            if(value!=string::npos) m.started=objs[i].compare(value,4,"true")==0 || objs[i][value]=='1';
        }
		out.push_back(m);
	}
	return true;
}

bool ZMP_CreateMatch(const string &map, const vector<string> &bots, MatchInfo &out, const MatchOptions &options)
{
	std::lock_guard<std::mutex> lock(g_local_mutex);
	g_last_error.clear();
	string host;
	int port;
	orchestrator_target(host, port);

    MapPreview preview;
    ZMP_ReadMapPreview(map, preview);
    if(!ZMP_ValidateSetup(preview, bots, options, g_last_error)) return false;
    if(ZSkirmish_IsGenerated(map)) return local_create_match(map,bots,out,options);
    if(map.empty() || map.find_first_of("/\\\"") != string::npos || map.find("..") != string::npos) { g_last_error = "Invalid map filename"; return false; }
	string body = "{\"map\":\"" + map + "\",\"bots\":[";
	for(size_t i = 0; i < bots.size(); i++)
	{
		if(i) body += ",";
		body += "\"" + bots[i] + "\"";
	}
    body += "],\"human_team\":\"" + team_type_string[options.human_team] + "\",\"difficulty\":" + to_string(options.difficulty)
        + ",\"game_speed\":" + to_string(options.game_speed) + "}";

	if(local_mode_requested() || (g_orch_down && !orchestrator_overridden())) return local_create_match(map, bots, out, options);

    ZHttpResponse capabilities = ZHttp_Get(host, port, "/capabilities");
    int version = 0;
    if(!capabilities.ok || capabilities.status != 200 || !json_int(capabilities.body, "match_setup", version) || version < 1) {
        if(!orchestrator_overridden()) { g_orch_down = true; return local_create_match(map, bots, out, options); }
        g_last_error = "Server needs match setup support. Update it or use ZOD_ORCHESTRATOR=local";
        return false;
    }
	ZHttpResponse r = ZHttp_Post(host, port, "/matches", body);
	if(!r.ok || (r.status != 201 && r.status != 200))
	{
        if(orchestrator_overridden() || (r.ok && r.status >= 400 && r.status < 500)) {
            g_last_error = r.ok ? "Server rejected match settings (HTTP " + to_string(r.status) + ")" : "Match service is unreachable";
            string detail;
            if(r.ok && json_str(r.body, "error", detail)) g_last_error = detail;
            return false;
        }
		g_orch_down = true;
		return local_create_match(map, bots, out, options);
	}

	json_str(r.body, "id", out.id);
	json_str(r.body, "host", out.host);
	json_str(r.body, "map", out.map);
	json_int(r.body, "port", out.port);
	json_int(r.body, "players", out.players);

	return out.port > 0 && !out.host.empty();
}

bool ZMP_Matchmake(MatchInfo &out)
{
	std::lock_guard<std::mutex> lock(g_local_mutex);
	g_last_error.clear();
	string host;
	int port;
	orchestrator_target(host, port);

	ZHttpResponse r = ZHttp_Post(host, port, "/matchmake", "");
	if(!r.ok || (r.status != 200 && r.status != 201)) return false;

	json_str(r.body, "id", out.id);
	json_str(r.body, "host", out.host);
	json_str(r.body, "map", out.map);
	json_int(r.body, "port", out.port);
	json_int(r.body, "players", out.players);

	return out.port > 0 && !out.host.empty();
}

bool ZMP_ListMaps(vector<MapMeta> &out)
{
	std::lock_guard<std::mutex> lock(g_local_mutex);
	g_last_error.clear();
	out.clear();

	string host;
	int port;
	orchestrator_target(host, port);

	if(local_mode_requested() || (g_orch_down && !orchestrator_overridden())) return local_list_maps(out);

	ZHttpResponse r = ZHttp_Get(host, port, "/maps");
	if(!r.ok || r.status != 200)
	{
		if(orchestrator_overridden()) return false;
		g_orch_down = true;
		return local_list_maps(out);
	}

	// /maps now returns [{"name":"x.map","players":N}, ...].
	vector<string> objs = json_split_array(r.body);
	for(size_t i = 0; i < objs.size(); i++)
	{
		MapMeta m;
		json_str(objs[i], "name", m.name);
		json_int(objs[i], "players", m.players);
		if(!m.name.empty()) out.push_back(m);
	}
	return true;
}
