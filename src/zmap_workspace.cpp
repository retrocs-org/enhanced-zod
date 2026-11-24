#include "zmap_workspace.h"
#include "zmenu_theme.h"
#include "zprefs.h"
#include "zasset_import.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <queue>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <climits>
#include <SDL3_image/SDL_image.h>
using namespace std;
namespace {
struct EditorObject {string name;int type,id;};
const vector<EditorObject> &editor_objects(){static vector<EditorObject> result=[](){vector<EditorObject> v={{"Fort",BUILDING_OBJECT,FORT_FRONT},{"Robot factory",BUILDING_OBJECT,ROBOT_FACTORY},{"Vehicle factory",BUILDING_OBJECT,VEHICLE_FACTORY},{"Flag",MAP_ITEM_OBJECT,FLAG_ITEM},{"Grunt",ROBOT_OBJECT,GRUNT},{"Light tank",VEHICLE_OBJECT,LIGHT}};for(int i=1;i<MAX_ROBOT_TYPES;++i)v.push_back({robot_type_string[i],ROBOT_OBJECT,i});for(int i=0;i<MAX_VEHICLE_TYPES;++i)if(i!=LIGHT)v.push_back({vehicle_type_string[i],VEHICLE_OBJECT,i});for(int i=0;i<MAX_CANNON_TYPES;++i)v.push_back({cannon_type_string[i],CANNON_OBJECT,i});return v;}();return result;}
}

namespace { const SDL_Color amber{244,164,65,255};const SDL_Color colors[]={{170,180,190,255},{255,70,70,255},{68,140,255,255},{70,220,100,255},{255,220,70,255}}; }
ZMapWorkspace::ZMapWorkspace(SDL_Renderer *r):renderer(r) {}
ZMapWorkspace::~ZMapWorkspace() {FinishCampaignTest(); if(SDL_WasInit(SDL_INIT_VIDEO))ReleaseGraphics(); }
void ZMapWorkspace::ReleaseGraphics() {
    SDL_DestroyTexture(palette);SDL_DestroyTexture(terrainTexture);palette=terrainTexture=nullptr;
    for(auto &entry:labels)SDL_DestroyTexture(entry.second);labels.clear();
    for(auto &entry:sprites)SDL_DestroyTexture(entry.second);sprites.clear();refresh=true;
}
void ZMapWorkspace::Text(const string &value,float x,float y,int size,SDL_Color color,float maxWidth) {
    float sx=1,sy=1;SDL_GetRenderScale(renderer,&sx,&sy);float raster=max(1.0f,sx);
    int pixels=(int)ceil(size*raster);
    string key=to_string(zod_text_percent)+":"+to_string(pixels)+":"+value;auto found=labels.find(key);
    if(found==labels.end()) { SDL_Surface *surface=ZMenuTheme::text(value.c_str(),pixels);if(!surface)return;SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);if(!texture)return;found=labels.emplace(key,texture).first; }
    float w,h;SDL_GetTextureSize(found->second,&w,&h);SDL_SetTextureColorMod(found->second,color.r,color.g,color.b);float fit=maxWidth>0?min(1.0f,maxWidth*raster/w):1.0f;SDL_FRect dst{x,y,w/raster*fit,h/raster*fit};SDL_RenderTexture(renderer,found->second,nullptr,&dst);
}
void ZMapWorkspace::Rect(float x,float y,float w,float h,SDL_Color color) { SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);SDL_FRect rect{x,y,w,h};SDL_RenderFillRect(renderer,&rect); }
bool ZMapWorkspace::Button(const string &value,float x,float y,float w,bool primary) {
    bool hot=mx>=x&&mx<x+w&&my>=y&&my<y+38;Rect(x,y,w,38,hot?SDL_Color{54,69,80,255}:SDL_Color{37,49,59,255});
    if(primary||hot) Rect(x,y,3,38,amber);Text(value,x+10,y+6,20,{218,226,232,255},w-20);
    if(hot&&modalLayer==0){if(value=="Save")hoverTip="Save [Ctrl/Cmd+S] / Originals always save as a copy";else if(value=="Test")hoverTip="Playtest an unsaved snapshot; return to this workspace";else if(value=="Undo")hoverTip="Undo [Ctrl/Cmd+Z]";else if(value=="Redo")hoverTip="Redo [Ctrl/Cmd+Y]";else if(value=="Fit map")hoverTip="Fit entire map [F]";else if(value=="Select / Move")hoverTip="Select [5] / Shift-click adds / Drag empty area to select";else hoverTip=value;}
    bool interactive=true;
    if(!pending.empty()&&dirty)interactive=modalLayer==2;
    else if(recoveryPrompt||helpPanel)interactive=modalLayer==3;
    else if(browser||fileMenu||fileForm||campaignPanel||validationPanel||importPanel||prefabPanel||connectionsPanel||terrainConfirm)interactive=modalLayer==1;
    if(interactive){int index=focusIndex++;if(index==focusButton){Rect(x,y,w,2,amber);Rect(x,y+36,w,2,amber);if(focusActivate){focusActivate=false;return true;}}}
    if(interactive&&clicked&&hot&&pressX>=x&&pressX<x+w&&pressY>=y&&pressY<y+38) { clicked=false;return true; }return false;
}
ZMapWorkspace::Snapshot ZMapWorkspace::Capture() { Snapshot s;auto &h=map.GetMapBasics();for(int i=0;i<h.width*h.height;++i)s.tiles.push_back(map.GetTile(i));s.objects=map.GetObjectList();s.zones=map.GetZoneList();return s; }
void ZMapWorkspace::Restore(const Snapshot &s) { selectedObject=-1;selectedObjects.clear();areaW=areaH=0; for(size_t i=0;i<s.tiles.size();++i)map.GetTile((unsigned)i)=s.tiles[i];map.GetObjectList()=s.objects;map.GetZoneList()=s.zones;refresh=dirty=true; }
void ZMapWorkspace::Remember() { if(undo.size()>=32)undo.erase(undo.begin());auto state=Capture();const char *actions[]={"terrain stroke","object placement","territory","erase","selection edit"};state.action=actions[max(0,min(4,tool))];undo.push_back(state);redo.clear();dirty=true; }
void ZMapWorkspace::Undo(bool forward) { auto &from=forward?redo:undo;auto &to=forward?undo:redo;if(from.empty())return;auto state=Capture();state.action=from.back().action;to.push_back(state);message=string(forward?"Redid ":"Undid ")+from.back().action;Restore(from.back());from.pop_back();zoneX=zoneY=-1; }
void ZMapWorkspace::LoadPalette() {
    SDL_DestroyTexture(palette);palette=nullptr;auto &h=map.GetMapBasics();connectionOverrides[0].clear();connectionOverrides[1].clear();{std::ifstream settings(ZAsset_Profile()+"terrain-connections-"+to_string(h.terrain_type)+".ini");int kind,mask,value;while(settings>>kind>>mask>>value){if(kind<0||kind>1||mask<0||mask>15||value<0||value>=MAX_PLANET_TILES)continue;auto info=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,value);if(info.is_usable&&!info.is_effect&&(kind?info.is_water:info.is_road))connectionOverrides[kind][mask]=value;}}
    SDL_Surface *surface=SDL_LoadBMP(ZSDL_DataPath("assets/planets/"+planet_type_string[h.terrain_type]+".bmp").c_str());
    if(surface) { palette=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);SDL_SetTextureScaleMode(palette,SDL_SCALEMODE_NEAREST); }
    refresh=true;
}
void ZMapWorkspace::Fit() { auto &h=map.GetMapBasics();zoom=min(944.0f/(h.width*16),540.0f/(h.height*16));cameraX=(h.width*16-944/zoom)/2;cameraY=(h.height*16-540/zoom)/2; }
bool ZMapWorkspace::Open(const string &file,const string &saveDirectory) {
    // Read into a temporary map first so a failed open preserves the current work.
    ZMap candidate;if(!candidate.Read(file.c_str())) { message="Could not open map";return false; }
    char *data=nullptr;int size=0;if(!candidate.GetMapData(data,size)||!map.Read(data,size)) { message="Could not read map data";return false; }
    templateKind=0;directory=saveDirectory;path=file;
    if(file.compare(0,directory.size()+1,directory+"/")!=0) { size_t slash=file.find_last_of("/\\");string name=file.substr(slash==string::npos?0:slash+1);path=directory+"/edited_"+name;
        SDL_PathInfo info;int suffix=2;string stem=path.substr(0,path.size()-4);
        while(SDL_GetPathInfo(path.c_str(),&info))path=stem+"_"+to_string(suffix++)+".map"; }
    selectedObject=-1;selectedObjects.clear();areaW=areaH=0;anchorX=anchorY=-1;undo.clear();redo.clear();dirty=false;zoneX=zoneY=-1;LoadPalette();Fit();message="Loaded level. Save writes to "+path;return true;
}
void ZMapWorkspace::Create(const string &file,const string &name,int terrain,int width,int height) {
    path=file;directory=file.substr(0,file.find_last_of("/\\"));SDL_PathInfo info;if(SDL_GetPathInfo(path.c_str(),&info))path=UniqueMapPath(name);map.MakeNewMap(name.substr(0,49).c_str(),(planet_type)terrain,width,height);
    if(templateKind){map.GetObjectList().clear();map.GetZoneList().clear();for(int side=0;side<2;++side){map_object fort{};fort.object_type=BUILDING_OBJECT;fort.object_id=FORT_FRONT;fort.owner=side+1;fort.x=side?width-14:8;fort.y=side?height-14:8;fort.health_percent=100;map.GetObjectList().push_back(fort);}for(int side=0;side<2;++side){map_object unit{};unit.object_type=ROBOT_OBJECT;unit.object_id=GRUNT;unit.owner=side+1;unit.x=side?width-20:16;unit.y=side?height-20:16;unit.health_percent=100;map.GetObjectList().push_back(unit);}for(int zone=0;zone<3;++zone){map_zone z{0,(unsigned short)(zone*height/3),(unsigned short)width,(unsigned short)(height/3)};map.AddZone(z);}}

    selectedObject=-1;selectedObjects.clear();areaW=areaH=0;anchorX=anchorY=-1;undo.clear();redo.clear();dirty=true;LoadPalette();Fit();message="New map: place forts for each player and add territory zones.";
}
bool ZMapWorkspace::Save() {
    string name=path.size()>directory.size()+1?path.substr(directory.size()+1):"";
    if(path.compare(0,directory.size()+1,directory+"/")!=0||name.empty()||name.find('/')!=string::npos||name.find('\\')!=string::npos||name.size()<4||name.substr(name.size()-4)!=".map") {message="Original levels are read-only. Use File > Save As to save a new copy.";return false;}
    SDL_PathInfo info;string safe;
    if(SDL_GetPathInfo(path.c_str(),&info)&&!ZCampaign_SafeAsset(directory,name,safe)){message="Cannot overwrite a linked file outside the map folder.";return false;}
    string temp=path+".tmp";
    if(!map.Write(temp.c_str())||!SDL_RenamePath(temp.c_str(),path.c_str())) { message="Save failed. Check the map folder permissions.";remove(temp.c_str());return false; }
    dirty=false;if(pending!="@recover")ClearRecovery();bool listed=false;for(const auto &file:files)listed|=file.second==path;if(!listed)files.push_back({string(map.GetMapBasics().map_name),path});message="Saved "+path;return true;
}
void ZMapWorkspace::Request(const string &action) { pending=action;if(!dirty)Proceed(); }
void ZMapWorkspace::Proceed() { string next=pending;pending.clear();if(next=="@close"){ClearRecovery();closed=true;}else if(next=="@recover")Recover();else if(next=="@new"){const int widths[]={64,80,128},heights[]={80,100,128};Create(UniqueMapPath(formName),formName,newTerrain,widths[newSize],heights[newSize]);fileForm=0;}else if(!next.empty()){Open(next,directory);browser=false;campaignPanel=false;fileMenu=false;fileForm=0;} }
void ZMapWorkspace::Paint() {
    if(mx<306||mx>=1250||my<162||my>=702||browser||fileMenu||fileForm||campaignPanel||validationPanel||importPanel||prefabPanel||connectionsPanel||terrainConfirm||recoveryPrompt||helpPanel||!pending.empty())return;
    auto &h=map.GetMapBasics();int x=(int)floor(((mx-306)/zoom+cameraX)/16),y=(int)floor(((my-162)/zoom+cameraY)/16);
    if(x<0||y<0||x>=h.width||y>=h.height)return;
    if(tool==1){x=x/snap*snap;y=y/snap*snap;}
    if(tool==4){
        auto &objects=map.GetObjectList();int found=-1;
        for(int i=(int)objects.size()-1;i>=0;--i)if(abs((int)objects[i].x-x)<3&&abs((int)objects[i].y-y)<3){found=i;break;}
        bool additive=(SDL_GetModState()&SDL_KMOD_SHIFT)!=0;if(!additive&&(found<0||!selectedObjects.count(found)))selectedObjects.clear();
        selectedObject=found;areaW=areaH=0;if(found>=0){if(additive&&selectedObjects.count(found))selectedObjects.erase(found);else selectedObjects.insert(found);anchorX=anchorY=-1;}else {anchorX=x;anchorY=y;areaX=x;areaY=y;}
        message=found<0?"Drag a rectangle to select terrain and objects":"Object index "+to_string(found)+" / Shift-click adds selection";return;
    }
    if(!painting) { if(tool!=0||terrainMode<4)Remember();painting=true; }
    if(tool==0)ApplyTerrain(x,y);
    else if(tool==1) {
        auto &objects=map.GetObjectList();for(auto &object:objects)if(object.x==x&&object.y==y)return;
        const auto &entry=editor_objects()[objectKind];
        map_object object{};object.x=x;object.y=y;object.owner=team;object.object_type=entry.type;object.object_id=entry.id;object.health_percent=100;objects.push_back(object);
    } else if(tool==2) {
        if(zoneX<0) {zoneX=x;zoneY=y;message="Click the opposite corner of the territory.";}
        else { map_zone zone{(unsigned short)min(x,zoneX),(unsigned short)min(y,zoneY),(unsigned short)(abs(x-zoneX)+1),(unsigned short)(abs(y-zoneY)+1)};map.AddZone(zone);zoneX=zoneY=-1; }
    } else if(tool==3) {
        auto &objects=map.GetObjectList();objects.erase(remove_if(objects.begin(),objects.end(),[&](const map_object &o){return abs((int)o.x-x)<2&&abs((int)o.y-y)<2;}),objects.end());
        auto &zones=map.GetZoneList();zones.erase(remove_if(zones.begin(),zones.end(),[&](const map_zone &z){return x>=z.x&&y>=z.y&&x<z.x+z.w&&y<z.y+z.h;}),zones.end());
    }
}
void ZMapWorkspace::Event(const SDL_Event &event) {
    if(pickEventLocation){if(event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE){pickEventLocation=false;campaignPanel=true;return;}if(event.type==SDL_EVENT_MOUSE_MOTION){mx=event.motion.x;my=event.motion.y;}if(event.type==SDL_EVENT_MOUSE_BUTTON_UP&&event.button.button==SDL_BUTTON_LEFT){int x=(int)(((event.button.x-306)/zoom+cameraX)/16),y=(int)(((event.button.y-162)/zoom+cameraY)/16);auto &h=map.GetMapBasics();if(event.button.x>=306&&event.button.x<1250&&event.button.y>=162&&event.button.y<702&&x>=0&&y>=0&&x<h.width&&y<h.height){eventX=to_string(x);eventY=to_string(y);pickEventLocation=false;campaignPanel=true;}}return;}
    if(terrainConfirm||connectionsPanel){if(event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE){terrainConfirm=connectionsPanel=false;terrainPreview.clear();return;}if(terrainConfirm&&event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_RETURN){ApplyTerrainPreview();return;}if(event.type==SDL_EVENT_MOUSE_MOTION){mx=event.motion.x;my=event.motion.y;}if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){mx=pressX=event.button.x;my=pressY=event.button.y;}if(event.type==SDL_EVENT_MOUSE_BUTTON_UP){mx=event.button.x;my=event.button.y;clicked=true;}return;}
    if(event.type==SDL_EVENT_KEY_DOWN&&editField<0&&event.key.key==SDLK_TAB){focusButton=(focusButton+1)%max(1,focusCount);return;}
    if(event.type==SDL_EVENT_KEY_DOWN&&editField<0&&event.key.key==SDLK_RETURN&&focusButton>=0){focusActivate=true;return;}
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){panning=down=painting=false;autosaveAt=0;Autosave();}
    if(recoveryPrompt||helpPanel){if(event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE){if(recoveryPrompt)recoveryDeferred=true;recoveryPrompt=helpPanel=false;return;}if(event.type==SDL_EVENT_MOUSE_MOTION){mx=event.motion.x;my=event.motion.y;}if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){mx=pressX=event.button.x;my=pressY=event.button.y;}if(event.type==SDL_EVENT_MOUSE_BUTTON_UP){mx=event.button.x;my=event.button.y;clicked=true;}return;}
    if(event.type==SDL_EVENT_MOUSE_MOTION) {hoverSince=SDL_GetTicks(); if(panning){cameraX-=(event.motion.x-mx)/zoom;cameraY-=(event.motion.y-my)/zoom;}mx=event.motion.x;my=event.motion.y;if(down&&tool==0&&(terrainMode==0||terrainMode==3))Paint();
        if(down&&tool==4&&selectedObject>=0&&mx>=306&&mx<1250&&my>=162&&my<702&&!browser&&!fileMenu&&!campaignPanel&&!validationPanel&&!importPanel&&pending.empty()){
            int x=(int)(((mx-306)/zoom+cameraX)/16),y=(int)(((my-162)/zoom+cameraY)/16);auto &h=map.GetMapBasics();auto &o=map.GetObjectList()[selectedObject];
            x=x/snap*snap;y=y/snap*snap;int dx=x-o.x,dy=y-o.y;bool valid=true;if(selectedObjects.empty())selectedObjects.insert(selectedObject);for(int index:selectedObjects){auto &item=map.GetObjectList()[index];valid&=(int)item.x+dx>=0&&(int)item.y+dy>=0&&item.x+dx<h.width&&item.y+dy<h.height;}
            if(valid&&(dx||dy)){if(!painting){Remember();painting=true;}for(int index:selectedObjects){auto &item=map.GetObjectList()[index];item.x+=dx;item.y+=dy;}}
        } }
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_MIDDLE){panning=true;mx=event.button.x;my=event.button.y;}
    if(event.type==SDL_EVENT_MOUSE_BUTTON_UP&&event.button.button==SDL_BUTTON_MIDDLE)panning=false;
    if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT){mx=pressX=event.button.x;my=pressY=event.button.y;down=true;painting=false;
        if(mx>=1118&&mx<1246&&my>=610&&my<698&&!browser&&!fileMenu&&!fileForm&&!campaignPanel&&!validationPanel&&!importPanel){auto &h=map.GetMapBasics();cameraX=(mx-1118)/128*h.width*16-472/zoom;cameraY=(my-610)/88*h.height*16-270/zoom;down=false;}else Paint();}
    if(event.type==SDL_EVENT_MOUSE_BUTTON_UP&&event.button.button==SDL_BUTTON_LEFT){mx=event.button.x;my=event.button.y;clicked=true;
        if(down&&tool==4&&selectedObject<0&&anchorX>=0){auto &h=map.GetMapBasics();int x=max(0,min((int)h.width-1,(int)(((mx-306)/zoom+cameraX)/16))),y=max(0,min((int)h.height-1,(int)(((my-162)/zoom+cameraY)/16)));areaX=min(x,anchorX);areaY=min(y,anchorY);areaW=abs(x-anchorX)+1;areaH=abs(y-anchorY)+1;selectedObjects.clear();for(int i=0;i<(int)map.GetObjectList().size();++i){auto&o=map.GetObjectList()[i];if(o.x>=areaX&&o.y>=areaY&&o.x<areaX+areaW&&o.y<areaY+areaH)selectedObjects.insert(i);}if(!selectedObjects.empty())selectedObject=*selectedObjects.begin();anchorX=anchorY=-1;}
        down=painting=false;}
    if(event.type==SDL_EVENT_MOUSE_WHEEL) {if(fileForm||fileMenu||importPanel||prefabPanel||connectionsPanel||terrainConfirm||(campaignPanel&&(eventPanel||compatibilityPanel))){}else if(campaignPanel&&timelinePanel)timelineScroll=max(0,min(timelineScroll-(int)event.wheel.y,max(0,(int)authorCampaign.missions[campaignMission].objectives.size()-8)));else if(validationPanel)issueScroll=max(0,min(issueScroll-(int)event.wheel.y,max(0,(int)issues.size()-8)));else if(campaignPanel)missionScroll=max(0,min(missionScroll-(int)event.wheel.y,max(0,(int)authorCampaign.missions.size()-6)));else if(browser)scroll=max(0,min(scroll-(int)event.wheel.y,max(0,(int)files.size()-7)));else if(mx>=306)zoom=max(0.25f,min(4.0f,zoom*(event.wheel.y>0?1.2f:1/1.2f)));else page=max(0,min(page-(int)event.wheel.y,(MAX_PLANET_TILES-1)/96));}
    if(event.type==SDL_EVENT_TEXT_INPUT)if(auto *field=Editing()){if(field->size()+strlen(event.text.text)<4096)*field+=event.text.text;}
    if(event.type==SDL_EVENT_KEY_DOWN) {
        auto key=event.key.key;bool ctrl=(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI))!=0;
        if(key==SDLK_ESCAPE) {if(editField>=0){editField=-1;SDL_StopTextInput(SDL_GetKeyboardFocus());}else if(campaignPanel&&eventPanel)eventPanel=false;else if(campaignPanel&&compatibilityPanel)compatibilityPanel=false;else if(campaignPanel&&timelinePanel)timelinePanel=false;else if(campaignPanel&&metadataPanel)metadataPanel=false;else if(!pending.empty())pending.clear();else if(fileForm){fileForm=0;fileMenu=true;}else if(fileMenu)fileMenu=false;else if(validationPanel)validationPanel=false;else if(prefabPanel)prefabPanel=false;else if(importPanel)importPanel=false;else if(campaignPanel)campaignPanel=false;else if(browser)browser=false;else Request("@close");return;}
        if(auto *field=Editing()) {
            if(key==SDLK_BACKSPACE&&!field->empty()){size_t pos=field->size()-1;while(pos>0&&(((unsigned char)(*field)[pos]&0xc0)==0x80))--pos;field->erase(pos);}
            if(key==SDLK_RETURN){if(editField==14||editField==56)*field+='\n';else{editField=-1;SDL_StopTextInput(SDL_GetKeyboardFocus());}}
            return;
        }
        if(browser||fileMenu||fileForm||campaignPanel||validationPanel||importPanel||prefabPanel||connectionsPanel||terrainConfirm||!pending.empty())return;
        if(key==SDLK_F1){helpPanel=true;return;}
        if(!ctrl&&key>=SDLK_1&&key<=SDLK_5){tool=key-SDLK_1;zoneX=-1;}
        if(!ctrl&&key==SDLK_F)Fit();if(!ctrl&&key==SDLK_G)grid=!grid;
        if(key==SDLK_EQUALS||key==SDLK_PLUS)zoom=min(4.0f,zoom*1.2f);if(key==SDLK_MINUS)zoom=max(.25f,zoom/1.2f);
        if(tool==4&&ctrl&&key==SDLK_C)CopySelection();
        if(tool==4&&(key==SDLK_DELETE||key==SDLK_BACKSPACE)&&!selectedObjects.empty()){Remember();for(auto i=selectedObjects.rbegin();i!=selectedObjects.rend();++i)map.GetObjectList().erase(map.GetObjectList().begin()+*i);selectedObjects.clear();selectedObject=-1;}
        if(ctrl&&key==SDLK_V&&hasClipboard){int x=(int)(((mx-306)/zoom+cameraX)/16),y=(int)(((my-162)/zoom+cameraY)/16);PasteSelection(x,y);}
        if(!ctrl&&ZPrefs_KeyIs(key,ZKEY_EDITOR_UNDO))Undo(false);if(!ctrl&&ZPrefs_KeyIs(key,ZKEY_EDITOR_REDO))Undo(true);
        if(ctrl&&key==SDLK_S)Save();if(ctrl&&key==SDLK_Z)Undo(false);if(ctrl&&key==SDLK_Y)Undo(true);
        if(key==SDLK_LEFT||key==SDLK_A)cameraX-=64/zoom;
        if(key==SDLK_UP||key==SDLK_W)cameraY-=64/zoom;
        if(key==SDLK_RIGHT||key==SDLK_D)cameraX+=64/zoom;
        if(key==SDLK_DOWN||(key==SDLK_S&&!ctrl))cameraY+=64/zoom;
    }
}
bool ZMapWorkspace::Render() {
    struct FocusFrame {ZMapWorkspace *self;~FocusFrame(){self->focusCount=self->focusIndex;self->focusActivate=false;}} focusFrame{this};
    focusIndex=0;modalLayer=0;hoverTip.clear();Autosave();
    string picked;if(ZCampaign_PollPack(picked)){importArchive=picked;string error;if(ZCampaign_InspectPack(picked,importSummary,error)){importPanel=true;campaignPanel=false;editField=-1;}else message=error;}
    if(!palette)LoadPalette();
    Rect(0,0,1280,800,{14,20,26,255});Text("MAP WORKSPACE",24,22,30,amber);auto &h=map.GetMapBasics();
    Text(string(h.map_name)+(dirty?" *":""),330,28,23);Text(to_string(h.width)+" x "+to_string(h.height)+" / "+planet_type_string[h.terrain_type],850,30);
    if(Button("File",24,82,140))fileMenu=true;
    if(tool==4&&Button("Object groups",24,696,254)){prefabPanel=true;prefabFiles.clear();auto folder=ZAsset_Profile()+"prefabs";SDL_CreateDirectory(folder.c_str());int count=0;char **files=SDL_GlobDirectory(folder.c_str(),"*.prefab",0,&count);for(int i=0;files&&i<count&&i<64;++i)prefabFiles.push_back(files[i]);SDL_free(files);prefabChoice=0;}
    if(Button("Help [F1]",1040,714,210))helpPanel=true;
    if(Button("Campaign Studio",1040,24,210))campaignPanel=true;
    if(Button("Save",176,82,100,true))Save();if(Button("Undo",288,82,100))Undo(false);if(Button("Redo",400,82,100))Undo(true);
    if(Button("Fit map",512,82,120))Fit();if(Button(grid?"Grid: On":"Grid: Off",644,82,130))grid=!grid;
    if(Button("Zoom -",786,82,110))zoom=max(0.25f,zoom/1.2f);if(Button("Zoom +",908,82,110))zoom=min(4.0f,zoom*1.2f);
    if(Button("Test",1030,82,92)) {
        issues=ZContent_ValidateMap(map);if(ZContent_HasErrors(issues)){validationPanel=true;issueScroll=0;message="Resolve errors before testing. Select an issue to locate it.";}else {playPath=ZAsset_Profile()+"editor-playtest-"+to_string(SDL_GetTicksNS())+".map";if(map.Write(playPath.c_str()))closed=true;else {playPath.clear();message="Could not create temporary playtest map";}}
    }
    if(Button("Back",1134,82,116))Request("@close");
    const char *tools[]={"Terrain","Objects","Territories","Erase","Select / Move","Validate"};
    for(int i=0;i<6;++i)if(Button(tools[i],24+(i%2)*134,162+(i/2)*46,126,tool==i)){if(i==5)Validate();else{tool=i;zoneX=-1;}}
    if(tool==0) {
        Text("TILES / "+to_string(tile),24,302,18,amber);if(Button("Connections",158,290,120)){connectionsPanel=true;connectionTile=tile;}
        if(palette) for(int i=0;i<96;++i) {int index=page*96+i;if(index>=MAX_PLANET_TILES)break;float x=24+(i%8)*32,y=328+(i/8)*25;SDL_FRect src{(float)(index%20*16),(float)(index/20*16),16,16},dst{x,y,30,26};SDL_RenderTexture(renderer,palette,&src,&dst);if(index==tile){SDL_SetRenderDrawColor(renderer,244,164,65,255);SDL_RenderRect(renderer,&dst);}if(clicked&&!browser&&!fileMenu&&!fileForm&&!campaignPanel&&!connectionsPanel&&!terrainConfirm&&!prefabPanel&&pending.empty()&&mx>=x&&mx<x+32&&my>=y&&my<y+25){tile=index;clicked=false;}}
        if(Button("< Tiles",24,650,120))page=max(0,page-1);if(Button("Tiles >",158,650,120))page=min((MAX_PLANET_TILES-1)/96,page+1);
        if(Button("Brush: "+to_string(brush),24,698,120))brush=brush==1?3:1;
        const char *modes[]={"Paint","Rectangle","Fill","Smooth","Road","River"};if(Button(modes[terrainMode],158,698,120)){terrainMode=(terrainMode+1)%6;anchorX=anchorY=-1;}
    } else if(tool==1) {
        const auto &objects=editor_objects();
        Field(search,20,24,306,254);if(search!=lastObjectSearch){objectScroll=0;lastObjectSearch=search;}if(search.empty()&&editField!=20)Text("Search objects...",34,312,18,{120,137,148,255});int row=0;
        for(int i=0;i<(int)objects.size();++i){string name=objects[i].name,query=search;transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});transform(query.begin(),query.end(),query.begin(),[](unsigned char c){return std::tolower(c);});if(name.find(query)==string::npos)continue;if(row++<objectScroll)continue;if(row>objectScroll+6)continue;int displayRow=row-objectScroll-1;
            if(Button(objects[i].name,62,354+displayRow*42,216,objectKind==i))objectKind=i;
            string planet=planet_type_string[h.terrain_type],teamName=team_type_string[max(1,team)];const string images[]={"assets/buildings/fort/fort_"+planet+"_front.png","assets/buildings/robot/base_"+planet+".png","assets/buildings/vehicle/base_"+planet+".png","assets/other/flag_"+teamName+"_0.png","assets/units/robots/stand_"+teamName+"_r000.png","assets/units/vehicles/light/base_"+teamName+"_r000_n00.png"};
            string icon=i<6?images[i]:"";const auto &entry=objects[i];if(entry.type==ROBOT_OBJECT){char number[24];snprintf(number,sizeof(number),"_r%03d.png",0);icon="assets/units/robots/stand_"+teamName+number;}if(entry.type==VEHICLE_OBJECT)icon="assets/units/vehicles/"+vehicle_type_string[entry.id]+"/base_"+teamName+"_r000_n00.png";if(i==objectKind)placementIcon=icon;auto found=sprites.find(icon);if(found==sprites.end()){SDL_Surface *surface=IMG_Load(ZSDL_DataPath(icon).c_str());SDL_Texture *texture=surface?SDL_CreateTextureFromSurface(renderer,surface):nullptr;SDL_DestroySurface(surface);found=sprites.emplace(icon,texture).first;}Rect(26,358+displayRow*42,26,26,colors[max(0,min(4,team))]);if(found->second){SDL_FRect dst{26,358.0f+displayRow*42,30,30};SDL_RenderTexture(renderer,found->second,nullptr,&dst);}else Text(to_string(i+1),30,359+displayRow*42,18);
        }
        if(row>6&&Button("More objects",24,660,254))objectScroll=objectScroll+6>=row?0:objectScroll+6;
        if(Button("Snap: "+to_string(snap)+" tiles",24,704,254))snap=snap==1?2:snap==2?4:1;
        if(Button("Team: "+(team?team_type_string[team]:string("Neutral")),24,620,254))team=(team+1)%5;
    } else {Text(tool==2?"Click two map corners":tool==4?"Drag objects to move":"Click objects / territories",24,310,18);Text("Ctrl+C / Ctrl+V copy object",24,346,18);Text("Delete removes selection",24,376,18);Text("History: "+to_string(undo.size())+" undo / "+to_string(redo.size())+" redo",24,406,18);
        if(Button("Snap: "+to_string(snap)+" tiles",24,650,254))snap=snap==1?2:snap==2?4:1;
        if(tool==4&&!selectedObjects.empty()){Text(to_string(selectedObjects.size())+" selected",24,450,20,amber);if(Button("Change team",24,492,254))ChangeProperties(0,1);if(Button("Health -",24,538,120))ChangeProperties(1,-10);if(Button("Health +",158,538,120))ChangeProperties(1,10);if(Button("Level -",24,584,120))ChangeProperties(2,-1);if(Button("Level +",158,584,120))ChangeProperties(2,1);}}

    SDL_Rect clip{306,162,944,540};SDL_SetRenderClipRect(renderer,&clip);Rect(306,162,944,540,{8,13,18,255});
    if(refresh&&palette) {
        SDL_DestroyTexture(terrainTexture);terrainTexture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,h.width*16,h.height*16);
        if(terrainTexture) {SDL_Texture *previous=SDL_GetRenderTarget(renderer);SDL_SetRenderTarget(renderer,terrainTexture);SDL_SetRenderClipRect(renderer,nullptr);SDL_RenderClear(renderer);
            for(int y=0;y<h.height;++y)for(int x=0;x<h.width;++x){int index=map.GetTile((unsigned)(y*h.width+x)).tile;SDL_FRect src{(float)(index%20*16),(float)(index/20*16),16,16},dst{(float)x*16,(float)y*16,16,16};SDL_RenderTexture(renderer,palette,&src,&dst);}SDL_SetRenderTarget(renderer,previous);SDL_SetRenderClipRect(renderer,&clip);SDL_SetTextureScaleMode(terrainTexture,SDL_SCALEMODE_NEAREST);}
        refresh=false;
    }
    if(terrainTexture){SDL_FRect dst{306-cameraX*zoom,162-cameraY*zoom,h.width*16*zoom,h.height*16*zoom};SDL_RenderTexture(renderer,terrainTexture,nullptr,&dst);}
    if(grid&&zoom>=0.6f){SDL_SetRenderDrawColor(renderer,60,70,80,255);for(int x=0;x<=h.width;++x)SDL_RenderLine(renderer,306+(x*16-cameraX)*zoom,162,306+(x*16-cameraX)*zoom,702);for(int y=0;y<=h.height;++y)SDL_RenderLine(renderer,306,162+(y*16-cameraY)*zoom,1250,162+(y*16-cameraY)*zoom);}
    int zoneIndex=0;for(const auto &zone:map.GetZoneList()){SDL_FRect dst{306+(zone.x*16-cameraX)*zoom,162+(zone.y*16-cameraY)*zoom,zone.w*16*zoom,zone.h*16*zoom};SDL_SetRenderDrawColor(renderer,244,164,65,255);SDL_RenderRect(renderer,&dst);if(tool==2)Text("Zone "+to_string(zoneIndex),dst.x+4,dst.y+4,17,amber);++zoneIndex;}
    int objectIndex=0;for(const auto &object:map.GetObjectList()) {
        float x=306+(object.x*16-cameraX)*zoom,y=162+(object.y*16-cameraY)*zoom;
        if(tool==4&&(objectIndex==selectedObject||selectedObjects.count(objectIndex))){SDL_FRect selection{x-2,y-2,36*zoom+4,36*zoom+4};SDL_SetRenderDrawColor(renderer,255,220,110,255);SDL_RenderRect(renderer,&selection);selection.x-=2;selection.y-=2;selection.w+=4;selection.h+=4;SDL_RenderRect(renderer,&selection);Rect(selection.x,selection.y,5,5,amber);Rect(selection.x+selection.w-5,selection.y+selection.h-5,5,5,amber);}++objectIndex;
        string image;
        if(object.object_type==BUILDING_OBJECT) {
            string planet=planet_type_string[h.terrain_type];
            if(object.object_id==FORT_FRONT||object.object_id==FORT_BACK)image="assets/buildings/fort/fort_"+planet+(object.object_id==FORT_FRONT?"_front.png":"_back.png");
            if(object.object_id==ROBOT_FACTORY)image="assets/buildings/robot/base_"+planet+".png";
            if(object.object_id==VEHICLE_FACTORY)image="assets/buildings/vehicle/base_"+planet+".png";
        }
        string owner=team_type_string[max(1,min(4,(int)object.owner))];
        if(object.object_type==ROBOT_OBJECT)image="assets/units/robots/stand_"+owner+"_r000.png";
        if(object.object_type==VEHICLE_OBJECT&&object.object_id<MAX_VEHICLE_TYPES)image="assets/units/vehicles/"+vehicle_type_string[object.object_id]+"/base_"+owner+"_r000_n00.png";
        if(object.object_type==CANNON_OBJECT&&object.object_id<MAX_CANNON_TYPES)image="assets/units/cannons/"+cannon_type_string[object.object_id]+"/fire_"+owner+"_r000_n00.png";
        if(zod_team_markers&&object.owner>0&&object.owner<MAX_TEAM_TYPES)Text(team_type_string[object.owner].substr(0,1),x,y-18,16,amber);
        SDL_Texture *sprite=nullptr;
        if(!image.empty()) {
            auto found=sprites.find(image);
            if(found==sprites.end()) {SDL_Surface *surface=IMG_Load(ZSDL_DataPath(image).c_str());sprite=surface?SDL_CreateTextureFromSurface(renderer,surface):nullptr;SDL_DestroySurface(surface);sprites[image]=sprite;}
            else sprite=found->second;
        }
        if(sprite) {float w,h;SDL_GetTextureSize(sprite,&w,&h);SDL_SetTextureScaleMode(sprite,SDL_SCALEMODE_NEAREST);SDL_FRect dst{x,y,w*zoom,h*zoom};SDL_RenderTexture(renderer,sprite,nullptr,&dst);}
        else {
            SDL_Color color=colors[max(0,min(4,(int)object.owner))];
            bool scenery=object.object_type==ROCK_OBJECT||object.object_type==ANIMAL_OBJECT;
            if(scenery)color={150,150,140,255};
            float size=scenery?max(1.0f,4*zoom):max(4.0f,12*zoom);
            Rect(x,y,size,size,color);
        }
    }
    if(tool==4&&areaW>0&&areaH>0){SDL_FRect area{306+(areaX*16-cameraX)*zoom,162+(areaY*16-cameraY)*zoom,areaW*16*zoom,areaH*16*zoom};SDL_SetRenderDrawColor(renderer,100,210,255,255);SDL_RenderRect(renderer,&area);}
    if(mx>=306&&mx<1250&&my>=162&&my<702&&!browser&&!fileMenu&&!fileForm&&!campaignPanel&&!validationPanel&&!importPanel){int tx=(int)(((mx-306)/zoom+cameraX)/16),ty=(int)(((my-162)/zoom+cameraY)/16);if(tx>=0&&ty>=0&&tx<h.width&&ty<h.height){SDL_FRect cursor{306+(tx*16-cameraX)*zoom,162+(ty*16-cameraY)*zoom,16*zoom*(tool==0?brush:1),16*zoom*(tool==0?brush:1)};if(tool==0&&palette&&terrainMode==0){SDL_FRect src{(float)(tile%20*16),(float)(tile/20*16),16,16};SDL_SetTextureAlphaMod(palette,130);SDL_SetTextureBlendMode(palette,SDL_BLENDMODE_BLEND);for(int by=0;by<brush;++by)for(int bx=0;bx<brush;++bx){SDL_FRect dst{cursor.x+bx*16*zoom,cursor.y+by*16*zoom,16*zoom,16*zoom};SDL_RenderTexture(renderer,palette,&src,&dst);}SDL_SetTextureAlphaMod(palette,255);}SDL_SetRenderDrawColor(renderer,255,220,110,255);SDL_RenderRect(renderer,&cursor);}}
    if(tool==1&&mx>=306&&mx<1250&&my>=162&&my<702&&!browser&&!campaignPanel&&!fileMenu&&!fileForm&&!validationPanel&&!importPanel){int tx=(int)(((mx-306)/zoom+cameraX)/16)/snap*snap,ty=(int)(((my-162)/zoom+cameraY)/16)/snap*snap;SDL_FRect ghost{306+(tx*16-cameraX)*zoom,162+(ty*16-cameraY)*zoom,32*zoom,32*zoom};auto found=sprites.find(placementIcon);if(found!=sprites.end()&&found->second){float w,h;SDL_GetTextureSize(found->second,&w,&h);ghost.w=w*zoom;ghost.h=h*zoom;SDL_SetTextureAlphaMod(found->second,130);SDL_RenderTexture(renderer,found->second,nullptr,&ghost);SDL_SetTextureAlphaMod(found->second,255);}else{SDL_SetRenderDrawColor(renderer,244,164,65,180);SDL_RenderRect(renderer,&ghost);}Text(editor_objects()[objectKind].name,ghost.x,ghost.y-22,16,amber);}
    if(terrainConfirm&&palette){SDL_SetTextureAlphaMod(palette,190);for(size_t i=0;i<terrainPreview.size();++i)if(terrainPreview[i].tile!=map.GetTile(i).tile){int value=terrainPreview[i].tile;SDL_FRect src{(float)(value%20*16),(float)(value/20*16),16,16},dst{306+(i%h.width*16-cameraX)*zoom,162+(i/h.width*16-cameraY)*zoom,16*zoom,16*zoom};SDL_RenderTexture(renderer,palette,&src,&dst);}SDL_SetTextureAlphaMod(palette,255);}
    SDL_SetRenderClipRect(renderer,nullptr);
    Rect(1116,608,132,92,{8,13,18,255});if(terrainTexture){SDL_Rect miniClip{1118,610,128,88};SDL_SetRenderClipRect(renderer,&miniClip);SDL_FRect mini{1118,610,128,88};SDL_RenderTexture(renderer,terrainTexture,nullptr,&mini);SDL_FRect view{1118+cameraX/(h.width*16)*128,610+cameraY/(h.height*16)*88,944/zoom/(h.width*16)*128,540/zoom/(h.height*16)*88};SDL_SetRenderDrawColor(renderer,255,220,110,255);SDL_RenderRect(renderer,&view);SDL_SetRenderClipRect(renderer,nullptr);}
    Text(to_string(map.GetObjectList().size())+" objects / "+to_string(map.GetZoneList().size())+" territories / WASD or middle-drag pan / Wheel zoom",306,714,18);Text(message,24,764,17,amber,1230);
    if(browser) {
        modalLayer=1;
        Rect(210,174,860,500,{24,32,39,255});Text("OPEN EXISTING LEVEL",238,192,28,amber);
        for(int i=0;i<7&&i+scroll<(int)files.size();++i)if(Button(files[i+scroll].first.substr(0,68),238,244+i*46,804,selectedFile==i+scroll))selectedFile=i+scroll;
        if(Button("Load copy",238,606,170,true)&&selectedFile<(int)files.size())Request(files[selectedFile].second);
        if(Button("Cancel",872,606,170))browser=false;
    }
    RenderFileTools();if(campaignPanel)RenderCampaignStudio();
    if(!pending.empty()&&dirty) {
        modalLayer=2;
        Rect(330,276,620,220,{24,32,39,255});Text("UNSAVED CHANGES",356,300,27,amber);Text("Save your work before leaving this level?",356,348,22);
        if(Button("Save",356,424,160,true)&&Save())Proceed();if(Button("Discard",536,424,160)){dirty=false;Proceed();}if(Button("Cancel",716,424,200))pending.clear();
    }
    if(validationPanel){modalLayer=1;Rect(210,166,860,528,{24,32,39,255});Text("VALIDATION / SELECT AN ISSUE TO LOCATE",236,188,25,amber);
        if(issues.empty())Text("No issues found",236,252,24);
        for(int i=0;i<8&&i+issueScroll<(int)issues.size();++i){auto &issue=issues[i+issueScroll];if(Button((issue.error?"ERROR: ":"NOTE: ")+issue.text.substr(0,82),236,238+i*46,808)){if(issue.x>=0){cameraX=issue.x*16-472/zoom;cameraY=issue.y*16-270/zoom;}message=issue.text;validationPanel=false;}}
        if(Button("Close",890,640,154))validationPanel=false;
    }
    if(importPanel){auto requirements=ZCampaign_Compatibility(importSummary,ZCampaign_List());modalLayer=1;Rect(250,220,780,320,{24,32,39,255});Text("IMPORT CAMPAIGN PACK",278,242,28,amber);Text(importSummary.title,278,294,24);Text(to_string(importSummary.missions.size())+" missions / "+importSummary.author,278,338,21);Text(requirements.empty()?"ID: "+importSummary.id+" / existing IDs will not be replaced":requirements.front(),278,380,18);if(Button("Install pack",278,466,300,true)&&requirements.empty()){string error;if(ZCampaign_ImportPack(importArchive,error)){message="Campaign installed";importPanel=false;}else message=error;}if(Button("Cancel",700,466,300))importPanel=false;}
    if(!hoverTip.empty()&&SDL_GetTicks()-hoverSince>650&&!recoveryPrompt&&!helpPanel&&pending.empty()){Rect(306,742,944,36,{28,40,49,255});Text(hoverTip.substr(0,105),316,748,18,amber);}
    if(recoveryPrompt){modalLayer=3;Rect(230,240,820,300,{24,32,39,255});Text("RECOVER UNSAVED WORK?",260,268,28,amber);Text("An editor recovery snapshot from a previous session is available.",260,324,20);Text("Restore opens a new copy; original levels remain protected.",260,360,20);if(Button("Restore",260,444,220,true)){recoveryPrompt=false;Recover();}if(Button("Later",500,444,220)){recoveryPrompt=false;recoveryDeferred=true;message="Previous recovery kept. Restore it from File before starting new edits.";}if(Button("Discard snapshot",740,444,280)){recoveryPrompt=false;ClearRecovery();}}
    if(helpPanel){modalLayer=3;Rect(230,174,820,500,{24,32,39,255});Text("EDITOR SHORTCUTS",260,200,28,amber);const char *lines[]={"1 Terrain / 2 Objects / 3 Territories / 4 Erase / 5 Select","F: Fit map / G: Grid / +/- or mouse wheel: Zoom","WASD / arrows: Pan / Middle mouse: Drag canvas","Ctrl/Cmd+S: Save / Ctrl/Cmd+Z: Undo / Ctrl/Cmd+Y: Redo","Shift-click: Add selection / Drag empty space: Area selection","Ctrl/Cmd+C/V: Copy/paste terrain and objects / Delete: Remove","Escape: Close one panel / Original maps always save as copies"};for(int i=0;i<7;++i)Text(lines[i],260,264+i*40,20);if(Button("Close",800,612,220))helpPanel=false;}
    if(prefabPanel)RenderPrefabs();if(connectionsPanel)RenderConnections();if(terrainConfirm){modalLayer=1;Rect(306,706,944,74,{24,32,39,255});Text(terrainUnresolved?to_string(terrainUnresolved)+" tiles use fallback; configure Connections for exact variants.":"Connected stroke preview: corners and junctions matched.",318,714,18,amber);if(Button("Apply stroke",680,742,260,true))ApplyTerrainPreview();if(Button("Cancel preview",958,742,270)){terrainConfirm=false;terrainPreview.clear();}}if(pickEventLocation)Text("Click a map tile for this event. Escape cancels.",330,714,22,amber);
    clicked=false;return closed;
}

string ZMapWorkspace::UniqueMapPath(const string &name) {
    string base;for(unsigned char c:name)if(std::isalnum(c)||c=='-'||c=='_')base+=(char)c;else if(c==' ')base+='_';
    if(base.empty())base="Untitled";base=base.substr(0,80);string result=directory+"/"+base+".map";int suffix=2;SDL_PathInfo info;
    while(SDL_GetPathInfo(result.c_str(),&info))result=directory+"/"+base+"_"+to_string(suffix++)+".map";return result;
}
string *ZMapWorkspace::Editing(){
    if(editField==51)return &eventDelay;if(editField==52)return &eventTarget;if(editField==53)return &eventX;if(editField==54)return &eventY;if(editField==55)return &eventCount;if(editField==56)return &eventDraft.text;
    if(editField==60)return &prefabName;if(editField==43)return &authorCampaign.engineMin;if(editField==44)return &authorCampaign.engineMax;if(editField==45)return &dependencyText;
    if(editField==30)return &objectiveTarget;if(editField==31)return &objectiveSeconds;if(editField==32)return &objectiveDraft.text;if(editField==42)return &authorCampaign.version;if(editField==40)return &authorCampaign.description;if(editField==41)return &authorCampaign.cover;if(editField==20)return &search;if(editField==0)return &formName;if(editField==1)return authorCampaign.root.empty()?&campaignTitle:&authorCampaign.title;
    if(campaignMission<0||campaignMission>=(int)authorCampaign.missions.size())return nullptr;
    auto &mission=authorCampaign.missions[campaignMission];
    switch(editField){case 10:return &mission.title;case 11:return &mission.speaker;case 12:return &mission.video;case 13:return &mission.victoryVideo;case 14:return &mission.briefing;default:return nullptr;}
}
void ZMapWorkspace::Field(string &value,int id,float x,float y,float w,int height){
    Rect(x,y,w,height,{12,21,28,255});if(editField==id)Rect(x,y,3,height,amber);
    if(height>50){vector<string> lines;std::istringstream stream(value+(editField==id?"|":""));string line;while(std::getline(stream,line))lines.push_back(line);int rows=max(1,(height-12)/24),start=max(0,(int)lines.size()-rows);for(int row=start;row<(int)lines.size();++row)Text(lines[row].substr(0,max(1,(int)(w/10)-2)),x+10,y+6+(row-start)*24,18);}
    else {string shown=value;if(shown.size()>70)shown=shown.substr(shown.size()-70);Text(shown+(editField==id?"|":""),x+10,y+6,19);}
    if((pending.empty()||!dirty)&&(!(browser||fileMenu||fileForm||campaignPanel||validationPanel||importPanel||prefabPanel||connectionsPanel||terrainConfirm)||modalLayer==1)&&clicked&&mx>=x&&mx<x+w&&my>=y&&my<y+height){editField=id;SDL_StartTextInput(SDL_GetKeyboardFocus());clicked=false;}
}
void ZMapWorkspace::RenderFileTools(){
    if(fileMenu){modalLayer=1;Rect(24,126,280,334,{24,32,39,255});
        if(Button("New map",36,138,256)){fileMenu=false;fileForm=1;formName="Untitled";}
        if(Button("Open existing level",36,184,256)){fileMenu=false;browser=true;}
        if(Button("Save As new map",36,230,256)){fileMenu=false;fileForm=2;formName=map.GetMapBasics().map_name;}
        if(Button("Campaign Studio",36,276,256)){fileMenu=false;campaignPanel=true;}
        if(Button("Recover autosave",36,322,256)){fileMenu=false;Request("@recover");}
        if(Button("Import campaign pack",36,368,256)){fileMenu=false;ZCampaign_PickPack(SDL_GetKeyboardFocus());}
        if(Button("Close",36,414,256))fileMenu=false;
    }
    if(fileForm){modalLayer=1;Rect(300,230,680,332,{24,32,39,255});Text(fileForm==1?"NEW MAP":"SAVE AS / NEW COPY",326,252,28,amber);
        Field(formName,0,326,308,628);
        if(fileForm==1){if(Button("Terrain: "+planet_type_string[newTerrain],326,362,300))newTerrain=(newTerrain+1)%MAX_PLANET_TYPES;
            const char *sizes[]={"64 x 80","80 x 100","128 x 128"};if(Button(string("Size: ")+sizes[newSize],654,362,300))newSize=(newSize+1)%3;}
        if(fileForm==1){const char *templates[]={"Blank","Duel","Defense","Escort"};if(Button(string("Template: ")+templates[templateKind],326,416,628))templateKind=(templateKind+1)%4;}Text("Always saved to your custom-map folder.",326,466,18);
        if(Button(fileForm==1?"Create":"Save copy",326,494,260,true)&&!formName.empty()){
            editField=-1;SDL_StopTextInput(SDL_GetKeyboardFocus());
            if(fileForm==1)Request("@new");else {string previous=path;path=UniqueMapPath(formName);if(Save())fileForm=0;else path=previous;}
        }
        if(Button("Cancel",694,494,260)){fileForm=0;editField=-1;SDL_StopTextInput(SDL_GetKeyboardFocus());}
    }
}
bool ZMapWorkspace::SaveCampaign(){
    if(compatibilityPanel){authorCampaign.requires.clear();std::istringstream dependencies(dependencyText);string entry;while(std::getline(dependencies,entry,',')){entry.erase(remove_if(entry.begin(),entry.end(),[](unsigned char c){return std::isspace(c);}),entry.end());if(!entry.empty())authorCampaign.requires.push_back(entry);}}
    string error;if(!ZCampaign_Write(authorCampaign,error)){message=error;return false;}
    message="Campaign saved: "+authorCampaign.root+" / share this folder as a campaign pack";return true;
}
void ZMapWorkspace::AddMission(){
    if(authorCampaign.root.empty())return;if(authorCampaign.missions.size()>=128){message="Campaigns support up to 128 missions";return;}SDL_PathInfo info;int index=(int)authorCampaign.missions.size()+1;string file;
    do{file="mission-"+to_string(index++)+".map";}while(SDL_GetPathInfo((authorCampaign.root+"/"+file).c_str(),&info));
    if(!map.Write((authorCampaign.root+"/"+file).c_str())){message="Could not copy the current level into the campaign";return;}
    ZCampaignMission mission;mission.title=map.GetMapBasics().map_name;mission.map=file;if(templateKind==2)mission.objectives={{"defend","Hold your fort for two minutes",0,120}};if(templateKind==3)mission.objectives={{"escort","Escort the starting squad to the far territory",2,2}};mission.briefing="Capture the opposing forts to complete this mission.";authorCampaign.missions.push_back(mission);campaignMission=authorCampaign.missions.size()-1;editField=-1;SaveCampaign();
}
void ZMapWorkspace::RenderCampaignStudio(){
    modalLayer=1;
    if(eventPanel){RenderEvents();return;}
    if(timelinePanel){RenderTimeline();return;}
    if(compatibilityPanel){Rect(180,180,920,530,{24,32,39,255});Text("CAMPAIGN COMPATIBILITY",208,202,28,amber);Text("Minimum engine version",208,254,20);Field(authorCampaign.engineMin,43,208,286,864);Text("Maximum engine version (optional)",208,346,20);Field(authorCampaign.engineMax,44,208,378,864);Text("Required packs: id@minimum-version, separated by commas",208,438,20);Field(dependencyText,45,208,470,864,70);Text("Missing packs, version mismatches and cycles block installation.",208,570,18);if(Button("Save requirements",208,638,400,true)&&SaveCampaign()){compatibilityPanel=false;editField=-1;}if(Button("Back",772,638,300)){compatibilityPanel=false;editField=-1;}return;}
    if(metadataPanel){Rect(180,180,920,530,{24,32,39,255});Text("CAMPAIGN DETAILS",208,202,28,amber);Text("Description",208,254,20);Field(authorCampaign.description,40,208,286,864,108);Text("Cover image (relative path inside campaign folder)",208,412,20);Field(authorCampaign.cover,41,208,446,864);if(Button("Difficulty: "+authorCampaign.difficulty,208,510,410)){const string levels[]={"Easy","Normal","Hard","Expert"};int i=0;for(;i<4&&levels[i]!=authorCampaign.difficulty;++i);authorCampaign.difficulty=levels[(i+1)%4];}Text("Pack version",652,494,18);Field(authorCampaign.version,42,652,520,420);if(Button("Compatibility",208,574,420)){dependencyText.clear();for(auto &entry:authorCampaign.requires){if(!dependencyText.empty())dependencyText+=", ";dependencyText+=entry;}compatibilityPanel=true;}if(Button("Open project folder",652,574,420))SDL_OpenURL(("file://"+authorCampaign.root).c_str());if(Button("Save details",208,638,300,true)&&SaveCampaign()){metadataPanel=false;editField=-1;}if(Button("Back",772,638,300)){metadataPanel=false;editField=-1;}return;}
Rect(150,150,980,600,{24,32,39,255});Text("CAMPAIGN STUDIO",176,170,28,amber);Text("Test uses the current map for the selected mission.",500,184,16);
    Field(authorCampaign.root.empty()?campaignTitle:authorCampaign.title,1,176,220,928);
    if(authorCampaign.root.empty()){
        if(Button("Create campaign",176,284,300,true)){string error;if(ZCampaign_Create(campaignTitle,authorCampaign,error)){authorCampaign.author=zod_player_name;message="New campaign project created. Add the current map to begin.";}else message=error;editField=-1;}
        Text("New campaigns are separate from Original Campaign.",176,358,23);
        auto projects=ZCampaign_List();int row=0;
        for(const auto &project:projects){string marker;if(row<4&&ZCampaign_SafeAsset(project.root,".zod-authoring",marker)){if(Button("Edit project: "+project.title,176,402+row*54,928)){authorCampaign=project;campaignMission=0;editField=-1;}++row;}}
        if(!row)Text("Share the resulting campaign folder to distribute your campaign.",176,442,22);
    } else {
        if(Button("Add current map",176,278,210,true))AddMission();
        if(Button("Save",404,278,100)){editField=-1;SaveCampaign();}if(Button("Test run",514,278,110,true))StageCampaignTest();
        if(Button("Export pack",632,278,210)){string output,error;if(!SaveCampaign())return;issues=ZContent_ValidateCampaign(authorCampaign,true);if(ZContent_HasErrors(issues)){issueScroll=0;validationPanel=true;campaignPanel=false;message="Resolve validation errors before exporting";return;}if(ZCampaign_Export(authorCampaign,output,error)){message="Exported "+output;SDL_OpenURL(("file://"+ZAsset_Profile()+"campaign-exports").c_str());}else message=error;}
        if(Button("Update map",860,278,244)&&campaignMission<(int)authorCampaign.missions.size()) {
            string target;if(SaveCampaign()&&ZCampaign_SafeAsset(authorCampaign.root,authorCampaign.missions[campaignMission].map,target)){
                string temp=target+".tmp";if(map.Write(temp.c_str())&&SDL_RenamePath(temp.c_str(),target.c_str()))message="Campaign mission map updated";else message="Could not update campaign map";
            }
        }
        int count=authorCampaign.missions.size();missionScroll=max(0,min(missionScroll,max(0,count-5)));
        for(int i=0;i<5&&i+missionScroll<count;++i){int index=i+missionScroll;if(Button(to_string(index+1)+". "+authorCampaign.missions[index].title.substr(0,18),176,342+i*44,270,campaignMission==index)){campaignMission=index;editField=-1;}}
        if(count&&Button("Events",176,532,270)){eventPanel=true;LoadEvent(-1);editField=-1;}
        if(count&&Button("Objectives",176,574,142)){timelinePanel=true;timelineIndex=-1;timelineScroll=0;editField=-1;}
        if(Button("Details",330,574,116)){metadataPanel=true;editField=-1;}
        if(count&&campaignMission<count){auto &mission=authorCampaign.missions[campaignMission];
            Text("Mission title",472,332,17);Field(mission.title,10,472,354,632);
            Text("Speaker",472,396,17);Field(mission.speaker,11,472,418,632);
            Text("Before video (relative MP4 or original:E_INTRO)",472,460,17);Field(mission.video,12,472,482,632);
            Text("Victory video (optional)",472,524,17);Field(mission.victoryVideo,13,472,546,632);
            if(Button("Win -> "+(mission.nextWin==-1?string("next"):mission.nextWin==-2?string("end"):to_string(mission.nextWin+1)),176,662,130)){mission.nextWin=mission.nextWin==-2?-1:mission.nextWin+1;if(mission.nextWin>=count)mission.nextWin=-2;SaveCampaign();}
            if(Button("Loss -> "+(mission.nextLoss==-1?string("retry"):mission.nextLoss==-2?string("end"):to_string(mission.nextLoss+1)),316,662,130)){mission.nextLoss=mission.nextLoss==-2?-1:mission.nextLoss+1;if(mission.nextLoss>=count)mission.nextLoss=-2;SaveCampaign();}
            Text("Briefing text (Enter adds a line)",472,588,17);Field(mission.briefing,14,472,610,632,66);
            if(Button("Up",176,618,76)&&campaignMission>0){for(auto &m:authorCampaign.missions)for(int *branch:{&m.nextWin,&m.nextLoss}){if(*branch==campaignMission)--*branch;else if(*branch==campaignMission-1)++*branch;}swap(authorCampaign.missions[campaignMission],authorCampaign.missions[campaignMission-1]);--campaignMission;SaveCampaign();}
            if(Button("Down",264,618,92)&&campaignMission+1<count){for(auto &m:authorCampaign.missions)for(int *branch:{&m.nextWin,&m.nextLoss}){if(*branch==campaignMission)++*branch;else if(*branch==campaignMission+1)--*branch;}swap(authorCampaign.missions[campaignMission],authorCampaign.missions[campaignMission+1]);++campaignMission;SaveCampaign();}
            if(Button("Remove",368,618,78)){for(auto &m:authorCampaign.missions)for(int *branch:{&m.nextWin,&m.nextLoss}){if(*branch==campaignMission)*branch=-1;else if(*branch>campaignMission)--*branch;}authorCampaign.missions.erase(authorCampaign.missions.begin()+campaignMission);campaignMission=max(0,campaignMission-1);editField=-1;SaveCampaign();return;}
            if(Button("Portrait: "+mission.character,176,708,270)){for(int i=0;i<MAX_ROBOT_TYPES;++i)if(robot_type_string[i]==mission.character){mission.character=robot_type_string[(i+1)%MAX_ROBOT_TYPES];break;}}
        }
    }
    if(!authorCampaign.missions.empty()&&Button("Load mission map",472,694,240)){
        string file;if(ZCampaign_SafeAsset(authorCampaign.root,authorCampaign.missions[campaignMission].map,file))Request(file);
    }
    if(Button("Validate",730,694,144)){SaveCampaign();issues=ZContent_ValidateCampaign(authorCampaign,true);issueScroll=0;validationPanel=true;campaignPanel=false;}
    if(Button("Close",894,694,210)){if(!authorCampaign.root.empty())SaveCampaign();campaignPanel=false;editField=-1;SDL_StopTextInput(SDL_GetKeyboardFocus());}
}

void ZMapWorkspace::Validate(){issues=ZContent_ValidateMap(map);issueScroll=0;validationPanel=true;}
void ZMapWorkspace::ClearRecovery(){recoveryDeferred=false;autosaveAt=0;SDL_RemovePath((ZAsset_Profile()+"editor-recovery.map").c_str());SDL_RemovePath((ZAsset_Profile()+"editor-recovery.view").c_str());}
void ZMapWorkspace::Autosave(){if(recoveryPrompt||recoveryDeferred||!dirty||painting||SDL_GetTicks()<autosaveAt)return;autosaveAt=SDL_GetTicks()+30000;SDL_CreateDirectory(ZAsset_Profile().c_str());string recovery=ZAsset_Profile()+"editor-recovery.map",temp=recovery+".tmp";if(map.Write(temp.c_str())&&SDL_RenamePath(temp.c_str(),recovery.c_str())){std::ofstream view(ZAsset_Profile()+"editor-recovery.view");view<<cameraX<<" "<<cameraY<<" "<<zoom<<"\n";}}
void ZMapWorkspace::Recover(){string dir=directory;if(Open(ZAsset_Profile()+"editor-recovery.map",dir)){recoveryDeferred=false;path=UniqueMapPath("Recovered");dirty=true;std::ifstream view(ZAsset_Profile()+"editor-recovery.view");float cx,cy,z;if(view>>cx>>cy>>z&&std::isfinite(cx)&&std::isfinite(cy)&&std::isfinite(z)&&z>=.1f&&z<=4){cameraX=cx;cameraY=cy;zoom=z;}message="Recovered autosave. Save creates a new custom map.";}}

void ZMapWorkspace::RenderTimeline(){
 Rect(150,150,980,600,{24,32,39,255});Text("MISSION OBJECTIVE TIMELINE",176,172,28,amber);
 if(campaignMission<0||campaignMission>=(int)authorCampaign.missions.size()){timelinePanel=false;return;}
 auto &rules=authorCampaign.missions[campaignMission].objectives;
 Text("Ordered steps. No steps uses standard battle victory rules.",176,216,20);
 for(int row=0;row<8&&row+timelineScroll<(int)rules.size();++row){int i=row+timelineScroll;if(Button(to_string(i+1)+". "+rules[i].kind+" / "+rules[i].text.substr(0,19),176,264+row*44,330,timelineIndex==i)){timelineIndex=i;objectiveDraft=rules[i];objectiveTarget=to_string(objectiveDraft.target);objectiveSeconds=to_string(objectiveDraft.seconds);editField=-1;}}
 if(Button("Type: "+objectiveDraft.kind,538,266,558)){const string kinds[]={"message","survive","capture","defend","escort"};int i=0;for(;i<5&&kinds[i]!=objectiveDraft.kind;++i);objectiveDraft.kind=kinds[(i+1)%5];}
 Text("Target: territory or building index (starts at 0)",538,320,18);Field(objectiveTarget,30,538,350,558);
 Text(objectiveDraft.kind=="escort"?"Destination territory index":"Duration / message delay in game seconds",538,404,18);Field(objectiveSeconds,31,538,434,558);
 Text(objectiveDraft.kind=="message"?"Briefing message shown after the delay":"Briefing message shown when step starts",538,488,18);Field(objectiveDraft.text,32,538,518,558,74);
 if(Button("Add step",538,620,172,true)||Button("Update",724,620,172)){
  ZMissionObjective parsed;string line=objectiveDraft.kind+"|"+objectiveTarget+"|"+objectiveSeconds+"|"+objectiveDraft.text;
  if(!ZMission_Parse(line,parsed))message="Invalid objective. Use numeric target/time and no pipe character in text.";
  else if(mx<710){if(rules.size()<64){rules.push_back(parsed);timelineIndex=rules.size()-1;timelineScroll=max(0,timelineIndex-7);SaveCampaign();}else message="Maximum 64 objective steps";}
  else if(timelineIndex>=0&&timelineIndex<(int)rules.size()){rules[timelineIndex]=parsed;SaveCampaign();}
 }
 if(timelineIndex>=0&&timelineIndex<(int)rules.size()){
  if(Button("Up",176,642,90)&&timelineIndex>0){swap(rules[timelineIndex],rules[timelineIndex-1]);--timelineIndex;SaveCampaign();}
  if(Button("Down",278,642,100)&&timelineIndex+1<(int)rules.size()){swap(rules[timelineIndex],rules[timelineIndex+1]);++timelineIndex;SaveCampaign();}
  if(Button("Remove",390,642,116)){rules.erase(rules.begin()+timelineIndex);timelineIndex=-1;SaveCampaign();}
 }
 if(Button("Back",910,692,186)){timelinePanel=false;editField=-1;}
 Text("Capture: own zone. Defend: keep red building alive. Survive: keep units alive.",176,704,17);
}

void ZMapWorkspace::ApplyTerrain(int x,int y){
 auto &h=map.GetMapBasics();if(terrainMode==3){SmoothTerrain(x,y);return;}if(terrainMode>=4){ConnectedTerrain(x,y,terrainMode==5);return;}if(terrainMode==1){if(anchorX<0){anchorX=x;anchorY=y;message="Rectangle: click the opposite corner";return;}for(int yy=min(y,anchorY);yy<=max(y,anchorY);++yy)for(int xx=min(x,anchorX);xx<=max(x,anchorX);++xx)map.GetTile((unsigned)(yy*h.width+xx)).tile=tile;anchorX=anchorY=-1;}
 else if(terrainMode==2){int old=map.GetTile((unsigned)(y*h.width+x)).tile;if(old==tile)return;std::queue<int> pending;pending.push(y*h.width+x);map.GetTile((unsigned)(y*h.width+x)).tile=tile;while(!pending.empty()){int p=pending.front();pending.pop();int px=p%h.width,py=p/h.width;for(auto step:std::vector<std::pair<int,int>>{{1,0},{-1,0},{0,1},{0,-1}}){int nx=px+step.first,ny=py+step.second;if(nx>=0&&ny>=0&&nx<h.width&&ny<h.height){int index=ny*h.width+nx;if(map.GetTile((unsigned)index).tile==old){map.GetTile((unsigned)index).tile=tile;pending.push(index);}}}}}
 else for(int yy=y;yy<min((int)h.height,y+brush);++yy)for(int xx=x;xx<min((int)h.width,x+brush);++xx)map.GetTile((unsigned)(yy*h.width+xx)).tile=tile;
 refresh=true;
}
void ZMapWorkspace::CopySelection(){
 copiedObjects.clear();copiedTiles.clear();copiedWidth=copiedHeight=0;if(selectedObjects.empty()&&selectedObject>=0)selectedObjects.insert(selectedObject);
 int x=areaW>0?areaX:INT_MAX,y=areaH>0?areaY:INT_MAX;for(int index:selectedObjects){auto&o=map.GetObjectList()[index];x=min(x,(int)o.x);y=min(y,(int)o.y);}
 if(areaW>0&&areaH>0){copiedWidth=areaW;copiedHeight=areaH;for(int yy=0;yy<areaH;++yy)for(int xx=0;xx<areaW;++xx)copiedTiles.push_back(map.GetTile((unsigned)((areaY+yy)*map.GetMapBasics().width+areaX+xx)));}
 for(int index:selectedObjects){auto o=map.GetObjectList()[index];o.x-=x;o.y-=y;copiedObjects.push_back(o);}hasClipboard=!copiedObjects.empty()||!copiedTiles.empty();if(!copiedObjects.empty())clipboard=copiedObjects[0];message="Copied selection: "+to_string(copiedObjects.size())+" objects";
}
void ZMapWorkspace::PasteSelection(int x,int y){
 auto &h=map.GetMapBasics();if(x<0||y<0||x>=h.width||y>=h.height||x+copiedWidth>h.width||y+copiedHeight>h.height){message="Selection would leave the map";return;}for(auto&o:copiedObjects)if(x+o.x>=h.width||y+o.y>=h.height){message="Objects would leave the map";return;}
 Remember();selectedObjects.clear();for(int yy=0;yy<copiedHeight;++yy)for(int xx=0;xx<copiedWidth;++xx)map.GetTile((unsigned)((y+yy)*h.width+x+xx))=copiedTiles[yy*copiedWidth+xx];for(auto o:copiedObjects){o.x+=x;o.y+=y;selectedObjects.insert(map.GetObjectList().size());map.GetObjectList().push_back(o);}selectedObject=selectedObjects.empty()?-1:*selectedObjects.begin();tool=4;refresh=true;
}
void ZMapWorkspace::ChangeProperties(int kind,int delta){if(selectedObjects.empty())return;Remember();for(int index:selectedObjects){auto&o=map.GetObjectList()[index];if(kind==0)o.owner=(o.owner+1)%5;if(kind==1)o.health_percent=max(1,min(100,(int)o.health_percent+delta));if(kind==2&&o.object_type==BUILDING_OBJECT)o.blevel=max(0,min(5,(int)o.blevel+delta));}}

namespace {void remove_editor_tree(const string &root){int count=0;char **entries=SDL_GlobDirectory(root.c_str(),"*",0,&count);for(int i=0;entries&&i<count;++i){string file=root+"/"+entries[i];SDL_PathInfo info;if(SDL_GetPathInfo(file.c_str(),&info)&&info.type==SDL_PATHTYPE_DIRECTORY)remove_editor_tree(file);else SDL_RemovePath(file.c_str());}SDL_free(entries);SDL_RemovePath((root+"/.zod-authoring").c_str());SDL_RemovePath(root.c_str());}}
void ZMapWorkspace::FinishCampaignTest(){if(testCampaign.root.empty())return;remove_editor_tree(testCampaign.root);testCampaign={};SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(),"ZOD_EDITOR_TEST_MANIFEST");}
bool ZMapWorkspace::StageCampaignTest(){
 if(authorCampaign.missions.empty()||campaignMission<0||campaignMission>=(int)authorCampaign.missions.size()){message="Add and select a mission before testing";return false;}
 FinishCampaignTest();string error;if(!ZCampaign_Create("Editor test "+to_string(SDL_GetTicksNS()),testCampaign,error)){message=error;return false;}
 string hidden=ZCampaign_Root()+"/."+testCampaign.id;
 if(!SDL_RenamePath(testCampaign.root.c_str(),hidden.c_str())){FinishCampaignTest();message="Could not isolate test campaign";return false;}testCampaign.root=hidden;
 testCampaign.title=authorCampaign.title+" / Playtest";testCampaign.author=authorCampaign.author;testCampaign.description=authorCampaign.description;testCampaign.missions=authorCampaign.missions;
 for(size_t i=0;i<testCampaign.missions.size();++i){auto &mission=testCampaign.missions[i];string source;string name="mission-"+to_string(i)+".map";string destination=hidden+"/"+name;
  bool ok=i==(size_t)campaignMission?map.Write(destination.c_str()):ZCampaign_SafeAsset(authorCampaign.root,mission.map,source)&&SDL_CopyFile(source.c_str(),destination.c_str());if(!ok){message="Could not snapshot mission "+to_string(i+1);FinishCampaignTest();return false;}mission.map=name;
  for(string *video:{&mission.video,&mission.victoryVideo})if(!video->empty()&&video->compare(0,9,"original:")!=0){if(!ZCampaign_SafeAsset(authorCampaign.root,*video,source)){message="Missing test video: "+*video;FinishCampaignTest();return false;}string relative="video-"+to_string(i)+(video==&mission.video?"-intro.mp4":"-victory.mp4");if(!SDL_CopyFile(source.c_str(),(hidden+"/"+relative).c_str())){message="Could not snapshot test video";FinishCampaignTest();return false;}string subtitle=video->substr(0,video->find_last_of('.'))+".srt",sidecar;if(ZCampaign_SafeAsset(authorCampaign.root,subtitle,sidecar))SDL_CopyFile(sidecar.c_str(),(hidden+"/"+relative.substr(0,relative.find_last_of('.'))+".srt").c_str());*video=relative;}
 }
 if(!ZCampaign_Write(testCampaign,error)){message=error;FinishCampaignTest();return false;}issues=ZContent_ValidateCampaign(testCampaign,true);if(ZContent_HasErrors(issues)){FinishCampaignTest();issueScroll=0;validationPanel=true;campaignPanel=false;message="Resolve campaign validation errors before testing";return false;}
 SDL_SetEnvironmentVariable(SDL_GetEnvironment(),"ZOD_EDITOR_TEST_MANIFEST",(hidden+"/campaign.ini").c_str(),true);
 playPath=hidden+"/"+testCampaign.missions[campaignMission].map;closed=true;message="Campaign playtest: no records or campaign progress will be saved";return true;
}

void ZMapWorkspace::SmoothTerrain(int x,int y){auto &h=map.GetMapBasics();auto before=Capture();for(int yy=max(1,y-brush);yy<min((int)h.height-1,y+brush+1);++yy)for(int xx=max(1,x-brush);xx<min((int)h.width-1,x+brush+1);++xx){auto current=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,before.tiles[yy*h.width+xx].tile);if(current.is_water||current.is_road||!current.is_passable)continue;std::map<int,int> frequency;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int candidate=before.tiles[(yy+dy)*h.width+xx+dx].tile;auto info=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,candidate);if(info.is_passable&&!info.is_water&&!info.is_road&&!info.is_effect)++frequency[candidate];}int selected=before.tiles[yy*h.width+xx].tile,best=0;for(auto &entry:frequency)if(entry.second>best){selected=entry.first;best=entry.second;}map.GetTile(yy*h.width+xx).tile=selected;}refresh=true;message="Ground smoothed; roads and water preserved";}
void ZMapWorkspace::ConnectedTerrain(int x,int y,bool water){auto &h=map.GetMapBasics();if(anchorX<0){anchorX=x;anchorY=y;message="Click the other end of the connected stroke";return;}auto before=Capture();auto kind=[&](int value){auto info=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,value);return water?info.is_water:info.is_road;};int chosen=tile;if(!kind(chosen)){chosen=-1;for(int i=0;i<MAX_PLANET_TILES;++i){auto info=ZMap::GetMapPaletteTileInfo((planet_type)h.terrain_type,i);if(kind(i)&&info.is_usable&&!info.is_effect){chosen=i;break;}}}if(chosen<0){message="This imported palette has no usable tiles for that stroke";anchorX=anchorY=-1;return;}
 auto atlas=PaletteConnections(water);terrainUnresolved=0;std::map<int,std::map<int,int>> examples;auto mask=[&](const std::vector<map_tile> &tiles,int px,int py){int pattern=0;int dx[]={0,1,0,-1},dy[]={-1,0,1,0};for(int side=0;side<4;++side){int nx=px+dx[side],ny=py+dy[side];if(nx>=0&&ny>=0&&nx<h.width&&ny<h.height&&kind(tiles[ny*h.width+nx].tile))pattern|=1<<side;}return pattern;};for(int yy=1;yy<h.height-1;++yy)for(int xx=1;xx<h.width-1;++xx){int value=before.tiles[yy*h.width+xx].tile;if(kind(value))examples[mask(before.tiles,xx,yy)][value]++;}
 int px=anchorX,py=anchorY;for(;;){map.GetTile(py*h.width+px).tile=chosen;if(px==x&&py==y)break;if(px!=x)px+=px<x?1:-1;else py+=py<y?1:-1;}auto after=Capture();for(int yy=max(0,min(y,anchorY)-1);yy<=min((int)h.height-1,max(y,anchorY)+1);++yy)for(int xx=max(0,min(x,anchorX)-1);xx<=min((int)h.width-1,max(x,anchorX)+1);++xx){if(!kind(after.tiles[yy*h.width+xx].tile))continue;auto found=examples.find(mask(after.tiles,xx,yy));int pattern=mask(after.tiles,xx,yy);auto custom=connectionOverrides[water?1:0].find(pattern);if(custom!=connectionOverrides[water?1:0].end()){map.GetTile(yy*h.width+xx).tile=custom->second;continue;}if(found==examples.end()){auto inferred=atlas.find(pattern);if(inferred!=atlas.end())map.GetTile(yy*h.width+xx).tile=inferred->second;else ++terrainUnresolved;continue;}int best=0;for(auto &entry:found->second)if(entry.second>best){map.GetTile(yy*h.width+xx).tile=entry.first;best=entry.second;}}terrainPreview=Capture().tiles;for(size_t i=0;i<before.tiles.size();++i)map.GetTile(i)=before.tiles[i];terrainConfirm=true;anchorX=anchorY=-1;refresh=true;message="Preview ready. Apply to keep the connected stroke.";}

void ZMapWorkspace::SavePrefab(){CopySelection();if(copiedObjects.empty()||copiedObjects.size()>256){message="Select 1-256 objects to save as a group";return;}string name;for(unsigned char c:prefabName)if(std::isalnum(c)||c=='-'||c=='_')name+=c;name=name.substr(0,48);if(name.empty()){message="A group name is required";return;}auto folder=ZAsset_Profile()+"prefabs/";SDL_CreateDirectory(folder.c_str());int minx=65535,miny=65535;for(auto &object:copiedObjects){minx=min(minx,(int)object.x);miny=min(miny,(int)object.y);}string target=folder+name+".prefab";std::ofstream out(target+".tmp");out<<"ZOD_PREFAB1\n"<<copiedObjects.size()<<"\n";for(auto &o:copiedObjects)out<<(int)o.object_type<<" "<<(int)o.object_id<<" "<<o.x-minx<<" "<<o.y-miny<<" "<<(int)o.owner<<" "<<(int)o.blevel<<" "<<(int)o.health_percent<<" "<<o.extra_links<<"\n";out.close();if(out&&SDL_RenamePath((target+".tmp").c_str(),target.c_str())){message="Object group saved";if(std::find(prefabFiles.begin(),prefabFiles.end(),name+".prefab")==prefabFiles.end())prefabFiles.push_back(name+".prefab");}else message="Could not save object group";}
void ZMapWorkspace::LoadPrefab(){if(prefabChoice<0||prefabChoice>=(int)prefabFiles.size())return;string path;if(!ZCampaign_SafeAsset(ZAsset_Profile()+"prefabs",prefabFiles[prefabChoice],path)){message="Unsafe group path";return;}std::ifstream input(path);string magic;int count;std::getline(input,magic);if(magic!="ZOD_PREFAB1"||!(input>>count)||count<1||count>256){message="Invalid object group";return;}vector<map_object> objects;for(int i=0;i<count;++i){int type,id,x,y,owner,level,health,links;if(!(input>>type>>id>>x>>y>>owner>>level>>health>>links)||x<0||y<0||x>1024||y>1024||owner<0||owner>4||level<0||level>5||health<1||health>100||links<0||links>1024||type<0||type>5||id<0||id>255||(type==ROBOT_OBJECT&&id>=MAX_ROBOT_TYPES)||(type==VEHICLE_OBJECT&&id>=MAX_VEHICLE_TYPES)||(type==CANNON_OBJECT&&id>=MAX_CANNON_TYPES)){message="Invalid object in group";return;}map_object object{};object.object_type=type;object.object_id=id;object.x=x;object.y=y;object.owner=owner;object.blevel=level;object.health_percent=health;object.extra_links=links;objects.push_back(object);}copiedObjects=objects;copiedTiles.clear();copiedWidth=copiedHeight=0;hasClipboard=true;prefabPanel=false;message="Group loaded. Point at map and press Ctrl/Cmd+V to place.";}
void ZMapWorkspace::RenderPrefabs(){modalLayer=1;Rect(220,170,840,540,{24,32,39,255});Text("OBJECT GROUP LIBRARY",252,194,28,amber);Field(prefabName,60,252,246,776);for(int row=0;row<6&&!prefabFiles.empty();++row){int i=(prefabChoice/6)*6+row;if(i>=(int)prefabFiles.size())break;if(Button(prefabFiles[i],252,306+row*42,776,prefabChoice==i))prefabChoice=i;}if(Button("Save selection",252,584,240,true))SavePrefab();if(Button("Load selected",508,584,240))LoadPrefab();if(Button("Next page",764,584,264)&&!prefabFiles.empty())prefabChoice=(prefabChoice/6*6+6)%prefabFiles.size();if(Button("Close",788,650,240)){prefabPanel=false;editField=-1;}}

void ZMapWorkspace::LoadEvent(int index){eventIndex=index;auto &events=authorCampaign.missions[campaignMission].events;eventDraft=index>=0&&index<(int)events.size()?events[index]:ZMissionEvent();if(eventDraft.type<3||eventDraft.type>5)eventDraft.type=5;int limit=eventDraft.type==5?MAX_ROBOT_TYPES:eventDraft.type==4?MAX_VEHICLE_TYPES:MAX_CANNON_TYPES;if(eventDraft.id<0||eventDraft.id>=limit)eventDraft.id=0;eventDelay=to_string(eventDraft.at);eventTarget=to_string(eventDraft.target);eventX=to_string(eventDraft.x);eventY=to_string(eventDraft.y);eventCount=to_string(eventDraft.count);editField=-1;}
bool ZMapWorkspace::StoreEvent(bool append){
 string encoded=eventDraft.trigger+"|"+eventDelay+"|"+eventTarget+"|"+eventDraft.kind+"|"+to_string(eventDraft.team)+"|"+to_string(eventDraft.type)+"|"+to_string(eventDraft.id)+"|"+eventX+"|"+eventY+"|"+eventCount+"|"+eventDraft.text;ZMissionEvent parsed;
 if(!ZMission_ParseEvent(encoded,parsed)){message="Check numeric fields: seconds 0-86400, count 1-16, valid tile coordinates.";return false;}
 if(parsed.kind=="reinforce"&&(parsed.x>=map.GetMapBasics().width||parsed.y>=map.GetMapBasics().height)){message="Spawn location must be inside the mission map";return false;}
 if(parsed.trigger=="capture"&&parsed.target>=(int)map.GetZoneList().size()){message="Choose an existing territory index";return false;}
 if(parsed.trigger=="destroy"&&parsed.target>=(int)map.GetObjectList().size()){message="Choose an existing map object index";return false;}
 auto &events=authorCampaign.missions[campaignMission].events;auto before=events;
 if(append){if(events.size()>=64){message="Maximum 64 events";return false;}events.push_back(parsed);eventIndex=events.size()-1;}else if(eventIndex>=0&&eventIndex<(int)events.size())events[eventIndex]=parsed;else{message="Select an event or use Add event";return false;}
 if(!SaveCampaign()){events=before;return false;}eventDraft=parsed;return true;
}
void ZMapWorkspace::RenderEvents(){
 auto &events=authorCampaign.missions[campaignMission].events;Rect(150,150,980,600,{24,32,39,255});Text("MISSION EVENT TIMELINE",176,172,28,amber);
 int duration=60;for(auto &event:events)duration=max(duration,event.at);Rect(176,220,928,4,{90,105,115,255});for(auto &event:events)if(event.trigger=="time")Rect(176+920.0f*event.at/duration,213,4,18,amber);Text("0s",176,235,16);Text(to_string(duration)+"s",1048,235,16);
 eventScroll=max(0,min(eventScroll,max(0,(int)events.size()-7)));
 for(int row=0;row<7&&eventScroll+row<(int)events.size();++row){int index=eventScroll+row;auto &event=events[index];string label=event.trigger=="time"?to_string(event.at)+"s":event.trigger+" #"+to_string(event.target);label+=" / "+event.kind;if(Button(label,176,278+row*42,280,index==eventIndex))LoadEvent(index);}
 if(Button("Previous",176,582,132))eventScroll=max(0,eventScroll-7);if(Button("Next",318,582,138))eventScroll=min(max(0,(int)events.size()-7),eventScroll+7);
 if(Button("Trigger: "+eventDraft.trigger,480,278,300)){eventDraft.trigger=eventDraft.trigger=="time"?"capture":eventDraft.trigger=="capture"?"destroy":"time";}
 if(Button("Action: "+eventDraft.kind,798,278,306))eventDraft.kind=eventDraft.kind=="dialogue"?"reinforce":"dialogue";
 Text("Seconds from start",480,326,17);Field(eventDelay,51,480,348,180);Text("Territory / object index",680,326,17);Field(eventTarget,52,680,348,180);if(Button("Team: "+to_string(eventDraft.team),880,348,224))eventDraft.team=eventDraft.team%4+1;
 if(Button(string("Type: ")+(eventDraft.type==5?"Robot":eventDraft.type==4?"Vehicle":"Cannon"),480,400,300)){eventDraft.type=eventDraft.type==5?4:eventDraft.type==4?3:5;eventDraft.id=0;}
 string unit=eventDraft.type==5?robot_type_string[eventDraft.id]:eventDraft.type==4?vehicle_type_string[eventDraft.id]:cannon_type_string[eventDraft.id];if(Button("Unit: "+unit,798,400,306)){int count=eventDraft.type==5?MAX_ROBOT_TYPES:eventDraft.type==4?MAX_VEHICLE_TYPES:MAX_CANNON_TYPES;eventDraft.id=(eventDraft.id+1)%count;}
 Text("X tile",480,446,17);Field(eventX,53,480,468,100);Text("Y tile",600,446,17);Field(eventY,54,600,468,100);Text("Groups",720,446,17);Field(eventCount,55,720,468,100);if(Button("Pick on map",840,468,264)){pickEventLocation=true;campaignPanel=false;editField=-1;}
 Text("Dialogue / arrival message",480,516,17);Field(eventDraft.text,56,480,538,624,72);
 Text("Unit and tile fields apply to reinforcement actions.",480,614,15);if(Button("New event",176,634,280))LoadEvent(-1);if(Button("Add event",480,634,190,true))StoreEvent(true);if(Button("Apply changes",684,634,220))StoreEvent(false);if(Button("Remove",918,634,186)&&eventIndex>=0&&eventIndex<(int)events.size()){events.erase(events.begin()+eventIndex);SaveCampaign();LoadEvent(-1);}
 if(Button("Back",850,694,254)){eventPanel=false;editField=-1;}Text("Select an event to edit. Timed markers scale to the latest event.",176,704,17);
}

void ZMapWorkspace::ApplyTerrainPreview(){if(!terrainConfirm)return;Remember();for(size_t i=0;i<terrainPreview.size();++i)map.GetTile(i)=terrainPreview[i];terrainPreview.clear();terrainConfirm=false;refresh=true;message="Connected stroke applied; Undo restores the previous terrain";}
std::map<int,int> ZMapWorkspace::PaletteConnections(bool water){
 std::map<int,int> result;auto planet=(planet_type)map.GetMapBasics().terrain_type;auto *source=SDL_LoadBMP(ZSDL_DataPath("assets/planets/"+planet_type_string[planet]+".bmp").c_str());if(!source)return result;SDL_Surface *image=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(source);if(!image||image->w<320||image->h<384){SDL_DestroySurface(image);return result;}double ground[4096]{},core[4096]{};double ng=0,nc=0;
 auto colour=[&](int tile,int x,int y){auto *p=(Uint8*)image->pixels+(tile/20*16+y)*image->pitch+(tile%20*16+x)*4;return (p[0]>>4)*256+(p[1]>>4)*16+(p[2]>>4);};auto kind=[&](int value){auto info=ZMap::GetMapPaletteTileInfo(planet,value);return water?info.is_water:info.is_road;};
 for(int value=0;value<MAX_PLANET_TILES;++value){auto info=ZMap::GetMapPaletteTileInfo(planet,value);if(!info.is_usable||info.is_effect)continue;for(int y=6;y<10;++y)for(int x=6;x<10;++x){int pixel=colour(value,x,y);if(kind(value)){++core[pixel];++nc;}else if(!info.is_water&&!info.is_road){++ground[pixel];++ng;}}}
 if(ng&&nc){double divergence=0;for(int i=0;i<4096;++i){core[i]/=nc;ground[i]/=ng;divergence+=std::abs(core[i]-ground[i]);}if(divergence>.3)for(int value=0;value<MAX_PLANET_TILES;++value){auto info=ZMap::GetMapPaletteTileInfo(planet,value);if(!kind(value)||!info.is_usable||info.is_effect)continue;int mask=0;for(int side=0;side<4;++side){int connected=0;for(int p=5;p<11;++p){int x=side==1?15:side==3?0:p,y=side==0?0:side==2?15:p;int pixel=colour(value,x,y);if(core[pixel]>.0001&&core[pixel]>ground[pixel]*2)++connected;}if(connected>=4)mask|=1<<side;}if(!result.count(mask))result[mask]=value;}}
 SDL_DestroySurface(image);return result;
}
void ZMapWorkspace::SaveConnections(){auto file=ZAsset_Profile()+"terrain-connections-"+to_string(map.GetMapBasics().terrain_type)+".ini";std::ofstream out(file+".tmp");for(int kind=0;kind<2;++kind)for(auto &entry:connectionOverrides[kind])out<<kind<<" "<<entry.first<<" "<<entry.second<<"\n";out.close();if(!out||!SDL_RenamePath((file+".tmp").c_str(),file.c_str()))message="Could not save connection preferences";}
void ZMapWorkspace::RenderConnections(){modalLayer=1;Rect(220,170,840,540,{24,32,39,255});Text("TERRAIN CONNECTIONS",252,192,28,amber);bool water=terrainMode==5;Text(water?"River variants":"Road variants",252,238,20);if(Button(water?"Switch to roads":"Switch to rivers",700,226,328))terrainMode=water?4:5;
 for(int mask=0;mask<16;++mask){string label;const char *sides[]={"N","E","S","W"};for(int side=0;side<4;++side)if(mask&(1<<side))label+=sides[side];if(label.empty())label="Isolated";auto found=connectionOverrides[water?1:0].find(mask);if(found!=connectionOverrides[water?1:0].end())label+=" / "+to_string(found->second);if(Button(label,252+(mask%4)*196,286+(mask/4)*44,184,mask==connectionMask)){connectionMask=mask;auto inferred=PaletteConnections(water);auto custom=connectionOverrides[water?1:0].find(mask);if(custom!=connectionOverrides[water?1:0].end())connectionTile=custom->second;else if(inferred.count(mask))connectionTile=inferred[mask];}}
 Text("Choose a palette tile for the selected connection pattern.",252,480,18);if(palette){SDL_FRect src{(float)(connectionTile%20*16),(float)(connectionTile/20*16),16,16},dst{252,520,64,64};SDL_RenderTexture(renderer,palette,&src,&dst);}Text("Tile "+to_string(connectionTile),336,536,22);auto cycle=[&](int direction){for(int i=0;i<MAX_PLANET_TILES;++i){connectionTile=(connectionTile+direction+MAX_PLANET_TILES)%MAX_PLANET_TILES;auto info=ZMap::GetMapPaletteTileInfo((planet_type)map.GetMapBasics().terrain_type,connectionTile);if(info.is_usable&&!info.is_effect&&(water?info.is_water:info.is_road))break;}};if(Button("Previous tile",492,520,200))cycle(-1);if(Button("Next tile",708,520,200))cycle(1);
 if(Button("Assign tile",252,614,240,true)){auto info=ZMap::GetMapPaletteTileInfo((planet_type)map.GetMapBasics().terrain_type,connectionTile);if(info.is_usable&&!info.is_effect&&(water?info.is_water:info.is_road)){connectionOverrides[water?1:0][connectionMask]=connectionTile;SaveConnections();}else message="Choose a usable tile of the correct terrain kind";}if(Button("Clear override",508,614,240)){connectionOverrides[water?1:0].erase(connectionMask);SaveConnections();}if(Button("Close",788,650,240))connectionsPanel=false;
}
