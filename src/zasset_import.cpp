#include "zperf.h"
#include "zbuild.h"
#include "zasset_import.h"
#include "zsdl.h"
#include "zmap.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>

static bool file_exists(const std::string &path) {
    SDL_PathInfo info; return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}
std::string ZAsset_FindHelper(const std::string &base) {
#ifdef _WIN32
    const std::string suffix=".exe";
#else
    const std::string suffix="";
#endif
    for(const std::string &dir : {base,base+"../MacOS/",base+"../bin/"}) {
        const std::string file=dir+"zod-asset-importer"+suffix;
        if(file_exists(file)) return file;
    }
    return "";
}
std::string ZAsset_Profile() {
    const char *custom=SDL_getenv("ZOD_PROFILE_DIR");
    if(custom && *custom) {std::string path=custom;if(path.back()!='/' && path.back()!='\\')path+='/';return path;}
    char *path=SDL_GetPrefPath("ZodEngine", "Zod");
    std::string result=path?path:""; SDL_free(path); return result;
}
std::string ZAsset_OverlayPath(const std::string &relative) {
    if(relative.find("..")!=std::string::npos)return "";
    if(relative.compare(0,7,"assets/")==0||relative.compare(0,5,"maps/")==0){std::string local=ZAsset_Profile()+"content/"+relative;if(file_exists(local))return local;}
    std::string root=[]() {
        const std::string profile=ZAsset_Profile(); if(profile.empty()) return std::string();
        std::ifstream marker(profile+"import-active.txt"); std::string id; std::getline(marker,id);
        if(id.empty() || id.size()>96 || !std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';})) return std::string();
        std::string base=profile+"imports/"+id+"/assets/";
        return file_exists(base+"original-z/manifest.json")?base:std::string();
    }();
    if(root.empty() || relative.compare(0,7,"assets/") || relative.find("..")!=std::string::npos) return "";
    const std::string path=root+relative.substr(7);
    return file_exists(path)?path:"";
}
std::vector<ZAssetCheck> ZAsset_CheckContent() {
    const std::vector<std::pair<std::string,std::vector<std::string>>> groups={
        {"Original levels",{"maps/p02_bb_orig01.map"}},
        {"Terrain",{"assets/planets/desert.bmp","assets/planets/desert.tileinfo"}},
        {"Buildings and units",{"assets/buildings/level_1.bmp","assets/units/robots/stand_red_r000.png","assets/teams/blue_palette.bmp"}},
        {"Interface",{"assets/other/main_menu_gui/list/list_button_down_normal.png","assets/other/map_items/map_object0.png","assets/other/production_gui/base_image.png","assets/other/menus/create_button.png","assets/summary_won.png","assets/frontend/disc/main_menu.png"}},
        {"Music",{"assets/sounds/AD1.MID"}},
        {"Sound effects",{"assets/sounds/MACHGUN2.wav","assets/sounds/RICOCH1.wav"}}
    };
    std::vector<ZAssetCheck> result;
    for(const auto &group:groups){ZAssetCheck check;check.category=group.first;check.total=group.second.size();for(const auto &file:group.second){SDL_PathInfo info;if(SDL_GetPathInfo(ZSDL_DataPath(file).c_str(),&info)&&info.type==SDL_PATHTYPE_FILE&&info.size>0)++check.present;}result.push_back(check);}
    return result;
}
bool ZAsset_RuntimeReady() {for(const auto &check:ZAsset_CheckContent())if(check.present!=check.total)return false;return true;}
namespace {std::mutex diagnosticMutex;std::vector<std::pair<Uint64,ZDiagnosticEvent>> diagnosticEvents;}
void ZAsset_RecordDiagnostic(ZDiagnosticEvent event){std::lock_guard<std::mutex> lock(diagnosticMutex);if(!diagnosticEvents.empty()&&diagnosticEvents.back().second==event)return;if(diagnosticEvents.size()==16)diagnosticEvents.erase(diagnosticEvents.begin());diagnosticEvents.push_back({SDL_GetTicks(),event});}
bool ZAsset_ExportDiagnostics(std::string &path,const std::string &phase) {
    const std::string folder=ZAsset_Profile()+"diagnostics";
    if(!SDL_CreateDirectory(folder.c_str()))return false;
    path=folder+"/report-"+std::to_string(SDL_GetTicksNS())+".txt";
    std::ofstream out(path+".tmp");
#ifndef ZOD_VERSION
#define ZOD_VERSION "development"
#endif
    out<<"zod-enhanced diagnostics v1\nEngine: "<<ZOD_VERSION<<"\nBuild: "<<ZBuild_ID()<<"\nSDL: "<<SDL_GetVersion()<<"\nPlatform: "<<SDL_GetPlatform()<<"\n";
    auto *renderer=ZVideo_GetRenderer();const char *name=renderer?SDL_GetRendererName(renderer):nullptr;
    out<<"Renderer: "<<(name?name:"not initialized")<<"\n";
    int w=0,h=0;if(renderer)SDL_GetRenderOutputSize(renderer,&w,&h);out<<"Output pixels: "<<w<<" x "<<h<<"\n";
    if(auto *window=ZVideo_GetWindow()){SDL_GetWindowSize(window,&w,&h);out<<"Window points: "<<w<<" x "<<h<<"\nDisplay scale: "<<SDL_GetWindowDisplayScale(window)<<"\nFullscreen: "<<((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)?"yes":"no")<<"\n";}
    // Only fixed status codes enter the report. Never copy raw logs, paths,
    // process arguments, SDL errors, preferences, account data or environment.
    const std::vector<std::string> phases={"idle","selecting","running","ready","error","cancelled","complete"};
    out<<"Import status: "<<(std::find(phases.begin(),phases.end(),phase)!=phases.end()?phase:"unknown")<<"\n";
    out<<"Recent import error: "<<(phase=="error"?"import_failed":"none reported")<<"\n";
    for(const auto &check:ZAsset_CheckContent())out<<check.category<<": "<<check.present<<"/"<<check.total<<" required samples present\n";
    {std::lock_guard<std::mutex> lock(diagnosticMutex);for(const auto &entry:diagnosticEvents){const char *code=entry.second==ZDiagnosticEvent::ImportFailed?"import_failed":entry.second==ZDiagnosticEvent::DeploymentFailed?"deployment_failed":"video_failed";out<<"Event at "<<entry.first<<" ms: "<<code<<"\n";}}
    out<<ZPerf_Report()<<ZPerf_ServerReport(ZAsset_Profile()+"diagnostics/server-performance.txt");
    out<<"Report excludes personal paths, credentials and raw logs.\n";out.close();
    if(!out||!SDL_RenamePath((path+".tmp").c_str(),path.c_str())){SDL_RemovePath((path+".tmp").c_str());return false;}return true;
}
ZAssetImport::ZAssetImport():phase("idle"),message("Select your original Z DOS disc: ISO or BIN/CUE.") {}
ZAssetImport::~ZAssetImport() {
    if(process) { Cancel(); SDL_WaitProcess(process,true,nullptr); SDL_DestroyProcess(process); }
}
void ZAssetImport::Choose(SDL_Window *window) {
    if(Busy())return;choosing_levels=false;
    selection=std::make_shared<Selection>();
    auto *hold=new std::shared_ptr<Selection>(selection);
    static const SDL_DialogFileFilter filters[]={{"Z disc image (ISO, CUE, BIN)","iso;cue;bin"}};
    dialog_pending=true;phase="selecting";message="Choose an ISO, or the CUE beside your BIN image.";
    SDL_ShowOpenFileDialog([](void *userdata,const char *const *files,int) {
        auto *hold=static_cast<std::shared_ptr<Selection> *>(userdata);
        { std::lock_guard<std::mutex> lock((*hold)->mutex);
          if(!files)(*hold)->error=SDL_GetError();
          else if(files[0])(*hold)->path=files[0];
          (*hold)->returned=true; }
        delete hold;
    },hold,window,filters,1,nullptr,false);
}
void ZAssetImport::Launch(bool inspect) {
    std::string base=SDL_GetBasePath()?SDL_GetBasePath():"";
#ifdef _WIN32
    const std::string suffix=".exe";
#else
    const std::string suffix="";
#endif
    std::string helper=ZAsset_FindHelper(base);
    const char *override_path=SDL_getenv("ZOD_IMPORT_HELPER");
    if(override_path && *override_path)helper=override_path;
    if(helper.empty() || !file_exists(helper)) {phase="error";message="Disc import tools are missing from this installation. Install a build that includes the asset importer.";return;}
    const size_t separator=helper.find_last_of("/\\");
    if(separator!=std::string::npos)base=helper.substr(0,separator+1);
    inspecting=inspect;cancel_requested=false;
    std::vector<std::string> args={helper,source,"--ui-state",state_file,"--cancel-file",cancel_file,"--output",job+"/assets"};
    if(inspect)args.push_back("--inspect");
    else {
        std::string sf=ZSDL_DataPath("assets/soundfonts/GeneralUser-GS.sf2");
        if(file_exists(sf) && file_exists(base+"fluidsynth"+suffix)) {args.push_back("--render-music");args.push_back("--soundfont");args.push_back(sf);}
    }
    std::vector<const char *> argv;for(const auto &arg:args)argv.push_back(arg.c_str());argv.push_back(nullptr);
    SDL_PropertiesID props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,argv.data());
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,SDL_PROCESS_STDIO_NULL);
    process=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);
    if(!process){phase="error";message=std::string("Could not start disc importer: ")+SDL_GetError();}
    else {phase="running";message=inspect?"Checking disc image...":"Importing original game media...";percent=0;}
}
void ZAssetImport::Poll() {
    if(dialog_pending && selection) {
        std::lock_guard<std::mutex> lock(selection->mutex);
        if(selection->returned) {
            dialog_pending=false;source=selection->path;
            if(!selection->error.empty()){phase="error";message=selection->error;}
            else if(source.empty()){phase="idle";message="No disc selected.";}
            else if(choosing_levels){choosing_levels=false;phase=ZAsset_ImportLevels(source,message)?"complete":"error";}
            else {
                const std::string profile=ZAsset_Profile();
                if(profile.empty()){phase="error";message="Could not locate your writable game profile.";return;}
                id="disc-"+std::to_string(SDL_GetPerformanceCounter());job=profile+"imports/"+id;
                if(!SDL_CreateDirectory(job.c_str())){phase="error";message=SDL_GetError();return;}
                state_file=job+"/progress.txt";cancel_file=job+"/cancel";Launch(true);
            }
        }
    }
    if(!process){if(phase=="error")ZAsset_RecordDiagnostic(ZDiagnosticEvent::ImportFailed);return;}
    auto read_state=[&]() {
    std::ifstream state(state_file);std::string next,amount,detail;
    if(std::getline(state,next)&&std::getline(state,amount)&&std::getline(state,detail)) {
        if(next=="running"||next=="ready"||next=="error"||next=="cancelled"||next=="complete") {
            phase=next;message=detail;percent=std::max(0,std::min(100,SDL_atoi(amount.c_str())));
        }
    }
    };read_state();
    int exit_code=0;
    if(SDL_WaitProcess(process,false,&exit_code)) {
        read_state();SDL_DestroyProcess(process);process=nullptr;if(exit_code!=0||phase=="error")ZAsset_RecordDiagnostic(ZDiagnosticEvent::ImportFailed);
        if(cancel_requested){phase="cancelled";message="Import cancelled. Your active media has not changed.";return;}
        if(exit_code==0 && !inspecting && phase=="complete") {
            const std::string marker=ZAsset_Profile()+"import-active.txt", temp=job+"/active.tmp";
            {std::ofstream file(temp,std::ios::binary);file<<id<<"\n";file.close();if(!file){phase="error";message="Could not save the imported asset selection.";return;}}
            if(!SDL_RenamePath(temp.c_str(),marker.c_str())){phase="error";message="Import finished, but the active media selection could not be saved.";}
        } else if(exit_code!=0 && phase!="error" && phase!="cancelled") {phase="error";message="The import process stopped unexpectedly. Your active media has not changed.";}
    }
}
void ZAssetImport::Start() {if(Ready()) {SDL_RemovePath(state_file.c_str());Launch(false);}}
void ZAssetImport::Cancel() {
    if(process){cancel_requested=true;std::ofstream file(cancel_file);file<<"cancel\n";message="Cancelling import...";}
}

void ZAssetImport::ChooseLevels(SDL_Window *window){
    if(Busy())return;choosing_levels=true;selection=std::make_shared<Selection>();auto *hold=new std::shared_ptr<Selection>(selection);dialog_pending=true;phase="selecting";message="Choose your existing Zod installation or converted maps folder.";
    SDL_ShowOpenFolderDialog([](void *userdata,const char *const *files,int){auto *hold=static_cast<std::shared_ptr<Selection>*>(userdata);{std::lock_guard<std::mutex> lock((*hold)->mutex);if(!files)(*hold)->error=SDL_GetError();else if(files[0])(*hold)->path=files[0];(*hold)->returned=true;}delete hold;},hold,window,nullptr,false);
}
bool ZAsset_ImportLevels(const std::string &folder,std::string &message){
    std::string source=folder;SDL_PathInfo info;if(SDL_GetPathInfo((folder+"/maps").c_str(),&info)&&info.type==SDL_PATHTYPE_DIRECTORY)source+="/maps";
    std::vector<std::string> names;
    if(!SDL_EnumerateDirectory(source.c_str(),[](void *data,const char*,const char *name){auto &names=*static_cast<std::vector<std::string>*>(data);std::string n=name;if(n.size()>4&&n.size()<100&&n.substr(n.size()-4)==".map"&&n.find("..") == std::string::npos&&std::all_of(n.begin(),n.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-'||c=='.';})&&names.size()<256)names.push_back(n);return SDL_ENUM_CONTINUE;},&names)||names.empty()){message="No converted .map levels found. Select an existing Zod installation or maps folder.";return false;}
    std::string destination=ZAsset_Profile()+"content/maps";if(!SDL_CreateDirectory(destination.c_str())){message="Could not create the imported levels directory.";return false;}
    int imported=0,kept=0;
    for(const auto &name:names){std::string from=source+"/"+name,to=destination+"/"+name,temp=to+".part";if(!SDL_GetPathInfo(from.c_str(),&info)||info.type!=SDL_PATHTYPE_FILE||info.size>16*1024*1024)continue;ZMap map;if(!map.Read(from.c_str()))continue;if(file_exists(to)){++kept;continue;}if(map.Write(temp.c_str())&&SDL_RenamePath(temp.c_str(),to.c_str()))++imported;else SDL_RemovePath(temp.c_str());}
    for(const char *planet:{"desert","volcanic","arctic","artic","jungle","city"}){std::string from=folder+"/assets/planets/"+planet+".tileinfo",dir=ZAsset_Profile()+"content/assets/planets",to=dir+"/"+planet+".tileinfo";if(file_exists(from)&&!file_exists(to)){SDL_CreateDirectory(dir.c_str());SDL_CopyFile(from.c_str(),to.c_str());}}
    message=std::to_string(imported)+" levels imported; "+std::to_string(kept)+" existing levels kept. Import disc media too, then restart.";return imported+kept>0;
}

std::string ZAsset_TerrainDirectory(int terrain){const char *names[]={"desert","volcanic","arctic","jungle","city"};if(terrain<0||terrain>4)return "assets";std::string relative=std::string("assets/planets/")+names[terrain]+".tileinfo";std::string path=ZAsset_OverlayPath(relative);if(path.empty()&&file_exists(relative))path=relative;if(path.empty())path=ZSDL_DataPath(relative);if(!file_exists(path))return "assets";return path.substr(0,path.rfind("/planets/"));}
