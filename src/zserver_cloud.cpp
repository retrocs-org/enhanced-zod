#include "zserver.h"
#include "zhttp.h"
#include "zsavegame.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <SDL3/SDL.h>

std::string ZCloud_JSON(const std::string &s) {
    std::string out="\""; const char *hex="0123456789abcdef";
    for(unsigned char c:s) { if(c=='"'||c=='\\') {out+='\\';out+=c;} else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];} else out+=c; } return out+'"';
}
void ZServer::ReserveCloudSlot(int player) {
    for(auto it=rejoin_slots.begin();it!=rejoin_slots.end();)if(it->second.until<current_time()||it->second.round!=cloud_round)it=rejoin_slots.erase(it);else ++it;
    if(player<0||player>=(int)player_info.size())return;
    if(rejoin_opt_out.erase(player_info[player].p_id)){private_rejoin_tokens.erase(player_info[player].p_id);return;}
    if(!cloud_required){auto token=private_rejoin_tokens.find(player_info[player].p_id);if(token!=private_rejoin_tokens.end()){if(allow_remote_clients&&game_on&&player_info[player].team>NULL_TEAM&&player_info[player].mode==PLAYER_MODE&&rejoin_slots.size()<64)rejoin_slots["private:"+token->second]={player_info[player].team,current_time()+90,cloud_round};private_rejoin_tokens.erase(token);}return;}
    if(!lan_match_started||!game_on)return;
    auto peer=cloud_peers.find(player_info[player].p_id);
    if(peer==cloud_peers.end()||peer->second.account.empty()||player_info[player].team<=NULL_TEAM||player_info[player].bot_logged_in)return;
    auto ban=cloud_roles.bans.find(peer->second.account);
    if(ban!=cloud_roles.bans.end()&&ban->second.expires>std::time(nullptr))return;
    if(rejoin_slots.size()<64)rejoin_slots[peer->second.account]={player_info[player].team,current_time()+90,cloud_round};
}
void ZServer::resume_session_event(ZServer *p,char *data,int size,int player){
    if(player<0||player>=(int)p->player_info.size())return;
    if(size==0){p->rejoin_opt_out.insert(p->player_info[player].p_id);return;}
    if(p->cloud_required||!data||size!=65||data[64]!=0)return;
    std::string token(data,64);if(token.find_first_not_of("0123456789abcdef")!=std::string::npos)return;
    p->RestoreCloudSlot(player,"private:"+token);
}
void ZServer::RestoreCloudSlot(int player,const std::string &account) {
    if(player<0||player>=(int)player_info.size())return;
    if(cloud_required){auto peer=cloud_peers.find(player_info[player].p_id);if(peer==cloud_peers.end()||peer->second.account!=account)return;auto ban=cloud_roles.bans.find(account);if(ban!=cloud_roles.bans.end()&&ban->second.expires>std::time(nullptr)){rejoin_slots.erase(account);return;}}
    auto found=rejoin_slots.find(account);if(found==rejoin_slots.end())return;
    auto slot=found->second;rejoin_slots.erase(found);
    if(slot.until<current_time()||slot.round!=cloud_round||!game_on){SendNews(player,"Your 90-second rejoin reservation expired or the round changed. Spectate until the next round.",255,190,80);return;}
    for(size_t i=0;i<player_info.size();++i)if((int)i!=player&&player_info[i].team==slot.team&&player_info[i].mode==PLAYER_MODE){SendNews(player,"Your former team is occupied. You are spectating.",255,190,80);return;}
    ChangePlayerTeam(player,slot.team);SendNews(player,"Rejoined your reserved team. The current world is downloading; previous orders continue on the server.",255,190,80);
}
bool ZServer::CloudAuthorized(int player) {
    if(player<0 || player>=(int)player_info.size()) return false;
    if(player_info[player].bot_logged_in) return true;
    auto found=cloud_peers.find(player_info[player].p_id); return found!=cloud_peers.end() && !found->second.account.empty();
}
void ZServer::CloudConnected(int player) {
    if(!cloud_required) {
        if(allow_remote_clients&&private_rejoin_tokens.size()<64){try{std::random_device random;std::ostringstream token;for(int n=0;n<8;++n)token<<std::hex<<std::setw(8)<<std::setfill('0')<<(std::uint32_t)random();private_rejoin_tokens[player_info[player].p_id]=token.str();server_socket.SendMessage(player,RESUME_SESSION,token.str().c_str(),65);}catch(...){SendNews(player,"Reconnect reservation unavailable; join again manually if disconnected.",255,190,80);}}

        if(getenv("ZOD_PERSISTENT_SERVER") && std::string(getenv("ZOD_PERSISTENT_SERVER"))=="1") {
            int mode=0;server_socket.SendMessage(player,CLOUD_REQUIRED,(char*)&mode,sizeof(mode));
        }
        return;
    }
    if(cloud_peers.size()>=64) {auto *handler=server_socket.GetHandler(player);if(handler)handler->Disconnect();return;}
    cloud_peers[player_info[player].p_id]=ZCloudPeer{current_time(),false,"","",""};
    int required=1;server_socket.SendMessage(player,CLOUD_REQUIRED,(char*)&required,sizeof(required));
}
bool ZServer::CloudAdmin(int player){
 if(!cloud_required||player<0||player>=(int)player_info.size()||!cloud_roles.valid||current_time()>=cloud_roles_until)return false;
 auto peer=cloud_peers.find(player_info[player].p_id);return peer!=cloud_peers.end()&&!peer->second.account.empty()&&(CloudRank(peer->second.account)>=2);
}
void ZServer::RefreshCloudRoles(){
 double now=current_time();ZCloudRoles update;
 if(cloud_roles_task.Poll(update)){cloud_roles=std::move(update);cloud_roles_until=cloud_roles.valid?now+15:0;cloud_roles_next=now+5;}
 if(!cloud_roles_task.Pending()&&now>=cloud_roles_next){
  int port=getenv("ZOD_CLOUD_AGENT_PORT")?atoi(getenv("ZOD_CLOUD_AGENT_PORT")):0;std::string secret=getenv("ZOD_CLOUD_AGENT_SECRET")?getenv("ZOD_CLOUD_AGENT_SECRET"):"";
  cloud_roles_next=now+5;cloud_roles_task.Start([port,secret](){ZCloudRoles roles;if(port<1||port>65535||secret.size()!=64)return roles;
   auto response=ZHttp_Post("127.0.0.1",port,"/roles","{\"Secret\":"+ZCloud_JSON(secret)+"}");if(!response.ok||response.status!=200)return roles;
   return ZCloud_ParseRoles(response.body);
  });
 }
 for(size_t i=0;i<player_info.size();++i)if(CloudAuthorized(i)&&!player_info[i].bot_logged_in){
  int id=player_info[i].p_id,role=0;int rank=CloudRank(cloud_peers[id].account);if(rank>0)role=rank==3?2:rank==2?1:3;
  auto previous=cloud_role_sent.find(id);if(previous==cloud_role_sent.end()||previous->second!=role){server_socket.SendMessage(i,CLOUD_SERVER_ROLE,(char*)&role,sizeof(role));cloud_role_sent[id]=role;for(auto entry:cloud_role_sent){int packet[]={entry.first,entry.second};for(size_t receiver=0;receiver<player_info.size();++receiver)if(CloudAuthorized(receiver))server_socket.SendMessage(receiver,CLOUD_PLAYER_ROLE,(char*)packet,sizeof(packet));}}
 }
 for(auto i=cloud_role_sent.begin();i!=cloud_role_sent.end();)if(!cloud_peers.count(i->first))i=cloud_role_sent.erase(i);else ++i;
}
bool ZServer::CloudFilter(int type,int event,char *data,int size,int player) {
    if(!cloud_required || type!=TCP_EVENT) return true;
    if(player<0 || player>=(int)player_info.size()) return false;
    if(event==SEND_BOT_BYPASS_DATA) {auto *handler=server_socket.GetHandler(player);return handler && handler->IsLoopback();}
    if(event==CLOUD_TICKET || event==REQUEST_VERSION) return true;
    if(!CloudAuthorized(player)) return false;
    if(!player_info[player].bot_logged_in){
        // Remote filesystem paths and save files are never public-server admin APIs.
        if(event==LOAD_GAME||event==SAVE_GAME||event==LOAD_MAP_FILE||event==GENERATE_MAP){SendNews(player,"Local file operations are unavailable on public servers.",0,0,0);return false;}
        bool administrative=event==SET_GAME_PAUSED||event==SET_GAME_SPEED||event==RESHUFFLE_TEAMS||event==START_BOT_EVENT||event==STOP_BOT_EVENT||event==SELECT_MAP||event==RESET_MAP||event==START_VOTE;
        if(administrative&&!CloudAdmin(player)){SendNews(player,"This action requires a server owner or administrator with current cloud authorization.",0,0,0);return false;}
    }
    // Cloud identity is immutable; legacy name/login commands cannot impersonate it.
    if(!player_info[player].bot_logged_in && (event==SET_NAME || event==GIVE_PLAYER_NAME || event==SET_PLAYER_MODE)) {
        if(event==SET_PLAYER_MODE && size==sizeof(player_mode_packet) && data && ((player_mode_packet*)data)->mode==PLAYER_MODE) return true;
        return false;
    }
    if(event==SET_TEAM && lan_match_started && !player_info[player].bot_logged_in && size==sizeof(int) && data) {
        int team;memcpy(&team,data,sizeof(team));
        if(team!=player_info[player].team) {
            int current=player_info[player].team;server_socket.SendMessage(player,SET_TEAM,(char*)&current,sizeof(current));
            SendNews(player,"Public match teams are locked after startup. You can spectate until the next round.",0,0,0);return false;
        }
    }
    if(!player_info[player].bot_logged_in && event==SEND_CHAT && size>0 && data && data[0]=='/') return false;
    return true;
}
void ZServer::cloud_ticket_event(ZServer *p,char *data,int size,int player) {
    if(!p->cloud_required || player<0 || player>=(int)p->player_info.size() || size!=65 || !data || data[64]!=0) return;
    for(int i=0;i<64;++i) if(!((data[i]>='0'&&data[i]<='9')||(data[i]>='a'&&data[i]<='f'))) return;
    auto &peer=p->cloud_peers[p->player_info[player].p_id];
    if(peer.requested || !peer.account.empty()) return;
    if(p->cloud_task.Pending()) {peer.ticket=std::string(data,64);return;}
    peer.requested=true;peer.ticket.clear();
    int id=p->player_info[player].p_id,port=getenv("ZOD_CLOUD_AGENT_PORT") ? atoi(getenv("ZOD_CLOUD_AGENT_PORT")) : 0;
    std::string secret=getenv("ZOD_CLOUD_AGENT_SECRET") ? getenv("ZOD_CLOUD_AGENT_SECRET") : "",ticket(data,64);
    p->cloud_task.Start([id,port,secret,ticket](){
        ZCloudClaim result;result.peer=id;
        if(port<1 || port>65535 || secret.size()!=64) return result;
        auto response=ZHttp_Post("127.0.0.1",port,"/verify","{\"Secret\":"+ZCloud_JSON(secret)+",\"Ticket\":"+ZCloud_JSON(ticket)+"}");
        if(!response.ok||response.status!=200){result.error=response.status==401?"Approval expired or already used. Select the server again for fresh browser approval.":response.status==403?"This account is not permitted on this server. Check server bans or contact its owner.":response.status==503?"Cloud authentication is unavailable or maintenance is pending. Retry later or choose another server.":"The host could not verify cloud approval. Check portal connectivity and retry.";return result;}
        std::istringstream input(response.body);std::getline(input,result.account);std::getline(input,result.name);std::getline(input,result.grant);
        if(result.account.size()!=64 || result.grant.size()!=64 || result.name.size()<3 || result.name.size()>24) {result.account.clear();result.grant.clear();}
        return result;
    });
}
void ZServer::CheckCloud() {
    if(!cloud_required) return;
    RefreshCloudRoles();ApplyCloudOperations();
    for(auto it=rejoin_slots.begin();it!=rejoin_slots.end();)if(it->second.until<current_time()||it->second.round!=cloud_round)it=rejoin_slots.erase(it);else ++it;
    ZCloudClaim claim;
    if(cloud_task.Poll(claim)) for(size_t i=0;i<player_info.size();++i) if(player_info[i].p_id==claim.peer) {
        bool duplicate=false;for(const auto &v:cloud_peers) if(v.first!=claim.peer && !claim.account.empty() && v.second.account==claim.account) duplicate=true;
        int success=(!claim.account.empty() && !duplicate) ? 1 : 0;
        if(success) {
            auto &peer=cloud_peers[claim.peer];peer.account=claim.account;peer.grant=claim.grant;
            player_info[i].name=claim.name; RelayLPlayerName((int)i);
            RestoreCloudSlot((int)i,claim.account);
        }
        if(!success){auto reason=duplicate?std::string("This account already has a connection to this server."):claim.error;if(!reason.empty())server_socket.SendMessage((int)i,CLOUD_NOTICE,reason.c_str(),reason.size()+1);}
        server_socket.SendMessage((int)i,CLOUD_RESULT,(char*)&success,sizeof(success));
        if(!success) {auto *handler=server_socket.GetHandler((int)i);if(handler) handler->Disconnect();}
    }
    for(size_t i=0;i<player_info.size();++i) {
        auto found=cloud_peers.find(player_info[i].p_id);if(found==cloud_peers.end()) continue;
        if(!CloudAuthorized((int)i) && current_time()-found->second.connected>30) {auto *handler=server_socket.GetHandler((int)i);if(handler)handler->Disconnect();}
        
    }
    if(!cloud_task.Pending()) for(size_t i=0;i<player_info.size();++i) {
        auto found=cloud_peers.find(player_info[i].p_id);
        if(found!=cloud_peers.end() && !found->second.ticket.empty() && !found->second.requested) {
            std::string ticket=found->second.ticket;cloud_ticket_event(this,const_cast<char*>(ticket.c_str()),65,(int)i);break;
        }
    }
    for(auto i=cloud_peers.begin();i!=cloud_peers.end();) {
        bool connected=false;for(const auto &p:player_info) if(p.p_id==i->first)connected=true;
        if(!connected)i=cloud_peers.erase(i);else ++i;
    }
}
void ZServer::FreezeCloudRoster() {
    if(!cloud_required || !game_on || cloud_roster_fixed) return;
    cloud_roster.clear();cloud_match_start=ztime.ztime;cloud_roster_fixed=true;
    for(const auto &p:player_info) {
        auto peer=cloud_peers.find(p.p_id);
        if(p.mode==PLAYER_MODE && p.team>NULL_TEAM && peer!=cloud_peers.end() && !peer->second.account.empty())
            cloud_roster[peer->second.account]={p.team,peer->second.grant};
    }
}
void ZServer::ResetCloudMatch() {
    rejoin_slots.clear();map_fingerprint.clear();cloud_round=std::to_string(SDL_GetTicksNS());cloud_applied_revision=0;cloud_voting_until=0;cloud_roles.winner.clear();cloud_roles.winnerRound.clear();
    cloud_roster_fixed=false;cloud_reported=false;cloud_roster.clear();cloud_deaths.clear();cloud_match_start=ztime.ztime;
    for(int t=0;t<MAX_TEAM_TYPES;++t) cloud_kills[t]=cloud_lost[t]=cloud_captures[t]=0;
}
void ZServer::ReportCloudMatch() {
    if(!cloud_required || cloud_roster.empty() || cloud_reported) return;
    cloud_reported=true;
    const char *folder=getenv("ZOD_CLOUD_OUTBOX");if(!folder || !*folder)return;
    bool alive[MAX_TEAM_TYPES]={};for(auto *object:object_list) {unsigned char type,id;object->GetObjectID(type,id);int team=object->GetOwner();if(team>NULL_TEAM&&team<MAX_TEAM_TYPES&&!object->IsDestroyed()&&(type==ROBOT_OBJECT||type==VEHICLE_OBJECT||type==CANNON_OBJECT))alive[team]=true;}
    std::random_device random;std::ostringstream uuid;for(int n=0;n<8;++n) uuid<<std::hex<<std::setw(8)<<std::setfill('0')<<(std::uint32_t)random();
    std::ostringstream report;report<<"{\"id\":"<<ZCloud_JSON(uuid.str())<<",\"map\":"<<ZCloud_JSON(map_name)<<",\"duration\":"<<std::max(0,(int)(ztime.ztime-cloud_match_start))<<",\"players\":[";
    bool comma=false;for(const auto &entry:cloud_roster) {int team=entry.second.team;if(team<1||team>=MAX_TEAM_TYPES)continue;if(comma)report<<",";comma=true;report<<"{\"account\":"<<ZCloud_JSON(entry.first)<<",\"grant\":"<<ZCloud_JSON(entry.second.grant)<<",\"team\":"<<team<<",\"won\":"<<(alive[team]?"true":"false")<<",\"kills\":"<<cloud_kills[team]<<",\"lost\":"<<cloud_lost[team]<<",\"captures\":"<<cloud_captures[team]<<"}";}
    report<<"]}";
    std::string path=std::string(folder)+"/"+uuid.str()+".json";
    const auto text=report.str();ZSaveWriter writer;writer.Raw(text.data(),(int)text.size());
    if(!writer.WriteFile(path)) {cloud_reported=false;printf("cloud: could not save match result\n");}
}
