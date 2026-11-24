#ifndef ZPERF_H
#define ZPERF_H
#include <string>
#include <SDL3/SDL.h>
Uint64 ZPerf_ResidentBytes();
void ZPerf_Enable(bool enabled);
bool ZPerf_Enabled();
void ZPerf_Record(const char *tag,Uint64 nanoseconds);
std::string ZPerf_Report();
bool ZPerf_Snapshot(const std::string &path);
std::string ZPerf_ServerReport(const std::string &path);
struct ZPerfScope {const char *tag;Uint64 start;explicit ZPerfScope(const char *name):tag(name),start(ZPerf_Enabled()?SDL_GetTicksNS():0){}~ZPerfScope(){if(start)ZPerf_Record(tag,SDL_GetTicksNS()-start);}};
#endif
