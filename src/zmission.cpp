#include "zmission.h"
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cmath>
bool ZMission_Parse(const std::string &line,ZMissionObjective &o){std::istringstream in(line);std::string target,seconds;if(!std::getline(in,o.kind,'|')||!std::getline(in,target,'|')||!std::getline(in,seconds,'|'))return false;std::getline(in,o.text);if(o.text.size()>512||o.text.find('|')!=std::string::npos)return false;if(o.kind!="capture"&&o.kind!="defend"&&o.kind!="survive"&&o.kind!="message"&&o.kind!="escort")return false;auto number=[](const std::string&s,int&v,int max){if(s.empty()||s.size()>8)return false;for(auto c:s)if(c<'0'||c>'9')return false;long n=strtol(s.c_str(),nullptr,10);if(n>max)return false;v=n;return true;};return number(target,o.target,1000000)&&number(seconds,o.seconds,86400);}
std::string ZMission_Encode(const ZMissionObjective &o){return o.kind+"|"+std::to_string(o.target)+"|"+std::to_string(o.seconds)+"|"+o.text;}
int ZMissionRuntime::Update(const ZMissionWorld &world,std::vector<std::string>&messages){
 if(outcome||objectives.empty())return outcome;if(!world.playerAlive){outcome=-1;return outcome;}
 while(step<(int)objectives.size()){
  const auto &o=objectives[step];if(!started){stepStarted=world.time;started=true;}
  if(!announced){if(!o.text.empty()&&o.kind!="message"&&o.kind!="escort")messages.push_back(o.text);announced=true;}
  bool done=false;double elapsed=world.time-stepStarted;
  if(o.kind=="escort"){if(o.target<0||o.target>=(int)world.objectOwners.size()||world.objectOwners[o.target]!=1){outcome=-1;return outcome;}done=o.target<(int)world.objectZones.size()&&world.objectZones[o.target]==o.seconds;}
  else if(o.kind=="capture"){if(o.target>=(int)world.zoneOwners.size()){outcome=-1;return outcome;}done=world.zoneOwners[o.target]==1;}
  else if(o.kind=="defend"){if(o.target>=(int)world.objectOwners.size()||world.objectOwners[o.target]!=1){outcome=-1;return outcome;}done=elapsed>=o.seconds;}
  else done=elapsed>=o.seconds;
  if(!done)return 0;if(o.kind=="message"&&!o.text.empty())messages.push_back(o.text);++step;stepStarted=world.time;announced=false;
 }
 outcome=1;messages.push_back("Mission objectives complete");return outcome;
}

bool ZMission_ParseEvent(const std::string &line,ZMissionEvent &event){std::istringstream in(line);std::string fields[10];for(auto &field:fields)if(!std::getline(in,field,'|'))return false;std::getline(in,event.text);if(event.text.size()>512||event.text.find('|')!=std::string::npos)return false;event.trigger=fields[0];event.kind=fields[3];if(event.trigger!="time"&&event.trigger!="capture"&&event.trigger!="destroy")return false;if(event.kind!="dialogue"&&event.kind!="reinforce")return false;int *values[]={&event.at,&event.target,&event.team,&event.type,&event.id,&event.x,&event.y,&event.count};int positions[]={1,2,4,5,6,7,8,9};for(int i=0;i<8;++i){const auto &value=fields[positions[i]];if(value.empty()||value.size()>6)return false;int result=0;for(char c:value){if(c<'0'||c>'9')return false;result=result*10+c-'0';}*values[i]=result;}return event.at<=86400&&event.target<=100000&&event.team>=1&&event.team<=4&&event.count>=1&&event.count<=16&&event.x<2048&&event.y<2048;}
std::string ZMission_EncodeEvent(const ZMissionEvent &e){return e.trigger+"|"+std::to_string(e.at)+"|"+std::to_string(e.target)+"|"+e.kind+"|"+std::to_string(e.team)+"|"+std::to_string(e.type)+"|"+std::to_string(e.id)+"|"+std::to_string(e.x)+"|"+std::to_string(e.y)+"|"+std::to_string(e.count)+"|"+e.text;}
std::vector<int> ZMissionRuntime::DueEvents(const ZMissionWorld &world){if(missionStarted<0)missionStarted=world.time;fired.resize(events.size(),false);std::vector<int> due;for(size_t i=0;i<events.size();++i){if(fired[i])continue;auto &e=events[i];bool trigger=e.trigger=="time"?world.time-missionStarted>=e.at:e.trigger=="capture"?e.target<(int)world.zoneOwners.size()&&world.zoneOwners[e.target]==e.team:e.target<(int)world.objectOwners.size()&&world.objectOwners[e.target]<0;if(trigger){fired[i]=true;due.push_back(i);}}return due;}
