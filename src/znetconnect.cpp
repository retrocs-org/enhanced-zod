#include "znetconnect.h"
#include "client_socket.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#endif
#include <chrono>
#include <future>
#include <thread>
#include <vector>
#include <cstring>
#include <climits>
#include <algorithm>
namespace {
void close_fd(int fd) {
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}
}
ZConnectedSocket::~ZConnectedSocket() {
    if(fd>=0) close_fd(fd);
#ifdef _WIN32
    if(network) WSACleanup();
#endif
}
int ZConnectedSocket::Take() { int result=fd; fd=-1; return result; }
std::shared_ptr<ZConnectedSocket> ZNetConnect(const std::string &host, int port, std::string &error, int milliseconds) {
    error.clear();
    auto result=std::shared_ptr<ZConnectedSocket>(new ZConnectedSocket);
    if(host.empty() || port<1 || port>65535) { error="Invalid server address"; return {}; }
#ifdef _WIN32
    WSADATA data;
    if(WSAStartup(MAKEWORD(2,2),&data)!=0) { error="Could not initialize networking"; return {}; }
    result->network=true;
#endif
    using Clock=std::chrono::steady_clock;
    auto deadline=Clock::now()+std::chrono::milliseconds(std::min(15000,std::max(100,milliseconds)));
    auto promise=std::make_shared<std::promise<std::vector<sockaddr_in>>>();
    auto resolved=promise->get_future();
    std::thread([host,port,promise](){
        std::vector<sockaddr_in> addresses;
#ifdef _WIN32
        WSADATA data; bool network=WSAStartup(MAKEWORD(2,2),&data)==0;
#endif
        addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
        addrinfo *list=nullptr;
        if(getaddrinfo(host.c_str(),std::to_string(port).c_str(),&hints,&list)==0) {
            for(auto a=list;a;a=a->ai_next) if(a->ai_addrlen==sizeof(sockaddr_in)) { sockaddr_in address; memcpy(&address,a->ai_addr,sizeof(address)); addresses.push_back(address); }
            freeaddrinfo(list);
        }
        promise->set_value(addresses);
#ifdef _WIN32
        if(network) WSACleanup();
#endif
    }).detach();
    if(resolved.wait_until(deadline)!=std::future_status::ready) { error="Address lookup timed out"; return {}; }
    auto addresses=resolved.get();
    if(addresses.empty()) { error="Host could not be resolved"; return {}; }
    for(const auto &address:addresses) {
#ifdef _WIN32
        SOCKET raw=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(raw==INVALID_SOCKET) continue;
        if(raw>INT_MAX) { closesocket(raw); continue; }
        int fd=(int)raw; u_long one=1;
        bool configured=ioctlsocket(raw,FIONBIO,&one)==0;
#else
        int fd=socket(AF_INET,SOCK_STREAM,0);
        if(fd<0) continue;
        int flags=fcntl(fd,F_GETFL,0);
        bool configured=fd<FD_SETSIZE && flags>=0 && fcntl(fd,F_SETFL,flags|O_NONBLOCK)==0;
#endif
        bool connected=false;
        if(configured) {
            connected=connect(fd,(sockaddr*)&address,sizeof(address))==0;
#ifdef _WIN32
            bool pending=WSAGetLastError()==WSAEWOULDBLOCK;
#else
            bool pending=errno==EINPROGRESS;
#endif
            if(!connected && pending) {
                auto remaining=std::chrono::duration_cast<std::chrono::microseconds>(deadline-Clock::now()).count();
                if(remaining>0) {
                    fd_set writes; FD_ZERO(&writes); FD_SET(fd,&writes);
                    timeval timeout{}; timeout.tv_sec=(long)(remaining/1000000); timeout.tv_usec=(int)(remaining%1000000);
                    if(select(fd+1,nullptr,&writes,nullptr,&timeout)>0) {
                        int failure=0; socklen_t length=sizeof(failure);
                        connected=getsockopt(fd,SOL_SOCKET,SO_ERROR,(char*)&failure,&length)==0 && failure==0;
                    }
                }
            }
        }
        if(connected) { result->fd=fd; return result; }
        close_fd(fd);
        if(Clock::now()>=deadline) break;
    }
    error=Clock::now()>=deadline ? "Connection timed out" : "Connection refused or host unreachable";
    return {};
}

// A disposable preflight leaves the player's current session untouched on failure.
bool ZNetCheckVersion(const std::string &host,int port,std::string &error,int milliseconds) {
    auto started=std::chrono::steady_clock::now();
    auto connection=ZNetConnect(host,port,error,milliseconds);
    if(!connection) return false;
    std::list<Event*> events;
    ClientSocket probe; probe.SetEventList(&events);
    if(!probe.Adopt(*connection) || !probe.SendMessage(REQUEST_VERSION,nullptr,0)) {
        for(auto *event:events)delete event;
        error="Could not request server version. Check the server address and port.";return false;
    }
    auto deadline=started+std::chrono::milliseconds(milliseconds);
    bool received=false,compatible=false,closed=false;
    while(!received && !closed && std::chrono::steady_clock::now()<deadline) {
        probe.Process();
        for(auto *event:events) {
            if(event->event_type==OTHER_EVENT && event->event_number==DISCONNECT_EVENT)closed=true;
            if(event->event_type==TCP_EVENT && event->event_number==GIVE_VERSION) {
                received=true;
                if(event->size!=sizeof(version_packet) || !event->data || !memchr(event->data,0,sizeof(version_packet)))
                    error="Server sent an invalid version response.";
                else {
                    std::string version(event->data);
                    compatible=version==GAME_VERSION;
                    if(!compatible)error="Version mismatch: server "+version+", your game "+GAME_VERSION+". Install the same version before joining.";
                }
            }
            delete event;
        }
        events.clear();
        if(!received && !closed)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if(!received)error=closed?"Server closed the version check. Check its game port.":"Server version check timed out. Check the game port and firewall.";
    return compatible;
}
