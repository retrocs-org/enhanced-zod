#ifndef ZCLOUD_H
#define ZCLOUD_H
#include <string>
#include <map>
#include <set>
#include "zmp_async.h"
struct ZCloudClaim { int peer=-1; std::string account,name,grant; };
struct ZCloudPeer { double connected=0; bool requested=false; std::string account,grant,ticket; };
struct ZCloudRoles {bool valid=false;std::string owner;std::set<std::string> admins;};
struct ZCloudRoster { int team=0; std::string grant; };
std::string ZCloud_JSON(const std::string &text);
#endif
