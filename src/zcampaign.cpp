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
#include <functional>
#include <set>

namespace {
bool progress_replace(const std::string &path){
 bool primaryValid=true;
 if(path.size()>=4&&path.substr(path.size()-4)==".txt"&&path.find("-records.txt")==std::string::npos&&path.find("-completed.txt")==std::string::npos){int current,unlocked;std::ifstream check(path);primaryValid=bool(check>>current>>unlocked)&&(current>=0||current==-2)&&current<=128&&unlocked>=0&&unlocked<=128;}
 SDL_PathInfo info;if(primaryValid&&SDL_GetPathInfo(path.c_str(),&info)){
  if(!SDL_CopyFile(path.c_str(),(path+".bak.tmp").c_str())||!SDL_RenamePath((path+".bak.tmp").c_str(),(path+".bak").c_str()))return false;
 }
 return SDL_RenamePath((path+".tmp").c_str(),path.c_str());
}

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
  if(!mission){if(key=="format")format=value=="1";else if(key=="id")out.id=value;else if(key=="title")out.title=value;else if(key=="author")out.author=value;else if(key=="description")out.description=value;else if(key=="cover")out.cover=value;else if(key=="difficulty")out.difficulty=value;else if(key=="version")out.version=value;else if(key=="engine_min")out.engineMin=value;else if(key=="engine_max")out.engineMax=value;else if(key=="requires"){if(out.requires.size()>=32){error="Too many campaign dependencies";return false;}out.requires.push_back(value);}}
  else {if(key=="next_win_uid")mission->nextWinUID=value;else if(key=="next_loss_uid")mission->nextLossUID=value;else if(key=="uid")mission->uid=value;else if(key=="map")mission->map=value;else if(key=="title")mission->title=value;else if(key=="briefing")mission->briefing=value;else if(key=="speaker")mission->speaker=value;else if(key=="character")mission->character=value;else if(key=="video")mission->video=value;else if(key=="victory_video")mission->victoryVideo=value;else if(key=="next_win"||key=="next_loss"){char *end=nullptr;long index=strtol(value.c_str(),&end,10);if(value.empty()||!end||*end||index< -2||index>127){error="Invalid branch mission index";return false;}(key=="next_win"?mission->nextWin:mission->nextLoss)=index;}else if(key=="event"){ZMissionEvent event;if(mission->events.size()>=64||!ZMission_ParseEvent(value,event)){error="Invalid mission event";return false;}mission->events.push_back(event);}else if(key=="objective"){ZMissionObjective o;if(mission->objectives.size()>=64||!ZMission_Parse(value,o)){error="Invalid objective; use kind|target|seconds|text";return false;}mission->objectives.push_back(o);}}
 }
 if(!format||!slug(out.id)||out.title.empty()||out.title.size()>120||out.missions.empty()){error="Campaign needs format=1, a unique ID, title and missions";return false;}
 if((!out.engineMin.empty()&&ZCampaign_CompareVersion(out.engineMin,out.engineMin)==-2)||(!out.engineMax.empty()&&ZCampaign_CompareVersion(out.engineMax,out.engineMax)==-2)||ZCampaign_CompareVersion(out.version,out.version)==-2){error="Campaign versions must use numeric components";return false;}
 if(!out.engineMin.empty()&&!out.engineMax.empty()&&ZCampaign_CompareVersion(out.engineMin,out.engineMax)>0){error="Minimum engine version exceeds maximum";return false;}
 std::set<std::string> identities;for(size_t i=0;i<out.missions.size();++i){auto &mission=out.missions[i];if(mission.uid.empty()){unsigned long long hash=1469598103934665603ULL;for(unsigned char c:mission.map){hash^=c;hash*=1099511628211ULL;}std::ostringstream name;name<<"mission-"<<std::hex<<hash;mission.uid=name.str();int suffix=1;while(identities.count(mission.uid))mission.uid=name.str()+"-"+std::to_string(++suffix);}if(!slug(mission.uid)||mission.uid.size()>64||!identities.insert(mission.uid).second){error="Mission IDs must be unique and contain letters, digits, hyphens or underscores";return false;}}
 for(auto &mission:out.missions)for(int branch=0;branch<2;++branch){auto &uid=branch?mission.nextLossUID:mission.nextWinUID;auto &index=branch?mission.nextLoss:mission.nextWin;if(uid.empty())continue;if(uid=="default")index=-1;else if(uid=="end")index=-2;else {index=-3;for(size_t i=0;i<out.missions.size();++i)if(out.missions[i].uid==uid)index=i;if(index==-3){error="Branch references an unknown mission ID";return false;}}}
 for(const auto &mission:out.missions)if(mission.nextWin>=(int)out.missions.size()||mission.nextLoss>=(int)out.missions.size()){error="Campaign branch points outside its mission list";return false;}
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
 campaign={};campaign.engineMin=ZOD_VERSION;campaign.id=id;campaign.title=title.substr(0,120);campaign.root=root+"/"+id;if(!SDL_CreateDirectory(campaign.root.c_str())){error=SDL_GetError();return false;}
 std::ofstream marker(campaign.root+"/.zod-authoring");marker<<"Zod campaign workspace\n";if(!marker){error="Could not create authoring project";return false;}return true;}
bool ZCampaign_Write(const ZCampaign &source,std::string &error){ZCampaign campaign=source;if(campaign.engineMin.empty()||ZCampaign_CompareVersion(campaign.engineMin,"0.7.0")<0)campaign.engineMin="0.7.0";std::set<std::string> used;for(auto &mission:campaign.missions){if(mission.uid.empty()){unsigned long long hash=1469598103934665603ULL;for(unsigned char c:mission.map){hash^=c;hash*=1099511628211ULL;}std::ostringstream name;name<<"mission-"<<std::hex<<hash;mission.uid=name.str();int suffix=1;while(used.count(mission.uid))mission.uid=name.str()+"-"+std::to_string(++suffix);}used.insert(mission.uid);}std::string marker;if(!slug(campaign.id)||!ZCampaign_SafeAsset(campaign.root,".zod-authoring",marker)||!inside(ZCampaign_Root(),campaign.root)){error="Imported campaigns are read-only; create a new campaign to author a copy";return false;}
 std::string path=campaign.root+"/campaign.ini",temp=path+".tmp";std::ofstream out(temp,std::ios::trunc);out<<"format=1\nid="<<campaign.id<<"\ntitle="<<escaped(campaign.title)<<"\nauthor="<<escaped(campaign.author)<<"\ndescription="<<escaped(campaign.description)<<"\ncover="<<escaped(campaign.cover)<<"\ndifficulty="<<escaped(campaign.difficulty)<<"\nversion="<<escaped(campaign.version)<<"\nengine_min="<<escaped(campaign.engineMin)<<"\nengine_max="<<escaped(campaign.engineMax)<<"\n";for(auto &dependency:campaign.requires)out<<"requires="<<escaped(dependency)<<"\n";
 for(const auto &m:campaign.missions){out<<"\n[mission]\nuid="<<escaped(m.uid)<<"\ntitle="<<escaped(m.title)<<"\nmap="<<escaped(m.map)<<"\nbriefing="<<escaped(m.briefing)<<"\nspeaker="<<escaped(m.speaker)<<"\ncharacter="<<escaped(m.character)<<"\nvideo="<<escaped(m.video)<<"\nvictory_video="<<escaped(m.victoryVideo)<<"\n";auto branchUID=[&](int index){return index==-1?std::string("default"):index==-2?std::string("end"):index>=0&&index<(int)campaign.missions.size()?campaign.missions[index].uid:std::string("invalid");};out<<"next_win_uid="<<branchUID(m.nextWin)<<"\nnext_loss_uid="<<branchUID(m.nextLoss)<<"\n";out<<"next_win="<<m.nextWin<<"\nnext_loss="<<m.nextLoss<<"\n";for(const auto &event:m.events)out<<"event="<<escaped(ZMission_EncodeEvent(event))<<"\n";for(const auto &o:m.objectives)out<<"objective="<<escaped(ZMission_Encode(o))<<"\n";}
 out.close();ZCampaign validated;if(!out||!ZCampaign_Load(temp,validated,error)){remove(temp.c_str());if(error.empty())error="Campaign could not be validated";return false;}if(!SDL_RenamePath(temp.c_str(),path.c_str())){error="Campaign could not be saved";return false;}return true;}
void ZCampaign_Progress(const std::string &id,int &current,int &unlocked,bool save){if(!slug(id))return;std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+".txt";
 if(save){std::ofstream out(path+".tmp");out<<current<<" "<<unlocked<<"\n";for(const auto &pack:ZCampaign_List())if(pack.id==id&&current>=0&&current<(int)pack.missions.size())out<<pack.missions[current].uid<<"\n";if(current==-2)out<<"end\n";out.close();if(out)progress_replace(path);}
 else {current=0;unlocked=0;std::ifstream input(path);int c,u;{int checkC,checkU;std::ifstream check(path);if(!(check>>checkC>>checkU)||(checkC<0&&checkC!=-2)||checkU<0||checkC>128||checkU>128){input.close();input.open(path+".bak");}}if(input>>c>>u&&(c>=0||c==-2)&&u>=0&&c<=128&&u<=128){current=c;unlocked=u;std::string uid;if(input>>uid){if(uid=="end")current=-2;else for(const auto &pack:ZCampaign_List())if(pack.id==id)for(size_t i=0;i<pack.missions.size();++i)if(pack.missions[i].uid==uid)current=i;}}}
}

std::vector<std::string> ZCampaign_Completed(const std::string &id){
 std::vector<std::string> maps;if(id!="original"&&!slug(id))return maps;
 std::ifstream input(ZAsset_Profile()+"campaign-progress/"+id+"-completed.txt");std::string line;
 while(maps.size()<4096&&std::getline(input,line))if(!line.empty()&&line.size()<1024&&std::find(maps.begin(),maps.end(),line)==maps.end())maps.push_back(line);return maps;
}
void ZCampaign_MarkCompleted(const std::string &id,const std::string &map){
 if((id!="original"&&!slug(id))||map.empty()||map.size()>=1024||map.find_first_of("\r\n")!=std::string::npos)return;
 auto maps=ZCampaign_Completed(id);if(std::find(maps.begin(),maps.end(),map)!=maps.end())return;maps.push_back(map);
 std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+"-completed.txt";std::ofstream output(path+".tmp");for(auto &name:maps)output<<name<<"\n";output.close();if(output)progress_replace(path);
}

std::vector<ZCampaignRecord> ZCampaign_Records(const std::string &id){
 std::vector<ZCampaignRecord> records;if(id!="original"&&!slug(id))return records;std::ifstream file(ZAsset_Profile()+"campaign-progress/"+id+"-records.txt");std::string line;
 while(records.size()<4096&&std::getline(file,line)){ZCampaignRecord r;std::istringstream row(line);if(!(row>>std::quoted(r.key)>>r.wins>>r.completedAt>>r.bestSeconds>>r.kills>>r.lost>>r.difficulty))continue;if(!(row>>r.medal))r.medal=r.lost==0&&r.difficulty>=2?3:r.lost==0?2:1;if(r.key.size()<1024&&r.wins>0&&r.wins<1000000&&r.completedAt>=0&&r.completedAt<=32503680000LL&&r.bestSeconds>=0&&r.kills>=0&&r.lost>=0&&r.difficulty>=0&&r.difficulty<4&&r.medal>=1&&r.medal<=3)records.push_back(r);}return records;
}
void ZCampaign_RecordWin(const std::string &id,const std::string &key,int seconds,int kills,int lost,int difficulty){
 if((id!="original"&&!slug(id))||key.empty()||key.size()>=1024||key.find_first_of("\n\r")!=std::string::npos||seconds<0)return;
 auto records=ZCampaign_Records(id);auto found=std::find_if(records.begin(),records.end(),[&](const ZCampaignRecord&r){return r.key==key;});if(found==records.end()){records.push_back({});found=records.end()-1;found->key=key;}
 found->medal=std::max(found->medal,lost==0&&difficulty>=2?3:lost==0?2:1);found->wins++;found->completedAt=std::time(nullptr);found->bestSeconds=found->bestSeconds<0?seconds:std::min(found->bestSeconds,seconds);found->kills=std::max(0,kills);found->lost=std::max(0,lost);found->difficulty=std::max(found->difficulty,std::max(0,std::min(3,difficulty)));
 std::string dir=ZAsset_Profile()+"campaign-progress";SDL_CreateDirectory(dir.c_str());std::string path=dir+"/"+id+"-records.txt";std::ofstream out(path+".tmp");for(auto&r:records)out<<std::quoted(r.key)<<" "<<r.wins<<" "<<r.completedAt<<" "<<r.bestSeconds<<" "<<r.kills<<" "<<r.lost<<" "<<r.difficulty<<" "<<r.medal<<"\n";out.close();if(out)progress_replace(path);
}
std::string ZCampaign_Medal(const ZCampaignRecord&r){if(!r.wins)return "";return r.medal>=3?"Gold":r.medal==2?"Silver":"Bronze";}

namespace {
bool version_parts(const std::string &text,std::vector<int> &parts){if(text.empty()||text.size()>32)return false;std::istringstream input(text);std::string part;while(std::getline(input,part,'.')){if(part.empty()||part.size()>6||parts.size()==3)return false;int value=0;for(char c:part){if(c<'0'||c>'9')return false;value=value*10+c-'0';}parts.push_back(value);}if(text.back()=='.')return false;parts.resize(3);return true;}}
int ZCampaign_CompareVersion(const std::string &a,const std::string &b){std::vector<int>x,y;if(!version_parts(a,x)||!version_parts(b,y))return -2;return x==y?0:x<y?-1:1;}
bool ZCampaign_Branched(const ZCampaign &campaign){for(const auto &mission:campaign.missions)if(mission.nextWin!=-1||mission.nextLoss!=-1)return true;return false;}
std::vector<std::string> ZCampaign_Compatibility(const ZCampaign &candidate,const std::vector<ZCampaign> &installed){
 std::vector<std::string> errors;std::set<std::string> stack,done;
 std::function<void(const ZCampaign&)> visit=[&](const ZCampaign &pack){if(stack.count(pack.id)){errors.push_back("Dependency cycle at "+pack.id);return;}if(done.count(pack.id))return;stack.insert(pack.id);
 if(!pack.engineMin.empty()&&ZCampaign_CompareVersion(ZOD_VERSION,pack.engineMin)<0)errors.push_back(pack.title+" requires engine "+pack.engineMin+" or newer");
 if(!pack.engineMax.empty()&&ZCampaign_CompareVersion(ZOD_VERSION,pack.engineMax)>0)errors.push_back(pack.title+" supports engine up to "+pack.engineMax);
 if(!pack.engineMin.empty()&&!pack.engineMax.empty()&&ZCampaign_CompareVersion(pack.engineMin,pack.engineMax)>0)errors.push_back("Minimum engine version exceeds maximum");
 for(const auto &requirement:pack.requires){auto at=requirement.find('@');std::string id=requirement.substr(0,at),minimum=at==std::string::npos?"0":requirement.substr(at+1);if(!slug(id)||ZCampaign_CompareVersion(minimum,minimum)==-2){errors.push_back("Invalid dependency: "+requirement);continue;}const ZCampaign *dependency=id==candidate.id?&candidate:nullptr;for(const auto &other:installed)if(other.id==id)dependency=id==candidate.id?&candidate:&other;if(!dependency){errors.push_back("Missing campaign: "+requirement);continue;}if(ZCampaign_CompareVersion(dependency->version,minimum)<0)errors.push_back("Update "+id+" to "+minimum+" or newer");visit(*dependency);}
 stack.erase(pack.id);done.insert(pack.id);
 };visit(candidate);return errors;
}

std::string ZCampaign_MissionKey(const ZCampaign &campaign,int index){if(index<0||index>=(int)campaign.missions.size())return "";return "uid:"+campaign.missions[index].uid;}
bool ZCampaign_MigrateIdentity(const ZCampaign &campaign,std::string &error){
 if(campaign.id.empty()||campaign.id=="original")return true;int current=0,unlocked=0;ZCampaign_Progress(campaign.id,current,unlocked);std::ifstream oldProgress(ZAsset_Profile()+"campaign-progress/"+campaign.id+".txt");int oldCurrent,oldUnlocked;std::string oldIdentity;oldProgress>>oldCurrent>>oldUnlocked>>oldIdentity;bool legacy=oldIdentity.empty();if(!legacy&&oldIdentity!="end"&&std::none_of(campaign.missions.begin(),campaign.missions.end(),[&](const ZCampaignMission &mission){return mission.uid==oldIdentity;})){error="Updated campaign removed your current mission. Restore the previous pack; progress was kept.";return false;}auto complete=ZCampaign_Completed(campaign.id);auto records=ZCampaign_Records(campaign.id);
 auto translate=[&](const std::string &key){for(size_t i=0;i<campaign.missions.size();++i)if(key==std::to_string(i)+":"+campaign.missions[i].map)return ZCampaign_MissionKey(campaign,i);return key;};
 for(auto &key:complete)key=translate(key);if(legacy&&!ZCampaign_Branched(campaign))for(int i=0;i<unlocked&&i<(int)campaign.missions.size();++i){auto key=ZCampaign_MissionKey(campaign,i);if(std::find(complete.begin(),complete.end(),key)==complete.end())complete.push_back(key);}for(auto &record:records)record.key=translate(record.key);
 auto folder=ZAsset_Profile()+"campaign-progress/";SDL_CreateDirectory(folder.c_str());auto write=[&](const std::string &suffix,const std::string &text){auto path=folder+campaign.id+suffix;std::ofstream out(path+".tmp");out<<text;out.close();return out&&progress_replace(path);};std::ostringstream done,score,progress;for(auto &key:complete)done<<key<<"\n";for(auto &v:records)score<<std::quoted(v.key)<<" "<<v.wins<<" "<<v.completedAt<<" "<<v.bestSeconds<<" "<<v.kills<<" "<<v.lost<<" "<<v.difficulty<<" "<<v.medal<<"\n";progress<<current<<" "<<unlocked<<"\n"<<(current>=0&&current<(int)campaign.missions.size()?campaign.missions[current].uid:current==-2?"end":"")<<"\n";if(!write("-completed.txt",done.str())||!write("-records.txt",score.str())||!write(".txt",progress.str())){error="Could not preserve campaign identity/progress";return false;}return true;
}
