#ifndef ZSERVER_BOOKMARKS_H
#define ZSERVER_BOOKMARKS_H
#include "zmpclient.h"
std::vector<MatchInfo> ZServerBookmarks(bool recent);
bool ZServerFavorite(const MatchInfo &server);
void ZServerToggleFavorite(const MatchInfo &server);
void ZServerRemember(const MatchInfo &server);
#endif
