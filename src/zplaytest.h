#ifndef ZPLAYTEST_H
#define ZPLAYTEST_H
#include "zcampaign.h"
struct ZPlaytestRecord {std::string uid,fingerprint,event,next,build;long long timestamp=0;};
std::string ZPlaytest_Fingerprint(const ZCampaign&,int index);
std::vector<ZPlaytestRecord> ZPlaytest_Read(const ZCampaign &source);
bool ZPlaytest_Event(const ZCampaign &snapshot,int index,const std::string &event,int next=-1);
std::string ZPlaytest_Report(const ZCampaign &source);
#endif
