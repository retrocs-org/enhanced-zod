#ifndef ZMAP_WORKSPACE_H
#define ZMAP_WORKSPACE_H
#include "zmap.h"
#include "zcampaign.h"
#include "zcontent.h"
#include "zasset_import.h"
#include <memory>
#include <map>
#include <set>
class ZMapWorkspace {
public:
    explicit ZMapWorkspace(SDL_Renderer *renderer);
    ~ZMapWorkspace();
    void ReleaseGraphics();
    bool Open(const std::string &path,const std::string &saveDirectory);
    void Create(const std::string &path,const std::string &name,int terrain,int width,int height);
    void SetFiles(std::vector<std::pair<std::string,std::string>> entries) { files=std::move(entries); }
    void Event(const SDL_Event &event);
    std::string PlayPath() const {return playPath;}
    const ZCampaign &TestCampaign() const{return testCampaign;}
    int TestMission() const{return campaignMission;}
    bool StageCampaignTest();
    void FinishCampaignTest();
    void ResumeAfterTest(){FinishCampaignTest();if(!playPath.empty())SDL_RemovePath(playPath.c_str());playPath.clear();closed=false;down=painting=clicked=false;message="Returned from playtest. Unsaved edits and history preserved.";}
    void Browse() {browser=true;}
    void Welcome() {dirty=false;SDL_PathInfo info;recoveryPrompt=SDL_GetPathInfo((ZAsset_Profile()+"editor-recovery.map").c_str(),&info);message="File > New / Open / Recover autosave. Ctrl+C/V copies selected objects; Delete removes.";}
    bool Render(); // true when the workspace closes
private:
    friend struct ZFrontEndRegression;
    struct Snapshot {std::string action; std::vector<map_tile> tiles;std::vector<map_object> objects;std::vector<map_zone> zones; };
    SDL_Renderer *renderer;ZMap map;
    SDL_Texture *palette=nullptr,*terrainTexture=nullptr;
    std::map<std::string,SDL_Texture*> labels,sprites;
    std::vector<std::pair<std::string,std::string>> files;
    std::vector<Snapshot> undo,redo;
    std::string path,directory,message,pending,playPath;
    std::set<int> selectedObjects;std::vector<map_object> copiedObjects;std::vector<map_tile> copiedTiles;int copiedWidth=0,copiedHeight=0,areaX=-1,areaY=-1,areaW=0,areaH=0,anchorX=-1,anchorY=-1,terrainMode=0;
    void CopySelection();void PasteSelection(int x,int y);void ApplyTerrain(int x,int y);void ChangeProperties(int kind,int delta);
    int selectedObject=-1;map_object clipboard{};bool hasClipboard=false,validationPanel=false,importPanel=false;
    int focusButton=-1,focusCount=0,focusIndex=0;bool focusActivate=false;
    int objectScroll=0,snap=1;std::string lastObjectSearch,placementIcon;
    std::string search,importArchive;ZCampaign importSummary;std::vector<ZContentIssue> issues;int issueScroll=0;Uint64 autosaveAt=0;
    void Validate();void Autosave();void Recover();
    bool historyPanel=false;void RenderHistory();
    int tool=0,tile=0,page=0,team=1,objectKind=0,brush=1,selectedFile=0,scroll=0;
    float mx=0,my=0,pressX=0,pressY=0,zoom=1,cameraX=0,cameraY=0;
    bool panning=false,fileMenu=false,campaignPanel=false,recoveryPrompt=false,helpPanel=false,recoveryDeferred=false;
    std::string hoverTip;float hoverX=-1,hoverY=-1;Uint64 hoverSince=0;
    void ClearRecovery();
    int fileForm=0,modalLayer=0,editField=-1,newTerrain=0,newSize=1,campaignMission=0,missionScroll=0;
    std::string formName="Untitled",campaignTitle="My campaign";
    ZCampaign authorCampaign,testCampaign,lastSavedCampaign;std::vector<ZCampaign> campaignUndo,campaignRedo;
    void UndoCampaign(bool forward);void AdoptCampaign(const ZCampaign &campaign);
    ZMissionEvent eventDraft;int eventIndex=-1,eventScroll=0;bool pickEventLocation=false;
    std::string eventDelay="0",eventTarget="0",eventX="8",eventY="8",eventCount="1";
    void RenderEvents();void LoadEvent(int index);bool StoreEvent(bool append);
    bool compatibilityPanel=false,eventPanel=false,prefabPanel=false;std::string dependencyText,prefabName="Selection group";std::vector<std::string> prefabFiles;int prefabChoice=0,templateKind=0;
    bool connectionsPanel=false,terrainConfirm=false;int connectionMask=0,connectionTile=0,terrainUnresolved=0;std::map<int,int> connectionOverrides[2];std::vector<map_tile> terrainPreview;
    void RenderConnections();void SaveConnections();std::map<int,int> PaletteConnections(bool water);void ApplyTerrainPreview();
    void RenderPrefabs();void SavePrefab();void LoadPrefab();void SmoothTerrain(int x,int y);void ConnectedTerrain(int x,int y,bool water);
    bool timelinePanel=false,metadataPanel=false;int timelineIndex=-1,timelineScroll=0;ZMissionObjective objectiveDraft;std::string objectiveTarget="0",objectiveSeconds="30";
    void RenderTimeline();
    void RenderFileTools();void RenderCampaignStudio();void Field(std::string &value,int id,float x,float y,float w,int height=38);
    std::string *Editing();std::string UniqueMapPath(const std::string &name);
    void AddMission();bool SaveCampaign();
    bool testReportPanel=false;int testReportScroll=0;void RenderPlaytestReport();
    bool clicked=false,down=false,painting=false,dirty=false,refresh=true,grid=false,browser=false,closed=false;
    int zoneX=-1,zoneY=-1;
    Snapshot Capture();void Restore(const Snapshot &state);void Remember();void Undo(bool forward);
    void LoadPalette();void Fit();bool Save();void Paint();void Request(const std::string &action);void Proceed();
    void Text(const std::string &value,float x,float y,int size=20,SDL_Color colour={218,226,232,255},float maxWidth=0);
    void Rect(float x,float y,float w,float h,SDL_Color colour);
    bool Button(const std::string &value,float x,float y,float w,bool primary=false);
};
#endif
