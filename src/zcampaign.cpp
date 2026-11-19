#include "zcampaign.h"
#include "zasset_import.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <iomanip>
#include <ctime>
namespace {
bool slug(const std::string &s){if(s.empty()||s.size()>64||s=="original")return false;for(unsigned char c:s)if(!std::isalnum(c)&&c!='-'&&c!='_')return false;return true;}
std::string clean(std::string s){s.erase(std::remove(s.begin(),s.end(),'\r'),s.end());return s;}
std::string escaped(const std::string &s){std::string out;for(char c:s){if(c=='\\')out+="\\\\";else if(c=='\n')out+="\\n";else if(c!='\r')out+=c;}return out;}
std::string unescape(const std::string &s){std::string out;for(size_t i=0;i<s.size();++i){if(s[i]=='\\'&&i+1<s.size()){++i;out+=s[i]=='n'?'\n':s[i];}else out+=s[i];}return out;}
std::string canonical(const std::string &path){
#ifdef _WIN32
 char buffer[_MAX_PATH];std::string value=_fullpath(buffer,path.c_str(),sizeof(buffer))?buffer:"";for(char &c:value){if(c=='\\')c='/';else c=(char)std::tolower((unsigned char)c);}return value;
#else
 char buffer[PATH_MAX];return realpath(path.c_str(),buffer)?buffer:"";
#endif
}
bool inside(const std::string &root,const std::string &file){auto r=canonical(root),f=canonical(file);return !r.empty()&&!f.empty()&&f.size()>r.size()&&f.compare(0,r.size()+1,r+"/")==0;}
}
std::string ZCampaign_Root(){if(const char *overridePath=SDL_getenv("ZOD_CAMPAIGN_DIR")){SDL_CreateDirectory(overridePath);return overridePath;}const char *folder=SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);std::string root=folder&&*folder?folder:ZAsset_Profile();if(root.back()!='/')root+='/';root+="Zod Campaigns";SDL_CreateDirectory(root.c_str());return root;}
bool ZCampaign_SafeAsset(const std::string &root,const std::string &relative,std::string &resolved){
 if(relative.empty()||relative.size()>240||relative[0]=='/'||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos)return false;
 std::istringstream stream(relative);std::string part;while(std::getline(stream,part,'/'))if(part.empty()||part=="."||part=="..")return false;
 resolved=root+"/"+relative;SDL_PathInfo info;return SDL_GetPathInfo(resolved.c_str(),&info)&&info.type==SDL_PATHTYPE_FILE&&inside(root,resolved);
}
bool ZCampaign_Load(const std::string &file,ZCampaign &out,std::string &error){
 out={};error.clear();SDL_PathInfo info;if(!SDL_GetPathInfo(file.c_str(),&info)||info.size>262144){error="Campaign manifest missing or too large";return false;}
 std::ifstream input(file);if(!input){error="Could not read campaign manifest";return false;}
 out.root=file.substr(0,file.find_last_of("/\\"));std::string line;bool format=false;ZCampaignMission *mission=nullptr;
 while(std::getline(input,line)){
  line=clean(line);if(line.empty()||line[0]=='#')continue;
  if(line=="[mission]"){if(out.missions.size()>=128){error="Too many missions";return false;}out.missions.emplace_back();mission=&out.missions.back();continue;}
  auto eq=line.find('=');if(eq==std::string::npos){error="Invalid campaign entry";return false;}auto key=line.substr(0,eq),value=unescape(line.substr(eq+1));
  if(value.size()>4096){error="Campaign text too long";return false;}
  if(!mission){if(key=="format")format=value=="1";else if(key=="id")out.id=value;else if(key=="title")out.title=value;else if(key=="author")out.author=value;else if(key=="description")out.description=value;else if(key=="cover")out.cover=value;else if(key=="difficulty")out.difficulty=value;else if(key=="version")out.version=value;}
  else {if(key=="map")mission->map=value;else if(key=="title")mission->title=value;else if(key=="briefing")mission->briefing=value;else if(key=="speaker")mission->speaker=value;else if(key=="character")mission->character=value;else if(key=="video")mission->video=value;else if(key=="victory_video")mission->victoryVideo=value;else if(key=="objective"){ZMissionObjective o;if(mission->objectives.size()>=64||!ZMission_Parse(value,o)){error="Invalid objective; use kind|target|seconds|text";return false;}mission->objectives.push_back(o);}}
 }
 if(!format||!slug(out.id)||out.title.empty()||out.title.size()>120||out.missions.empty()){error="Campaign needs format=1, a unique ID, title and missions";return false;}
 if(!out.cover.empty()){std::string asset;if(!ZCampaign_SafeAsset(out.root,out.cover,asset)){error="Missing or unsafe cover image";return false;}}
 for(auto &m:out.missions){std::string path;if(!ZCampaign_SafeAsset(out.root,m.map,path)||m.map.size()<4||m.map.substr(m.map.size()-4)!=".map"){error="Missing or unsafe mission map: "+m.map;return false;}
  if(m.title.empty())m.title=m.map;
  for(const auto &video:{m.video,m.victoryVideo})if(!video.empty()){
    if(video.compare(0,9,"original:")==0){auto clip=video.substr(9);if(clip.empty()||clip.size()>32){error="Invalid original video name";return false;}for(unsigned char c:clip)if(!std::isalnum(c)&&c!='_'){error="Invalid original video name";return false;}}
    else {if(video.find("..")!=std::string::npos||video[0]=='/'||video.find(':')!=std::string::npos||video.find('\\')!=std::string::npos){error="Unsafe video path";return false;}
      std::string ignored;SDL_PathInfo info;if(SDL_GetPathInfo((out.root+"/"+video).c_str(),&info)&&!ZCampaign_SafeAsset(out.root,video,ignored)){error="Video escapes campaign folder";return false;}}
  }
 }
 return true;
}
std::vector<ZCampaign> ZCampaign_List(std::vector<std::string> *errors){std::vector<ZCampaign> result;std::string root=ZCampaign_Root();int count=0;char **paths=SDL_GlobDirectory(root.c_str(),"*",0,&count);
 for(int i=0;paths&&i<count;++i){if(paths[i][0]=='.')continue;std::string dir=root+"/"+paths[i];SDL_PathInfo info;if(!SDL_GetPathInfo(dir.c_str(),&info)||info.type!=SDL_PATHTYPE_DIRECTORY||!inside(root,dir))continue;ZCampaign campaign;std::string error;
  if(ZCampaign_Load(dir+"/campaign.ini",campaign,error)) {bool duplicate=false;for(const auto &c:result)duplicate|=c.id==campaign.id;if(!duplicate)result.push_back(campaign);else if(errors)errors->push_back("Duplicate campaign ID: "+campaign.id);}
  else if(errors)errors->push_back(std::string(paths[i])+": "+error);
 }SDL_free(paths);std::sort(result.begin(),result.end(),[](const ZCampaign&a,const ZCampaign&b){return a.title<b.title;});return result;}
bool ZCampaign_Create(const std::string &title,ZCampaign &campaign,std::string &error){if(title.empty()){error="Campaign title is required";return false;}std::string id;for(unsigned char c:title)if(std::isalnum(c))id+=(char)std::tolower(c);else if(c==' '||c=='-')id+='-';if(id.empty()||id=="original")id="new-campaign";id=id.substr(0,48);auto root=ZCampaign_Root();std::string base=id;SDL_PathInfo info;int suffix=2;while(SDL_GetPathInfo((root+"/"+id).c_str(),&info))id=base+"-"+std::to_string(suffix++);
 campaign={};campaign.id=id;campaign.title=title.substr(0,120);campaign.root=root+"/"+id;if(!SDL_CreateDirectory(campaign.root.c_str())){error=SDL_GetError();return false;}
 std::ofstream marker(campaign.root+"/.zod-authoring");marker<<"Zod campaign workspace\n";if(!marker){error="Could not create authoring project";return false;}return true;}
bool ZCampaign_Write(const ZCampaign &campaign,std::string &error){std::string marker;if(!slug(campaign.id)||!ZCampaign_SafeAsset(campaign.root,".zod-authoring",marker)||!inside(ZCampaign_Root(),campaign.root)){error="Imported campaigns are read-only; create a new campaign to author a copy";return false;}
 std::string path=campaign.root+"/campaign.ini",temp=path+".tmp";std::ofstream out(temp,std::ios::trunc);out<<"format=1\nid="<<campaign.id<<"\ntitle="<<escaped(campaign.title)<<"\nauthor="<<escaped(campaign.author)<<"\ndescription="<<escaped(campaign.description)<<"\ncover="<<escaped(campaign.cover)<<"\ndifficulty="<<escaped(campaign.difficulty)<<"\nversion="<<escaped(campaign.version)<<"\n";
 for(const auto &m:campaign.missions){out<<"\n[mission]\ntitle="<<escaped(m.title)<<"\nmap="<<escaped(m.map)<<"\nbriefing="<<escaped(m.briefing)<<"\nspeaker="<<escaped(m.speaker)<<"\ncharacter="<<escaped(m.character)<<"\nvideo="<<escaped(m.video)<<"\nvictory_video="<<escaped(m.victoryVideo)<<"\n";for(const auto &o:m.objectives)out<<"objective="<<escaped(ZMission_Encode(o))<<"\n";}
 out.close();ZCampaign validated;if(!out||!ZCampaign_Load(temp,validated,error)){remove(temp.c_str());if(error.empty())error="Campaign could not be validated";return false;}if(!SDL_RenamePath(temp.c_str(),path.c_str())){error="Campaign could not be saved";return false;}return true;}
void ZCampaign_Progress(const std::string &id,int &current,int &unlocked,bool save){if(!slug(id))return;std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+".txt";
 if(save){std::ofstream out(path+".tmp");out<<current<<" "<<unlocked<<"\n";out.close();if(out)SDL_RenamePath((path+".tmp").c_str(),path.c_str());}
 else {current=0;unlocked=0;std::ifstream input(path);int c,u;if(input>>c>>u&&c>=0&&u>=0&&c<=128&&u<=128){current=c;unlocked=u;}}
}

std::vector<std::string> ZCampaign_Completed(const std::string &id){
 std::vector<std::string> maps;if(id!="original"&&!slug(id))return maps;
 std::ifstream input(ZAsset_Profile()+"campaign-progress/"+id+"-completed.txt");std::string line;
 while(maps.size()<4096&&std::getline(input,line))if(!line.empty()&&line.size()<1024&&std::find(maps.begin(),maps.end(),line)==maps.end())maps.push_back(line);return maps;
}
void ZCampaign_MarkCompleted(const std::string &id,const std::string &map){
 if((id!="original"&&!slug(id))||map.empty()||map.size()>=1024||map.find_first_of("\r\n")!=std::string::npos)return;
 auto maps=ZCampaign_Completed(id);if(std::find(maps.begin(),maps.end(),map)!=maps.end())return;maps.push_back(map);
 std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+"-completed.txt";std::ofstream output(path+".tmp");for(auto &name:maps)output<<name<<"\n";output.close();if(output)SDL_RenamePath((path+".tmp").c_str(),path.c_str());
}

std::vector<ZCampaignRecord> ZCampaign_Records(const std::string &id){
 std::vector<ZCampaignRecord> records;if(id!="original"&&!slug(id))return records;std::ifstream file(ZAsset_Profile()+"campaign-progress/"+id+"-records.txt");std::string line;
 while(records.size()<4096&&std::getline(file,line)){ZCampaignRecord r;std::istringstream row(line);if(!(row>>std::quoted(r.key)>>r.wins>>r.completedAt>>r.bestSeconds>>r.kills>>r.lost>>r.difficulty))continue;if(!(row>>r.medal))r.medal=r.lost==0&&r.difficulty>=2?3:r.lost==0?2:1;if(r.key.size()<1024&&r.wins>0&&r.wins<1000000&&r.completedAt>=0&&r.completedAt<=32503680000LL&&r.bestSeconds>=0&&r.kills>=0&&r.lost>=0&&r.difficulty>=0&&r.difficulty<4&&r.medal>=1&&r.medal<=3)records.push_back(r);}return records;
}
void ZCampaign_RecordWin(const std::string &id,const std::string &key,int seconds,int kills,int lost,int difficulty){
 if((id!="original"&&!slug(id))||key.empty()||key.size()>=1024||key.find_first_of("\n\r")!=std::string::npos||seconds<0)return;
 auto records=ZCampaign_Records(id);auto found=std::find_if(records.begin(),records.end(),[&](const ZCampaignRecord&r){return r.key==key;});if(found==records.end()){records.push_back({});found=records.end()-1;found->key=key;}
 found->medal=std::max(found->medal,lost==0&&difficulty>=2?3:lost==0?2:1);found->wins++;found->completedAt=std::time(nullptr);found->bestSeconds=found->bestSeconds<0?seconds:std::min(found->bestSeconds,seconds);found->kills=std::max(0,kills);found->lost=std::max(0,lost);found->difficulty=std::max(found->difficulty,std::max(0,std::min(3,difficulty)));
 std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+"-records.txt";std::ofstream out(path+".tmp");for(auto&r:records)out<<std::quoted(r.key)<<" "<<r.wins<<" "<<r.completedAt<<" "<<r.bestSeconds<<" "<<r.kills<<" "<<r.lost<<" "<<r.difficulty<<" "<<r.medal<<"\n";out.close();if(out)SDL_RenamePath((path+".tmp").c_str(),path.c_str());
}
std::string ZCampaign_Medal(const ZCampaignRecord&r){if(!r.wins)return "";return r.medal>=3?"Gold":r.medal==2?"Silver":"Bronze";}
