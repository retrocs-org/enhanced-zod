#ifndef _ZHTTP_H_
#define _ZHTTP_H_

#include <string>

// Blocking HTTP client with a two-second overall DNS/connect/send/read deadline.
// Call from a worker (the multiplayer menus use ZMPAsync).
struct ZHttpResponse
{
	bool ok;          // a response line was received and parsed
	int status;       // HTTP status (e.g. 200), or 0 if the request failed outright
	std::string body; // response body (the JSON)
};

ZHttpResponse ZHttp_Get(const std::string &host, int port, const std::string &path);
ZHttpResponse ZHttp_Post(const std::string &host, int port, const std::string &path,
                         const std::string &json_body);

#endif
