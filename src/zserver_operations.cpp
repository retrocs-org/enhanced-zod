#include "zserver.h"
#include "zhttp.h"
#include "zprefs.h"
#include <sstream>
#include <algorithm>
#include <ctime>
namespace {
bool account(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool number(const std::string &s,long long &n){if(s.empty()||s.size()>18||s.find_first_not_of("0123456789")!=std::string::npos)return false;try{n=std::stoll(s);return true;}catch(...){return false;}}
std::vector<std::string> fields(const std::string &s){std::vector<std::string> v;std::istringstream in(s);std::string part;while(std::getline(in,part,':'))v.push_back(part);return v;}
bool unhex(const std::string &s,std::string &out,size_t max){if(s.size()%2||s.size()>max*2||s.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;out.clear();for(size_t i=0;i<s.size();i+=2){char c=(char)strtol(s.substr(i,2).c_str(),nullptr,16);if((unsigned char)c<32||c==127)return false;out+=c;}return true;}
}
ZCloudRoles ZCloud_ParseRoles(const std::string &body){
 ZCloudRoles roles;std::istringstream input(body);if(!std::getline(input,roles.owner)||!account(roles.owner))return {};
 std::string line;while(std::getline(input,line)){
  if(account(line)){if(roles.admins.size()>=32)return {};roles.admins.insert(line);continue;}
  auto f=fields(line);if(f.empty())return {};long long a=0,b=0,c=0,d=0;
  if(f[0]=="m"&&f.size()==2&&account(f[1])&&roles.moderators.size()<32)roles.moderators.insert(f[1]);
  else if(f[0]=="b"&&f.size()==4&&account(f[1])&&number(f[2],a)&&roles.bans.size()<128){ZCloudBan ban;ban.expires=a;if(!unhex(f[3],ban.reason,160))return {};roles.bans[f[1]]=ban;}
  else if(f[0]=="k"&&f.size()==5&&number(f[1],a)&&account(f[2])&&account(f[3])&&roles.commands.size()<32){ZCloudCommand command;command.id=f[1];command.actor=f[2];command.target=f[3];if(!unhex(f[4],command.reason,160))return {};roles.commands.push_back(command);}
  else if(f[0]=="c"&&f.size()==5&&number(f[1],a)&&a>0&&number(f[2],b)&&b<4&&number(f[3],c)&&c>=25&&c<=300&&number(f[4],d)&&d<2){roles.revision=a;roles.difficulty=b;roles.speed=c;roles.voting=d;}
  else if(f[0]=="r"&&f.size()==2&&roles.rotation.size()<16){std::string path;if(!unhex(f[1],path,240)||path.empty())return {};roles.rotation.push_back(path);}
  else if(f[0]=="t"&&f.size()==2&&number(f[1],a)&&a>0&&a<MAX_TEAM_TYPES)roles.bots.insert(a);
  else if(f[0]=="w"&&f.size()==3){if(!unhex(f[1],roles.winnerRound,40)||!unhex(f[2],roles.winner,240))return {};}
  else return {};
 }
 roles.valid=true;return roles;
}
int ZServer::CloudRank(const std::string &id){
 if(!cloud_roles.valid||current_time()>=cloud_roles_until||id.empty())return 0;
 if(id==cloud_roles.owner)return 3;auto ban=cloud_roles.bans.find(id);if(ban!=cloud_roles.bans.end()&&ban->second.expires>std::time(nullptr))return 0;
 return cloud_roles.admins.count(id)?2:cloud_roles.moderators.count(id)?1:0;
}
std::string ZServer::NextCloudMap(){
 const auto &maps=cloud_settings.rotation;if(maps.empty())return map_name;
 if(cloud_settings.voting&&cloud_roles.winnerRound==cloud_round&&std::find(maps.begin(),maps.end(),cloud_roles.winner)!=maps.end())return cloud_roles.winner;
 auto current=std::find(maps.begin(),maps.end(),map_name);return current==maps.end()||++current==maps.end()?maps.front():*current;
}
void ZServer::ApplyCloudOperations(){
 double now=current_time();
 if(cloud_roles.valid&&now<cloud_roles_until){
  if(cloud_roles.revision>0&&cloud_roles.revision!=cloud_settings.revision){bool valid=true;for(auto &path:cloud_roles.rotation)if(path!=map_name&&std::find(selectable_map_list.begin(),selectable_map_list.end(),path)==selectable_map_list.end())valid=false;if(valid)cloud_settings=cloud_roles;}
  for(auto &command:cloud_roles.commands)if(!cloud_seen_commands.count(command.id)){
   int actor=CloudRank(command.actor),target=CloudRank(command.target);bool applied=actor>=1&&actor>target&&command.target!=cloud_roles.owner;
   if(applied)for(size_t i=0;i<player_info.size();++i){auto peer=cloud_peers.find(player_info[i].p_id);if(peer!=cloud_peers.end()&&peer->second.account==command.target){SendNews(i,("Removed by server moderation: "+command.reason).c_str(),0,0,0);auto *handler=server_socket.GetHandler(i);if(handler)handler->Disconnect();}}
   cloud_seen_commands[command.id]=now;cloud_command_results[command.id]=applied;
  }
  for(size_t i=0;i<player_info.size();++i){auto peer=cloud_peers.find(player_info[i].p_id);if(peer==cloud_peers.end()||peer->second.account==cloud_roles.owner)continue;auto ban=cloud_roles.bans.find(peer->second.account);if(ban!=cloud_roles.bans.end()&&ban->second.expires>std::time(nullptr)){SendNews(i,("Temporarily banned: "+ban->second.reason).c_str(),0,0,0);auto *handler=server_socket.GetHandler(i);if(handler)handler->Disconnect();}}
 }
 for(auto i=cloud_seen_commands.begin();i!=cloud_seen_commands.end();)if(now-i->second>120)i=cloud_seen_commands.erase(i);else ++i;
 bool acknowledged;if(cloud_ack_task.Poll(acknowledged)){if(acknowledged)for(auto &id:cloud_ack_batch)cloud_command_results.erase(id);cloud_ack_batch.clear();cloud_ack_after=now+3;}
 if(!cloud_ack_task.Pending()&&!cloud_command_results.empty()&&now>=cloud_ack_after){
  std::string data="{\"Secret\":"+ZCloud_JSON(getenv("ZOD_CLOUD_AGENT_SECRET")?getenv("ZOD_CLOUD_AGENT_SECRET"):"")+",\"results\":[";bool comma=false;cloud_ack_batch.clear();
  for(auto &result:cloud_command_results){if(cloud_ack_batch.size()>=32)break;if(comma)data+=",";comma=true;data+="{\"id\":"+ZCloud_JSON(result.first)+",\"applied\":"+(result.second?"true":"false")+"}";cloud_ack_batch.push_back(result.first);}data+="]}";int port=getenv("ZOD_CLOUD_AGENT_PORT")?atoi(getenv("ZOD_CLOUD_AGENT_PORT")):0;
  cloud_ack_task.Start([data,port](){auto response=ZHttp_Post("127.0.0.1",port,"/ack",data);return response.ok&&response.status==204;});
 }
 if(cloud_settings.revision>0&&cloud_applied_revision!=cloud_settings.revision&&game_on&&!lan_match_started&&ztime.IsPaused()){
  zod_bot_difficulty=cloud_settings.difficulty;ChangeGameSpeed(cloud_settings.speed/100.0);
  for(int team=1;team<MAX_TEAM_TYPES;++team){bool human=false;for(auto &player:player_info)if(player.mode==PLAYER_MODE&&player.team==team)human=true;if(!human&&TeamHasFort(team)){if(cloud_settings.bots.count(team)){if(!bot_thread[team])StartBot(team);SetTeamsBotsIgnored(team,false);}else SetTeamsBotsIgnored(team,true);}}
  cloud_applied_revision=cloud_settings.revision;
 }
}
