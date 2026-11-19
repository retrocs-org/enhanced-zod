#ifndef ZLOCALSERVER_H
#define ZLOCALSERVER_H
#include <cstdint>
#include <string>
#include <vector>

// Owns one local dedicated server. Moving transfers ownership; destruction
// stops the server and releases its process handles (including its Windows job).
class ZLocalServerProcess
{
public:
    ZLocalServerProcess() = default;
    ~ZLocalServerProcess();
    ZLocalServerProcess(ZLocalServerProcess &&other) noexcept;
    ZLocalServerProcess &operator=(ZLocalServerProcess &&other) noexcept;
    ZLocalServerProcess(const ZLocalServerProcess &) = delete;
    ZLocalServerProcess &operator=(const ZLocalServerProcess &) = delete;
    bool Running();  //also reaps an exited child
    void Stop();
private:
    std::intptr_t process = 0, job = 0;
    friend bool ZLocalServer_Start(std::vector<std::string>, int,
                                   ZLocalServerProcess &, std::string &, std::vector<std::string>);
};

// Spawn a local match, waiting up to five seconds for its loopback listener.
// Failure cleans up the child; success transfers ownership to `process`.
bool ZLocalServer_Start(std::vector<std::string> args, int port,
                        ZLocalServerProcess &process, std::string &error_message,
                        std::vector<std::string> settings = {});
#endif
