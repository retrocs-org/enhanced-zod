#include "zlan.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <random>
#include <set>

namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
bool init() { WSADATA data; return WSAStartup(MAKEWORD(2,2), &data) == 0; }
void cleanup() { WSACleanup(); }
bool nonblocking(Socket s) { u_long value = 1; return ioctlsocket(s, FIONBIO, &value) == 0; }
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) { close(s); }
bool init() { return true; }
void cleanup() {}
bool nonblocking(Socket s) { int flags = fcntl(s,F_GETFL,0); return flags >= 0 && fcntl(s,F_SETFL,flags|O_NONBLOCK) == 0; }
#endif
uint64_t nonce() { std::random_device random; return (uint64_t(random()) << 32) ^ random(); }
void put64(char *p, uint64_t value) { for(int i=7;i>=0;--i) { p[i]=(char)value; value >>= 8; } }
uint64_t get64(const char *p) { uint64_t v=0; for(int i=0;i<8;++i) v=(v<<8)|(unsigned char)p[i]; return v; }
std::vector<std::string> addresses() {
    std::set<std::string> result{"127.0.0.1", "255.255.255.255"};
#ifdef _WIN32
    ULONG size = 15000;
    std::vector<unsigned char> storage(size);
    auto adapters = (IP_ADAPTER_ADDRESSES*)storage.data();
    ULONG status = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &size);
    if(status == ERROR_BUFFER_OVERFLOW) { storage.resize(size); adapters=(IP_ADAPTER_ADDRESSES*)storage.data(); status=GetAdaptersAddresses(AF_INET,0,nullptr,adapters,&size); }
    if(status == NO_ERROR) for(auto a=adapters;a;a=a->Next) if(a->OperStatus==IfOperStatusUp)
        for(auto u=a->FirstUnicastAddress;u;u=u->Next) if(u->Address.lpSockaddr->sa_family==AF_INET && u->OnLinkPrefixLength>0 && u->OnLinkPrefixLength<31) {
            auto ip=(sockaddr_in*)u->Address.lpSockaddr;
            uint32_t mask=0xffffffffu << (32-u->OnLinkPrefixLength);
            in_addr broadcast{}; broadcast.s_addr=htonl((ntohl(ip->sin_addr.s_addr)&mask)|~mask);
            char text[INET_ADDRSTRLEN]; if(inet_ntop(AF_INET,&broadcast,text,sizeof(text))) result.insert(text);
        }
#else
    ifaddrs *list=nullptr;
    if(getifaddrs(&list)==0) {
        for(auto a=list;a;a=a->ifa_next) if(a->ifa_addr && a->ifa_addr->sa_family==AF_INET && (a->ifa_flags&IFF_UP) && (a->ifa_flags&IFF_BROADCAST) && a->ifa_broadaddr) {
            auto ip=(sockaddr_in*)a->ifa_broadaddr;
            char text[INET_ADDRSTRLEN]; if(inet_ntop(AF_INET,&ip->sin_addr,text,sizeof(text))) result.insert(text);
        }
        freeifaddrs(list);
    }
#endif
    return {result.begin(),result.end()};
}
}
ZLANServer::~ZLANServer() { Stop(); }
void ZLANServer::Stop() {
    if((Socket)socket!=invalid) close_socket((Socket)socket);
    socket=(intptr_t)invalid;
    if(network) cleanup(); network=false;
}
bool ZLANServer::Start(int port) {
    Stop(); if(port<1 || port>65535 || !init()) return false; network=true;
    Socket fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd==invalid) { Stop(); return false; }
    socket=(intptr_t)fd;
    sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons((unsigned short)port); local.sin_addr.s_addr=htonl(INADDR_ANY);
    if(!nonblocking(fd) || ::bind(fd,(sockaddr*)&local,sizeof(local))!=0) { Stop(); return false; }
    instance=nonce(); game_port=port; return true;
}
void ZLANServer::Poll(const std::string &map, int players, int capacity, bool started) {
    if((Socket)socket==invalid || capacity<1 || capacity>8) return;
    for(int i=0;i<8;++i) {
        char query[32]; sockaddr_in source{}; socklen_t length=sizeof(source);
        int n=(int)recvfrom((Socket)socket,query,sizeof(query),0,(sockaddr*)&source,&length);
        if(n<0) break;
        if(n!=16 || memcmp(query,"ZODLAN1?",8)) continue;
        std::string title=map.substr(map.find_last_of("/\\") == std::string::npos ? 0 : map.find_last_of("/\\")+1);
        if(title.size()>96) title.resize(96);
        for(char &c:title) if((unsigned char)c<32 || (unsigned char)c>126) c='?';
        char reply[126]{}; memcpy(reply,"ZODLAN1!",8); memcpy(reply+8,query+8,8); put64(reply+16,instance);
        reply[24]=(char)(game_port>>8); reply[25]=(char)game_port;
        reply[26]=(char)std::min(255,std::max(0,players)); reply[27]=(char)capacity;
        reply[28]=started ? 1 : 0; reply[29]=(char)title.size(); memcpy(reply+30,title.data(),title.size());
        sendto((Socket)socket,reply,(int)(30+title.size()),0,(sockaddr*)&source,length);
    }
}
bool ZLAN_Discover(std::vector<MatchInfo> &out, std::string &error, int milliseconds,
                   std::vector<std::string> targets, std::vector<int> ports) {
    out.clear(); error.clear();
    if(!init()) { error="Could not initialize LAN networking"; return false; }
    struct Context { ~Context() { cleanup(); } } context;
    Socket fd=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd==invalid) { error="Could not open LAN discovery socket"; return false; }
    struct Connection { Socket fd; ~Connection(){close_socket(fd);} } connection{fd};
    int one=1; setsockopt(fd,SOL_SOCKET,SO_BROADCAST,(const char*)&one,sizeof(one));
    setsockopt(fd,IPPROTO_IP,IP_TTL,(const char*)&one,sizeof(one));
    if(!nonblocking(fd)) { error="Could not scan LAN"; return false; }
    if(targets.empty()) targets=addresses();
    if(ports.empty()) { ports.push_back(2137); for(int port=2300;port<=2399;++port) ports.push_back(port); }
    uint64_t token=nonce(); char query[16]; memcpy(query,"ZODLAN1?",8); put64(query+8,token);
    for(const auto &target:targets) for(int port:ports) {
        if(port<1 || port>65535) continue;
        sockaddr_in remote{}; remote.sin_family=AF_INET; remote.sin_port=htons((unsigned short)port);
        if(inet_pton(AF_INET,target.c_str(),&remote.sin_addr)==1) sendto(fd,query,sizeof(query),0,(sockaddr*)&remote,sizeof(remote));
    }
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::min(3000,std::max(50,milliseconds)));
    int replies=0;
    while(std::chrono::steady_clock::now()<deadline && replies<1024) {
        char reply[512]; sockaddr_in source{}; socklen_t length=sizeof(source);
        int n=(int)recvfrom(fd,reply,sizeof(reply),0,(sockaddr*)&source,&length);
        if(n<0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        ++replies;
        if(n<30 || memcmp(reply,"ZODLAN1!",8) || get64(reply+8)!=token) continue;
        int size=(unsigned char)reply[29], port=((unsigned char)reply[24]<<8)|(unsigned char)reply[25];
        if(size>96 || n!=30+size || !port || port!=ntohs(source.sin_port) || (unsigned char)reply[27]>8 || !reply[27] || (unsigned char)reply[28]>1) continue;
        bool valid=true; for(int i=30;i<n;++i) if((unsigned char)reply[i]<32 || (unsigned char)reply[i]>126) valid=false;
        if(!valid) continue;
        char host[INET_ADDRSTRLEN]; if(!inet_ntop(AF_INET,&source.sin_addr,host,sizeof(host))) continue;
        MatchInfo match; match.id="lan-"+std::to_string(get64(reply+16)); match.host=host; match.port=port;
        match.map=std::string(reply+30,reply+30+size); match.name=match.map;
        match.players=(unsigned char)reply[26]; match.capacity=(unsigned char)reply[27];
        match.lan=true; match.started=reply[28]!=0;
        auto existing=std::find_if(out.begin(),out.end(),[&](const MatchInfo &item){return item.id==match.id;});
        if(existing==out.end()) out.push_back(match);
        else if(existing->host=="127.0.0.1" && match.host!="127.0.0.1") *existing=match;
    }
    return true;
}
bool ZMP_ParseAddress(const std::string &input, std::string &host, int &port, std::string &error) {
    host.clear(); port=-1; error.clear();
    auto fail=[&](const char *message){error=message;return false;};
    size_t begin=input.find_first_not_of(" \t\r\n"), end=input.find_last_not_of(" \t\r\n");
    if(begin==std::string::npos) return fail("Enter an IP address or hostname");
    std::string address=input.substr(begin,end-begin+1);
    size_t colon=address.find(':');
    if(colon!=std::string::npos && address.find(':',colon+1)!=std::string::npos) return fail("Use an IPv4 address or hostname, optionally :port");
    host=address.substr(0,colon); port=2137;
    if(host.empty() || host.size()>253) return fail("Invalid host address");
    if(colon!=std::string::npos) {
        std::string value=address.substr(colon+1);
        if(value.empty() || value.size()>5 || value.find_first_not_of("0123456789")!=std::string::npos) return fail("Port must be a number from 1 to 65535");
        port=std::stoi(value); if(port<1 || port>65535) return fail("Port must be from 1 to 65535");
    }
    bool numeric=host.find_first_not_of("0123456789.")==std::string::npos;
    if(numeric) { in_addr ip{}; if(inet_pton(AF_INET,host.c_str(),&ip)!=1) return fail("Invalid IPv4 address"); }
    else {
        size_t start=0;
        for(size_t i=0;i<=host.size();++i) if(i==host.size() || host[i]=='.') {
            if(i==start || i-start>63 || host[start]=='-' || host[i-1]=='-') return fail("Invalid hostname"); start=i+1;
        } else if(!((host[i]>='a'&&host[i]<='z')||(host[i]>='A'&&host[i]<='Z')||(host[i]>='0'&&host[i]<='9')||host[i]=='-')) return fail("Invalid hostname");
    }
    return true;
}
