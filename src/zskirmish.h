#ifndef ZSKIRMISH_H
#define ZSKIRMISH_H
#include "zmpclient.h"

struct RandomMapOptions {
    std::uint32_t seed = 1234567;
    int players = 2, size = 1, terrain = 0, tech = 0, vehicles = -1;
};
struct SkirmishPreferences {
    std::string map;
    bool random_mode = false;
    MatchOptions match;
    std::uint32_t bots = 0;
    RandomMapOptions random;
};
bool ZSkirmish_ParseSeed(const std::string &text, std::uint32_t &seed);
std::string ZSkirmish_RandomKey(const RandomMapOptions &options);
bool ZSkirmish_IsGenerated(const std::string &key);
bool ZSkirmish_MapPath(const std::string &key, std::string &path);
bool ZSkirmish_Generate(const RandomMapOptions &options, MapPreview &preview);
SkirmishPreferences ZSkirmish_Load(int default_difficulty = 1);
bool ZSkirmish_Save(const SkirmishPreferences &preferences);
#endif
