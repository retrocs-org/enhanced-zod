#include "zperf.h"
#include <atomic>
#include <mutex>
#include <map>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <ctime>
#include <cmath>
#ifndef _WIN32
#include <sys/resource.h>
#endif
namespace {struct Sample {Uint64 count=0,total=0,maximum=0,histogram[32]{};};std::atomic<bool> enabled{false};std::mutex guard;std::map<std::string,Sample> samples;}
void ZPerf_Enable(bool value){enabled=value;}
bool ZPerf_Enabled(){return enabled;}
void ZPerf_Record(const char *tag,Uint64 ns){if(!enabled||!tag)return;std::lock_guard<std::mutex> lock(guard);if(samples.size()>=32&&!samples.count(tag))return;auto &sample=samples[tag];++sample.count;sample.total+=ns;sample.maximum=std::max(sample.maximum,ns);Uint64 us=ns/1000;int bin=0;while(us>1&&bin<31){us>>=1;++bin;}++sample.histogram[bin];}
std::string ZPerf_Report(){std::lock_guard<std::mutex> lock(guard);std::ostringstream out;out<<"Profiling: "<<(enabled?"enabled":"disabled")<<"\nStage,count,average_ms,max_ms,p95_bucket_upper_ms\n";for(const auto &pair:samples){const auto &s=pair.second;Uint64 cumulative=0;int bin=0;for(;bin<31;++bin){cumulative+=s.histogram[bin];if(cumulative*100>=s.count*95)break;}out<<pair.first<<","<<s.count<<","<<s.total/1e6/s.count<<","<<s.maximum/1e6<<","<<(1ULL<<(bin+1))/1000.0<<"\n";}
#ifndef _WIN32
struct rusage usage{};if(getrusage(RUSAGE_SELF,&usage)==0)out<<"Peak resident memory bytes: "<<usage.ru_maxrss
#ifndef __APPLE__
*1024LL
#endif
<<"\n";
#endif
return out.str();}

bool ZPerf_Snapshot(const std::string &path){if(!ZPerf_Enabled())return false;std::ofstream file(path+".tmp");file<<"Captured Unix seconds: "<<std::time(nullptr)<<"\n"<<ZPerf_Report();file.close();if(!file||!SDL_RenamePath((path+".tmp").c_str(),path.c_str())){SDL_RemovePath((path+".tmp").c_str());return false;}return true;}
std::string ZPerf_ServerReport(const std::string &path){std::ifstream file(path);if(!file)return "";char buffer[4096];file.read(buffer,sizeof(buffer));std::istringstream lines(std::string(buffer,file.gcount()));std::string line;std::ostringstream out;bool found=false;while(std::getline(lines,line)){if(line.size()>512)continue;std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);std::string tag;unsigned long long count;double average,maximum,p95;row>>tag;if(tag!="map_load"&&tag!="server_objects"&&tag!="pathfinding")continue;if(!(row>>count>>average>>maximum>>p95)||!count||!std::isfinite(average)||!std::isfinite(maximum)||!std::isfinite(p95)||average<0||maximum<0||p95<0)continue;found=true;out<<tag<<","<<count<<","<<average<<","<<maximum<<","<<p95<<"\n";}return found?"Last local server snapshot (may be from a previous battle):\nStage,count,average_ms,max_ms,p95_bucket_upper_ms\n"+out.str():"";}
