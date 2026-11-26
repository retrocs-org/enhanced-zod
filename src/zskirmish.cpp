#include "zskirmish.h"
#include "map_gen_core.h"
#include "zsavegame.h"
#include "constants.h"
#include "zasset_import.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <mutex>
#include <random>

namespace {
const int widths[] = {64,80,112}, heights[] = {80,100,140};
std::string directory() {
    const char *home=getenv("HOME");
    if(!home || !home[0]) home=getenv("USERPROFILE");
    if(const char *profile=getenv("ZOD_PROFILE_DIR"))if(*profile)return std::string(profile)+"/skirmish";
    if(home && home[0]) return std::string(home)+"/.zod_skirmish";
    char *pref=SDL_GetPrefPath("zodengine","skirmish");
    std::string result=pref ? pref : ""; SDL_free(pref); return result;
}
bool valid_recipe(const RandomMapOptions &r) {
    return r.players>=2 && r.players<=4 && r.size>=0 && r.size<3 && r.terrain>=0 && r.terrain<5
        && r.tech>=0 && r.tech<=5 && (r.vehicles==-1 || r.vehicles==0 || r.vehicles==2 || r.vehicles==6);
}
bool valid_name(const std::string &name) {
    return name.size()<=240 && name.find_first_of("/\\\"\r\n\t") == std::string::npos && name.find("..") == std::string::npos;
}
}
bool ZSkirmish_ParseSeed(const std::string &text,std::uint32_t &seed) {
    if(text.empty() || text.size()>10) return false;
    std::uint64_t value=0;
    for(char c:text) { if(c<'0' || c>'9') return false; value=value*10+(c-'0'); if(value>UINT32_MAX) return false; }
    seed=(std::uint32_t)value; return true;
}
std::string ZSkirmish_RandomKey(const RandomMapOptions &r) {
    return "@generated:random-v2-s"+std::to_string(r.seed)+"-p"+std::to_string(r.players)+"-z"+std::to_string(r.size)
        +"-t"+std::to_string(r.terrain)+"-l"+std::to_string(r.tech)+"-v"+std::to_string(r.vehicles+1)+".map";
}
bool ZSkirmish_IsGenerated(const std::string &key) { return key.compare(0,11,"@generated:")==0; }
bool ZSkirmish_MapPath(const std::string &key,std::string &path) {
    path.clear();
    std::string name=ZSkirmish_IsGenerated(key) ? key.substr(11) : key;
    if(name.empty() || !valid_name(name) || name.find(':')!=std::string::npos) return false;
    if(ZSkirmish_IsGenerated(key)) {
        if(name.compare(0,10,"random-v2-")!=0 || directory().empty()) return false;
        path=directory()+"/maps/"+name;
    } else path="maps/"+name;
    return true;
}
bool ZSkirmish_Generate(const RandomMapOptions &r,MapPreview &preview) {
    preview=MapPreview(); preview.name=ZSkirmish_RandomKey(r);
    if(!valid_recipe(r)) { preview.error="Invalid random-map settings"; return false; }
    std::string path;
    if(!ZSkirmish_MapPath(preview.name,path) || !SDL_CreateDirectory((directory()+"/maps").c_str())) {
        preview.error="Could not create the user maps folder"; return false;
    }
    // Serialize installation too, so separate menu workers cannot replace one
    // another's temporary file. No installed game assets are written.
    static std::mutex mutex; std::lock_guard<std::mutex> lock(mutex);
    std::string temp=path+".part";
    if(!MapGen::Generate(temp,r.players-1,widths[r.size],heights[r.size],r.terrain,r.tech,r.seed,r.vehicles,ZAsset_TerrainDirectory(r.terrain))) {
        SDL_RemovePath(temp.c_str()); preview.error="Map generation failed; check free disk space and game assets"; return false;
    }
    if(!SDL_RenamePath(temp.c_str(),path.c_str())) {
        SDL_RemovePath(temp.c_str()); preview.error="Could not save the generated map"; return false;
    }
    const std::string key=preview.name;
    return ZMP_ReadMapPreview(key,preview);
}
SkirmishPreferences ZSkirmish_Load(int difficulty) {
    SkirmishPreferences defaults;
    if(difficulty>=0 && difficulty<4) defaults.match.difficulty=difficulty;
    auto result=defaults;
    std::string root=directory(); if(root.empty()) return result;
    SDL_PathInfo info;
    std::string path=root+"/setup.dat";
    if(!SDL_GetPathInfo(path.c_str(),&info) || info.size>4096) return result;
    ZSaveReader reader;
    if(!reader.ReadFile(path)) return defaults;
    auto version=reader.Str();if(version!="ZODSKIRMISH1"&&version!="ZODSKIRMISH2")return defaults;
    result.map=reader.Str(); result.random_mode=reader.Int()!=0;
    result.match.human_team=reader.Int(); result.match.difficulty=reader.Int(); result.match.game_speed=reader.Double();
    result.bots=(std::uint32_t)reader.Int(); result.random.seed=(std::uint32_t)reader.Int();
    result.random.players=reader.Int(); result.random.size=reader.Int(); result.random.terrain=reader.Int();
    result.random.tech=reader.Int(); result.random.vehicles=reader.Int();
    if(version=="ZODSKIRMISH2") {for(int t=0;t<MAX_TEAM_TYPES;++t){int d=reader.Int();if(d<0||d>3)return defaults;result.match.team_difficulties.push_back(d);}}
    bool speed=result.match.game_speed==0.5 || result.match.game_speed==1 || result.match.game_speed==1.5 || result.match.game_speed==2;
    if(!reader.Ok() || !valid_name(result.map) || result.map.find(':')!=std::string::npos || !valid_recipe(result.random)
       || result.match.human_team<1 || result.match.human_team>=MAX_TEAM_TYPES || result.match.difficulty<0 || result.match.difficulty>3 || !speed) return defaults;
    result.bots &= ((1u<<MAX_TEAM_TYPES)-1u)&~1u;
    result.bots &= ~(1u<<result.match.human_team);
    return result;
}
bool ZSkirmish_Save(const SkirmishPreferences &p) {
    std::string root=directory(); if(root.empty() || !SDL_CreateDirectory(root.c_str())) return false;
    ZSaveWriter writer;
    writer.Str("ZODSKIRMISH2"); writer.Str(p.map); writer.Int(p.random_mode ? 1 : 0);
    writer.Int(p.match.human_team); writer.Int(p.match.difficulty); writer.Double(p.match.game_speed);
    writer.Int((int)p.bots); writer.Int((int)p.random.seed); writer.Int(p.random.players); writer.Int(p.random.size);
    writer.Int(p.random.terrain); writer.Int(p.random.tech); writer.Int(p.random.vehicles);
    for(int t=0;t<MAX_TEAM_TYPES;++t)writer.Int(p.match.team_difficulties.size()==MAX_TEAM_TYPES?p.match.team_difficulties[t]:p.match.difficulty);
    return writer.WriteFile(root+"/setup.dat");
}
