#include "zserver.h"
#include "zplayer.h"
#include "zsync.h"
#include "zbuild.h"
#include "zasset_import.h"
#include <sstream>
#include <fstream>
#include <cstdlib>
namespace {bool digest(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}bool sequence(const std::string &s,unsigned long long &value){if(s.empty()||s.size()>15||s.find_first_not_of("0123456789")!=std::string::npos)return false;value=strtoull(s.c_str(),nullptr,10);return value>0;}}
void ZServer::CheckStateSync(){
 if(!game_on||!zmap.Loaded()||current_time()<sync_next)return;sync_next=current_time()+5;sync_hash=ZSync_Hash(object_list,zmap);++sync_sequence;sync_replied.clear();auto packet=std::to_string(sync_sequence)+"|"+MapFingerprint()+"|"+sync_hash;
 for(size_t i=0;i<player_info.size();++i)if(player_info[i].mode==PLAYER_MODE&&player_info[i].world_sync==1&&(!cloud_required||CloudAuthorized(i)))server_socket.SendMessage(i,STATE_SYNC_CHECK,packet.c_str(),packet.size()+1);
}
void ZServer::state_sync_reply_event(ZServer *p,char *data,int size,int player){
 if(player<0||player>=(int)p->player_info.size()||!data||size<67||size>90||data[size-1]!=0||memchr(data,0,size-1))return;std::istringstream input(std::string(data,size-1));std::string seq,hash;std::getline(input,seq,'|');std::getline(input,hash);unsigned long long value;if(!sequence(seq,value)||value!=p->sync_sequence||!digest(hash))return;int peer=p->player_info[player].p_id;if(p->sync_replied[peer]==value)return;p->sync_replied[peer]=value;++p->sync_checks;if(hash!=p->sync_hash)++p->sync_mismatches;
}
void ZPlayer::state_sync_event(ZPlayer *p,char *data,int size,int){
 if(p->replay_active||!p->zmap.Loaded()||!data||size<132||size>150||data[size-1]!=0||memchr(data,0,size-1))return;std::istringstream input(std::string(data,size-1));std::string seq,map,expected;std::getline(input,seq,'|');std::getline(input,map,'|');std::getline(input,expected);unsigned long long value;if(!sequence(seq,value)||value<=p->sync_last_sequence||!digest(map)||!digest(expected)||map!=p->loaded_map_hash)return;p->sync_last_sequence=value;
 auto actual=ZSync_Hash(p->object_list,p->zmap);auto reply=seq+"|"+actual;p->client_socket.SendMessage(STATE_SYNC_REPLY,reply.c_str(),reply.size()+1);bool match=actual==expected;p->sync_strikes=match?0:p->sync_strikes+1;p->sync_history.push_back(seq+" expected="+expected+" actual="+actual);if(p->sync_history.size()>8)p->sync_history.erase(p->sync_history.begin());
 if(p->sync_strikes<3||p->sync_reported_hash==map)return;p->sync_reported_hash=map;auto directory=ZAsset_Profile()+"diagnostics";SDL_CreateDirectory(directory.c_str());std::ofstream report(directory+"/desync-"+std::to_string(SDL_GetTicksNS())+".txt");report<<"Replicated-state mismatch (not a movement/latency comparison)\nBuild: "<<ZBuild_ID()<<"\nMap SHA-256: "<<map<<"\n";for(auto &line:p->sync_history)report<<line<<'\n';report.close();p->AddNewsEntry("Persistent state mismatch. Reconnect to resynchronize; a local diagnostics report was saved.");
}
