#ifndef ZMISSION_H
#define ZMISSION_H
#include <string>
#include <vector>
struct ZMissionObjective {std::string kind="message",text;int target=0,seconds=0;};
bool ZMission_Parse(const std::string &line,ZMissionObjective &objective);
std::string ZMission_Encode(const ZMissionObjective &objective);
struct ZMissionEvent {std::string speaker="Commander",character="grunt";int duration=8;std::string trigger="time",kind="dialogue",text;int at=0,target=0,team=1,type=5,id=0,x=0,y=0,count=1;};
bool ZMission_ParseEvent(const std::string &line,ZMissionEvent &event);
std::string ZMission_EncodeEvent(const ZMissionEvent &event);
struct ZMissionWorld {double time=0;bool playerAlive=true;std::vector<int> zoneOwners,objectOwners,objectZones;};
struct ZMissionRuntime {
 std::vector<ZMissionObjective> objectives;std::vector<ZMissionEvent> events;std::vector<bool> fired;double missionStarted=-1;
 std::vector<int> DueEvents(const ZMissionWorld &world);
 int step=0,outcome=0;double stepStarted=-1;bool announced=false,started=false;
 // Ordered objectives: capture a territory, defend a building, survive, or a timed message.
 int Update(const ZMissionWorld &world,std::vector<std::string> &messages);
};
#endif
