#ifndef ZREPLAY_H
#define ZREPLAY_H
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
struct ZReplayPacket {std::uint64_t ms=0;int id=0;std::vector<char> data;};
bool ZReplay_Allowed(int id);
bool ZReplay_SafePacket(int id,const char *data,int size);
bool ZReplay_Recover(const std::string &partial,std::string &output,std::string &error);
class ZReplayRecorder {public:~ZReplayRecorder(){Finish();}bool Start(const std::string &path,int team);bool Record(std::uint64_t ms,int id,const char *data,int size);bool Finish();bool Active()const{return stream.is_open();}private:std::ofstream stream;std::string path;std::uint64_t bytes=0,last=0,packets=0;bool complete=false,failed=false;};
class ZReplayTape {public:bool Load(const std::string &path,std::string &error);std::vector<ZReplayPacket> packets;std::string mapName,campaign,mission;int team=1;std::uint64_t duration=0;};
#endif
