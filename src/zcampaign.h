#ifndef ZCAMPAIGN_H
#define ZCAMPAIGN_H
#include <string>
#include <vector>
#include "zmission.h"
struct ZCampaignMission {std::string title,map,briefing,speaker="Commander",character="grunt",video,victoryVideo;std::vector<ZMissionObjective> objectives;};
struct ZCampaign {std::string id,title,author,root,description,cover,difficulty="Normal",version="1.0";std::vector<ZCampaignMission> missions;};
struct ZCampaignRecord {std::string key;int wins=0,bestSeconds=-1,kills=0,lost=0,difficulty=0,medal=0;long long completedAt=0;};
std::vector<ZCampaignRecord> ZCampaign_Records(const std::string &id);
void ZCampaign_RecordWin(const std::string &id,const std::string &key,int seconds,int kills,int lost,int difficulty);
std::string ZCampaign_Medal(const ZCampaignRecord &record);
std::vector<std::string> ZCampaign_Completed(const std::string &id);
void ZCampaign_MarkCompleted(const std::string &id,const std::string &map);
std::string ZCampaign_Root();
bool ZCampaign_SafeAsset(const std::string &root,const std::string &relative,std::string &resolved);
bool ZCampaign_Load(const std::string &manifest,ZCampaign &campaign,std::string &error);
std::vector<ZCampaign> ZCampaign_List(std::vector<std::string> *errors=nullptr);
bool ZCampaign_Create(const std::string &title,ZCampaign &campaign,std::string &error);
bool ZCampaign_Write(const ZCampaign &campaign,std::string &error);
void ZCampaign_Progress(const std::string &id,int &current,int &unlocked,bool save=false);
#endif
