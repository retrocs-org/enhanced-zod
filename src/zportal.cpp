#include "zportal.h"
#include "zsdl.h"
#include <fstream>
#include <curl/curl.h>
#include <mutex>
#include <cstdlib>
#include <cctype>
namespace {
size_t receive(char *data,size_t size,size_t count,void *target){auto &text=*static_cast<std::string*>(target);if(count>1024*1024||size>1024*1024||size*count>1024*1024-text.size())return 0;text.append(data,size*count);return size*count;}
bool allowed(const std::string &url){
    CURLU *parsed=curl_url();if(!parsed)return false;
    bool ok=curl_url_set(parsed,CURLUPART_URL,url.c_str(),0)==CURLUE_OK;
    char *scheme=nullptr,*host=nullptr,*user=nullptr,*query=nullptr,*fragment=nullptr;
    curl_url_get(parsed,CURLUPART_SCHEME,&scheme,0);curl_url_get(parsed,CURLUPART_HOST,&host,0);
    curl_url_get(parsed,CURLUPART_USER,&user,0);curl_url_get(parsed,CURLUPART_QUERY,&query,0);curl_url_get(parsed,CURLUPART_FRAGMENT,&fragment,0);
    std::string protocol=scheme?scheme:"",name=host?host:"";const char *dev=getenv("ZOD_PORTAL_DEV_HTTP");
    ok=ok&&!user&&!query&&!fragment&&(protocol=="https"||(protocol=="http"&&dev&&std::string(dev)=="1"&&(name=="127.0.0.1"||name=="localhost"||name=="[::1]")));
    curl_free(scheme);curl_free(host);curl_free(user);curl_free(query);curl_free(fragment);curl_url_cleanup(parsed);return ok;
}
}
std::string ZPortal_URL(){const char *configured=getenv("ZOD_PORTAL_URL");std::string base=configured&&*configured?configured:"https://zodee.retrocs.org/index.php";while(!base.empty()&&base.back()=='/')base.pop_back();return base;}
ZPortalResponse ZPortal_Request(const std::string &path,const std::string &json){
    ZPortalResponse response;static std::once_flag once;static bool initialized=false;
    std::call_once(once,[]{initialized=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;});
    if(!initialized){response.error="Could not initialize portal networking.";return response;}
    auto base=ZPortal_URL();if(!allowed(base)||path.empty()||path[0]!='/'||path.find("..")!=std::string::npos||path.find('\r')!=std::string::npos||path.find('\n')!=std::string::npos){response.error="Portal URL must use HTTPS (development HTTP is loopback-only).";return response;}
    CURL *curl=curl_easy_init();if(!curl){response.error="Could not open portal connection.";return response;}
    auto url=base+path;curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS|CURLPROTO_HTTP);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,5000L);curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,2500L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"zod-enhanced/native-portal");
    const char *ca=getenv("ZOD_PORTAL_CA_FILE");if(ca&&*ca)curl_easy_setopt(curl,CURLOPT_CAINFO,ca);
    else {auto bundle=ZSDL_DataPath("assets/portal-ca.crt");if(std::ifstream(bundle).good())curl_easy_setopt(curl,CURLOPT_CAINFO,bundle.c_str());}
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(curl,CURLOPT_WRITEDATA,&response.body);
    curl_slist *headers=nullptr;headers=curl_slist_append(headers,"Accept: application/json");
    if(!json.empty()){headers=curl_slist_append(headers,"Content-Type: application/json");curl_easy_setopt(curl,CURLOPT_POSTFIELDS,json.c_str());curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)json.size());}
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    CURLcode status=curl_easy_perform(curl);long code=0;curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);response.status=(int)code;
    char *content=nullptr;curl_easy_getinfo(curl,CURLINFO_CONTENT_TYPE,&content);
    response.ok=status==CURLE_OK&&content&&std::string(content).find("application/json")==0;
    if(status==CURLE_COULDNT_RESOLVE_HOST)response.error="Portal DNS lookup failed. Check your network or configured portal hostname.";
    else if(status==CURLE_OPERATION_TIMEDOUT)response.error="Portal timed out. Retry shortly; an outage does not require changing your password.";
    else if(status==CURLE_PEER_FAILED_VERIFICATION||status==CURLE_SSL_CACERT_BADFILE)response.error="Portal certificate verification failed. Check the system clock and certificate trust.";
    else if(status==CURLE_COULDNT_CONNECT)response.error="Portal is unreachable. Check network access; try again when the service returns.";
    else if(status!=CURLE_OK)response.error="Portal transport failed. Check network access and certificate trust.";
    else if(!response.ok)response.error="Portal did not return JSON. Check its URL and webserver routing.";
    else if(code>=300&&code<400){response.ok=false;response.error="Portal redirected the request. Configure the final HTTPS portal URL.";}
    curl_slist_free_all(headers);curl_easy_cleanup(curl);return response;
}
// Fields consumed by device authorization are ASCII and must not contain escapes.
bool ZPortal_String(const std::string &json,const std::string &key,std::string &out){
    auto start=json.find("\""+key+"\"");if(start==std::string::npos)return false;start=json.find(':',start+key.size()+2);if(start==std::string::npos)return false;
    do{++start;}while(start<json.size()&&std::isspace((unsigned char)json[start]));if(start>=json.size()||json[start++]!='"')return false;
    auto end=json.find('"',start);if(end==std::string::npos||end-start>512)return false;out=json.substr(start,end-start);
    for(unsigned char c:out)if(c<32||c>126||c=='\\')return false;return true;
}
