#include "zserver_bookmarks.h"
#include "zasset_import.h"
#include "zlan.h"
#include <fstream>
#include <algorithm>
namespace {
std::string file(bool recent){return ZAsset_Profile()+(recent?"recent-servers.txt":"favorite-servers.txt");}
std::string key(const MatchInfo &m){return m.host+":"+std::to_string(m.port);}
void save(std::vector<MatchInfo> entries,bool recent){SDL_CreateDirectory(ZAsset_Profile().c_str());auto name=file(recent);{std::ofstream out(name+".tmp");for(size_t i=0;i<entries.size()&&i<32;++i)out<<key(entries[i])<<'\n';if(!out)return;}SDL_RenamePath((name+".tmp").c_str(),name.c_str());}
}
std::vector<MatchInfo> ZServerBookmarks(bool recent){std::ifstream in(file(recent));std::vector<MatchInfo> entries;std::string line,error;while(entries.size()<32&&std::getline(in,line)){MatchInfo m;if(ZMP_ParseAddress(line,m.host,m.port,error))entries.push_back(m);}return entries;}
bool ZServerFavorite(const MatchInfo &m){auto entries=ZServerBookmarks(false);return std::any_of(entries.begin(),entries.end(),[&](const MatchInfo &e){return key(e)==key(m);});}
void ZServerToggleFavorite(const MatchInfo &m){std::string host,error;int port;if(!ZMP_ParseAddress(key(m),host,port,error))return;auto entries=ZServerBookmarks(false);bool exists=ZServerFavorite(m);entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const MatchInfo &e){return key(e)==key(m);}),entries.end());if(!exists)entries.insert(entries.begin(),m);save(entries,false);}
void ZServerRemember(const MatchInfo &m){std::string host,error;int port;if(!ZMP_ParseAddress(key(m),host,port,error))return;auto entries=ZServerBookmarks(true);entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const MatchInfo &e){return key(e)==key(m);}),entries.end());entries.insert(entries.begin(),m);save(entries,true);}
