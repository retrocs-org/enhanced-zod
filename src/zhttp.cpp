#include "zhttp.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif
#include <chrono>
#include <future>
#include <thread>
#include <memory>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <sstream>

using namespace std;
namespace {
using Clock = chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
const Socket bad_socket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
bool pending() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
using Socket = int;
const Socket bad_socket = -1;
void close_socket(Socket s) { close(s); }
bool pending() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
#endif
struct Connection {
    Socket fd;
    ~Connection() { if(fd != bad_socket) close_socket(fd); }
};
struct Address { sockaddr_storage addr; socklen_t size; };

bool wait_socket(Socket s, bool writing, Clock::time_point deadline)
{
    for(;;) {
        auto remaining = chrono::duration_cast<chrono::microseconds>(deadline - Clock::now()).count();
        if(remaining <= 0) return false;
        timeval timeout; timeout.tv_sec = (long)(remaining / 1000000); timeout.tv_usec = (long)(remaining % 1000000);
        fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
        int n = select((int)s + 1, writing ? nullptr : &fds, writing ? &fds : nullptr, nullptr, &timeout);
        if(n > 0) return true;
        if(n == 0 || !pending()) return false;
    }
}

ZHttpResponse request(const string &method, const string &host, int port,
                      const string &path, const string &body)
{
    ZHttpResponse failed{false, 0, ""};
    if(port < 1 || port > 65535 || host.find_first_of("\r\n") != string::npos
       || path.find_first_of("\r\n") != string::npos) return failed;
    auto deadline = Clock::now() + chrono::seconds(2);
    // getaddrinfo has no portable cancellation API. Its worker owns all data,
    // so a slow resolver may finish safely after the request deadline expires.
    auto promise = make_shared<std::promise<vector<Address>>>();
    auto resolved = promise->get_future();
    thread([promise, host, port]() {
        vector<Address> addresses;
        addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
        addrinfo *result = nullptr;
        if(getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &result) == 0) {
            for(auto a = result; a; a = a->ai_next) {
                if(a->ai_addrlen > sizeof(sockaddr_storage)) continue;
                Address address{}; address.size = (socklen_t)a->ai_addrlen;
                memcpy(&address.addr, a->ai_addr, a->ai_addrlen); addresses.push_back(address);
            }
            freeaddrinfo(result);
        }
        promise->set_value(addresses);
    }).detach();
    if(resolved.wait_until(deadline) != future_status::ready) return failed;
    Connection connection{bad_socket};
    for(const auto &a : resolved.get()) {
        Connection candidate{socket(a.addr.ss_family, SOCK_STREAM, 0)};
        if(candidate.fd == bad_socket) continue;
#ifdef _WIN32
        u_long nonblocking = 1;
        if(ioctlsocket(candidate.fd, FIONBIO, &nonblocking) != 0) continue;
#else
        if(candidate.fd >= FD_SETSIZE) continue;
        int flags = fcntl(candidate.fd, F_GETFL, 0);
        if(flags < 0 || fcntl(candidate.fd, F_SETFL, flags | O_NONBLOCK) < 0) continue;
#ifdef SO_NOSIGPIPE
        int one = 1; setsockopt(candidate.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
        if(connect(candidate.fd, (const sockaddr*)&a.addr, a.size) != 0) {
            if(!pending() || !wait_socket(candidate.fd, true, deadline)) continue;
            int error = 0; socklen_t size = sizeof(error);
            if(getsockopt(candidate.fd, SOL_SOCKET, SO_ERROR, (char*)&error, &size) != 0 || error) continue;
        }
        connection.fd = candidate.fd; candidate.fd = bad_socket; break;
    }
    if(connection.fd == bad_socket) return failed;
    ostringstream header;
    header << method << " " << path << " HTTP/1.1\r\nHost: " << host << ":" << port
           << "\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: " << body.size() << "\r\n\r\n";
    string data = header.str() + body;
    size_t sent = 0;
    while(sent < data.size()) {
        if(!wait_socket(connection.fd, true, deadline)) return failed;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        int n = (int)send(connection.fd, data.data() + sent, (int)(data.size() - sent), flags);
        if(n < 0 && pending()) continue;
        if(n <= 0) return failed;
        sent += n;
    }
    string raw;
    char buffer[4096];
    for(;;) {
        if(!wait_socket(connection.fd, false, deadline)) return failed;
        int n = (int)recv(connection.fd, buffer, sizeof(buffer), 0);
        if(n < 0 && pending()) continue;
        if(n < 0) return failed;
        if(n == 0) break;
        raw.append(buffer, (size_t)n);
        if(raw.size() > 4 * 1024 * 1024) return failed;
    }
    size_t end = raw.find("\r\n\r\n");
    if(end == string::npos || raw.compare(0, 5, "HTTP/") != 0) return failed;
    size_t space = raw.find(' ');
    if(space == string::npos || space > raw.find("\r\n")) return failed;
    int status = atoi(raw.c_str() + space + 1);
    if(status < 100 || status > 599) return failed;
    // The orchestrator uses Connection: close; reject truncated fixed-length bodies.
    string response_body = raw.substr(end + 4);
    string headers = raw.substr(0, end);
    for(char &c : headers) if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
    size_t length = headers.find("\r\ncontent-length:");
    if(length != string::npos && strtoull(headers.c_str() + length + 17, nullptr, 10) != response_body.size()) return failed;
    return ZHttpResponse{true, status, response_body};
}
}
ZHttpResponse ZHttp_Get(const string &host, int port, const string &path)
{ return request("GET", host, port, path, ""); }
ZHttpResponse ZHttp_Post(const string &host, int port, const string &path, const string &body)
{ return request("POST", host, port, path, body); }
