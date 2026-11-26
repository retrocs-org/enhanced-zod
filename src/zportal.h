#ifndef ZPORTAL_H
#define ZPORTAL_H
#include <string>
struct ZPortalResponse {bool ok=false;int status=0;std::string body,error;};
// HTTPS only; explicit development HTTP is restricted to loopback hosts.
std::string ZPortal_URL();
ZPortalResponse ZPortal_Request(const std::string &path,const std::string &json="");
bool ZPortal_String(const std::string &json,const std::string &key,std::string &out);
#endif
