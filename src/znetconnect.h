#ifndef ZNETCONNECT_H
#define ZNETCONNECT_H
#include <memory>
#include <string>

class ZConnectedSocket {
public:
    ~ZConnectedSocket();
    ZConnectedSocket(const ZConnectedSocket &) = delete;
    ZConnectedSocket &operator=(const ZConnectedSocket &) = delete;
    int Take();
private:
    ZConnectedSocket() = default;
    int fd = -1;
    bool network = false;
    friend std::shared_ptr<ZConnectedSocket> ZNetConnect(const std::string &, int, std::string &, int);
};
// Run on a worker. One deadline covers DNS and every connect attempt.
std::shared_ptr<ZConnectedSocket> ZNetConnect(const std::string &host, int port,
                                             std::string &error, int milliseconds = 3000);
#endif
