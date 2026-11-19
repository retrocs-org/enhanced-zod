#ifndef ZLAN_H
#define ZLAN_H
#include "zmpclient.h"
#include <cstdint>

// Dedicated/LAN hosts answer UDP discovery on their TCP game port. Discovery
// probes the default 2137 and local hosting pool 2300-2399; custom ports can be
// entered directly. Single-player servers never advertise.
class ZLANServer {
public:
    ZLANServer() = default;
    ~ZLANServer();
    ZLANServer(const ZLANServer &) = delete;
    ZLANServer &operator=(const ZLANServer &) = delete;
    bool Start(int port);
    void Stop();
    void Poll(const std::string &map, int players, int capacity, bool started);
private:
    std::intptr_t socket = -1;
    std::uint64_t instance = 0;
    int game_port = 0;
    bool network = false;
};
bool ZLAN_Discover(std::vector<MatchInfo> &out, std::string &error,
                   int milliseconds = 900, std::vector<std::string> targets = {},
                   std::vector<int> ports = {});
bool ZMP_ParseAddress(const std::string &input, std::string &host, int &port, std::string &error);
#endif
