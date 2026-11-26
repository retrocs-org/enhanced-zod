#ifndef ZCONTENT_H
#define ZCONTENT_H
#include "zmap.h"
#include "zcampaign.h"
struct ZContentIssue {bool error;std::string text;int x=-1,y=-1,mission=-1;};
std::vector<ZContentIssue> ZContent_ValidateMap(ZMap &map);
std::vector<ZContentIssue> ZContent_ValidateCampaign(const ZCampaign &campaign,bool distribution=false);
bool ZContent_HasErrors(const std::vector<ZContentIssue> &issues);
bool ZCampaign_Export(const ZCampaign &campaign,std::string &output,std::string &error);
bool ZCampaign_CheckPortable(const std::string &archive,std::string &report,std::string &error);
bool ZContent_WriteBundle(const std::vector<std::pair<std::string,std::vector<unsigned char>>> &files,const std::string &path,std::string &error);
bool ZCampaign_InspectPack(const std::string &archive,ZCampaign &summary,std::string &error);
bool ZCampaign_ImportPack(const std::string &archive,std::string &error,bool update=false);
struct ZCampaignBackup {std::string folder,version;};
std::vector<ZCampaignBackup> ZCampaign_Backups(const std::string &id);
bool ZCampaign_RestoreBackup(const std::string &id,const std::string &folder,std::string &error);
bool ZCampaign_Uninstall(const std::string &id,std::string &error);
void ZCampaign_PickPack(SDL_Window *window);
bool ZCampaign_PollPack(std::string &path);
#endif
