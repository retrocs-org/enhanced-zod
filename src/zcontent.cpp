#include "zcontent.h"
#include "zasset_import.h"
#include <algorithm>
#include <queue>
#include <set>
#include <fstream>
#include <sstream>
#include <mutex>
#include <cctype>
#include <zlib.h>
using namespace std;
bool ZContent_HasErrors(const vector<ZContentIssue> &issues){for(const auto &i:issues)if(i.error)return true;return false;}
vector<ZContentIssue> ZContent_ValidateMap(ZMap &map){
 vector<ZContentIssue> out;auto &h=map.GetMapBasics();bool red=false,enemy=false;vector<pair<int,int>> forts;
 for(const auto &o:map.GetObjectList()){
  if(o.x>=h.width||o.y>=h.height){out.push_back({true,"Object outside map bounds",o.x,o.y});continue;}
  if(o.object_type==BUILDING_OBJECT&&(o.object_id==FORT_FRONT||o.object_id==FORT_BACK)){red|=o.owner==RED_TEAM;enemy|=o.owner>NULL_TEAM&&o.owner!=RED_TEAM;forts.push_back({o.x,o.y});}
 }
 for(const auto &o:map.GetObjectList()){
  if(o.owner<NULL_TEAM||o.owner>=MAX_TEAM_TYPES)out.push_back({true,"Object has an invalid team",o.x,o.y});
  bool valid=o.object_type<MAX_MAP_OBJECT_TYPES;
  if(o.object_type==BUILDING_OBJECT)valid=o.object_id<MAX_BUILDING_TYPES;
  if(o.object_type==ROBOT_OBJECT)valid=o.object_id<MAX_ROBOT_TYPES;
  if(o.object_type==VEHICLE_OBJECT)valid=o.object_id<MAX_VEHICLE_TYPES;
  if(o.object_type==CANNON_OBJECT)valid=o.object_id<MAX_CANNON_TYPES;
  if(!valid)out.push_back({true,"Object has an invalid type",o.x,o.y});
 }
 if(!red)out.push_back({true,"Place a red player fort"});if(!enemy)out.push_back({true,"Place at least one opposing fort"});
 if(map.GetZoneList().empty())out.push_back({false,"No territory zones; factories cannot change territory ownership"});
 for(auto &z:map.GetZoneList())if(!z.w||!z.h||z.x+z.w>h.width||z.y+z.h>h.height)out.push_back({true,"Territory outside map bounds",z.x,z.y});
 // Static terrain connectivity is advisory: bridges and destructible terrain can change routes.
 vector<int> component(h.width*h.height,-1);int regions=0;bool metadata=false;
 for(int i=0;i<h.width*h.height;++i)metadata|=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,map.GetTile(i).tile).is_passable;
 if(metadata){
  for(int y=0;y<h.height;++y)for(int x=0;x<h.width;++x){int start=y*h.width+x;if(component[start]>=0||!ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,map.GetTile(start).tile).is_passable)continue;
   queue<int> q;q.push(start);component[start]=regions;
   while(!q.empty()){int p=q.front();q.pop();int px=p%h.width,py=p/h.width;for(auto step:vector<pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}){int nx=px+step.first,ny=py+step.second;if(nx<0||ny<0||nx>=h.width||ny>=h.height)continue;int n=ny*h.width+nx;if(component[n]<0&&ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,map.GetTile(n).tile).is_passable){component[n]=regions;q.push(n);}}}++regions;
  }
  // A fort can touch more than one region. Compare every approach, rather than
  // the first passable tile (which may be an isolated decorative patch).
  auto approaches=[&](int px,int py){set<int> found;for(int dy=-1;dy<=5;++dy)for(int dx=-1;dx<=5;++dx){int x=px+dx,y=py+dy;if(x>=0&&y>=0&&x<h.width&&y<h.height&&component[y*h.width+x]>=0)found.insert(component[y*h.width+x]);}return found;};
  set<int> playerRegions;
  for(const auto &o:map.GetObjectList())if(o.object_type==BUILDING_OBJECT&&(o.object_id==FORT_FRONT||o.object_id==FORT_BACK)&&o.owner==RED_TEAM){auto regions=approaches(o.x,o.y);playerRegions.insert(regions.begin(),regions.end());}
  for(auto f:forts){auto regions=approaches(f.first,f.second);bool reachable=false;for(int region:regions)reachable|=playerRegions.count(region)>0;
   if(regions.empty())out.push_back({false,"Fort has no nearby passable terrain",f.first,f.second});
   else if(!playerRegions.empty()&&!reachable)out.push_back({false,"Fort may be unreachable by land; check bridges and routes",f.first,f.second});
  }
  for(auto &z:map.GetZoneList()){
   if(!z.w||!z.h||z.x+z.w>h.width||z.y+z.h>h.height)continue;
   bool passable=false,reachable=false;
   for(int y=z.y;y<z.y+z.h;++y)for(int x=z.x;x<z.x+z.w;++x){int region=component[y*h.width+x];passable|=region>=0;reachable|=playerRegions.count(region)>0;}
   if(!passable)out.push_back({false,"Territory has no passable land; check capture access",z.x,z.y});
   else if(!playerRegions.empty()&&!reachable)out.push_back({false,"Territory may be unreachable from the player fort; check bridges and routes",z.x,z.y});
  }
 }
 return out;
}
vector<ZContentIssue> ZContent_ValidateCampaign(const ZCampaign &campaign,bool distribution){
 vector<ZContentIssue> out;ZCampaign parsed;string error;
 if(!ZCampaign_Load(campaign.root+"/campaign.ini",parsed,error)){out.push_back({true,error});return out;}
 vector<bool> reached(parsed.missions.size(),false);vector<int> pending{0};bool canFinish=false;
 while(!pending.empty()){int i=pending.back();pending.pop_back();if(i<0||i>=(int)reached.size()||reached[i])continue;reached[i]=true;const auto &m=parsed.missions[i];for(int outcome=0;outcome<2;++outcome){int branch=outcome?m.nextLoss:m.nextWin;if(branch==-2){canFinish=true;continue;}int next=branch==-1?(outcome?i:i+1):branch;if(next==(int)reached.size())canFinish=true;else pending.push_back(next);}}
 for(size_t i=0;i<reached.size();++i)if(!reached[i])out.push_back({false,"Mission "+to_string(i+1)+": unreachable from the opening mission; check win/loss links"});
 if(!canFinish)out.push_back({false,"Campaign has no reachable ending; all mission branches loop"});
 vector<bool> finishes(parsed.missions.size(),false);bool changed=true;
 while(changed){changed=false;for(size_t i=0;i<parsed.missions.size();++i)if(!finishes[i]){const auto &m=parsed.missions[i];for(int outcome=0;outcome<2;++outcome){int branch=outcome?m.nextLoss:m.nextWin;int next=branch==-1?(outcome?(int)i:(int)i+1):branch;if(branch==-2||next==(int)finishes.size()||(next>=0&&next<(int)finishes.size()&&finishes[next])){finishes[i]=true;changed=true;break;}}}}
 for(size_t i=0;i<finishes.size();++i)if(reached[i]&&!finishes[i])out.push_back({distribution,"Mission "+to_string(i+1)+": reachable branch cannot reach a campaign ending; fix the win/loss loop"});
 if(!parsed.cover.empty()){string cover;if(!ZCampaign_SafeAsset(parsed.root,parsed.cover,cover))out.push_back({distribution,"Campaign cover image is missing"});}
 set<string> usedMaps;
 for(size_t i=0;i<parsed.missions.size();++i){auto &m=parsed.missions[i];string path;ZMap map;
  size_t firstIssue=out.size();
  const string prefix="Mission "+to_string(i+1)+": ";
  if(!usedMaps.insert(m.map).second)out.push_back({false,prefix+"reuses a previous mission map; check mission order"});
  if(m.briefing.empty())out.push_back({false,prefix+"no briefing text; add instructions before distributing"});
  if(m.objectives.empty())out.push_back({false,prefix+"uses the default opposing-forts victory condition"});
  bool waiting=false;
  for(const auto &objective:m.objectives){ZMissionObjective checked;if(!ZMission_Parse(ZMission_Encode(objective),checked))out.push_back({true,prefix+"invalid objective"});if(objective.kind=="survive"&&objective.seconds>0)waiting=true;}
  if(!m.objectives.empty()&&!waiting&&std::all_of(m.objectives.begin(),m.objectives.end(),[](const ZMissionObjective &o){return o.kind=="message";}))out.push_back({false,prefix+"only message objectives; mission will finish after its messages"});
  if(!ZCampaign_SafeAsset(parsed.root,m.map,path)||!map.Read(path.c_str()))out.push_back({true,"Mission "+to_string(i+1)+": invalid map"});
  else {int reinforcements=0;for(const auto &event:m.events){auto prefix="Mission "+to_string(i+1)+": ";if(event.trigger=="capture"&&event.target>=(int)map.GetZoneList().size())out.push_back({true,prefix+"event territory does not exist"});if(event.trigger=="destroy"&&event.target>=(int)map.GetObjectList().size())out.push_back({true,prefix+"event object does not exist"});if(event.kind=="reinforce"){reinforcements+=event.count;bool unit=(event.type==ROBOT_OBJECT&&event.id<MAX_ROBOT_TYPES)||(event.type==VEHICLE_OBJECT&&event.id<MAX_VEHICLE_TYPES)||(event.type==CANNON_OBJECT&&event.id<MAX_CANNON_TYPES);if(!unit||event.x>=map.GetMapBasics().width||event.y>=map.GetMapBasics().height)out.push_back({true,prefix+"invalid reinforcement type or location"});}}if(reinforcements>256)out.push_back({true,"Too many scripted reinforcements"});for(const auto &o:m.objectives){if(o.kind=="escort"&&(o.target>=(int)map.GetObjectList().size()||o.seconds>=(int)map.GetZoneList().size()||map.GetObjectList()[o.target].owner!=RED_TEAM))out.push_back({true,"Escort needs a red map object and destination territory"});if(o.kind=="escort"&&o.target>=0&&o.target<(int)map.GetObjectList().size()){
    const auto &target=map.GetObjectList()[o.target];if(target.object_type!=ROBOT_OBJECT&&target.object_type!=VEHICLE_OBJECT)out.push_back({true,prefix+"escort target must be a movable robot or vehicle",target.x,target.y});
   }if(o.kind=="capture"&&o.target>=(int)map.GetZoneList().size())out.push_back({true,"Mission "+to_string(i+1)+": capture target territory does not exist"});if(o.kind=="defend"&&(o.target>=(int)map.GetObjectList().size()||map.GetObjectList()[o.target].object_type!=BUILDING_OBJECT||map.GetObjectList()[o.target].owner!=RED_TEAM))out.push_back({true,"Mission "+to_string(i+1)+": defend target must be a red building"});}
  for(auto issue:ZContent_ValidateMap(map)){issue.text="Mission "+to_string(i+1)+": "+issue.text;out.push_back(issue);}}
  for(auto video:{m.video,m.victoryVideo})if(!video.empty()&&video.compare(0,9,"original:")!=0&&!ZCampaign_SafeAsset(parsed.root,video,path))out.push_back({distribution,"Mission "+to_string(i+1)+": missing video "+video+(distribution?"; remove the reference or add the file before export":" (briefing fallback)")});
  for(size_t issue=firstIssue;issue<out.size();++issue)out[issue].mission=(int)i;
 }
 return out;
}
namespace {
struct Entry{string name;vector<unsigned char> bytes;};
void put(vector<unsigned char>&b,unsigned n,int size){for(int i=0;i<size;++i)b.push_back((n>>(i*8))&255);}
unsigned get(const vector<unsigned char>&b,size_t p,int n){if(p+n>b.size())throw string("Truncated campaign archive");unsigned v=0;for(int i=0;i<n;++i)v|=(unsigned)b[p+i]<<(8*i);return v;}
vector<unsigned char> read(const string &path){ifstream f(path,ios::binary|ios::ate);if(!f||f.tellg()<0||f.tellg()>512*1024*1024)throw string("Campaign file missing or exceeds 512 MB");vector<unsigned char>b((size_t)f.tellg());f.seekg(0);if(!b.empty()&&!f.read((char*)b.data(),b.size()))throw string("Campaign file could not be read");return b;}
bool safe(const string &name){if(name.empty()||name.size()>240||name[0]=='/'||name.find('\\')!=string::npos||name.find(':')!=string::npos||name.find('\0')!=string::npos)return false;istringstream s(name);string p;while(getline(s,p,'/')){if(p.empty()||p=="."||p==".."||p[0]=='.'||p.back()=='.'||p.back()==' ')return false;for(unsigned char c:p)if(c<32||c=='<'||c=='>'||c=='"'||c=='|'||c=='?'||c=='*')return false;string base=p.substr(0,p.find('.'));transform(base.begin(),base.end(),base.begin(),::toupper);if(base=="CON"||base=="PRN"||base=="AUX"||base=="NUL"||(base.size()==4&&(base.substr(0,3)=="COM"||base.substr(0,3)=="LPT")&&base[3]>='1'&&base[3]<='9'))return false;}return true;}
void removeTree(const string &root){int count=0;char **paths=SDL_GlobDirectory(root.c_str(),"*",0,&count);for(int i=0;paths&&i<count;++i){string p=root+"/"+paths[i];SDL_PathInfo info;if(SDL_GetPathInfo(p.c_str(),&info)&&info.type==SDL_PATHTYPE_DIRECTORY)removeTree(p);else SDL_RemovePath(p.c_str());}SDL_free(paths);SDL_RemovePath(root.c_str());}
vector<Entry> unpack(const string &path){auto b=read(path);if(b.size()<22)throw string("Not a ZIP campaign pack");size_t end=b.size()-22,limit=b.size()>65557?b.size()-65557:0;while(get(b,end,4)!=0x06054b50){if(end==limit)throw string("ZIP directory missing");--end;}
 if(get(b,end+4,2)||get(b,end+6,2))throw string("Multi-disk archives are unsupported");unsigned count=get(b,end+10,2);if(!count||count>1024||get(b,end+8,2)!=count)throw string("Campaign pack has too many files");size_t pos=get(b,end+16,4);vector<Entry> out;set<string> names;size_t total=0;
 for(unsigned i=0;i<count;++i){if(get(b,pos,4)!=0x02014b50)throw string("Invalid ZIP directory");unsigned flags=get(b,pos+8,2),method=get(b,pos+10,2),crc=get(b,pos+16,4),compressed=get(b,pos+20,4),size=get(b,pos+24,4),len=get(b,pos+28,2),extra=get(b,pos+30,2),comment=get(b,pos+32,2),attr=get(b,pos+38,4),offset=get(b,pos+42,4);if(pos+46+len+extra+comment>b.size())throw string("Truncated ZIP directory");string name((char*)b.data()+pos+46,len);pos+=46+len+extra+comment;
  if((flags&1)||(method!=0&&method!=8)||((attr>>16)&0170000)==0120000)throw string("Encrypted or linked archive entries are unsupported");if(!name.empty()&&name.back()=='/'){name.pop_back();if(!safe(name))throw string("Unsafe campaign directory");continue;}if(!safe(name))throw string("Unsafe campaign filename");string lower=name;transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return tolower(c);});if(!names.insert(lower).second)throw string("Duplicate campaign filename");total+=size;if(total>512*1024*1024||size>128*1024*1024)throw string("Unpacked campaign exceeds size limits");if(get(b,offset,4)!=0x04034b50||get(b,offset+8,2)!=method)throw string("Invalid ZIP entry");size_t start=offset+30+get(b,offset+26,2)+get(b,offset+28,2);if(start+compressed>b.size())throw string("Truncated ZIP entry");Entry e;e.name=name;e.bytes.resize(size);
  if(method==0){if(size!=compressed)throw string("Invalid stored ZIP size");copy(b.begin()+start,b.begin()+start+size,e.bytes.begin());}else{z_stream stream{};stream.next_in=b.data()+start;stream.avail_in=compressed;unsigned char empty;stream.next_out=size?e.bytes.data():&empty;stream.avail_out=size?size:1;if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK)throw string("Cannot initialize ZIP decoder");int result=inflate(&stream,Z_FINISH);inflateEnd(&stream);if(result!=Z_STREAM_END||stream.total_out!=size||stream.total_in!=compressed)throw string("Invalid compressed ZIP entry");}
  if(crc32(0,e.bytes.data(),e.bytes.size())!=crc)throw string("Campaign checksum failed");out.push_back(std::move(e));
 }
 return out;
}
bool stage(const string &archive,ZCampaign &summary,string &staged,string &error,const string &isolated=""){try{auto entries=unpack(archive);string prefix;int manifests=0;for(auto &e:entries)if(e.name=="campaign.ini"||e.name.size()>13&&e.name.substr(e.name.size()-13)=="/campaign.ini"){prefix=e.name.substr(0,e.name.size()-12);++manifests;}if(manifests!=1)throw string("A campaign pack must contain exactly one campaign.ini");staged=(isolated.empty()?ZCampaign_Root():isolated)+"/.import-preview-"+to_string(SDL_GetTicksNS());if(!SDL_CreateDirectory(staged.c_str()))throw string("Cannot create import staging folder");
 for(auto &e:entries){if(e.name.compare(0,prefix.size(),prefix)!=0)throw string("Files outside campaign folder");string name=e.name.substr(prefix.size());if(!safe(name))throw string("Unsafe package contents");string target=staged+"/"+name;auto slash=target.find_last_of('/');SDL_CreateDirectory(target.substr(0,slash).c_str());ofstream f(target,ios::binary);f.write((char*)e.bytes.data(),e.bytes.size());if(!f)throw string("Could not extract campaign");}
 if(!ZCampaign_Load(staged+"/campaign.ini",summary,error))throw error;auto issues=ZContent_ValidateCampaign(summary);for(auto &issue:issues)if(issue.error)throw issue.text;return true;
 }catch(const string &e){error=e;if(!staged.empty())removeTree(staged);staged.clear();return false;}}
mutex dialogMutex;string dialogPath;
}
bool ZCampaign_InspectPack(const string &archive,ZCampaign &summary,string &error){string staged;if(!stage(archive,summary,staged,error))return false;removeTree(staged);summary.root.clear();return true;}
bool ZCampaign_ImportPack(const string &archive,string &error,bool update){
 ZCampaign c;string staged;if(!stage(archive,c,staged,error))return false;
 auto compatibility=ZCampaign_Compatibility(c,ZCampaign_List());if(!compatibility.empty()){error=compatibility.front();removeTree(staged);return false;}
 string target=ZCampaign_Root()+"/"+c.id,backup;bool existing=false;
 for(auto &installed:ZCampaign_List())if(installed.id==c.id){existing=true;target=installed.root;
 if(update&&ZCampaign_CompareVersion(c.version,installed.version)<=0){error="Update must have a newer campaign version; installed "+installed.version;removeTree(staged);return false;}
 if(update){bool compatible=c.missions.size()>=installed.missions.size();for(size_t i=0;compatible&&i<installed.missions.size();++i)compatible=std::any_of(c.missions.begin(),c.missions.end(),[&](const ZCampaignMission &mission){return mission.uid==installed.missions[i].uid;});if(!compatible){error="Update removes or changes mission IDs. Preserve existing IDs or use a new campaign ID.";removeTree(staged);return false;}}
 string marker;if(!update||ZCampaign_SafeAsset(target,".zod-authoring",marker)){error=update?"Authoring projects must be updated in the editor":"Campaign ID already installed: "+c.id+". Choose Update explicitly.";removeTree(staged);return false;}}
 SDL_PathInfo info;if(!existing&&SDL_GetPathInfo(target.c_str(),&info)){error="Campaign destination already exists";removeTree(staged);return false;}
 if(existing){for(auto &pack:ZCampaign_List())if(pack.id==c.id&&!ZCampaign_MigrateIdentity(pack,error)){removeTree(staged);return false;}backup=ZCampaign_Root()+"/.backup-"+c.id+"-"+to_string(SDL_GetTicksNS());if(!SDL_RenamePath(target.c_str(),backup.c_str())){error="Could not back up installed campaign";removeTree(staged);return false;}}
 if(!SDL_RenamePath(staged.c_str(),target.c_str())){if(!backup.empty())SDL_RenamePath(backup.c_str(),target.c_str());removeTree(staged);error="Could not install campaign; previous version restored";return false;}
 // Retain the old version as a hidden rollback backup; progress/records live outside the pack.
 return true;
}
bool ZCampaign_Uninstall(const string &id,string &error){
 for(auto &campaign:ZCampaign_List())if(campaign.id==id){string marker;if(ZCampaign_SafeAsset(campaign.root,".zod-authoring",marker)){error="Authoring projects cannot be uninstalled here";return false;}string destination=ZCampaign_Root()+"/.uninstalled-"+id+"-"+to_string(SDL_GetTicksNS());if(!SDL_RenamePath(campaign.root.c_str(),destination.c_str())){error="Could not remove campaign";return false;}return true;}error="Campaign is not installed";return false;
}
bool ZCampaign_Export(const ZCampaign &campaign,string &output,string &error){
 try{auto issues=ZContent_ValidateCampaign(campaign,true);for(auto &i:issues)if(i.error)throw i.text;set<string> names{"campaign.ini"};if(!campaign.cover.empty())names.insert(campaign.cover);for(auto &m:campaign.missions){names.insert(m.map);for(auto v:{m.video,m.victoryVideo}){string p;if(!v.empty()&&v.compare(0,9,"original:")!=0&&ZCampaign_SafeAsset(campaign.root,v,p))names.insert(v);size_t dot=v.find_last_of('.');string subtitle=v.substr(0,dot)+".srt";if(ZCampaign_SafeAsset(campaign.root,subtitle,p))names.insert(subtitle);}}
 vector<unsigned char> bytes,central;unsigned count=0;for(auto &name:names){string path;if(!ZCampaign_SafeAsset(campaign.root,name,path)||!safe(name))throw string("Unsafe export path");auto data=read(path);if(bytes.size()+data.size()>512*1024*1024)throw string("Campaign pack exceeds 512 MB");string full=campaign.id+"/"+name;unsigned crc=crc32(0,data.data(),data.size()),offset=bytes.size();put(bytes,0x04034b50,4);put(bytes,20,2);put(bytes,0x800,2);put(bytes,0,2);put(bytes,0,2);put(bytes,33,2);put(bytes,crc,4);put(bytes,data.size(),4);put(bytes,data.size(),4);put(bytes,full.size(),2);put(bytes,0,2);bytes.insert(bytes.end(),full.begin(),full.end());bytes.insert(bytes.end(),data.begin(),data.end());put(central,0x02014b50,4);put(central,20,2);put(central,20,2);put(central,0x800,2);put(central,0,2);put(central,0,2);put(central,33,2);put(central,crc,4);put(central,data.size(),4);put(central,data.size(),4);put(central,full.size(),2);for(int i=0;i<4;++i)put(central,0,2);put(central,0,4);put(central,offset,4);central.insert(central.end(),full.begin(),full.end());++count;}
 unsigned offset=bytes.size();bytes.insert(bytes.end(),central.begin(),central.end());put(bytes,0x06054b50,4);put(bytes,0,2);put(bytes,0,2);put(bytes,count,2);put(bytes,count,2);put(bytes,central.size(),4);put(bytes,offset,4);put(bytes,0,2);string folder=ZAsset_Profile()+"campaign-exports";SDL_CreateDirectory(folder.c_str());output=folder+"/"+campaign.id+"-"+to_string(SDL_GetTicksNS())+".zip";ofstream f(output+".tmp",ios::binary);f.write((char*)bytes.data(),bytes.size());f.close();if(!f||!SDL_RenamePath((output+".tmp").c_str(),output.c_str()))throw string("Could not write campaign pack");return true;
 }catch(const string &e){error=e;return false;}
}
void ZCampaign_PickPack(SDL_Window *window){static const SDL_DialogFileFilter filter={"Enhanced ZOD campaign pack","zip"};SDL_ShowOpenFileDialog([](void*,const char *const *files,int){lock_guard<mutex> lock(dialogMutex);dialogPath=files&&files[0]?files[0]:"";},nullptr,window,&filter,1,nullptr,false);}
bool ZCampaign_PollPack(string &path){lock_guard<mutex> lock(dialogMutex);if(dialogPath.empty())return false;path=dialogPath;dialogPath.clear();return true;}

std::vector<ZCampaignBackup> ZCampaign_Backups(const string &id){vector<ZCampaignBackup> result;if(id.empty()||id.size()>100||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=string::npos)return result;auto root=ZCampaign_Root();int count=0;char **files=SDL_GlobDirectory(root.c_str(),(".backup-"+id+"-*").c_str(),0,&count);vector<pair<SDL_Time,ZCampaignBackup>> sorted;for(int i=0;files&&i<count&&i<128;++i){string manifest,error;if(!ZCampaign_SafeAsset(root,string(files[i])+"/campaign.ini",manifest))continue;ZCampaign backup;SDL_PathInfo info{};if(ZCampaign_Load(manifest,backup,error)&&backup.id==id&&SDL_GetPathInfo(manifest.c_str(),&info))sorted.push_back({info.modify_time,{files[i],backup.version}});}SDL_free(files);std::sort(sorted.begin(),sorted.end(),[](const pair<SDL_Time,ZCampaignBackup> &a,const pair<SDL_Time,ZCampaignBackup> &b){return a.first>b.first;});for(auto &entry:sorted)result.push_back(entry.second);return result;}
bool ZCampaign_RestoreBackup(const string &id,const string &folder,string &error){
 auto backups=ZCampaign_Backups(id);if(std::none_of(backups.begin(),backups.end(),[&](const ZCampaignBackup &backup){return backup.folder==folder;})){error="Backup not found";return false;}auto installed=ZCampaign_List();auto current=find_if(installed.begin(),installed.end(),[&](const ZCampaign &pack){return pack.id==id;});if(current==installed.end()){error="Install the campaign before restoring an update";return false;}string marker;if(ZCampaign_SafeAsset(current->root,".zod-authoring",marker)){error="Authoring projects cannot be replaced";return false;}string manifest;ZCampaign candidate;if(!ZCampaign_SafeAsset(ZCampaign_Root(),folder+"/campaign.ini",manifest)||!ZCampaign_Load(manifest,candidate,error))return false;if(ZContent_HasErrors(ZContent_ValidateCampaign(candidate))){error="Backup content failed validation";return false;}auto target=current->root;*current=candidate;for(auto &pack:installed){auto issues=ZCampaign_Compatibility(pack,installed);if(!issues.empty()){error="Cannot restore: "+issues.front();return false;}}
 if(!ZCampaign_MigrateIdentity(candidate,error))return false;
 string preserved=ZCampaign_Root()+"/.backup-"+id+"-"+to_string(SDL_GetTicksNS());if(!SDL_RenamePath(target.c_str(),preserved.c_str())){error="Could not preserve current version";return false;}if(!SDL_RenamePath(candidate.root.c_str(),target.c_str())){bool restored=SDL_RenamePath(preserved.c_str(),target.c_str());error=restored?"Restore failed; current version retained":"Restore failed; current version preserved at "+preserved;return false;}return true;
}

bool ZCampaign_CheckPortable(const string &archive,string &report,string &error){
 auto root=ZAsset_Profile()+"portability-"+to_string(SDL_GetTicksNS());if(!SDL_CreateDirectory(root.c_str())){error="Cannot create isolated profile";return false;}ZCampaign campaign;string staged;bool ok=stage(archive,campaign,staged,error,root);int maps=0;if(ok){string installed=root+"/"+campaign.id;ok=SDL_RenamePath(staged.c_str(),installed.c_str());if(ok){staged=installed;ok=ZCampaign_Load(installed+"/campaign.ini",campaign,error);}for(const auto &mission:campaign.missions){if(!ok)break;string path;ZMap map;ok=ZCampaign_SafeAsset(campaign.root,mission.map,path)&&map.Read(path.c_str());if(ok){auto issues=ZContent_ValidateMap(map);ok=!ZContent_HasErrors(issues);if(!ok)error="Imported mission failed validation";else ++maps;}else error="Imported mission could not load";}if(ok){auto issues=ZContent_ValidateCampaign(campaign,true);ok=!ZContent_HasErrors(issues);if(!ok)error="Isolated pack has missing or invalid assets";}}
 report=ok?"Clean import passed: "+to_string(maps)+" maps reloaded from an isolated profile; "+to_string(campaign.requires.size())+" declared dependencies":"Clean import failed: "+error;removeTree(root);return ok;
}
bool ZContent_WriteBundle(const vector<pair<string,vector<unsigned char>>> &files,const string &path,string &error){
 try{vector<unsigned char> data,central;unsigned count=0;for(auto &file:files){auto &name=file.first;auto &bytes=file.second;if(!safe(name)||bytes.size()>128*1024*1024||data.size()+bytes.size()>256*1024*1024)throw string("Invalid or oversized diagnostic attachment");unsigned offset=data.size(),crc=crc32(0,bytes.data(),bytes.size());put(data,0x04034b50,4);put(data,20,2);put(data,0x800,2);put(data,0,2);put(data,0,2);put(data,33,2);put(data,crc,4);put(data,bytes.size(),4);put(data,bytes.size(),4);put(data,name.size(),2);put(data,0,2);data.insert(data.end(),name.begin(),name.end());data.insert(data.end(),bytes.begin(),bytes.end());put(central,0x02014b50,4);put(central,20,2);put(central,20,2);put(central,0x800,2);put(central,0,2);put(central,0,2);put(central,33,2);put(central,crc,4);put(central,bytes.size(),4);put(central,bytes.size(),4);put(central,name.size(),2);for(int i=0;i<4;++i)put(central,0,2);put(central,0,4);put(central,offset,4);central.insert(central.end(),name.begin(),name.end());++count;}unsigned offset=data.size();data.insert(data.end(),central.begin(),central.end());put(data,0x06054b50,4);put(data,0,2);put(data,0,2);put(data,count,2);put(data,count,2);put(data,central.size(),4);put(data,offset,4);put(data,0,2);ofstream out(path+".tmp",ios::binary);out.write((char*)data.data(),data.size());out.close();if(!out||!SDL_RenamePath((path+".tmp").c_str(),path.c_str()))throw string("Could not save diagnostic bundle");return true;}catch(const string &message){error=message;SDL_RemovePath((path+".tmp").c_str());return false;}
}
