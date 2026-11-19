#ifndef ZMISSION_H
#define ZMISSION_H
#include <string>
#include <vector>
struct ZMissionObjective {std::string kind="message",text;int target=0,seconds=0;};
bool ZMission_Parse(const std::string &line,ZMissionObjective &objective);
std::string ZMission_Encode(const ZMissionObjective &objective);
struct ZMissionWorld {double time=0;bool playerAlive=true;std::vector<int> zoneOwners,objectOwners;};
struct ZMissionRuntime {
 std::vector<ZMissionObjective> objectives;int step=0,outcome=0;double stepStarted=-1;bool announced=false,started=false;
 // Ordered objectives: capture a territory, defend a building, survive, or a timed message.
 int Update(const ZMissionWorld &world,std::vector<std::string> &messages);
};
#endif
