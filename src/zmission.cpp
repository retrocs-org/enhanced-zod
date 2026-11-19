#include "zmission.h"
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cmath>
bool ZMission_Parse(const std::string &line,ZMissionObjective &o){std::istringstream in(line);std::string target,seconds;if(!std::getline(in,o.kind,'|')||!std::getline(in,target,'|')||!std::getline(in,seconds,'|'))return false;std::getline(in,o.text);if(o.text.size()>512||o.text.find('|')!=std::string::npos)return false;if(o.kind!="capture"&&o.kind!="defend"&&o.kind!="survive"&&o.kind!="message")return false;auto number=[](const std::string&s,int&v,int max){if(s.empty()||s.size()>8)return false;for(auto c:s)if(c<'0'||c>'9')return false;long n=strtol(s.c_str(),nullptr,10);if(n>max)return false;v=n;return true;};return number(target,o.target,1000000)&&number(seconds,o.seconds,86400);}
std::string ZMission_Encode(const ZMissionObjective &o){return o.kind+"|"+std::to_string(o.target)+"|"+std::to_string(o.seconds)+"|"+o.text;}
int ZMissionRuntime::Update(const ZMissionWorld &world,std::vector<std::string>&messages){
 if(outcome||objectives.empty())return outcome;if(!world.playerAlive){outcome=-1;return outcome;}
 while(step<(int)objectives.size()){
  const auto &o=objectives[step];if(!started){stepStarted=world.time;started=true;}
  if(!announced){if(!o.text.empty()&&o.kind!="message")messages.push_back(o.text);announced=true;}
  bool done=false;double elapsed=world.time-stepStarted;
  if(o.kind=="capture"){if(o.target>=(int)world.zoneOwners.size()){outcome=-1;return outcome;}done=world.zoneOwners[o.target]==1;}
  else if(o.kind=="defend"){if(o.target>=(int)world.objectOwners.size()||world.objectOwners[o.target]!=1){outcome=-1;return outcome;}done=elapsed>=o.seconds;}
  else done=elapsed>=o.seconds;
  if(!done)return 0;if(o.kind=="message"&&!o.text.empty())messages.push_back(o.text);++step;stepStarted=world.time;announced=false;
 }
 outcome=1;messages.push_back("Mission objectives complete");return outcome;
}
