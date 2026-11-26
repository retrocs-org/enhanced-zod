#ifndef ZCLOUD_H
#define ZCLOUD_H
#include <string>
#include <map>
#include <set>
#include <vector>
#include "zmp_async.h"
struct ZCloudClaim { int peer=-1; std::string account,name,grant; };
struct ZCloudPeer { double connected=0; bool requested=false; std::string account,grant,ticket; };
struct ZCloudBan {long long expires=0;std::string reason;};
struct ZCloudCommand {std::string id,actor,target,reason;};
struct ZCloudRoles {bool valid=false,voting=false;std::string owner,winner,winnerRound;std::set<std::string> admins,moderators;std::map<std::string,ZCloudBan> bans;std::vector<ZCloudCommand> commands;long long revision=0;int difficulty=1,speed=100;std::vector<std::string> rotation;std::set<int> bots;};
ZCloudRoles ZCloud_ParseRoles(const std::string &body);
struct ZCloudRoster { int team=0; std::string grant; };
std::string ZCloud_JSON(const std::string &text);
#endif
