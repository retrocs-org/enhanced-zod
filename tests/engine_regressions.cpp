#include "zserver.h"
#include "zgamepad.h"
#include "zperf.h"
#include "zreplay.h"
#include "zmovie.h"
#include "zcontent.h"
#include "zasset_import.h"
#include "zteam.h"
#include "zfrontend.h"
#include "zvideo.h"
#include "zplayer.h"
#include "zsavegame.h"
#include "zmpclient.h"
#include "zportal.h"
#include "zserver_bookmarks.h"
#include "gmm_multiplayer_create.h"
#include "zprefs.h"
#include "map_gen_core.h"
#include "zskirmish.h"
#include "zlan.h"
#include "gmm_multiplayer.h"
#include "gmm_main_menu.h"
#include "gmm_warning.h"
#include "gmm_save_game.h"
#include "zmenu_theme.h"
#include "gmmw_slider.h"
#include "client_socket.h"
#include <iomanip>
#include <set>
#include <array>
#include <zlib.h>
#include "packet_io.h"
#include <SDL3/SDL.h>
#include <climits>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifndef _WIN32
#include <unistd.h>
#endif

#define REQUIRE(c) do { if(!(c)) throw std::runtime_error(std::string(#c) + " at line " + std::to_string(__LINE__)); } while(0)

static std::vector<char> bytes(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(input), {});
}

extern bool zod_render_smoothing;

static std::vector<char> legacySave(std::vector<char> data,char version){
 REQUIRE(data.size()>100);auto length=[&](size_t offset){REQUIRE(offset+4<=data.size());unsigned n=0;for(int i=0;i<4;++i)n|=(unsigned char)data[offset+i]<<(i*8);return n;};size_t p=12;p+=4+length(p);p+=4+length(p);p+=36;p+=4+length(p);p+=4;size_t end=p+4+length(p)+4;REQUIRE(end<data.size()-12);data.erase(data.begin()+p,data.begin()+end);data.resize(data.size()-12);data[11]=version;if(version=='6'){unsigned crc=crc32(0,(const Bytef*)data.data(),data.size());const char magic[]="ZODCRC1!";data.insert(data.end(),magic,magic+8);for(int i=0;i<4;++i)data.push_back((crc>>(i*8))&255);}return data;
}

static void verify_editor_server(const std::string &map,const std::string &id);

// Synthetic menu artwork keeps rendering/input tests independent of game media.
struct ZFrontEndRegression {
    static void authoring_tools(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZMap::ServerInit();ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());frontend.custom_dir=root;REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto &editor=*frontend.workspace;
        editor.templateKind=3;editor.Create(root+"/escort.map","Escort",0,64,80);REQUIRE(editor.map.GetObjectList().size()==4&&editor.map.GetZoneList().size()==3);editor.selectedObjects={2};editor.selectedObject=2;editor.prefabName="squad";editor.SavePrefab();REQUIRE(editor.prefabFiles.size()==1);editor.prefabChoice=0;editor.copiedWidth=editor.copiedHeight=4;editor.LoadPrefab();REQUIRE(editor.copiedWidth==0&&editor.copiedHeight==0);REQUIRE(editor.copiedObjects.size()==1&&editor.hasClipboard);auto count=editor.map.GetObjectList().size();editor.PasteSelection(30,30);REQUIRE(editor.map.GetObjectList().size()==count+1);editor.Undo(false);REQUIRE(editor.map.GetObjectList().size()==count);
        editor.Remember();editor.map.GetTile(10*64+10).tile=20;editor.SmoothTerrain(10,10);REQUIRE(editor.map.GetTile(10*64+10).tile!=20);editor.tile=460;editor.anchorX=-1;editor.ConnectedTerrain(20,20,false);editor.ConnectedTerrain(25,24,false);REQUIRE(editor.terrainConfirm&&!ZMap::GetMapPaletteTileInfo(DESERT,editor.map.GetTile(20*64+20).tile).is_road);editor.ApplyTerrainPreview();REQUIRE(!editor.terrainConfirm);for(int x=20;x<=25;++x)REQUIRE(ZMap::GetMapPaletteTileInfo(DESERT,editor.map.GetTile(20*64+x).tile).is_road);for(int y=20;y<=24;++y)REQUIRE(ZMap::GetMapPaletteTileInfo(DESERT,editor.map.GetTile(y*64+25).tile).is_road);
        auto inferred=editor.PaletteConnections(false);REQUIRE(inferred.size()==16);REQUIRE(editor.map.GetTile(20*64+25).tile==472);editor.connectionOverrides[0][12]=467;editor.SaveConnections();editor.anchorX=-1;editor.ConnectedTerrain(40,20,false);editor.ConnectedTerrain(44,24,false);REQUIRE(editor.terrainPreview[20*64+44].tile==467);auto history=editor.undo.size();editor.terrainConfirm=false;editor.terrainPreview.clear();REQUIRE(editor.undo.size()==history);editor.LoadPalette();REQUIRE(editor.connectionOverrides[0][12]==467);editor.connectionOverrides[0].clear();editor.SaveConnections();
        auto capture=[&](const char *name){frontend.Render();SDL_SetRenderTarget(ZVideo_GetRenderer(),frontend.target);auto *image=SDL_RenderReadPixels(ZVideo_GetRenderer(),nullptr);REQUIRE(image);REQUIRE(IMG_SavePNG(image,(root+"/"+name+".png").c_str()));SDL_DestroySurface(image);SDL_SetRenderTarget(ZVideo_GetRenderer(),nullptr);};
        editor.campaignPanel=true;editor.authorCampaign.title="Tools campaign";string error;REQUIRE(ZCampaign_Create("Tools campaign",editor.authorCampaign,error));editor.AddMission();REQUIRE(editor.authorCampaign.missions[0].objectives[0].kind=="escort");capture("studio");editor.eventPanel=true;editor.LoadEvent(-1);editor.eventDraft.text="Hello commander";editor.eventDelay="5";REQUIRE(editor.StoreEvent(true));REQUIRE(editor.authorCampaign.missions[0].events.size()==1);editor.eventDelay="-1";REQUIRE(!editor.StoreEvent(false));editor.LoadEvent(0);capture("events");editor.eventPanel=false;editor.metadataPanel=true;editor.compatibilityPanel=true;capture("compatibility");editor.compatibilityPanel=false;editor.metadataPanel=false;editor.campaignPanel=false;editor.connectionsPanel=true;capture("connections");editor.connectionsPanel=false;frontend.screen=ZFrontEnd::SCR_ALERTS;capture("alerts");frontend.screen=ZFrontEnd::SCR_EDITOR;editor.prefabPanel=true;editor.Render();
        auto clone=editor.authorCampaign.missions[0];clone.uid.clear();editor.authorCampaign.missions.push_back(clone);editor.authorCampaign.missions.push_back(clone);editor.authorCampaign.missions[0].nextWin=2;REQUIRE(ZCampaign_Write(editor.authorCampaign,error));frontend.ScanCampaigns();int choice=-1;for(int i=0;i<(int)frontend.campaigns.size();++i)if(frontend.campaigns[i].id==editor.authorCampaign.id)choice=i;REQUIRE(choice>=0);frontend.SelectCampaign(choice);frontend.current_level=0;frontend.community_running=true;frontend.RecordCampaignResult(true,10,1,0,1);REQUIRE(frontend.progress_current==2);auto visible=frontend.CampaignVisibleMissions(&frontend.selected_campaign,2);REQUIRE(visible.size()==2&&visible[0]==0&&visible[1]==2);frontend.selected_campaign.missions[2].nextWin=-2;frontend.current_level=2;frontend.RecordCampaignResult(true,10,1,0,1);int current=0,unlocked=0;ZCampaign_Progress(frontend.selected_campaign.id,current,unlocked);REQUIRE(current==-2);frontend.screen=ZFrontEnd::SCR_CAMPAIGNS;frontend.campaign_pack_summary=editor.authorCampaign;frontend.campaign_pack_summary.requires={"base-campaign@1.0","voice-pack@2.0"};frontend.campaign_import_preview=true;capture("dependencies");frontend.campaign_import_preview=false;frontend.screen=ZFrontEnd::SCR_EDITOR;REQUIRE(editor.Open("maps/p02_bb_orig01.map",root)&&editor.templateKind==0);frontend.FreeGraphics();SDL_Quit();std::cout<<"authoring-tools-ok";
    }

    static ZMapWorkspace *prepare_playtest(ZFrontEnd &frontend){
        frontend.Show();REQUIRE(frontend.EnsureGraphics());frontend.custom_dir=ZAsset_Profile()+"return-test";SDL_CreateDirectory(frontend.custom_dir.c_str());REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto *workspace=frontend.workspace.get();workspace->Remember();workspace->cameraX=77;workspace->mx=workspace->pressX=1040;workspace->my=workspace->pressY=90;workspace->clicked=true;frontend.Render();ZFrontEnd::Action action;REQUIRE(frontend.PopAction(action));REQUIRE(frontend.EditorTesting());frontend.Hide();return workspace;
    }
    static void check_playtest_return(ZFrontEnd &frontend,ZMapWorkspace *saved){REQUIRE(frontend.screen==ZFrontEnd::SCR_EDITOR&&frontend.workspace.get()==saved&&saved->cameraX==77&&saved->dirty&&!frontend.EditorTesting());}
    static void start_deployment(ZFrontEnd &frontend){frontend.Show();frontend.StartLevel(0,false);}
    static bool deployment_failed(ZFrontEnd &frontend){return frontend.deployment_failed;}
    static void retry_deployment(ZFrontEnd &frontend){frontend.RetryDeployment();}
    static void enhancements(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());
        frontend.custom_dir=root+"/custom";SDL_CreateDirectory(frontend.custom_dir.c_str());REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto *identity=frontend.workspace.get();auto &e=*identity;
        string original=e.path;auto originalBytes=bytes("maps/p02_bb_orig01.map");e.Remember();e.map.GetTile(0).tile=23;e.cameraX=44;e.cameraY=55;auto history=e.undo.size();e.clicked=true;e.mx=e.pressX=1040;e.my=e.pressY=90;frontend.Render();
        REQUIRE(frontend.EditorTesting()&&frontend.workspace.get()==identity&&e.dirty);ZFrontEnd::Action action;REQUIRE(frontend.PopAction(action)&&action.type==ZFrontEnd::FE_START_MAP_FILE);REQUIRE(action.text!=original);ZMap test;REQUIRE(test.Read(action.text.c_str()));REQUIRE(test.GetTile(0).tile==23);REQUIRE(bytes("maps/p02_bb_orig01.map")==originalBytes);
        frontend.Hide();frontend.Show();REQUIRE(frontend.screen==ZFrontEnd::SCR_EDITOR&&!frontend.EditorTesting()&&frontend.workspace.get()==identity);REQUIRE(e.cameraX==44&&e.cameraY==55&&e.undo.size()==history&&e.dirty&&e.path==original);
        e.Create(root+"/custom/tools.map","Tools",DESERT,32,32);e.terrainMode=1;e.tile=10;e.ApplyTerrain(3,4);e.ApplyTerrain(5,6);REQUIRE(e.map.GetTile((unsigned)(5*32+4)).tile==10);e.terrainMode=2;e.tile=11;e.ApplyTerrain(4,5);REQUIRE(e.map.GetTile((unsigned)(6*32+5)).tile==11);
        map_object first{};first.x=3;first.y=4;first.object_type=BUILDING_OBJECT;first.object_id=ROBOT_FACTORY;first.owner=RED_TEAM;first.health_percent=100;auto second=first;second.x=5;e.map.GetObjectList()={first,second};e.selectedObjects={0,1};e.selectedObject=0;e.areaX=3;e.areaY=4;e.areaW=3;e.areaH=3;e.CopySelection();REQUIRE(e.copiedObjects.size()==2&&e.copiedTiles.size()==9);e.PasteSelection(10,10);REQUIRE(e.map.GetObjectList().size()==4&&e.map.GetObjectList()[3].x==12);e.ChangeProperties(1,-10);REQUIRE(e.map.GetObjectList()[2].health_percent==90);e.Undo(false);REQUIRE(e.map.GetObjectList()[2].health_percent==100);e.Undo(false);REQUIRE(e.map.GetObjectList().size()==2);
        ZSaveInfo saveInfo;saveInfo.map_name="one.map";saveInfo.campaign_id="records";saveInfo.mission_index=1;ZSaveWriter writer;ZSave_WriteInfo(writer,saveInfo);REQUIRE(writer.WriteFile(root+"/context.save"));ZSaveReader reader;REQUIRE(reader.ReadFile(root+"/context.save"));ZSaveInfo restored;REQUIRE(ZSave_ReadInfo(reader,restored)&&restored.campaign_id=="records"&&restored.mission_index==1);
        ZSaveWriter legacy;legacy.Blob("ZODSAVE3",8);legacy.Str("maps/p02_bb_orig01.map");legacy.Str("Original");legacy.Int(0);legacy.Int(0);legacy.Double(30);legacy.Int(2);legacy.Int(1);legacy.Int(1);legacy.Double(123);REQUIRE(legacy.WriteFile(root+"/legacy.save"));ZSaveReader old;REQUIRE(old.ReadFile(root+"/legacy.save"));REQUIRE(ZSave_ReadInfo(old,restored)&&restored.campaign_id.empty()&&restored.mission_index==-1);
        ZCampaign_RecordWin("records","0:map",120,8,2,1);ZCampaign_RecordWin("records","0:map",90,10,0,2);auto records=ZCampaign_Records("records");REQUIRE(records.size()==1&&records[0].wins==2&&records[0].bestSeconds==90&&records[0].completedAt>0&&ZCampaign_Medal(records[0])=="Gold");
        frontend.workspace.reset();frontend.screen=ZFrontEnd::SCR_MAIN;frontend.StartLevel(0,false);REQUIRE(frontend.PopAction(action));frontend.DeploymentFailed("Fixture disconnect");REQUIRE(frontend.deployment_failed);frontend.RetryDeployment();REQUIRE(frontend.Loading()&&frontend.PopAction(action)&&action.type==ZFrontEnd::FE_START_MAP);
        // Simulate renderer state left by a cinematic and then resize the window.
        auto *renderer=ZVideo_GetRenderer();SDL_SetRenderTarget(renderer,nullptr);SDL_Rect stale{0,0,320,200};SDL_SetRenderViewport(renderer,&stale);SDL_SetRenderClipRect(renderer,&stale);SDL_SetRenderScale(renderer,2,2);
        SDL_SetWindowSize(ZVideo_GetWindow(),1440,900);SDL_SyncWindow(ZVideo_GetWindow());frontend.Render();
        REQUIRE(!SDL_RenderViewportSet(renderer)&&!SDL_RenderClipEnabled(renderer));float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);REQUIRE(sx==1&&sy==1);int lw,lh;SDL_RendererLogicalPresentation presentation;SDL_GetRenderLogicalPresentation(renderer,&lw,&lh,&presentation);REQUIRE(lw==1280&&lh==800&&presentation==SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderTarget(renderer,frontend.target);REQUIRE(!SDL_RenderViewportSet(renderer)&&!SDL_RenderClipEnabled(renderer));SDL_GetRenderLogicalPresentation(renderer,&lw,&lh,&presentation);REQUIRE(presentation==SDL_LOGICAL_PRESENTATION_DISABLED);SDL_SetRenderTarget(renderer,nullptr);
        frontend.FreeGraphics();SDL_Quit();std::cout<<"enhancements-ok";
    }
    static void polish(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZFontEngine::Init();ZGuiMainMenuBase::Init();GMMWButton::Init();ZSDL_Surface::SetScreenDimensions(1280,800);
        zod_text_percent=120;zod_cursor_percent=150;zod_team_markers=zod_reduced_motion=true;ZPrefs_Save();zod_text_percent=zod_cursor_percent=100;zod_team_markers=zod_reduced_motion=false;ZPrefs_Load();REQUIRE(zod_text_percent==120&&zod_cursor_percent==150&&zod_team_markers&&zod_reduced_motion);
        GMMOptions options;for(int i=0;i<4;++i)options.KeyPress(']');options.KeyPress(13);REQUIRE(zod_text_percent==100);options.KeyPress(SDLK_DOWN);options.KeyPress(13);REQUIRE(zod_cursor_percent==200);ZPrefs_ResetCategory(5);REQUIRE(zod_cursor_percent==100&&!zod_team_markers&&!zod_reduced_motion);
        ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());frontend.custom_dir=root+"/polish-maps";SDL_CreateDirectory(frontend.custom_dir.c_str());REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto &editor=*frontend.workspace;
        editor.Remember();editor.map.GetTile(0).tile=29;editor.cameraX=65;editor.cameraY=48;editor.zoom=1.25;editor.Autosave();auto snapshot=bytes(ZAsset_Profile()+"editor-recovery.map");REQUIRE(!snapshot.empty());
        frontend.workspace.reset();REQUIRE(frontend.LaunchEditor({}));auto &recovered=*frontend.workspace;REQUIRE(recovered.recoveryPrompt);recovered.Remember();recovered.Autosave();REQUIRE(bytes(ZAsset_Profile()+"editor-recovery.map")==snapshot);recovered.mx=recovered.pressX=300;recovered.my=recovered.pressY=460;recovered.clicked=true;frontend.Render();REQUIRE(!recovered.recoveryPrompt&&recovered.dirty&&recovered.map.GetTile(0).tile==29&&recovered.cameraX==65&&recovered.zoom==1.25f);REQUIRE(recovered.path.find(frontend.custom_dir)==0);recovered.ClearRecovery();
        SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.key=SDLK_F1;recovered.Event(key);REQUIRE(recovered.helpPanel);key.key.key=SDLK_ESCAPE;recovered.Event(key);REQUIRE(!recovered.helpPanel&&!recovered.closed);key.key.key=SDLK_2;recovered.Event(key);REQUIRE(recovered.tool==1);key.key.key=SDLK_G;recovered.Event(key);REQUIRE(recovered.grid);key.key.key=SDLK_F;recovered.Event(key);REQUIRE(recovered.zoom!=1.25f);
        auto capture=[&](const string &name){frontend.Render();auto *renderer=ZVideo_GetRenderer();SDL_SetRenderTarget(renderer,frontend.target);SDL_Surface *surface=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(surface&&IMG_SavePNG(surface,(root+"/"+name+".png").c_str()));SDL_DestroySurface(surface);SDL_SetRenderTarget(renderer,nullptr);};
        recovered.helpPanel=true;capture("editor-help");recovered.helpPanel=false;
        recovered.Remember();recovered.map.GetTile(0).tile=31;recovered.historyPanel=true;auto beforeHistory=recovered.undo.size();capture("editor-history");REQUIRE(recovered.undo.size()==beforeHistory);recovered.Undo(false);REQUIRE(recovered.map.GetTile(0).tile==29);recovered.Undo(true);REQUIRE(recovered.map.GetTile(0).tile==31);recovered.historyPanel=false;frontend.workspace.reset();
        for(int slot=0;slot<=ZSAVE_USER_SLOTS;++slot){ZSaveInfo info;info.map_name="maps/p02_bb_orig01.map";info.map_title="Virgin Soldiers";info.campaign_id="original";info.mission_index=0;info.elapsed=754;info.when=1795000000;ZSaveWriter writer;ZSave_WriteInfo(writer,info);REQUIRE(writer.WriteFile(ZSave_Path(slot)));}SDL_Surface *screen=SDL_CreateSurface(1280,800,SDL_PIXELFORMAT_ARGB8888);REQUIRE(screen);ZSDL_Surface::SetMainSoftwareSurface(screen);ZMap dummy;
        auto renderMenu=[&](ZGuiMainMenuBase &menu,const string &name){auto *renderer=ZVideo_GetRenderer();SDL_FillSurfaceRect(screen,nullptr,0xff0e171e);ZMenuTheme::beginText(renderer);menu.DoRender(dummy,screen);ZMenuTheme::endText();SDL_Texture *frame=SDL_CreateTextureFromSurface(renderer,screen);REQUIRE(frame);SDL_RenderTexture(renderer,frame,nullptr,nullptr);ZMenuTheme::presentText(renderer,1280,800);SDL_Surface *image=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(image&&IMG_SavePNG(image,(root+"/"+name+".png").c_str()));SDL_DestroySurface(image);SDL_DestroyTexture(frame);};
        zod_text_percent=120;GMMSaveGame saveMenu;saveMenu.SetCenterCoords(640,400);saveMenu.SetRenderScale(2);renderMenu(saveMenu,"save-menu");saveMenu.KeyPress(SDLK_DOWN);saveMenu.KeyPress(SDLK_RETURN);REQUIRE(saveMenu.KillMe()&&saveMenu.GetGMMFlags().save_slot==2);
        GMMOptions gameOptions;gameOptions.SetCenterCoords(640,400);for(int i=0;i<4;++i)gameOptions.KeyPress(']');gameOptions.Process();gameOptions.SetRenderScale(2);renderMenu(gameOptions,"game-accessibility");gameOptions.KeyPress(']');gameOptions.KeyPress(']');gameOptions.Process();renderMenu(gameOptions,"game-alerts");
        for(auto size:{std::pair<int,int>{640,480},{800,600},{1280,800}}){ZSDL_Surface::SetScreenDimensions(size.first,size.second);for(double scale:{1.0,1.5,2.0,3.0}){GMMOptions audit;for(int category=0;category<7;++category){audit.SetRenderScale(scale);int w,h;audit.GetDimensions(w,h);REQUIRE(w*audit.GetRenderScale()<=size.first&&h*audit.GetRenderScale()<=size.second);audit.KeyPress(']');}}}
        ZSDL_Surface::SetScreenDimensions(1280,800);ZSDL_Surface::SetMainSoftwareSurface(nullptr);SDL_DestroySurface(screen);zod_text_percent=100;
        frontend.LoadSaveInfo();REQUIRE(frontend.saves.size()==6&&frontend.saves[0].campaign.find("Original Campaign / Mission 1")!=string::npos);frontend.screen=ZFrontEnd::SCR_LOAD;frontend.load_selected=0;capture("save-browser");REQUIRE(frontend.map_preview_texture);
        frontend.screen=ZFrontEnd::SCR_KEYS;frontend.settings_category=3;frontend.settings_focus=2;frontend.kb_back=true;frontend.Render();REQUIRE(frontend.screen==ZFrontEnd::SCR_SETTINGS&&frontend.settings_category==3&&frontend.settings_focus==2);
        frontend.settings_category=5;zod_text_percent=120;capture("accessibility");frontend.screen=ZFrontEnd::SCR_CAMPAIGNS;frontend.ScanCampaigns();capture("campaign-polish");
        frontend.screen=ZFrontEnd::SCR_SETTINGS;for(int category:{2,3,4}){frontend.settings_category=category;capture("settings-"+to_string(category));}
        frontend.screen=ZFrontEnd::SCR_KEYS;frontend.keys_scroll=MAX_ZKEYS-8;capture("editor-bindings");
        REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));frontend.workspace->tool=1;frontend.workspace->objectScroll=6;frontend.workspace->objectKind=8;capture("object-palette");ZPrefs_ResetCategory(5);frontend.FreeGraphics();SDL_Quit();std::cout<<"polish-ok";
    }
    static void movie(const string &path){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(640,480,false));auto cues=ZMovie_LoadSubtitles(path.substr(0,path.find_last_of('.'))+".srt");REQUIRE(cues.size()==1&&cues[0].text=="Test subtitle");
        auto *renderer=ZVideo_GetRenderer();
        for(auto size:{std::pair<int,int>{640,480},{1920,1200},{800,1000}}){
            SDL_SetWindowSize(ZVideo_GetWindow(),size.first,size.second);SDL_SyncWindow(ZVideo_GetWindow());ZVideo_SetCanvasMode(1280,800);
            SDL_Texture *canvas=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1920,1200);REQUIRE(canvas);SDL_SetRenderTarget(renderer,canvas);SDL_SetRenderScale(renderer,1.5,1.5);SDL_Rect clip{17,29,800,500};SDL_SetRenderClipRect(renderer,&clip);
            {
                ZVideoWindowScope video(renderer);REQUIRE(SDL_GetRenderTarget(renderer)==nullptr);int w,h;SDL_RendererLogicalPresentation mode;SDL_GetRenderLogicalPresentation(renderer,&w,&h,&mode);REQUIRE(mode==SDL_LOGICAL_PRESENTATION_DISABLED);REQUIRE(!SDL_RenderViewportSet(renderer)&&!SDL_RenderClipEnabled(renderer));
                int outw,outh;REQUIRE(SDL_GetRenderOutputSize(renderer,&outw,&outh));SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_SetRenderDrawColor(renderer,0,0,255,255);SDL_FRect picture{outw*.1f,outh*.1f,outw*.8f,outh*.8f};SDL_RenderFillRect(renderer,&picture);
                SDL_Surface *image=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(image);for(auto point:{std::pair<float,float>{.2f,.2f},{.8f,.8f},{.5f,.5f}}){Uint8 r,g,b,a;REQUIRE(SDL_ReadSurfacePixel(image,(int)(image->w*point.first),(int)(image->h*point.second),&r,&g,&b,&a));REQUIRE(b>240&&r<10&&g<10);}if(size.first==1920)REQUIRE(IMG_SavePNG(image,(path+".presentation.png").c_str()));SDL_DestroySurface(image);
            }
            REQUIRE(SDL_GetRenderTarget(renderer)==canvas);float sx,sy;SDL_GetRenderScale(renderer,&sx,&sy);REQUIRE(sx==1.5f&&sy==1.5f&&SDL_RenderClipEnabled(renderer));SDL_Rect restored;SDL_GetRenderClipRect(renderer,&restored);REQUIRE(restored.x==17&&restored.y==29&&restored.w==800&&restored.h==500);
            SDL_SetRenderTarget(renderer,nullptr);int w,h;SDL_RendererLogicalPresentation mode;SDL_GetRenderLogicalPresentation(renderer,&w,&h,&mode);REQUIRE(w==1280&&h==800&&mode==SDL_LOGICAL_PRESENTATION_LETTERBOX);REQUIRE(!SDL_RenderViewportSet(renderer));SDL_SetRenderTarget(renderer,canvas);
            if(size.first==1920){zod_video_volume=50;zod_video_subtitles=true;SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.key=SDLK_UP;SDL_PushEvent(&key);REQUIRE(ZMovie_Play(path)==ZMOVIE_FINISHED);REQUIRE(zod_video_volume==55&&SDL_GetRenderTarget(renderer)==canvas);SDL_GetRenderScale(renderer,&sx,&sy);REQUIRE(sx==1.5f&&sy==1.5f&&SDL_RenderClipEnabled(renderer));key.key.key=SDLK_ESCAPE;SDL_PushEvent(&key);REQUIRE(ZMovie_Play(path)==ZMOVIE_SKIPPED);REQUIRE(SDL_GetRenderTarget(renderer)==canvas);}
            SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(canvas);
        }
        SDL_Quit();std::cout<<"movie-ok";
    }
    static void display_matrix(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZFrontEnd frontend;frontend.Show();
        int index=0;
        for(auto size:{std::pair<int,int>{878,540},{1280,800},{1920,1200},{800,1000}}){
            REQUIRE(SDL_SetWindowSize(ZVideo_GetWindow(),size.first,size.second));SDL_SyncWindow(ZVideo_GetWindow());
            for(int text:{100,120})for(int page:{0,1,2,3}){
                zod_text_percent=text;
                frontend.screen=page==0?ZFrontEnd::SCR_MAIN:page==1?ZFrontEnd::SCR_SETTINGS:page==2?ZFrontEnd::SCR_IMPORT:ZFrontEnd::SCR_CAMPAIGNS;
                frontend.settings_category=page==1?4:0;frontend.Render();
                REQUIRE(SDL_GetRenderTarget(ZVideo_GetRenderer())==nullptr);
                int w,h;SDL_RendererLogicalPresentation mode;REQUIRE(SDL_GetRenderLogicalPresentation(ZVideo_GetRenderer(),&w,&h,&mode));REQUIRE(w==1280&&h==800&&mode==SDL_LOGICAL_PRESENTATION_LETTERBOX);
                REQUIRE(!SDL_RenderViewportSet(ZVideo_GetRenderer())&&!SDL_RenderClipEnabled(ZVideo_GetRenderer()));
                SDL_Surface *image=SDL_RenderReadPixels(ZVideo_GetRenderer(),nullptr);REQUIRE(image&&image->w>0&&image->h>0);
                // Check the menu's centre survives physical-window presentation.
                Uint8 red,green,blue,alpha;REQUIRE(SDL_ReadSurfacePixel(image,image->w/2,image->h/2,&red,&green,&blue,&alpha));REQUIRE(red+green+blue>15);
                REQUIRE(IMG_SavePNG(image,(root+"/display-"+to_string(index++)+".png").c_str()));SDL_DestroySurface(image);
                SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=SDL_GetWindowID(ZVideo_GetWindow());int pointsW,pointsH;SDL_GetWindowSize(ZVideo_GetWindow(),&pointsW,&pointsH);event.motion.x=pointsW*.5f;event.motion.y=pointsH*.5f;ZVideo_ConvertEventCoords(&event);REQUIRE(std::abs(event.motion.x-640)<2&&std::abs(event.motion.y-400)<2);
            }
        }
        ZVideo_SetFullscreen(true);SDL_SyncWindow(ZVideo_GetWindow());frontend.Render();REQUIRE(ZVideo_GetFullscreen());
        ZVideo_SetFullscreen(false);SDL_SyncWindow(ZVideo_GetWindow());frontend.Render();REQUIRE(!ZVideo_GetFullscreen());
        frontend.FreeGraphics();SDL_Quit();std::cout<<"display-matrix-ok";
    }
    static void next_features(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());
        frontend.custom_dir=root+"/maps";SDL_CreateDirectory(frontend.custom_dir.c_str());REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto *workspace=frontend.workspace.get();auto &editor=*workspace;
        string error;ZCampaign author;REQUIRE(ZCampaign_Create("Studio integration",author,error));
        for(auto name:{"one.map","two.map"})REQUIRE(SDL_CopyFile("maps/p02_bb_orig01.map",(author.root+"/"+name).c_str()));
        ZCampaignMission first;first.title="First test";first.map="one.map";first.briefing="Test survival";first.objectives={{"survive","Hold",0,1}};auto second=first;second.map="two.map";second.title="Second test";author.missions={first,second};REQUIRE(ZCampaign_Write(author,error));
        auto source=bytes(author.root+"/one.map");editor.authorCampaign=author;editor.campaignMission=0;editor.Remember();editor.map.GetTile(0).tile=23;size_t history=editor.undo.size();
        REQUIRE(editor.StageCampaignTest());string staged=editor.TestCampaign().root,id=editor.TestCampaign().id;ZMap snapshot;REQUIRE(snapshot.Read(editor.PlayPath().c_str()));REQUIRE(snapshot.GetTile(0).tile==23);REQUIRE(bytes(author.root+"/one.map")==source);
        REQUIRE(ZCampaign_List().size()==1);zod_scenes=false;frontend.Render();ZFrontEnd::Action action;REQUIRE(frontend.PopAction(action)&&action.type==ZFrontEnd::FE_START_CAMPAIGN_MAP);REQUIRE(action.value==0&&frontend.EditorTesting());
        verify_editor_server(action.text,id);
        frontend.RecordCampaignResult(true,50,3,0,1);frontend.PrepareEditorReturn(true);frontend.Hide();frontend.Show();frontend.ContinueCommunity();REQUIRE(frontend.PopAction(action)&&action.value==1&&frontend.EditorTesting());
        frontend.RecordCampaignResult(false,20,0,1,1);frontend.PrepareEditorReturn(true);frontend.Hide();frontend.Show();frontend.ContinueCommunity();REQUIRE(frontend.PopAction(action)&&action.value==1);
        frontend.RecordCampaignResult(true,35,3,0,1);frontend.PrepareEditorReturn(true);frontend.Hide();frontend.Show();frontend.ContinueCommunity();REQUIRE(frontend.screen==ZFrontEnd::SCR_EDITOR&&frontend.workspace.get()==workspace&&!frontend.EditorTesting());REQUIRE(editor.dirty&&editor.undo.size()==history);SDL_PathInfo info;REQUIRE(!SDL_GetPathInfo(staged.c_str(),&info));REQUIRE(!SDL_getenv("ZOD_EDITOR_TEST_MANIFEST"));REQUIRE(ZCampaign_Records(author.id).empty()&&ZCampaign_Records(id).empty());REQUIRE(bytes(author.root+"/one.map")==source);
        // Added unit palette entries and snap placement survive undo/redo.
        editor.tool=1;editor.objectKind=6;editor.snap=4;editor.zoom=1;editor.cameraX=editor.cameraY=0;editor.mx=306+23*16;editor.my=162+21*16;editor.campaignPanel=false;editor.painting=false;auto count=editor.map.GetObjectList().size();editor.Paint();REQUIRE(editor.map.GetObjectList().size()==count+1);auto placed=editor.map.GetObjectList().back();REQUIRE(placed.x==20&&placed.y==20&&placed.object_id==PSYCHO);editor.Undo(false);REQUIRE(editor.map.GetObjectList().size()==count);editor.Undo(true);REQUIRE(editor.map.GetObjectList().size()==count+1&&editor.message.find("Redid")!=string::npos);
        // Shared accessibility/control preferences and live dead-zone filtering.
        zod_stick_deadzone=35;zod_edge_scroll_percent=0;zod_subtitle_percent=150;zod_subtitle_background=false;zod_keys[ZKEY_EDITOR_UNDO]='k';ZPrefs_Save();zod_stick_deadzone=5;zod_edge_scroll_percent=100;zod_subtitle_percent=100;zod_subtitle_background=true;ZPrefs_Load();REQUIRE(zod_stick_deadzone==35&&zod_edge_scroll_percent==0&&zod_subtitle_percent==150&&!zod_subtitle_background&&zod_keys[ZKEY_EDITOR_UNDO]=='k');REQUIRE(ZGamepad_FilterAxis(8192)==0);zod_stick_deadzone=5;REQUIRE(ZGamepad_FilterAxis(8192)>0&&ZGamepad_FilterAxis(32767)>.99f);
        SDL_Event pad{};pad.type=SDL_EVENT_GAMEPAD_BUTTON_DOWN;pad.gbutton.button=SDL_GAMEPAD_BUTTON_SOUTH;ZGamepad_SetEditor(true);ZGamepad_HandleEvent(pad,true);SDL_Event event{};bool pressed=false;while(SDL_PollEvent(&event))pressed|=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT;REQUIRE(pressed);pad.type=SDL_EVENT_GAMEPAD_BUTTON_UP;ZGamepad_SetEditor(false);ZGamepad_HandleEvent(pad,false);bool released=false;while(SDL_PollEvent(&event))released|=event.type==SDL_EVENT_MOUSE_BUTTON_UP&&event.button.button==SDL_BUTTON_LEFT;REQUIRE(released);
        frontend.screen=ZFrontEnd::SCR_SETTINGS;frontend.settings_category=0;frontend.Render();event={};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_TAB;frontend.HandleEvent(event);frontend.HandleEvent(event);event.key.key=SDLK_RETURN;frontend.HandleEvent(event);frontend.Render();REQUIRE(frontend.settings_category==1);
        ZPerf_Enable(true);{ZPerfScope measured("integration_test");SDL_Delay(2);}REQUIRE(snapshot.Read("maps/p02_bb_orig01.map"));ZPerf_Enable(false);ZPerf_Record("disabled",1000);auto report=ZPerf_Report();{std::ofstream forged(root+"/server-performance.txt");forged<<"pathfinding,2,1.5,2.0,2.1\nprivate-user-token=secret\n";}REQUIRE(ZPerf_ServerReport(root+"/server-performance.txt").find("secret")==string::npos);REQUIRE(report.find("integration_test")!=string::npos&&report.find("map_load")!=string::npos&&report.find("disabled,")==string::npos);
        string file;REQUIRE(ZAsset_ExportDiagnostics(file,"idle"));auto diagnostic=bytes(file);REQUIRE(!diagnostic.empty());
        frontend.FreeGraphics();SDL_Quit();std::cout<<"next-features-ok";
    }
    static void polish_preview(const string &mode,const string &image){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());
        if(mode=="campaigns-preview"||mode=="records-preview"||mode=="gallery-preview"){frontend.ScanCampaigns();frontend.screen=ZFrontEnd::SCR_CAMPAIGNS;if(mode=="records-preview"){frontend.campaign_previous_choice=frontend.campaign_choice;frontend.campaign_mission_choice=0;ZCampaign_RecordWin("original",frontend.original_levels[0].base,91,12,0,2);frontend.campaign_records=true;}if(mode=="gallery-preview")frontend.campaign_gallery=true;}
        else if(mode=="settings-preview")frontend.screen=ZFrontEnd::SCR_SETTINGS;
        else {frontend.custom_dir=ZAsset_Profile()+"workspace-maps";SDL_CreateDirectory(frontend.custom_dir.c_str());REQUIRE(frontend.LaunchEditor({"maps/p02_bb_orig01.map"}));auto &editor=*frontend.workspace;editor.campaignPanel=editor.timelinePanel=true;editor.authorCampaign.title="Desert Rebellion";ZCampaignMission mission;mission.objectives={{"message","Secure the crossing",0,0},{"capture","Capture the first territory",0,0},{"defend","Hold the factory",3,90}};editor.authorCampaign.missions={mission};}
        frontend.Render();SDL_SetRenderTarget(ZVideo_GetRenderer(),frontend.target);SDL_Surface *captured=SDL_RenderReadPixels(ZVideo_GetRenderer(),nullptr);REQUIRE(captured);REQUIRE(IMG_SavePNG(captured,image.c_str()));SDL_DestroySurface(captured);SDL_SetRenderTarget(ZVideo_GetRenderer(),nullptr);frontend.FreeGraphics();SDL_Quit();
    }
    static bool snapshot_validation(){
        ZMap map;map.MakeNewMap("Validation",DESERT,32,32);
        auto &info=ZMap::GetMapPaletteTileInfo(DESERT,0);bool original=info.is_passable;info.is_passable=true;
        map_object fort{};fort.object_type=BUILDING_OBJECT;fort.object_id=FORT_FRONT;fort.owner=RED_TEAM;fort.x=3;fort.y=3;map.GetObjectList().push_back(fort);fort.owner=BLUE_TEAM;fort.x=24;fort.y=24;map.GetObjectList().push_back(fort);
        auto contains=[&](const string &text){for(auto &issue:ZContent_ValidateMap(map))if(issue.text.find(text)!=string::npos)return true;return false;};
        REQUIRE(!ZContent_HasErrors(ZContent_ValidateMap(map)));
        map.GetObjectList()[1].owner=100;REQUIRE(contains("invalid team"));map.GetObjectList()[1].owner=BLUE_TEAM;
        map.GetObjectList()[1].object_id=255;REQUIRE(contains("invalid type"));map.GetObjectList()[1].object_id=FORT_FRONT;
        auto &wall=ZMap::GetMapPaletteTileInfo(DESERT,1);bool passable=wall.is_passable;wall.is_passable=false;
        for(int y=0;y<32;++y)map.GetTile(y*32+16).tile=1;
        REQUIRE(contains("Fort may be unreachable"));map_zone zone{};zone.x=20;zone.y=20;zone.w=5;zone.h=5;map.GetZoneList().push_back(zone);REQUIRE(contains("Territory may be unreachable"));
        for(int y=20;y<25;++y)for(int x=20;x<25;++x)map.GetTile(y*32+x).tile=1;
        REQUIRE(contains("Territory has no passable land"));
        info.is_passable=original;wall.is_passable=passable;return true;
    }
    static void content_polish(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));ZCampaign campaign;string error;REQUIRE(ZCampaign_Create("Pack test",campaign,error));
        REQUIRE(SDL_CopyFile("maps/p02_bb_orig01.map",(campaign.root+"/one.map").c_str()));
        ZCampaignMission mission;mission.map="one.map";mission.title="Mission one";mission.objectives={{"message","Begin now",0,0},{"survive","Hold position",0,5}};campaign.missions={mission};campaign.description="A campaign description";campaign.difficulty="Hard";
        REQUIRE(ZCampaign_Write(campaign,error));
        campaign.missions[0].video="missing.mp4";REQUIRE(ZCampaign_Write(campaign,error));
        REQUIRE(!ZContent_HasErrors(ZContent_ValidateCampaign(campaign)));
        REQUIRE(ZContent_HasErrors(ZContent_ValidateCampaign(campaign,true)));
        string rejected;REQUIRE(!ZCampaign_Export(campaign,rejected,error));REQUIRE(rejected.empty());
        campaign.missions[0].video.clear();campaign.missions[0].objectives.push_back({"capture","Invalid territory",999,0});REQUIRE(ZCampaign_Write(campaign,error));REQUIRE(ZContent_HasErrors(ZContent_ValidateCampaign(campaign,true)));
        campaign.missions[0].objectives.pop_back();campaign.missions[0].objectives.push_back({"escort","Escort a building",0,0});REQUIRE(ZCampaign_Write(campaign,error));REQUIRE(ZContent_HasErrors(ZContent_ValidateCampaign(campaign,true)));campaign.missions[0].objectives.pop_back();REQUIRE(ZCampaign_Write(campaign,error));auto issues=ZContent_ValidateCampaign(campaign);REQUIRE(!ZContent_HasErrors(issues));
        string archive;REQUIRE(ZCampaign_Export(campaign,archive,error));ZCampaign summary;REQUIRE(ZCampaign_InspectPack(archive,summary,error));REQUIRE(summary.description==campaign.description&&summary.missions[0].objectives.size()==2);
        REQUIRE(!ZCampaign_ImportPack(archive,error));REQUIRE(error.find("already installed")!=string::npos);
        REQUIRE(SDL_RenamePath(campaign.root.c_str(),(root+"/author-backup").c_str()));REQUIRE(ZCampaign_ImportPack(archive,error));auto installed=ZCampaign_List();REQUIRE(installed.size()==1);string marker;REQUIRE(!ZCampaign_SafeAsset(installed[0].root,".zod-authoring",marker));
        ZCampaign_RecordWin(campaign.id,"0:one.map",45,4,0,1);auto author=campaign;author.root=root+"/author-backup";author.version="2.0";{std::ofstream manifest(author.root+"/campaign.ini",std::ios::app);}
        string manifestPath=author.root+"/campaign.ini";auto manifestBytes=bytes(manifestPath);string text(manifestBytes.begin(),manifestBytes.end());auto version=text.find("version=1.0");REQUIRE(version!=string::npos);text.replace(version,11,"version=2.0");{std::ofstream out(manifestPath);out<<text;}REQUIRE(ZCampaign_Load(manifestPath,author,error));string updateArchive;REQUIRE(ZCampaign_Export(author,updateArchive,error));REQUIRE(ZCampaign_ImportPack(updateArchive,error,true));REQUIRE(ZCampaign_List()[0].version=="2.0");auto installedManifest=bytes(ZCampaign_List()[0].root+"/campaign.ini");REQUIRE(!ZCampaign_ImportPack(archive,error,true));REQUIRE(error.find("newer")!=string::npos);REQUIRE(bytes(ZCampaign_List()[0].root+"/campaign.ini")==installedManifest);auto backups=ZCampaign_Backups(campaign.id);REQUIRE(!backups.empty()&&backups[0].version=="1.0");REQUIRE(!ZCampaign_RestoreBackup(campaign.id,"../bad",error));REQUIRE(ZCampaign_RestoreBackup(campaign.id,backups[0].folder,error));REQUIRE(ZCampaign_List()[0].version=="1.0");REQUIRE(ZCampaign_Records(campaign.id).size()==1);REQUIRE(ZCampaign_Uninstall(campaign.id,error));REQUIRE(ZCampaign_List().empty());REQUIRE(ZCampaign_Records(campaign.id).size()==1);
        ZMissionRuntime runtime;runtime.objectives=mission.objectives;ZMissionWorld world;world.time=10;vector<string> messages;REQUIRE(runtime.Update(world,messages)==0&&runtime.step==1);world.time=14;REQUIRE(runtime.Update(world,messages)==0);world.time=15;REQUIRE(runtime.Update(world,messages)==1);
        runtime={};runtime.objectives={{"defend","Keep factory",0,10}};world.objectOwners={1};world.time=0;REQUIRE(runtime.Update(world,messages)==0);world.objectOwners={2};REQUIRE(runtime.Update(world,messages)==-1);
        runtime={};runtime.objectives={{"capture","Capture territory",0,0}};world.zoneOwners={2};REQUIRE(runtime.Update(world,messages)==0);world.zoneOwners={1};REQUIRE(runtime.Update(world,messages)==1);
        ZMissionObjective objective;REQUIRE(!ZMission_Parse("survive|0|-1|Invalid",objective));REQUIRE(!ZMission_Parse("execute|0|0|Invalid",objective));
        ZMap empty;empty.MakeNewMap("Invalid",DESERT,32,32);REQUIRE(ZContent_HasErrors(ZContent_ValidateMap(empty)));
        REQUIRE(snapshot_validation());
        zod_effects_volume=12;zod_music_volume=34;ZPrefs_ResetCategory(0);REQUIRE(zod_effects_volume==12&&zod_music_volume==34);ZPrefs_ResetCategory(1);REQUIRE(zod_effects_volume==100&&zod_music_volume==100);
        SDL_Quit();std::cout<<"content-polish-ok";
    }
    static void campaigns_and_audio(const string &root) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));ZFrontEnd frontend;frontend.Show();
        auto originals=frontend.original_levels;REQUIRE(!originals.empty());
        ZCampaign campaign;string error;REQUIRE(ZCampaign_Create("Test campaign",campaign,error));
        REQUIRE(SDL_CopyFile("maps/p02_bb_orig01.map",(campaign.root+"/one.map").c_str()));
        ZCampaignMission mission;mission.map="one.map";mission.title="First mission";mission.briefing="A new story\nA second line";mission.video="missing.mp4";
        campaign.missions={mission,mission};campaign.missions[1].title="Second mission";
        REQUIRE(ZCampaign_Write(campaign,error));ZCampaign loaded;REQUIRE(ZCampaign_Load(campaign.root+"/campaign.ini",loaded,error));
        REQUIRE(loaded.missions[0].briefing==mission.briefing);
        frontend.screen=ZFrontEnd::SCR_LOADING;frontend.DeploymentFailed("that campaign map is locked");REQUIRE(!frontend.Loading()&&frontend.status_message=="that campaign map is locked");frontend.screen=ZFrontEnd::SCR_CAMPAIGNS;frontend.DeploymentFailed("ignored outside deployment");REQUIRE(frontend.screen==ZFrontEnd::SCR_CAMPAIGNS);
        REQUIRE(frontend.CampaignMoviePath(&campaign,0,false).empty());
        {std::ofstream movie(campaign.root+"/intro.mp4");movie<<"video fixture";}
        auto mediaCampaign=campaign;mediaCampaign.missions[0].video="intro.mp4";mediaCampaign.missions[0].victoryVideo="missing.mp4";
        REQUIRE(frontend.CampaignMoviePath(&mediaCampaign,0,false)==campaign.root+"/intro.mp4");REQUIRE(frontend.CampaignMoviePath(&mediaCampaign,0,true).empty());mediaCampaign.missions[0].video="../outside.mp4";REQUIRE(frontend.CampaignMoviePath(&mediaCampaign,0,false).empty());
        frontend.ScanCampaigns();REQUIRE(frontend.campaigns.size()==1);
        auto visible=frontend.CampaignVisibleMissions(&frontend.campaigns[0],0);REQUIRE(visible==vector<int>({0}));
        frontend.SelectCampaign(0);frontend.OpenHistoryBriefing(0);REQUIRE(frontend.screen==ZFrontEnd::SCR_BRIEFING&&frontend.history_briefing);ZFrontEnd::Action pending;REQUIRE(!frontend.PopAction(pending));
        zod_scenes=false;frontend.StartLevel(0,false);ZFrontEnd::Action action;REQUIRE(frontend.PopAction(action));REQUIRE(action.type==ZFrontEnd::FE_START_CAMPAIGN_MAP);
        REQUIRE(frontend.CommunityBattle());frontend.RecordCampaignResult(false);REQUIRE(ZCampaign_Completed(campaign.id).empty());frontend.RecordCampaignResult(true);
        REQUIRE(!loaded.missions[0].uid.empty());REQUIRE(ZCampaign_Completed(campaign.id)==vector<string>({"uid:"+loaded.missions[0].uid}));REQUIRE(ZCampaign_Completed("original").empty());
        REQUIRE(frontend.CampaignVisibleMissions(&frontend.campaigns[0],1)==vector<int>({0,1}));frontend.ContinueCommunity();
        REQUIRE(frontend.PopAction(action)&&action.type==ZFrontEnd::FE_START_CAMPAIGN_MAP&&action.value==1);
        frontend.RecordCampaignResult(false);frontend.ContinueCommunity();REQUIRE(frontend.PopAction(action)&&action.value==1);
        frontend.RecordCampaignResult(true);frontend.ContinueCommunity();REQUIRE(frontend.screen==ZFrontEnd::SCR_CAMPAIGNS);
        int current,unlocked;ZCampaign_Progress(campaign.id,current,unlocked);REQUIRE(current==1&&unlocked==1);REQUIRE(ZCampaign_Completed(campaign.id).size()==2);
        auto progressFile=ZAsset_Profile()+"campaign-progress/"+campaign.id+".txt";auto beforeReplay=bytes(progressFile);
        frontend.StartLevel(0,false);REQUIRE(frontend.mission_replay&&frontend.PopAction(action));frontend.RecordCampaignResult(true,25,3,0,1);REQUIRE(bytes(progressFile)==beforeReplay);REQUIRE(frontend.community_next==-1);
        REQUIRE(!bytes(progressFile+".bak").empty());auto changed=frontend.campaigns[0];changed.missions.erase(changed.missions.begin()+1);REQUIRE(!ZCampaign_MigrateIdentity(changed,error));REQUIRE(bytes(progressFile)==beforeReplay);
        {std::ofstream broken(progressFile);broken<<"interrupted write";}ZCampaign_Progress(campaign.id,current,unlocked);REQUIRE(current>=0&&unlocked>=0);auto recoveredBackup=bytes(progressFile+".bak");ZCampaign_Progress(campaign.id,current,unlocked,true);REQUIRE(bytes(progressFile+".bak")==recoveredBackup);

        auto originalVisible=frontend.CampaignVisibleMissions(nullptr,2);REQUIRE(originalVisible==vector<int>({0,1,2}));
        ZCampaign many=loaded;many.missions.resize(10,mission);for(size_t i=2;i<many.missions.size();++i)many.missions[i].uid="extra-"+to_string(i);REQUIRE(frontend.CampaignVisibleMissions(&many,4)==vector<int>({0,1,4}));
        frontend.SelectCampaign(-1);REQUIRE(frontend.selected_campaign.id.empty());REQUIRE(frontend.levels.size()==originals.size());
        REQUIRE(frontend.levels[0].file==originals[0].file);
        frontend.screen=ZFrontEnd::SCR_LOADING;frontend.ResumeSavedCampaign(campaign.root+"/one.map",campaign.id,0);REQUIRE(frontend.CommunityBattle()&&frontend.current_level==0&&frontend.Loading());frontend.ResumeSavedCampaign(campaign.root+"/one.map",campaign.id,1);REQUIRE(frontend.current_level==1);frontend.ResumeSavedCampaign(originals[0].file);REQUIRE(!frontend.CommunityBattle()&&frontend.selected_campaign.id.empty()&&frontend.current_level==0);
        {std::ofstream file(campaign.root+"/campaign.ini");file<<"format=1\nid=evil\ntitle=Escape\n[mission]\nmap=../outside.map\n";}
        REQUIRE(!ZCampaign_Load(campaign.root+"/campaign.ini",loaded,error));
        ZCampaign imported=campaign;SDL_RemovePath((campaign.root+"/.zod-authoring").c_str());REQUIRE(!ZCampaign_Write(imported,error));
        zod_effects_volume=100;zod_music_volume=73;
        GMMWSlider effects("Effects",&zod_effects_volume),music("Music",&zod_music_volume);effects.SetCoords(10,20);effects.SetDimensions(101,34);music.SetCoords(10,80);music.SetDimensions(101,34);
        REQUIRE(effects.Click(60,42));REQUIRE(zod_effects_volume==50&&zod_music_volume==73);
        REQUIRE(effects.Motion(500,42));REQUIRE(zod_effects_volume==100);REQUIRE(effects.UnClick(10,42));REQUIRE(zod_effects_volume==0&&zod_music_volume==73);
        REQUIRE(music.Click(35,102));REQUIRE(music.UnClick(35,102));REQUIRE(zod_music_volume==25&&zod_effects_volume==0);
        ZPrefs_Save();zod_effects_volume=99;zod_music_volume=99;ZPrefs_Load();REQUIRE(zod_effects_volume==0&&zod_music_volume==25);
        SDL_Quit();std::cout<<"campaign-audio-ok";
    }
    static void imported_preview_palette() {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));SDL_Surface *palette=SDL_CreateSurface(320,384,SDL_PIXELFORMAT_RGBA32);REQUIRE(palette);
        for(int y=0;y<384;++y)for(int x=0;x<320;++x){SDL_Rect pixel{x,y,1,1};SDL_FillSurfaceRect(palette,&pixel,SDL_MapRGBA(SDL_GetPixelFormatDetails(palette->format),nullptr,(x*7)%256,(y*5)%256,20,255));}
        string file=ZAsset_Profile()+"imports/preview-fixture/assets/planets/desert.bmp";
        REQUIRE(SDL_SaveBMP(palette,file.c_str()));SDL_DestroySurface(palette);
        MapPreview preview;REQUIRE(ZMP_ReadMapPreview("p02_bb_orig01.map",preview));
        std::set<uint32_t> colors(preview.pixels.begin(),preview.pixels.end());REQUIRE(colors.size()>8);
        SDL_Quit();std::cout<<"imported-preview-ok";
    }
    static void workspace(const string &image,bool exercise) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));
        ZFrontEnd frontend;frontend.Show();REQUIRE(frontend.EnsureGraphics());
        frontend.custom_dir=ZAsset_Profile()+"workspace-maps";SDL_CreateDirectory(frontend.custom_dir.c_str());
        string original="maps/p02_bb_orig01.map";auto before=bytes(original);
        REQUIRE(frontend.LaunchEditor({original}));REQUIRE(frontend.screen==ZFrontEnd::SCR_EDITOR);
        auto &editor=*frontend.workspace;
        if(exercise) {
            auto baseline=editor.Capture();int initial=editor.map.GetTile(0).tile;
            editor.tool=0;editor.tile=(initial+5)%MAX_PLANET_TILES;editor.zoom=1;editor.cameraX=editor.cameraY=0;editor.mx=306+20*16+1;editor.my=162+17*16+1;editor.painting=false;editor.Paint();REQUIRE(editor.map.GetTile((unsigned)(17*editor.map.GetMapBasics().width+20)).tile==editor.tile);REQUIRE(editor.map.GetTile(0).tile==initial);editor.Restore(baseline);
            editor.Remember();editor.map.GetTile(0).tile=(initial+1)%MAX_PLANET_TILES;
            editor.Undo(false);REQUIRE(editor.map.GetTile(0).tile==initial);
            editor.Undo(true);REQUIRE(editor.map.GetTile(0).tile==(initial+1)%MAX_PLANET_TILES);
            auto snapshot=editor.Capture();REQUIRE(!editor.Open("missing-level.map",frontend.custom_dir));
            REQUIRE(editor.map.GetTile(0).tile==snapshot.tiles[0].tile);
            editor.Restore(baseline);
            editor.zoom=1;editor.cameraX=editor.cameraY=0;editor.mx=314;editor.my=170;editor.tool=1;editor.painting=false;
            size_t objects=editor.map.GetObjectList().size();editor.objectKind=5;editor.Paint();REQUIRE(editor.map.GetObjectList().size()==objects+1);REQUIRE(editor.map.GetObjectList().back().object_type==VEHICLE_OBJECT&&editor.map.GetObjectList().back().object_id==LIGHT);
            editor.tool=2;editor.zoneX=-1;editor.painting=false;editor.Paint();editor.mx+=64;editor.my+=64;editor.painting=false;editor.Paint();
            REQUIRE(editor.map.GetZoneList().size()==baseline.zones.size()+1);
            REQUIRE(editor.Save());REQUIRE(editor.Save());
            REQUIRE(bytes(original)==before);REQUIRE(editor.path!=original);
            string safe=editor.path;editor.path=original;REQUIRE(!editor.Save());REQUIRE(bytes(original)==before);editor.path=safe;
            ZMap restored;REQUIRE(restored.Read(editor.path.c_str()));
            REQUIRE(restored.GetMapBasics().width==editor.map.GetMapBasics().width);
            REQUIRE(restored.GetObjectList().size()==editor.map.GetObjectList().size());
            REQUIRE(restored.GetZoneList().size()==editor.map.GetZoneList().size());
            editor.dirty=true;editor.painting=false;editor.autosaveAt=0;editor.Autosave();SDL_PathInfo recovery;REQUIRE(SDL_GetPathInfo((ZAsset_Profile()+"editor-recovery.map").c_str(),&recovery));editor.Recover();REQUIRE(editor.dirty&&editor.path.find("Recovered")!=string::npos);
            editor.tool=4;editor.mx=314;editor.my=170;editor.zoom=1;editor.cameraX=editor.cameraY=0;editor.Paint();REQUIRE(editor.selectedObject>=0);SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.key=SDLK_C;key.key.mod=SDL_KMOD_CTRL;editor.Event(key);REQUIRE(editor.hasClipboard);size_t count=editor.map.GetObjectList().size();editor.mx+=64;key.key.key=SDLK_V;editor.Event(key);REQUIRE(editor.map.GetObjectList().size()==count+1);editor.Undo(false);REQUIRE(editor.map.GetObjectList().size()==count&&editor.selectedObject==-1);
            frontend.InvalidateGraphics();REQUIRE(frontend.workspace.get()==&editor);REQUIRE(editor.map.GetMapBasics().width==restored.GetMapBasics().width);REQUIRE(frontend.EnsureGraphics());
            editor.Remember();editor.Request("@close");REQUIRE(!editor.closed && !editor.pending.empty());editor.pending.clear();
        }
        frontend.Render();SDL_SetRenderTarget(ZVideo_GetRenderer(),frontend.target);
        SDL_Surface *captured=SDL_RenderReadPixels(ZVideo_GetRenderer(),nullptr);REQUIRE(captured);REQUIRE(IMG_SavePNG(captured,image.c_str()));SDL_DestroySurface(captured);
        SDL_SetRenderTarget(ZVideo_GetRenderer(),nullptr);frontend.FreeGraphics();SDL_Quit();
        std::cout<<"workspace-ok";
    }
    static void disc_menu(const string &image,bool exercise,bool options=false) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO)); REQUIRE(ZVideo_SetMode(1280,800,false));
        if(exercise) {
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> art(SDL_CreateSurface(320,200,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            REQUIRE(art);
            SDL_FillSurfaceRect(art.get(),nullptr,SDL_MapRGBA(SDL_GetPixelFormatDetails(art->format),nullptr,60,90,120,255));
            REQUIRE(IMG_SavePNG(art.get(),(ZAsset_Profile()+"imports/disc-fixture/assets/frontend/disc/main_menu.png").c_str()));
        }
        ZFrontEnd frontend; frontend.Show(); REQUIRE(frontend.EnsureGraphics()); REQUIRE(frontend.disc_background);
        if(exercise) {
            frontend.progress_current=-1;frontend.have_save=true;
            ZFrontEnd::SaveSlot slot;slot.slot=3;slot.label="Slot 4";slot.title="Saved battle";
            frontend.saves={slot};frontend.load_selected=0;
        }
        if(options) frontend.screen=ZFrontEnd::SCR_SETTINGS;
        frontend.Render();
        SDL_SetRenderTarget(ZVideo_GetRenderer(),frontend.target);
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> captured(SDL_RenderReadPixels(ZVideo_GetRenderer(),nullptr),SDL_DestroySurface);
        REQUIRE(captured);REQUIRE(IMG_SavePNG(captured.get(),image.c_str()));
        if(exercise) {
            Uint32 pixel=*(Uint32*)((Uint8*)captured->pixels+120*captured->pitch+640*4);
            Uint8 r,g,b,a;SDL_GetRGBA(pixel,SDL_GetPixelFormatDetails(captured->format),nullptr,&r,&g,&b,&a);
            REQUIRE(r<60 && g<90 && b<120);
        }
        SDL_SetRenderTarget(ZVideo_GetRenderer(),nullptr);
        if(exercise) {
            auto click=[&](float x,float y) {
                frontend.screen=ZFrontEnd::SCR_MAIN;frontend.actions.clear();frontend.kb_item=-1;
                SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;
                event.button.button=SDL_BUTTON_LEFT;event.button.x=x;event.button.y=y;
                frontend.HandleEvent(event);event.type=SDL_EVENT_MOUSE_BUTTON_UP;frontend.HandleEvent(event);
                frontend.Render();
            };
            ZFrontEnd::Action action;
            click(800,622); REQUIRE(frontend.PopAction(action));
            REQUIRE(action.type==ZFrontEnd::FE_LOAD_SAVE && action.value==3);
            click(280,555);REQUIRE(frontend.PopAction(action));REQUIRE(action.type==ZFrontEnd::FE_OPEN_MULTIPLAYER);
            click(280,445);REQUIRE(frontend.PopAction(action));REQUIRE(action.type==ZFrontEnd::FE_OPEN_SKIRMISH);
            click(1128,737);REQUIRE(frontend.screen==ZFrontEnd::SCR_SETTINGS);
            int effects=zod_effects_volume,music=zod_music_volume;
            frontend.press_x=frontend.click_x=480;frontend.press_y=frontend.click_y=270;frontend.clicked=true;frontend.Render();
            REQUIRE(frontend.settings_category==1);
            frontend.press_x=frontend.mouse_x=800;frontend.press_y=frontend.mouse_y=360;frontend.mouse_down=true;frontend.Render();
            REQUIRE(zod_effects_volume>=49&&zod_effects_volume<=50&&zod_music_volume==music);
            frontend.mouse_x=600;frontend.Render();REQUIRE(zod_effects_volume==0);
            frontend.mouse_down=false;frontend.Render();zod_effects_volume=effects;zod_music_volume=music;
            frontend.kb_back=true;frontend.Render();REQUIRE(frontend.screen==ZFrontEnd::SCR_MAIN);
            click(948,737);REQUIRE(frontend.screen==ZFrontEnd::SCR_IMPORT);
            click(280,610);REQUIRE(frontend.screen==ZFrontEnd::SCR_SCORES);REQUIRE(!frontend.PopAction(action));
            frontend.screen=ZFrontEnd::SCR_MAIN;frontend.kb_item=-1;frontend.kb_dy=1;frontend.Render();
            zod_scenes=false;frontend.kb_activate=true;frontend.Render();
            REQUIRE(frontend.screen==ZFrontEnd::SCR_CAMPAIGNS);
            frontend.SelectCampaign(-1);frontend.StartLevel(0,false);
            REQUIRE(frontend.PopAction(action));REQUIRE(action.type==ZFrontEnd::FE_START_MAP && action.value==0);
            std::cout<<"disc-menu-actions-ok";
        }
        frontend.FreeGraphics();SDL_Quit();
    }
};

// A narrowly scoped friend fixture exercises production world serialization.
class ZServerRegression {
public:
    static void scripted_stress(const string &map,int ticks,int units){
        REQUIRE(ticks>=1&&ticks<=216000&&units>=1&&units<=512);ZPerf_Enable(true);ZMap::ServerInit();auto server=std::make_unique<ZServer>();server->LoadNextMap(map);REQUIRE(server->game_on);
        for(int i=0;i<units;++i){auto *unit=server->CreateObject(ROBOT_OBJECT,GRUNT,(2+i%28)*16,((i%2?36:32)+i/56)*16,i%2?BLUE_TEAM:RED_TEAM);REQUIRE(unit);waypoint order;order.mode=MOVE_WP;order.x=(4+i%28)*16;order.y=(38+i/56)*16;order.player_given=true;unit->GetWayPointList().push_back(order);}
        server->mission_runtime.events={{}};server->mission_runtime.events[0].at=1;server->mission_runtime.events[0].text="Stress event";server->mission_runtime.missionStarted=0;
        auto state=[&](){std::map<int,std::array<int,4>> snapshot;for(auto *object:server->object_list){unsigned char type,id;object->GetObjectID(type,id);if(type!=BUILDING_OBJECT&&object->IsDestroyed())continue;int x,y;object->GetCords(x,y);snapshot[object->GetRefID()]={{x,y,object->GetHealth(),object->GetOwner()}};}return snapshot;};
        int moved=0,damaged=0,dead=0,reloads=0;auto previous=state();vector<Uint64> memory;memory.push_back(ZPerf_ResidentBytes());
        for(int tick=0;tick<ticks;++tick){ZPerfScope scope("stress.tick");server->ztime.ztime=tick*.1;server->ScuffleUnits();server->ProcessObjects();server->ProcessMissiles();server->ProcessPathFindingResults();server->CheckCampaignObjectives(server->ztime.ztime);if(tick%20==0)SDL_Delay(1);
            if(tick%10==0){auto current=state();for(auto &entry:previous){auto next=current.find(entry.first);if(next==current.end()){++dead;continue;}moved+=entry.second[0]!=next->second[0]||entry.second[1]!=next->second[1];damaged+=entry.second[2]>next->second[2];}previous=current;}
            if((tick+1)%100==0||tick==ticks-1){auto before=state();REQUIRE(server->SaveGame(0,0,0,server->ztime.ztime,0));REQUIRE(server->LoadSavedGame(0,0));REQUIRE(state()==before);++reloads;if(reloads%100==0||tick==ticks-1){auto bytes=ZPerf_ResidentBytes();memory.push_back(bytes);std::cout<<"MEMORY_CHECKPOINT "<<reloads<<" "<<bytes<<"\n";}}
        }
        int liveUnits=0;for(auto *object:server->object_list){unsigned char type,id;object->GetObjectID(type,id);if(!object->IsDestroyed()&&(type==ROBOT_OBJECT||type==VEHICLE_OBJECT||type==CANNON_OBJECT))++liveUnits;}REQUIRE(liveUnits>0);REQUIRE(!server->object_list.empty());if(ticks>=120)REQUIRE(moved>0);if(ticks>=600)REQUIRE(damaged>0);if(ticks>10)REQUIRE(server->mission_runtime.fired.size()==1&&server->mission_runtime.fired[0]);Uint64 warm=memory.size()>2?memory[1]:memory[0],last=memory.back();std::cout<<"STRESS_METRICS {\"ticks\":"<<ticks<<",\"reloads\":"<<reloads<<",\"moved\":"<<moved<<",\"damaged\":"<<damaged<<",\"dead\":"<<dead<<",\"live_units\":"<<liveUnits<<",\"survivors\":"<<server->object_list.size()<<",\"warm_rss\":"<<warm<<",\"final_rss\":"<<last<<",\"peak_rss\":"<<*std::max_element(memory.begin()+(memory.size()>2?1:0),memory.end())<<"}\n";std::cout<<"stress-ok reloads="<<reloads<<" objects="<<server->object_list.size()<<"\n"<<ZPerf_Report();

    }
    static void script_save(const string &map){ZMap::ServerInit();auto server=std::make_unique<ZServer>();server->LoadNextMap(map);auto &runtime=server->mission_runtime;runtime.events={{}};runtime.events[0].kind="reinforce";runtime.events[0].type=ROBOT_OBJECT;runtime.events[0].x=35;runtime.events[0].y=35;runtime.events[0].at=0;runtime.missionStarted=0;auto count=server->object_list.size();server->CheckCampaignObjectives(0);REQUIRE(server->object_list.size()>count);count=server->object_list.size();REQUIRE(runtime.fired[0]);REQUIRE(server->SaveGame(0,0,0,0,0));REQUIRE(server->LoadSavedGame(0,0));server->CheckCampaignObjectives(1);REQUIRE(server->object_list.size()==count&&server->mission_runtime.fired[0]);auto old=legacySave(bytes(ZSave_Path(0)),'4');string contents(old.begin(),old.end());auto trailer=contents.find("zod-ee_SCRIPT2");REQUIRE(trailer!=string::npos&&trailer>=4);old.resize(trailer-4);old[11]='4';{std::ofstream legacy(ZSave_Path(0),std::ios::binary);legacy.write(old.data(),old.size());}REQUIRE(server->LoadSavedGame(0,0));REQUIRE(server->mission_object_refs.size()==server->zmap.GetObjectList().size());std::cout<<"script-save-ok";}

    static void verify_editor(const string &map,const string &id){auto server=std::make_unique<ZServer>();ZMap::ServerInit();server->LoadNextMap(map);REQUIRE(server->SaveGame(0,0,0,0,0));ZSaveInfo info;REQUIRE(ZSave_ReadInfo(0,info));REQUIRE(info.campaign_id==id&&info.mission_index==0);REQUIRE(server->mission_runtime.objectives.size()==1);server->CheckCampaignObjectives(0);server->CheckCampaignObjectives(1);REQUIRE(server->mission_runtime.outcome==1);server->mission_runtime={};server->mission_runtime.objectives={{"defend","Protect fort",0,5}};for(auto *object:server->object_list){unsigned char type,kind;object->GetObjectID(type,kind);if(type==BUILDING_OBJECT&&(kind==FORT_FRONT||kind==FORT_BACK)&&object->GetOwner()==RED_TEAM)object->SetOwner(BLUE_TEAM);}server->CheckCampaignObjectives(0);REQUIRE(server->mission_runtime.outcome==-1);}
    static void movement(const string &map){
        auto server=std::make_unique<ZServer>();ZMap::ServerInit();server->LoadNextMap(map);ZObject *robot=nullptr,*vehicle=nullptr;for(auto *object:server->object_list){unsigned char type,kind;object->GetObjectID(type,kind);if(object->GetOwner()!=RED_TEAM)continue;if(type==ROBOT_OBJECT&&!robot)robot=object;if(type==VEHICLE_OBJECT&&!vehicle)vehicle=object;}REQUIRE(robot&&vehicle);
        robot->SetCords(400,400);vehicle->SetCords(400,448);vehicle->SetHealth(vehicle->GetHealth()/3,server->zmap);robot->GetWayPointList().clear();vehicle->GetWayPointList().clear();waypoint move;move.mode=MOVE_WP;move.x=700;move.y=400;move.player_given=true;robot->GetWayPointList().push_back(move);move.y=448;vehicle->GetWayPointList().push_back(move);
        for(int i=0;i<30;++i){server->ztime.ztime=i*.1;robot->ProcessServer(server->zmap,server->ols);vehicle->ProcessServer(server->zmap,server->ols);server->ProcessPathFindingResults();SDL_Delay(1);}int rx,ry,vx,vy;robot->GetCords(rx,ry);vehicle->GetCords(vx,vy);REQUIRE(rx>400&&vx>400&&rx!=vx);
        auto &pf=server->zmap.GetPathFinder();robot->GetWayPointList().clear();move.x=720;move.y=500;robot->GetWayPointList().push_back(move);server->ztime.ztime+=.1;robot->ProcessServer(server->zmap,server->ols);bool failedResponse=false;
        for(int tries=0;tries<2000&&!failedResponse;++tries){pf.Lock_List();for(auto *response:pf.GetList())if(response->obj_ref_id==robot->GetRefID()){response->pf_point_list.clear();response->path_cost=-1;failedResponse=true;}pf.Unlock_List();if(!failedResponse)SDL_Delay(1);}
        REQUIRE(failedResponse);server->ProcessPathFindingResults();REQUIRE(robot->GetWayPointList().empty()&&robot->GetSFlags().order_unreachable);
        for(int y=0;y<server->zmap.GetMapBasics().height;++y)pf.SetImpassable(32,y,true);pf.SetTileWideWeights();pf.RebuildRegions();robot->GetWayPointList().clear();move.x=55*16;move.y=60*16;robot->GetWayPointList().push_back(move);server->ztime.ztime+=.2;robot->ProcessServer(server->zmap,server->ols);REQUIRE(robot->GetSFlags().order_unreachable);server->NotifyUnreachable(robot);REQUIRE(server->route_notice_at[RED_TEAM]>0&&server->route_notice_at[BLUE_TEAM]==0);std::cout<<"movement-ok";
    }

    static void campaign_menu_resume(){
        auto server=std::make_unique<ZServer>();server->map_list={"maps/p02_bb_orig01.map","maps/p02_bb_orig02.map","maps/p02_bb_orig03.map"};server->selectable_map_list=server->map_list;server->load_maps_randomly=false;server->menu_first=true;
        string progress=server->CampaignProgressPath();{std::ofstream output(progress);output<<"0\n2\n";}auto before=bytes(progress);
        REQUIRE(server->map_name.empty());server->LoadCampaignProgress();REQUIRE(server->max_unlocked_i==2);
        server->map_name="community.map";server->SaveCampaignProgress();REQUIRE(bytes(progress)==before);server->map_name.clear();
        ZMap::ServerInit();int_packet request{};request.map_num=2;ZServer::select_map_event(server.get(),(char*)&request,sizeof(request),-1);
        REQUIRE(server->game_on&&server->zmap.Loaded()&&!server->menu_first);REQUIRE(server->map_name==server->map_list[2]&&server->current_map_i==2);server->ProcessEndGame();REQUIRE(server->end_return_to_menu&&!server->game_on);server->next_reset_game_time=0;server->CheckResetGame();REQUIRE(server->current_map_i==2&&!server->game_on);
        server->map_name=server->map_list[2];server->current_map_i=2;server->SaveCampaignProgress();server->map_name=server->map_list[0];server->current_map_i=0;server->SaveCampaignProgress();{std::ifstream saved(progress);int resume;saved>>resume;REQUIRE(resume==2);}REQUIRE(!bytes(progress+".bak").empty());{std::ofstream broken(progress);broken<<"interrupted";}server->LoadCampaignProgress();REQUIRE(server->current_map_i==1);
        std::cout<<"campaign-menu-resume-ok";
    }
    static void objective_round(){
        ZCampaign campaign;string error;REQUIRE(ZCampaign_Create("Server objective",campaign,error));REQUIRE(SDL_CopyFile("maps/p02_bb_orig01.map",(campaign.root+"/one.map").c_str()));ZCampaignMission mission;mission.map="one.map";mission.objectives={{"survive","Hold position",0,30}};campaign.missions={mission};REQUIRE(ZCampaign_Write(campaign,error));
        auto server=std::make_unique<ZServer>();ZMap::ServerInit();server->LoadNextMap(campaign.root+"/one.map");REQUIRE(server->mission_runtime.objectives.size()==1);server->mission_runtime.stepStarted=server->ztime.ztime-7;server->mission_runtime.announced=true;server->mission_runtime.started=true;
        REQUIRE(server->SaveGame(0,0,0,7,0));server->mission_runtime={};REQUIRE(server->LoadSavedGame(0,0));REQUIRE(server->mission_runtime.objectives.size()==1&&server->mission_runtime.announced);REQUIRE(std::abs(server->ztime.ztime-server->mission_runtime.stepStarted-7)<0.1);
        REQUIRE(server->CheckCampaignObjectives(server->ztime.ztime));REQUIRE(server->mission_runtime.outcome==0);
        int target=-1;auto &objects=server->zmap.GetObjectList();for(int i=0;i<(int)objects.size();++i)if(objects[i].owner==RED_TEAM&&objects[i].object_type==BUILDING_OBJECT){target=i;break;}REQUIRE(target>=0);server->mission_runtime={};server->mission_runtime.objectives={{"defend","Defend factory",target,30}};REQUIRE(server->CheckCampaignObjectives(server->ztime.ztime));REQUIRE(server->mission_runtime.outcome==0);
        server->SetAllowRemoteClients(true);server->ConfigureCampaignMission();REQUIRE(server->mission_runtime.objectives.empty());std::cout<<"objective-server-ok";
    }
    static void persistent_round() {
        auto server=std::make_unique<ZServer>();
        server->SetAllowRemoteClients(true); server->psettings.start_map_paused=true;
        ZMap::ServerInit(); server->LoadNextMap("maps/p02_bb_orig01.map");
        server->game_on=false;server->end_return_to_menu=true;server->do_reset_game=true;server->next_reset_game_time=0;
        server->CheckResetGame();
        REQUIRE(server->game_on && server->zmap.Loaded() && server->ztime.IsPaused());
        REQUIRE(!server->end_return_to_menu && !server->lan_match_started);
        server->cloud_roles.valid=true;server->cloud_roles.revision=1;server->cloud_roles.speed=125;server->cloud_roles.difficulty=2;server->cloud_roles.rotation={server->map_name};server->cloud_roles_until=current_time()+15;
        server->ApplyCloudOperations();REQUIRE(server->cloud_applied_revision==1&&zod_bot_difficulty==2);
        server->lan_match_started=true;server->cloud_roles.revision=2;server->cloud_roles.difficulty=3;
        server->ApplyCloudOperations();REQUIRE(server->cloud_applied_revision==1&&zod_bot_difficulty==2);

    }
    static void offline_policy() {
        REQUIRE(getenv("ZOD_REQUIRE_CLOUD") && string(getenv("ZOD_REQUIRE_CLOUD")) == "1");
        auto server=std::make_unique<ZServer>();
        REQUIRE(server->cloud_required);
        p_info admin(500);admin.mode=PLAYER_MODE;server->player_info.push_back(admin);server->cloud_peers[500].account=string(64,'a');server->cloud_roles.valid=true;server->cloud_roles.owner=string(64,'a');server->cloud_roles_until=current_time()+15;
        REQUIRE(server->CloudAdmin(0));REQUIRE(server->CloudFilter(TCP_EVENT,RESET_MAP,nullptr,0,0));
        server->cloud_peers[500].account=string(64,'b');REQUIRE(!server->CloudAdmin(0));REQUIRE(server->CloudFilter(TCP_EVENT,SET_READY,nullptr,0,0));
        server->cloud_roles.admins.insert(string(64,'b'));REQUIRE(server->CloudFilter(TCP_EVENT,SET_GAME_PAUSED,nullptr,0,0));server->cloud_roles.admins.clear();REQUIRE(!server->CloudAdmin(0));
        server->cloud_roles.admins.insert(string(64,'b'));server->cloud_roles_until=current_time()-1;REQUIRE(!server->CloudAdmin(0));

        auto roles=ZCloud_ParseRoles(string(64,'a')+"\n"+string(64,'b')+"\nm:"+string(64,'c')+"\nc:4:2:125:1\nr:6d6170732f612e6d6170\nt:2\n");
        REQUIRE(roles.valid&&roles.revision==4&&roles.speed==125&&roles.rotation.size()==1&&roles.moderators.count(string(64,'c')));
        REQUIRE(!ZCloud_ParseRoles(string(64,'a')+"\nc:1:2:999:1\n").valid);
        REQUIRE(!ZCloud_ParseRoles(string(64,'a')+"\nr:0a\n").valid);
        server->cloud_roles=roles;server->cloud_roles_until=current_time()+15;
        server->cloud_peers[500].account=string(64,'c');REQUIRE(server->CloudRank(string(64,'c'))==1&&!server->CloudAdmin(0));
        server->cloud_settings=roles;server->map_name="maps/a.map";REQUIRE(server->NextCloudMap()=="maps/a.map");

        server->SetCloudRequired(false); // same policy used by the local campaign launcher
        REQUIRE(!server->cloud_required);
        REQUIRE(server->CloudFilter(TCP_EVENT, 0, nullptr, 0, 0));
    }
    static void cloud_report(const string &account,const string &grant) {
        auto server=std::make_unique<ZServer>();
        ZMap::ServerInit(); server->LoadNextMap("maps/p02_bb_orig01.map");
        server->cloud_required=true; server->cloud_roster[account]={RED_TEAM,grant};
        server->cloud_kills[RED_TEAM]=4; server->cloud_lost[RED_TEAM]=2; server->cloud_captures[RED_TEAM]=3;
        server->ztime.ztime=80; server->ReportCloudMatch();
    }
    static void recovery(const string &map){
        auto server=std::make_unique<ZServer>();ZMap::ServerInit();server->LoadNextMap(map);server->PauseGame();REQUIRE(server->game_on);
        for(int kills=1;kills<=4;++kills)REQUIRE(server->SaveGame(0,kills,0,10,0));
        for(int generation=1;generation<=3;++generation){ZSaveReader reader;ZSaveInfo info;REQUIRE(reader.ReadFile(ZSave_Path(0)+".bak"+to_string(generation))&&ZSave_ReadInfo(reader,info));REQUIRE(info.kills==4-generation);}
        auto intact=bytes(ZSave_Path(0));REQUIRE(intact.size()>256);auto legacy4=legacySave(intact,'4');{std::ofstream old(ZSave_Path(0)+".legacy4",std::ios::binary);old.write(legacy4.data(),legacy4.size());}ZSaveReader oldReader;ZSaveInfo oldInfo;REQUIRE(oldReader.ReadFile(ZSave_Path(0)+".legacy4")&&ZSave_ReadInfo(oldReader,oldInfo)&&oldInfo.kills==4);intact[intact.size()/2]^=1;{std::ofstream damaged(ZSave_Path(0),std::ios::binary);damaged.write(intact.data(),intact.size());}ZSaveInfo corruptInfo;REQUIRE(!ZSave_ReadInfo(0,corruptInfo));
        {std::ofstream damaged(ZSave_Path(0));damaged<<"broken";}ZSaveInfo info;REQUIRE(!ZSave_ReadInfo(0,info));REQUIRE(ZSave_RecoveryInfo(0,info)&&info.kills==3);REQUIRE(ZSave_Recover(0));REQUIRE(ZSave_ReadInfo(0,info)&&info.kills==3);auto preserved=bytes(ZSave_Path(0)+".damaged");REQUIRE(string(preserved.begin(),preserved.end())=="broken");server->ResetGame();REQUIRE(server->LoadSavedGame(0,0));REQUIRE(server->game_on&&!server->object_list.empty());REQUIRE(ZSave_Delete(0));REQUIRE(!ZSave_RecoveryInfo(0,info));std::cout<<"recovery-ok";
    }
    static void roundtrip(const std::string &map) {
        auto server = std::make_unique<ZServer>();
        ZMap::ServerInit();
        server->LoadNextMap(map);
        server->PauseGame();
        REQUIRE(server->game_on);
        REQUIRE(!server->object_list.empty());
        ZObject *robot = nullptr, *vehicle = nullptr, *factory = nullptr;
        for(auto object : server->object_list) {
            unsigned char type, id; object->GetObjectID(type, id);
            if(type == ROBOT_OBJECT && !robot) robot = object;
            if(type == VEHICLE_OBJECT && !vehicle) vehicle = object;
            if(type == BUILDING_OBJECT && object->CanSetRallypoints() && object->GetOwner() != NULL_TEAM && !factory) factory = object;
        }
        REQUIRE(robot && vehicle && factory);
        robot->SetHealth(robot->GetHealth() / 2, server->zmap);
        robot->SetDirection(3);
        robot->SetGrenadeAmount(3);
        waypoint order;
        order.x = 160; order.y = 176; order.mode = MOVE_WP;
        order.player_given = true;
        robot->GetWayPointList().push_back(order);
        factory->GetRallyPointList().push_back(order);
        unsigned char production_type, production_id;
        REQUIRE(factory->GetBuildUnit(production_type, production_id));
        REQUIRE(factory->AddBuildingQueue(production_type, production_id));
        REQUIRE(factory->StoreBuiltCannon(GATLING));
        vehicle->ClearDrivers();
        vehicle->SetDriverType(GRUNT);
        vehicle->AddDriver(37);
        const int robot_ref = robot->GetRefID();
        const int vehicle_ref = vehicle->GetRefID();
        const int factory_ref = factory->GetRefID();
        const int health = robot->GetHealth();
        const int next_ref = server->next_ref_id;
        const size_t count = server->object_list.size();
        server->playing_generated_map = true;
        REQUIRE(server->SaveGame(0, 19, 7, 123, ZSAVE_AUTO_SLOT));
        auto original = bytes(ZSave_Path(0));
        REQUIRE(!original.empty());
        ZSaveReader reader; ZSaveInfo info;
        REQUIRE(reader.ReadFile(ZSave_Path(0)) && ZSave_ReadInfo(reader, info));
        REQUIRE(info.kills == 19 && info.lost == 7 && info.elapsed == 123);
        REQUIRE(info.one_off_map && info.map_name == map);
        // Destroy the world first: a no-op loader cannot pass these checks.
        server->ResetGame();
        REQUIRE(server->object_list.empty());
        REQUIRE(server->LoadSavedGame(0, 0));
        REQUIRE(server->game_on && server->ztime.IsPaused());
        REQUIRE(server->object_list.size() == count && server->next_ref_id == next_ref);
        robot = server->GetObjectFromID(robot_ref, server->object_list);
        vehicle = server->GetObjectFromID(vehicle_ref, server->object_list);
        factory = server->GetObjectFromID(factory_ref, server->object_list);
        REQUIRE(robot && vehicle && factory);
        REQUIRE(robot->GetHealth() == health && robot->GetDirection() == 3);
        REQUIRE(robot->GetGrenadeAmount() == 3);
        REQUIRE(robot->GetWayPointList().size() == 1);
        REQUIRE(robot->GetWayPointList()[0].x == order.x && robot->GetWayPointList()[0].y == order.y);
        REQUIRE(robot->GetWayPointList()[0].player_given);
        REQUIRE(factory->GetRallyPointList().size() == 1);
        REQUIRE(factory->GetRallyPointList()[0].x == order.x);
        REQUIRE(factory->HaveStoredCannon(GATLING));
        REQUIRE(vehicle->GetDriverType() == GRUNT && vehicle->GetDrivers().size() == 1);
        REQUIRE(vehicle->GetDrivers()[0].driver_health == 37);
        // A second save must preserve every serialized world field. Timestamps
        // are the only intentionally variable metadata (the last header double).
        REQUIRE(server->SaveGame(0, 19, 7, 123, ZSAVE_AUTO_SLOT));
        auto restored = bytes(ZSave_Path(0));
        size_t timestamp = 12 + 4 + info.map_name.size() + 4 + info.map_title.size() + 28;
        REQUIRE(original.size() == restored.size());
        REQUIRE(timestamp + 8 <= original.size());
        for(size_t i = timestamp; i < timestamp + 8; ++i) original[i] = restored[i] = 0;
        REQUIRE(original == restored);
        server->ResetGame();
    }
};

static void close_socket(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}
struct Peer {
    int sender;
    std::unique_ptr<SocketHandler> receiver;
    Peer() {
        int listener = (int)socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(listener >= 0);
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(listener, (sockaddr*)&address, sizeof(address)) == 0);
        socklen_t length = sizeof(address);
        REQUIRE(getsockname(listener, (sockaddr*)&address, &length) == 0);
        REQUIRE(listen(listener, 1) == 0);
        sender = (int)socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(connect(sender, (sockaddr*)&address, sizeof(address)) == 0);
        int accepted = (int)accept(listener, nullptr, nullptr);
        close_socket(listener);
        REQUIRE(accepted >= 0);
        receiver = std::make_unique<SocketHandler>(accepted, address);
    }
    ~Peer() { receiver->Disconnect(); close_socket(sender); }
    void send_bytes(const std::vector<char> &data) {
        REQUIRE(send(sender, data.data(), (int)data.size(), 0) == (int)data.size());
        for(int i = 0; i < 1000; ++i) {
            if(receiver->DoRecv()) return;
            SDL_Delay(1);
        }
        throw std::runtime_error("packet receive timed out");
    }
};
static std::vector<char> packet(int size, int id, std::string body = "") {
    std::vector<char> result(8 + body.size());
    PacketSetInt(result.data(), 0, size); PacketSetInt(result.data(), 4, id);
    if(!body.empty()) memcpy(result.data() + 8, body.data(), body.size());
    return result;
}
static void packets() {
    for(bool fast : {false, true}) {
        for(int length : {-1, INT_MIN, INT_MAX, MAX_DATA_STORED, MAX_DATA_STORED - 7}) {
            Peer peer; peer.send_bytes(packet(length, 7));
            char *message = nullptr; int size = 0, id = 0;
            REQUIRE(!(fast ? peer.receiver->DoFastProcess(&message, &size, &id) : peer.receiver->DoProcess(&message, &size, &id)));
            peer.receiver->ResetFastProcess();
            REQUIRE(!peer.receiver->Connected());
        }
        Peer peer;
        auto first = packet(3, 7, "abc");
        // Fragment the header, then the payload, before completing it together
        // with a zero-length packet at an intentionally unaligned offset.
        peer.send_bytes({first.begin(), first.begin() + 3});
        char *message = nullptr; int size = 0, id = 0;
        auto decode = [&]() { return fast ? peer.receiver->DoFastProcess(&message, &size, &id) : peer.receiver->DoProcess(&message, &size, &id); };
        REQUIRE(!decode());
        peer.receiver->ResetFastProcess();
        peer.send_bytes({first.begin() + 3, first.begin() + 9});
        REQUIRE(!decode());
        peer.receiver->ResetFastProcess();
        auto last = std::vector<char>(first.begin() + 9, first.end());
        auto empty = packet(0, 8); last.insert(last.end(), empty.begin(), empty.end());
        peer.send_bytes(last);
        REQUIRE(decode() && size == 3 && id == 7 && std::string(message, size) == "abc");
        REQUIRE(decode() && size == 0 && id == 8);
        REQUIRE(!decode()); peer.receiver->ResetFastProcess();
        REQUIRE(peer.receiver->Connected());
        auto head = packet(1, 9, "q");
        auto tail = packet(64, 10, std::string(64, 'z'));
        head.insert(head.end(), tail.begin(), tail.begin() + 40);
        peer.send_bytes(head);
        REQUIRE(decode() && id == 9);
        REQUIRE(!decode());
        peer.receiver->ResetFastProcess(); // compaction moves overlapping ranges
        peer.send_bytes({tail.begin() + 40, tail.end()});
        REQUIRE(decode() && id == 10 && size == 64 && std::string(message, size) == std::string(64, 'z'));
        peer.receiver->ResetFastProcess();
        // Largest legal frame must remain accepted, across many receives.
        auto maximum = packet(MAX_DATA_STORED - 8, 11, std::string(MAX_DATA_STORED - 8, 'm'));
        for(size_t offset = 0; offset < maximum.size(); offset += 1024) {
            size_t end = std::min(offset + 1024, maximum.size());
            peer.send_bytes({maximum.begin() + offset, maximum.begin() + end});
        }
        REQUIRE(decode() && size == MAX_DATA_STORED - 8 && id == 11);
        REQUIRE(message[0] == 'm' && message[size - 1] == 'm');
        peer.receiver->ResetFastProcess();
        REQUIRE(!peer.receiver->SendMessage(1, nullptr, 1));
        REQUIRE(!peer.receiver->SendMessage(1, "x", -1));
        REQUIRE(!peer.receiver->SendMessage(1, "x", INT_MAX));
        auto valid_then_bad = packet(1, 12, "v");
        auto invalid = packet(INT_MAX, 13);
        valid_then_bad.insert(valid_then_bad.end(), invalid.begin(), invalid.end());
        peer.send_bytes(valid_then_bad);
        REQUIRE(decode() && id == 12);
        REQUIRE(!decode() && !peer.receiver->Connected());
        peer.receiver->ResetFastProcess();
    }
    // The older peek-based API must also avoid consuming a partial frame.
    for(int length : {-1, INT_MAX, MAX_BUF_SIZE}) {
        Peer peer; auto invalid = packet(length, 1);
        REQUIRE(send(peer.sender, invalid.data(), (int)invalid.size(), 0) == (int)invalid.size());
        for(int tries = 0; tries < 1000 && peer.receiver->Connected(); ++tries) {
            REQUIRE(!peer.receiver->PacketAvailable()); SDL_Delay(1);
        }
        REQUIRE(!peer.receiver->Connected());
    }
    Peer peek;
    auto frame = packet(3, 14, "end");
    char *message = nullptr; int size, id;
    REQUIRE(send(peek.sender, frame.data(), 9, 0) == 9);
    REQUIRE(!peek.receiver->GetPacket(&message, &size, &id));
    REQUIRE(send(peek.sender, frame.data() + 9, 2, 0) == 2);
    bool received = false;
    for(int tries = 0; tries < 1000 && !received; ++tries) {
        received = peek.receiver->GetPacket(&message, &size, &id) != 0;
        if(!received) SDL_Delay(1);
    }
    REQUIRE(received && size == 3 && id == 14 && std::string(message, size) == "end");
}

static void local_match(const string &map)
{
    vector<MapMeta> maps;
    REQUIRE(ZMP_ListMaps(maps));
    auto selected = std::find_if(maps.begin(), maps.end(), [&](const MapMeta &item) { return item.name == map; });
    REQUIRE(selected != maps.end());
    MatchInfo match;
    REQUIRE(!ZMP_CreateMatch("../missing.map", {}, match));
    if(!ZMP_CreateMatch(map, {}, match)) throw std::runtime_error(ZMP_LastError());
    REQUIRE(match.host == "127.0.0.1" && match.port >= 2300 && match.port <= 2399);
    REQUIRE(match.capacity == selected->players && match.capacity >= 2);
    vector<MatchInfo> matches;
    REQUIRE(ZMP_ListMatches(matches) && matches.size() == 1 && matches[0].id == match.id);
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons((unsigned short)match.port);
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0 && connect(fd, (sockaddr*)&address, sizeof(address)) == 0);
    auto client = std::make_unique<SocketHandler>(fd, address);
    REQUIRE(client->SendMessage(GET_GAME_PAUSED, nullptr, 0));
    REQUIRE(client->SendMessage(REQUEST_MAP, nullptr, 0));
    REQUIRE(client->SendMessage(REQUEST_OBJECTS, nullptr, 0));
    REQUIRE(client->SendMessage(REQUEST_PLAYER_ID, nullptr, 0));
    vector<char> downloaded;
    bool paused = false, map_done = false, barrier = false;
    int objects = 0, chunk = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!barrier && std::chrono::steady_clock::now() < deadline) {
        client->DoRecv();
        char *data; int size, id;
        while(client->DoFastProcess(&data, &size, &id)) {
            if(id == UPDATE_GAME_PAUSED) { REQUIRE(size == 1); paused = data[0] != 0; }
            if(id == STORE_MAP) {
                REQUIRE(size >= 4);
                int number = PacketGetInt(data, 0);
                if(number == -1) map_done = true;
                else { REQUIRE(number == chunk++); downloaded.insert(downloaded.end(), data + 4, data + size); }
            }
            if(id == ADD_NEW_OBJECT) { REQUIRE(size == sizeof(object_init_packet)); ++objects; }
            if(id == GIVE_PLAYER_ID) barrier = true;
        }
        client->ResetFastProcess();
        REQUIRE(client->Connected()); SDL_Delay(1);
    }
    REQUIRE(paused && map_done && barrier && objects > 0);
    REQUIRE(downloaded == bytes("maps/" + map));
    client->Disconnect();
    std::cout << "LOCAL_MATCH_PORT=" << match.port << std::endl;
    // The process registry's exit cleanup must stop this server. Python probes
    // the port after this fixture exits, including on Windows via its job.
}

static void generated_download(const MatchInfo &match,const string &key)
{
    string error,path;
    REQUIRE(ZSkirmish_MapPath(key,path));
    auto connection=ZNetConnect(match.host,match.port,error);
    REQUIRE(connection);
    list<Event*> events; ClientSocket client; client.SetEventList(&events); REQUIRE(client.Adopt(*connection)); REQUIRE(client.GetPort()==match.port);
    REQUIRE(client.SendMessage(REQUEST_MAP,nullptr,0));
    vector<char> data; bool done=false; int chunk=0;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!done && std::chrono::steady_clock::now()<deadline) {
        client.Process();
        for(auto *event:events) {
            if(event->event_type==TCP_EVENT && event->event_number==STORE_MAP) {
                REQUIRE(event->size>=4); int n=PacketGetInt(event->data,0);
                if(n==-1) done=true;
                else { REQUIRE(n==chunk++); data.insert(data.end(),event->data+4,event->data+event->size); }
            }
            delete event;
        }
        events.clear(); SDL_Delay(1);
    }
    REQUIRE(done && data==bytes(path)); client.ClearConnection();
}
static void random_module_tests(const string &root)
{
    std::uint32_t seed=7;
    REQUIRE(ZSkirmish_ParseSeed("0",seed) && seed==0);
    REQUIRE(ZSkirmish_ParseSeed("4294967295",seed) && seed==UINT32_MAX);
    for(const string &bad:{"", "-1", "4294967296", "5x", " 1"}) REQUIRE(!ZSkirmish_ParseSeed(bad,seed));
    string path;
    REQUIRE(!ZSkirmish_MapPath("@generated:../escape.map",path));
    REQUIRE(!ZSkirmish_MapPath("../escape.map",path));
    RandomMapOptions options; options.size=0;
    MapPreview preview;
    for(int terrain=0;terrain<5;++terrain) {
        options.terrain=terrain; options.seed=terrain ? UINT32_MAX : 0;
        REQUIRE(ZSkirmish_Generate(options,preview)); REQUIRE(preview.teams.size()==2);
        REQUIRE(ZSkirmish_MapPath(preview.name,path)); auto first=bytes(path);
        REQUIRE(ZSkirmish_Generate(options,preview)); REQUIRE(first==bytes(path));
    }
    // Generator calls must not re-seed or consume the battle's global RNG.
    srand(9321); int expected=rand(); srand(9321);
    REQUIRE(MapGen::Generate(root+"/rng.map",1,64,80,0,0,9));
    REQUIRE(rand()==expected);
    SkirmishPreferences prefs; prefs.map="p02_bb_orig01.map"; prefs.match.human_team=BLUE_TEAM;
    prefs.match.difficulty=3; prefs.match.game_speed=1.5; prefs.random.seed=UINT32_MAX; prefs.bots=1u<<RED_TEAM;
    REQUIRE(ZSkirmish_Save(prefs));
    auto loaded=ZSkirmish_Load();
    REQUIRE(loaded.map==prefs.map && loaded.match.human_team==BLUE_TEAM && loaded.match.difficulty==3 && loaded.match.game_speed==1.5);
    REQUIRE(loaded.random.seed==UINT32_MAX && loaded.bots==(1u<<RED_TEAM));
    prefs.match.game_speed=0; REQUIRE(ZSkirmish_Save(prefs)); REQUIRE(ZSkirmish_Load().match.game_speed==1);
    REQUIRE(ZSkirmish_Save(loaded));
    std::ofstream damaged(root+"/.zod_skirmish/setup.dat",std::ios::binary|std::ios::trunc); damaged<<"broken"; damaged.close();
    REQUIRE(ZSkirmish_Load().map.empty());
}

class ZLobbyRegression {
public:
    static void run(const string &root,const string &image) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        ZFontEngine::Init(); ZGuiMainMenuBase::Init(); GMMWButton::Init(); GMMWList::Init();
        SDL_Surface *screen=SDL_CreateSurface(640,480,SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(screen); ZSDL_Surface::SetMainSoftwareSurface(screen);
        vector<p_info> players;
        auto add=[&](int id,const string &name,int team,int mode,bool ready) {
            p_info p(id); p.name=name; p.team=(team_type)team; p.mode=(player_mode)mode; p.ready=ready; players.push_back(p);
        };
        add(100,"Alice",RED_TEAM,PLAYER_MODE,true); add(101,"Bob",BLUE_TEAM,PLAYER_MODE,false);
        add(102,"Computer",GREEN_TEAM,BOT_MODE,false); add(103,"Observer",NULL_TEAM,PLAYER_MODE,false);
        int local_id=101; ZTime clock; clock.Pause();
        GMMLobby menu; menu.SetPlayerInfoList(&players); menu.SetLocalPId(&local_id); menu.SetZTime(&clock);
        menu.SetSession("192.168.1.20:2301","Arctic Islands",{RED_TEAM,BLUE_TEAM,GREEN_TEAM,YELLOW_TEAM},false);
        menu.Process(); menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.ready_button);
        REQUIRE(!menu.GetGMMFlags().toggle_ready); // Cannot ready before map load.
        menu.SetSession("192.168.1.20:2301","Arctic Islands",{RED_TEAM,BLUE_TEAM,GREEN_TEAM,YELLOW_TEAM},true);
        menu.Process(); REQUIRE(menu.player_list.GetEntryList().size()==5);
        bool self=false,open=false,spectator=false;
        for(const auto &row:menu.player_list.GetEntryList()) {
            self|=row.text.find("Bob (you) | NOT READY")!=string::npos;
            open|=row.text.find("Open - waiting")!=string::npos;
            spectator|=row.text.find("Spectator | Observer")!=string::npos;
        }
        REQUIRE(self && open && spectator);
        menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.ready_button);
        REQUIRE(menu.ready_pending && menu.GetGMMFlags().toggle_ready && menu.GetGMMFlags().ready_value);
        menu.GetGMMFlags().clear(); menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.ready_button);
        REQUIRE(!menu.GetGMMFlags().toggle_ready); // Don't flood duplicate ready requests.
        players[1].ready=true; menu.Process(); REQUIRE(!menu.ready_pending);
        players[0].world_sync=0;menu.Process();REQUIRE(menu.player_list.GetEntryList()[0].text.find("RECEIVING MAP")!=string::npos);players[0].world_sync=1;
        menu.SetCountdown(5);menu.Process();menu.SetCountdown(-1);menu.Process();REQUIRE(menu.cancelled_at>0);

        menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.team_button);
        REQUIRE(menu.GetGMMFlags().change_team && menu.GetGMMFlags().change_team_type==YELLOW_TEAM);
        menu.GetGMMFlags().clear(); local_id=103; menu.Process();
        menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.ready_button); REQUIRE(!menu.GetGMMFlags().toggle_ready);
        local_id=101; players[1].ready=true; players[2].ignored=true; menu.SetCountdown(12); menu.Process();
        menu.SetCenterCoords(320,240); SDL_FillSurfaceRect(screen,nullptr,0xff171b20);
        ZMap background;REQUIRE(background.Read("maps/p02_bb_orig01.map")); menu.RenderContents(background,screen); REQUIRE(IMG_SavePNG(screen,image.c_str()));
        menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.ready_button); menu.ready_sent_at=SDL_GetTicks()-6000;
        menu.Process(); REQUIRE(menu.sticky_error && !menu.ready_pending);
        menu.GetGMMFlags().clear();menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.spectate_button);REQUIRE(menu.GetGMMFlags().change_team&&menu.GetGMMFlags().change_team_type==-1);
        menu.GetGMMFlags().clear();menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.auto_button);REQUIRE(menu.GetGMMFlags().change_team);
        menu.GetGMMFlags().clear();menu.KeyPress(9);menu.KeyPress(SDLK_DOWN);REQUIRE(menu.player_list.GetFirstSelected()>=0);menu.KeyPress(9);menu.KeyPress(13);REQUIRE(menu.GetGMMFlags().change_team);
        menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.leave_button); REQUIRE(menu.GetGMMFlags().leave_match);
        // The title-bar close action must leave too, rather than expose a
        // paused match with its ready gate hidden.
        int menuWidth,menuHeight;menu.GetDimensions(menuWidth,menuHeight);int closeX=320+menuWidth/2-8,closeY=240-menuHeight/2+8;menu.Click(closeX,closeY); menu.UnClick(closeX,closeY); REQUIRE(menu.GetGMMFlags().leave_match && !menu.KillMe());
        clock.Resume(); menu.Process(); REQUIRE(menu.KillMe());
        ZSDL_Surface::SetMainSoftwareSurface(nullptr); SDL_DestroySurface(screen);

        // Three forts: two ready humans should count down despite a spectator.
        REQUIRE(MapGen::Generate(root+"/lobby.map",2,64,80,0,0,55));
        auto server=std::make_unique<ZServer>(); auto &s=*server;
        ZMap::ServerInit(); s.LoadNextMap(root+"/lobby.map"); s.PauseGame();
        s.player_info.clear();
        for(int i=0;i<3;++i) { p_info p(i); p.mode=PLAYER_MODE; p.team=(team_type)(i==2 ? NULL_TEAM : i+1); p.ready=i!=2; s.player_info.push_back(p); }
        s.CheckAutoStart(); REQUIRE(s.ztime.IsPaused() && s.auto_start_deadline>0 && s.last_lobby_countdown>0);
        s.player_info[1].ready=false; s.CheckAutoStart(); REQUIRE(s.auto_start_deadline==0 && s.last_lobby_countdown==-1);
        s.player_info[0].ready=false;
        s.player_info[0].team=RED_TEAM; s.player_info[0].ready=true; s.player_info[1].ready=true;
        s.player_info[2].team=GREEN_TEAM; s.player_info[2].mode=BOT_MODE;
        s.CheckAutoStart(); REQUIRE(!s.ztime.IsPaused() && s.lan_match_started);
        s.PauseGame(); s.CheckAutoStart(); REQUIRE(s.ztime.IsPaused()); // Mid-game pause must stick.
        s.LoadNextMap(root+"/lobby.map"); for(const auto &p:s.player_info) REQUIRE(!p.ready);
    }
};

class ZPlayerUIRegression {
public:
    static void replay_feedback(const string &root){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));ZMap::ServerInit();
        auto map=bytes("maps/p02_bb_orig01.map");vector<char> packet(map.size()+4);int index=0;memcpy(packet.data(),&index,4);memcpy(packet.data()+4,map.data(),map.size());ZReplayRecorder recorder;string file=root+"/play.zreplay";REQUIRE(recorder.Start(file,RED_TEAM));REQUIRE(recorder.Record(0,RESET_GAME,nullptr,0));REQUIRE(recorder.Record(0,STORE_MAP,packet.data(),packet.size()));index=-1;REQUIRE(recorder.Record(0,STORE_MAP,(char*)&index,4));REQUIRE(recorder.Finish());
        auto player=std::make_unique<ZPlayer>();auto &p=*player;p.init_w=1280;p.init_h=800;REQUIRE(!p.PlayReplay(root+"/missing.zreplay")&&!p.frontend.Loading()&&p.frontend.Visible());REQUIRE(p.PlayReplay(file));p.ProcessReplay();REQUIRE(p.replay_active&&p.replay_ended&&p.replay_paused&&p.zmap.Loaded());auto *display=SDL_CreateSurface(1280,800,SDL_PIXELFORMAT_ARGB8888);REQUIRE(display);ZSDL_Surface::SetMainSoftwareSurface(display);SDL_FillSurfaceRect(display,nullptr,0xff0e171e);auto *renderer=ZVideo_GetRenderer();ZMenuTheme::beginText(renderer);p.RenderReplay();p.RenderBattleFeedback();ZMenuTheme::endText();auto *texture=SDL_CreateTextureFromSurface(renderer,display);REQUIRE(texture);SDL_RenderTexture(renderer,texture,nullptr,nullptr);ZMenuTheme::presentText(renderer,1280,800);auto *capture=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(capture&&IMG_SavePNG(capture,(root+"/replay-controls.png").c_str()));SDL_DestroySurface(capture);SDL_DestroyTexture(texture);ZSDL_Surface::SetMainSoftwareSurface(nullptr);SDL_DestroySurface(display);p.NotifyBattle("Under fire",320,400,true);p.NotifyBattle("Under fire",330,410,true);REQUIRE(p.battle_notifications.size()==1);REQUIRE(p.ClickNotification(20,140));REQUIRE(p.notification_click);p.SeekReplay(0);p.ProcessReplay();REQUIRE(p.zmap.Loaded()&&!p.replay_seeking);p.ReplayClick(22,50);REQUIRE(!p.replay_paused);p.ReplayClick(340,50);REQUIRE(p.replay_speed==2);p.LoadMainMenu(GMM_SAVE_GAME);REQUIRE(p.gui_menu_list.empty());zod_damage_alerts=false;auto count=p.battle_notifications.size();p.NotifyBattle("Ignored damage",100,100,true);REQUIRE(p.battle_notifications.size()==count);zod_damage_alerts=true;zod_production_alerts=false;p.NotifyBattle("Ignored production",100,100,false);REQUIRE(p.battle_notifications.size()==count);zod_production_alerts=true;for(int i=0;i<40;++i)p.NotifyBattle("Notice "+to_string(i),i*160,200,false);REQUIRE(p.battle_notifications.size()==32);REQUIRE(p.ClickNotification(20,230)&&p.notification_history);REQUIRE(p.ClickNotification(160,320)&&p.notification_history_page==1);p.init_w=640;p.ReplayClick(560,50);REQUIRE(!p.replay_active&&!p.zmap.Loaded());std::cout<<"replay-feedback-ok";
    }

    static void multiplayer_reconnect(int port){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));ZMap::ServerInit();
        auto player=std::make_unique<ZPlayer>();auto &p=*player;
        p.LoadMainMenu(GMM_MULTIPLAYER);p.JoinHostedMatch("127.0.0.1",port,RED_TEAM,p.gui_menu_list.front());
        auto wait=[&](){auto deadline=SDL_GetTicks()+10000;while((p.join_task.Pending()||p.reconnecting)&&SDL_GetTicks()<deadline){p.PollHostedJoin();SDL_Delay(5);}REQUIRE(!p.join_task.Pending()&&!p.reconnecting);};
        wait();REQUIRE(p.client_socket.GetHandler()&&p.hosted_remote_session);p.ehandler.ProcessEvents();
        p.SetPlayerTeam(RED_TEAM);p.client_socket.ClearConnection();p.ProcessDisconnect();
        REQUIRE(p.reconnecting&&p.reconnect_team==RED_TEAM&&p.reconnect_port==port);wait();
        REQUIRE(p.client_socket.GetHandler()&&p.LobbyOpen());p.ehandler.ProcessEvents();
        // Public tickets must never be silently reused by recovery.
        p.public_remote_session=true;p.client_socket.ClearConnection();p.ProcessDisconnect();REQUIRE(!p.reconnecting);
        // A cancelled reconnect cannot adopt a late worker result into a new menu.
        p.public_remote_session=false;p.hosted_remote_session=true;p.ProcessDisconnect();REQUIRE(p.reconnecting);
        p.reconnect_after=0;p.PollHostedJoin();p.CloseAllMenus();p.LoadMainMenu(GMM_MULTIPLAYER);wait();REQUIRE(!p.client_socket.GetHandler());
        // Unavailable endpoints stop after three attempts rather than looping forever.
        p.reconnecting=true;p.reconnect_attempt=0;p.reconnect_after=0;p.reconnect_port=0;p.LoadMainMenu(GMM_LOBBY);wait();REQUIRE(p.reconnect_attempt==3&&!p.client_socket.GetHandler());
        player.reset();SDL_Quit();std::cout<<"multiplayer-reconnect-ok";
    }
    static void reconnect_failure(int port){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));auto player=std::make_unique<ZPlayer>();auto &p=*player;p.SetRemoteAddress("127.0.0.1");REQUIRE(!p.client_socket.Start("256.256.256.256",port));
        ZFrontEndRegression::start_deployment(p.frontend);
        for(int attempt=0;attempt<2;++attempt){auto deadline=SDL_GetTicks()+8000;while(!ZFrontEndRegression::deployment_failed(p.frontend)&&SDL_GetTicks()<deadline){p.ProcessFrontEndActions();SDL_Delay(5);}REQUIRE(ZFrontEndRegression::deployment_failed(p.frontend)&&!p.frontend.Loading());if(attempt==0)ZFrontEndRegression::retry_deployment(p.frontend);}
        SDL_Quit();std::cout<<"reconnect-failure-ok";
    }
    static void objectives_and_summary(){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(640,480,false));auto player=std::make_unique<ZPlayer>();auto &p=*player;p.init_w=640;p.init_h=480;
        p.ReceiveObjective("@objective|1|3|10|160|240|Defend factory");REQUIRE(p.objective_step==1&&p.objective_total==3&&p.objective_remaining==10&&p.objective_x==160&&p.objective_text=="Defend factory");p.RenderObjectiveHUD();p.ReceiveObjective("@objective|4|3|10|0|0|Invalid");REQUIRE(p.objective_text=="Defend factory");
        auto *workspace=ZFrontEndRegression::prepare_playtest(p.frontend);p.show_match_summary=true;p.summary_pending=false;p.summary_continue_sent=false;ZPlayer::return_to_menu_event(&p,nullptr,0,0);REQUIRE(p.summary_menu_ready&&p.show_match_summary);p.SendContinueAfterEnd();REQUIRE(!p.show_match_summary&&p.frontend.Visible()&&p.objective_text.empty());ZFrontEndRegression::check_playtest_return(p.frontend,workspace);p.frontend.InvalidateGraphics();SDL_Quit();std::cout<<"objectives-summary-ok";
    }
    static void menu_navigation() {
        struct Probe : ZGuiMainMenuBase {
            int renders=0,inputs=0;
            void DoRender(ZMap&,SDL_Surface*) override { ++renders; }
            bool Click(int,int) override { ++inputs;return false; }
            bool UnClick(int,int) override { ++inputs;return false; }
            bool Motion(int,int) override { ++inputs;return false; }
            bool KeyPress(int) override { ++inputs;return false; }
            bool WheelUpButton() override { ++inputs;return false; }
            bool WheelDownButton() override { ++inputs;return false; }
        };
        auto player=std::make_unique<ZPlayer>();auto &p=*player;
        auto *back=new Probe();auto *front=new Probe();p.gui_menu_list={front,back};
        p.RenderMainMenu();REQUIRE(front->renders==1 && back->renders==0);
        double saved_scale=zod_menu_scale;zod_menu_scale=1.0;p.RenderMainMenu();
        REQUIRE(front->GetRenderScale()==1 && back->GetRenderScale()==1);
        zod_menu_scale=1.5;p.RenderMainMenu();REQUIRE(front->GetRenderScale()==1.5 && back->GetRenderScale()==1.5);
        zod_menu_scale=saved_scale;
        REQUIRE(p.MainMenuAbsorbLClick());REQUIRE(p.MainMenuAbsorbLUnClick());REQUIRE(p.MainMenuMotion());
        p.MainMenuKeyPress('a');p.MainMenuWheelUp();p.MainMenuWheelDown();
        REQUIRE(front->inputs==6 && back->inputs==0);
        p.BackFromMenu();
        p.RenderMainMenu();REQUIRE(back->renders==1);
        p.BackFromMenu();REQUIRE(p.gui_menu_list.empty());REQUIRE(!p.MainMenuAbsorbLClick());
        GMMMainMenu solo(false,true,true),multi(false,false,false);
        REQUIRE(solo.menu_button[GMMMM_MULTIPLAYER_BUTTON].GetText().empty());
        REQUIRE(solo.menu_button[GMMMM_SELECT_MAP_BUTTON].GetText().empty());
        REQUIRE(solo.menu_button[GMMMM_CHANGE_TEAMS_BUTTON].GetText().empty());
        REQUIRE(solo.menu_button[GMMMM_SAVE_GAME_BUTTON].GetText()=="Save Game");
        REQUIRE(multi.menu_button[GMMMM_PLAYER_LIST_BUTTON].GetText()=="Player List");
        REQUIRE(multi.menu_button[GMMMM_SAVE_GAME_BUTTON].GetText().empty());
        REQUIRE(multi.menu_button[GMMMM_EXIT_TO_MENU_BUTTON].GetText()=="Leave match");
        std::cout<<"menu-navigation-ok";
    }
    static void run(const string &image) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        ZFontEngine::Init(); ZGuiMainMenuBase::Init(); GMMWButton::Init(); GMMWList::Init();
        ZMap::ServerInit();
        SDL_Surface *screen=SDL_CreateSurface(640,480,SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(screen); ZSDL_Surface::SetMainSoftwareSurface(screen);
        ZSDL_Surface::SetUseOpenGL(false); ZSDL_Surface::SetScreenDimensions(640,480);
        auto player=std::make_unique<ZPlayer>(); auto &p=*player;
        p.init_w=640; p.init_h=480; p.our_team=RED_TEAM; p.ResetMatchStats();
        REQUIRE(p.zmap.Read("maps/p02_bb_orig01.map"));
        p.zmap.SetViewingDimensions(640,480);
        auto add=[&](int type,int id,int ref,int team) {
            object_init_packet packet{}; packet.x=160; packet.y=160; packet.owner=team;
            packet.object_type=type; packet.object_id=id; packet.ref_id=ref;
            auto *obj=p.ProcessNewObject((char*)&packet,sizeof(packet)); REQUIRE(obj); return obj;
        };
        auto *a=add(ROBOT_OBJECT,GRUNT,8,RED_TEAM);
        bool previous_smoothing=zod_render_smoothing;zod_render_smoothing=true;
        a->ProcessRenderSmoothing(1);a->SetCords(168,160);a->ProcessRenderSmoothing(1.016);
        double ox,oy;a->GetRenderOffset(ox,oy);REQUIRE(ox!=0);
        zod_render_smoothing=false;a->GetRenderOffset(ox,oy);REQUIRE(ox==0 && oy==0);
        a->SetCords(160,160);a->ProcessRenderSmoothing(1.032);zod_render_smoothing=previous_smoothing;
        auto *b=add(VEHICLE_OBJECT,LIGHT,3,RED_TEAM);
        auto *enemy=add(ROBOT_OBJECT,GRUNT,1,BLUE_TEAM);
        auto *gun=add(CANNON_OBJECT,GATLING,2,RED_TEAM);
        auto *factory=dynamic_cast<ZBuilding*>(add(BUILDING_OBJECT,ROBOT_FACTORY,20,RED_TEAM)); REQUIRE(factory);
        p.SelectNextIdleFactory();REQUIRE(p.select_info.selected_list.size()==1&&p.select_info.selected_list[0]==factory);p.BattleFeedback("Blocked production fixture");REQUIRE(p.battle_notice=="Blocked production fixture"&&p.battle_notice_until>SDL_GetTicks());
        REQUIRE(p.IsIdleUnit(a) && p.IsIdleUnit(b)); REQUIRE(!p.IsIdleUnit(enemy) && !p.IsIdleUnit(gun));
        p.SelectNextIdleUnit(); REQUIRE(p.select_info.selected_list.size()==1 && p.select_info.selected_list[0]==b);
        p.SelectNextIdleUnit(); REQUIRE(p.select_info.selected_list[0]==a);
        p.SelectNextIdleUnit(); REQUIRE(p.select_info.selected_list[0]==b);
        waypoint order; order.mode=MOVE_WP; order.x=300; order.y=300;
        b->GetWayPointList().push_back(order); REQUIRE(!p.IsIdleUnit(b));
        p.SelectNextIdleUnit(); REQUIRE(p.select_info.selected_list[0]==a);
        a->GetWayPointDevList().push_back(order); REQUIRE(!p.IsIdleUnit(a));
        b->GetWayPointList().clear(); a->GetWayPointDevList().clear();
        a->SetAttackObject(enemy); REQUIRE(!p.IsIdleUnit(a)); a->SetAttackObject(nullptr);
        p.our_team=NULL_TEAM; REQUIRE(!p.IsIdleUnit(a)); p.our_team=RED_TEAM;
        factory->SetHealthPercent(100,p.zmap);
        REQUIRE(factory->ProductionStatus().find("tech")!=string::npos || factory->ProductionStatus().find("Tech")!=string::npos);
        p.unit_limit_reached[RED_TEAM]=true;
        REQUIRE(factory->ProductionStatus().find("Unit limit")!=string::npos);
        p.unit_limit_reached[RED_TEAM]=false; factory->SetHealthPercent(50,p.zmap);
        REQUIRE(factory->ProductionStatus().find("Damaged")!=string::npos);
        factory->SetOwner(NULL_TEAM); REQUIRE(factory->ProductionStatus().find("Capture")!=string::npos);
        factory->SetOwner(RED_TEAM);
        GWProduction::Init(); ZGuiButton::Init();
        GWProduction production(&p.ztime); production.SetType(GWPROD_ROBOT);
        production.SetMap(&p.zmap); production.SetBuildList(&p.buildlist); production.SetBuildingObj(factory); production.SetCords(320,200);
        production.Process(); SDL_FillSurfaceRect(screen,nullptr,0xff171b20);
        production.DoRender(p.zmap,screen); p.RenderSelectionReadout();
        REQUIRE(p.selection_readout_text.find("1 selected")!=string::npos);
        REQUIRE(IMG_SavePNG(screen,(image+".production.png").c_str()));

        zone_info_packet zone{}; zone.zone_number=0; zone.owner=BLUE_TEAM;
        p.set_zone_info_event(&p,(char*)&zone,sizeof(zone),0); REQUIRE(p.match_zones_captured==0);
        zone.owner=RED_TEAM; p.set_zone_info_event(&p,(char*)&zone,sizeof(zone),0); REQUIRE(p.match_zones_captured==1);
        p.set_zone_info_event(&p,(char*)&zone,sizeof(zone),0); REQUIRE(p.match_zones_captured==1);
        zone.zone_number=-1; p.set_zone_info_event(&p,(char*)&zone,sizeof(zone),0); REQUIRE(p.match_zones_captured==1);
        team_ended_packet result{RED_TEAM,false};
        p.team_ended_event(&p,(char*)&result,sizeof(result),0);
        REQUIRE(p.match_result_known && !p.match_won); // A surviving unit cannot override the server's loss.
        p.end_game_event(&p,nullptr,0,0); REQUIRE(!p.match_won && p.summary_pending);
        p.summary_pending=false; p.show_match_summary=true;
        p.match_duration=755; p.match_units_lost=12; p.match_enemies_killed=25; p.match_zones_captured=7;
        p.hosted_setup_available=true;
        p.hosted_setup.map="p02_bb_orig01.map"; p.hosted_setup.match.human_team=BLUE_TEAM;
        p.hosted_setup.match.difficulty=3; p.hosted_setup.match.game_speed=1.5; p.hosted_setup.bots=1u<<RED_TEAM;
        p.summary_lost_raw=ZSDL_DataIMG_Load("assets/summary_lost.png");
        p.BuildMatchSummary(); REQUIRE(p.summary_line_count==4);
        REQUIRE(p.MatchActionAt(5,5)==-1); REQUIRE(p.MatchActionAt(200,440)==1 && p.MatchActionAt(420,440)==2);
        p.ChooseMatchAction(-1); REQUIRE(p.show_match_summary);
        SDL_FillSurfaceRect(screen,nullptr,0xff171b20); p.RenderMatchSummary(); REQUIRE(IMG_SavePNG(screen,image.c_str()));
        p.ChooseMatchAction(2); REQUIRE(!p.show_match_summary && !p.gui_menu_list.empty());
        auto *menu=dynamic_cast<GMMMultiplayerCreate*>(p.gui_menu_list.front()); REQUIRE(menu);
        auto prefs=menu->GetSetupPreferences();
        REQUIRE(prefs.map==p.hosted_setup.map && prefs.match.human_team==BLUE_TEAM && prefs.match.difficulty==3 && prefs.match.game_speed==1.5);
        p.CloseAllMenus();
        // A rematch must use the finished session, even if preferences changed.
        SkirmishPreferences unrelated; unrelated.match.difficulty=0; REQUIRE(ZSkirmish_Save(unrelated));
        p.show_match_summary=true; p.summary_continue_sent=false;
        p.ChooseMatchAction(1); REQUIRE(!p.gui_menu_list.empty());
        menu=dynamic_cast<GMMMultiplayerCreate*>(p.gui_menu_list.front()); REQUIRE(menu);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!menu->GetGMMFlags().join_match && std::chrono::steady_clock::now()<deadline) { menu->Process(); SDL_Delay(2); }
        REQUIRE(menu->GetGMMFlags().join_match && menu->GetGMMFlags().join_team==BLUE_TEAM);
        prefs=menu->GetSetupPreferences();
        REQUIRE(prefs.map==p.hosted_setup.map && prefs.match.difficulty==3 && prefs.bots==(1u<<RED_TEAM));
        p.CloseAllMenus();
        p.hosted_setup.random_mode=true; p.hosted_setup.random.seed=UINT32_MAX;
        p.hosted_setup.random.players=3; p.hosted_setup.random.size=0; p.hosted_setup.random.terrain=2;
        p.show_match_summary=true; p.summary_continue_sent=false;
        p.ChooseMatchAction(1); menu=dynamic_cast<GMMMultiplayerCreate*>(p.gui_menu_list.front()); REQUIRE(menu);
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!menu->GetGMMFlags().join_match && std::chrono::steady_clock::now()<deadline) { menu->Process(); SDL_Delay(2); }
        REQUIRE(menu->GetGMMFlags().join_match);
        prefs=menu->GetSetupPreferences();
        REQUIRE(prefs.random_mode && prefs.random.seed==UINT32_MAX && prefs.random.players==3 && prefs.random.terrain==2);
        MatchInfo match; match.host=menu->GetGMMFlags().join_host; match.port=menu->GetGMMFlags().join_port;
        generated_download(match,ZSkirmish_RandomKey(p.hosted_setup.random));
        p.CloseAllMenus();
        p.hosted_setup_available=false; p.show_match_summary=true;
        REQUIRE(p.MatchActionAt(200,440)==0 && p.MatchActionAt(420,440)==1); REQUIRE(p.MatchActionAt(5,5)==-1);
        p.ChooseMatchAction(1); REQUIRE(p.show_match_summary); // A request never resets a guest world without server approval
        p.ResetMatchStats(); REQUIRE(p.match_zones_captured==0 && p.seen_zone_owners.empty());
        p.ols.DeleteAllObjects();
        ZSDL_Surface::SetMainSoftwareSurface(nullptr); SDL_DestroySurface(screen);
    }
};

class ZSkirmishRegression {
public:
    static void random_setup(const string &image,bool restore) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        ZFontEngine::Init(); ZGuiMainMenuBase::Init(); GMMWButton::Init(); GMMWList::Init();
        SDL_Surface *screen=SDL_CreateSurface(640,480,SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(screen); ZSDL_Surface::SetMainSoftwareSurface(screen);
        {
            GMMMultiplayerCreate menu;
            auto wait=[&]() {
                auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
                do { menu.Process(); SDL_Delay(2); }
                while((menu.maps_task.Pending() || menu.preview_task.Pending() || menu.generation_task.Pending() || menu.preview_at) && std::chrono::steady_clock::now()<deadline);
                menu.Process(); REQUIRE(!menu.Busy() && !menu.preview_task.Pending());
            };
            wait();
            if(!restore) {
                REQUIRE(!menu.random_mode);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.source_button); wait();
                REQUIRE(menu.random_mode && menu.preview.valid && !menu.random_dirty);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.filter_button); //3 players
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.size_button);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.size_button); //small
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.terrain_button);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.terrain_button); //arctic
                wait();REQUIRE(!menu.random_dirty&&menu.preview.terrain==2&&menu.preview.width==64&&menu.preview.teams.size()==3&&!menu.preview.details.empty());
                menu.seed_entry.SetText("4294967295"); menu.seed_entry.SetSelected(true); REQUIRE(menu.KeyPress(13)); wait();
                REQUIRE(menu.random.seed==UINT32_MAX && menu.preview.teams.size()==3 && menu.preview.width==64 && menu.preview.height==80);
                string path; REQUIRE(ZSkirmish_MapPath(menu.generated_key,path)); auto first=bytes(path);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.generate_button); wait(); REQUIRE(first==bytes(path));
                auto original=menu.random.seed;
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.reroll_button); wait(); REQUIRE(menu.random.seed!=original);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.size_button); menu.StartMatch(); REQUIRE(!menu.create_task.Pending());
                menu.seed_entry.SetText("4294967296"); menu.GenerateRandom(false); REQUIRE(!menu.generation_task.Pending());
                menu.random.size=0; menu.seed_entry.SetText("4294967295"); menu.GenerateRandom(false); wait();
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.human_button); REQUIRE(menu.options.human_team==BLUE_TEAM);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.bot_button[RED_TEAM-1]); REQUIRE(menu.bot_on[RED_TEAM]);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.difficulty_button);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.difficulty_button);
                menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.speed_button);
                menu.Remember();
            } else {
                REQUIRE(menu.random_mode && !menu.random_dirty && menu.preview.valid);
                REQUIRE(menu.options.human_team==BLUE_TEAM && menu.bot_on[RED_TEAM] && !menu.bot_on[BLUE_TEAM]);
                REQUIRE(menu.options.difficulty==3 && menu.speed_i==2);
                REQUIRE(menu.random.seed==UINT32_MAX && menu.random.players==3 && menu.random.size==0 && menu.random.terrain==2);
            }
            menu.SetCenterCoords(320,240); SDL_FillSurfaceRect(screen,nullptr,0xff171b20);
            ZMap background;REQUIRE(background.Read("maps/p02_bb_orig01.map")); menu.RenderContents(background,screen); REQUIRE(IMG_SavePNG(screen,image.c_str()));
            if(restore) {
                menu.StartMatch(); auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
                while(!menu.GetGMMFlags().join_match && std::chrono::steady_clock::now()<deadline) { menu.Process(); SDL_Delay(2); }
                REQUIRE(menu.GetGMMFlags().join_match); generated_download(menu.created_match,menu.generated_key);
            }
        }
        ZSDL_Surface::SetMainSoftwareSurface(nullptr); SDL_DestroySurface(screen);
    }

    static void run(const string &image) {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        ZFontEngine::Init(); ZGuiMainMenuBase::Init(); GMMWButton::Init(); GMMWList::Init();
        SDL_Surface *screen = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_ARGB8888);
        REQUIRE(screen); ZSDL_Surface::SetMainSoftwareSurface(screen);
        {
            GMMMultiplayerCreate menu;
            auto wait = [&]() {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
                do { menu.Process(); SDL_Delay(2); }
                while((menu.maps_task.Pending() || menu.preview_task.Pending()) && std::chrono::steady_clock::now() < deadline);
                menu.Process(); REQUIRE(!menu.maps_task.Pending() && !menu.preview_task.Pending());
            };
            wait(); REQUIRE(!menu.map_names.empty());
            bool found = false;
            for(auto &entry : menu.map_list.GetEntryList()) {
                bool selected = menu.map_names[entry.ref_id].name == "p02_bb_orig01.map";
                entry.state = selected ? MMLIST_PRESSED : MMLIST_NORMAL;
                found |= selected;
            }
            REQUIRE(found); menu.RefreshSelection(); wait();
            REQUIRE(menu.preview.valid && menu.preview.teams.size() == 2);
            REQUIRE(menu.preview_image.GetBaseSurface());
            string error;
            MatchOptions invalid;
            REQUIRE(!ZMP_ValidateSetup(menu.preview, {"red"}, invalid, error));
            invalid.human_team = GREEN_TEAM;
            REQUIRE(!ZMP_ValidateSetup(menu.preview, {}, invalid, error));
            invalid.human_team = BLUE_TEAM; invalid.game_speed = 0;
            REQUIRE(!ZMP_ValidateSetup(menu.preview, {}, invalid, error));
            invalid.game_speed = 1.5; invalid.difficulty = BOT_DIFF_EXPERT;
            REQUIRE(ZMP_ValidateSetup(menu.preview, {"red"}, invalid, error));
            REQUIRE(!ZMP_ValidateSetup(menu.preview, {"red", "red"}, invalid, error));
            auto teams = menu.preview.teams;
            menu.preview.teams.clear();
            for(int t = 1; t < MAX_TEAM_TYPES; ++t) menu.preview.teams.push_back(t);
            menu.UpdateLabels(); int width, height; menu.GetDimensions(width, height); REQUIRE(height <= 450);
            menu.preview.teams = teams; menu.UpdateLabels();
            menu.HandleWidgetEvent(GMM_UNCLICK_EVENT, &menu.human_button);
            REQUIRE(menu.options.human_team == BLUE_TEAM);
            menu.HandleWidgetEvent(GMM_UNCLICK_EVENT, &menu.bot_button[RED_TEAM - 1]);
            REQUIRE(menu.bot_on[RED_TEAM] && !menu.bot_on[BLUE_TEAM]);
            menu.HandleWidgetEvent(GMM_UNCLICK_EVENT, &menu.bot_button[GREEN_TEAM - 1]);
            REQUIRE(!menu.bot_on[GREEN_TEAM]); //team has no fort on this map
            menu.options.difficulty = BOT_DIFF_EXPERT; menu.speed_i = 2; menu.UpdateLabels();
            menu.SetCenterCoords(320, 240);
            SDL_FillSurfaceRect(screen, nullptr, 0xff171b20);
            ZMap background; menu.RenderContents(background, screen);
            REQUIRE(IMG_SavePNG(screen, image.c_str()));
            menu.StartMatch();
            REQUIRE(menu.status.find("Starting match") != string::npos);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            while(!menu.GetGMMFlags().join_match && std::chrono::steady_clock::now() < deadline) { menu.Process(); SDL_Delay(2); }
            REQUIRE(menu.GetGMMFlags().join_match && menu.GetGMMFlags().join_team == BLUE_TEAM);
            REQUIRE(menu.GetGMMFlags().join_port >= 2300);
        }
        ZSDL_Surface::SetMainSoftwareSurface(nullptr); SDL_DestroySurface(screen);
    }
};

class ZLANRegression {
public:
    static void portal_join(int port,const string &approvalFile,const string &expected){
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));ZMap::ServerInit();auto player=std::make_unique<ZPlayer>();auto &p=*player;
        p.LoadMainMenu(GMM_MULTIPLAYER);auto *menu=dynamic_cast<GMMMultiplayer*>(p.gui_menu_list.front());REQUIRE(menu);menu->open_approval=false;
        MatchInfo server;server.id="test";server.name="Portal test";server.host="127.0.0.1";server.port=port;server.portal=true;menu->QueueJoin(server);
        auto deadline=SDL_GetTicks()+30000;bool wrote=false;
        while(!p.zmap.Loaded()&&SDL_GetTicks()<deadline){
            p.ProcessSocketEvents();p.ehandler.ProcessEvents();p.ProcessGame();
            if(!wrote&&!p.gui_menu_list.empty())if(auto *browser=dynamic_cast<GMMMultiplayer*>(p.gui_menu_list.front()))if(!browser->auth_url.empty()){std::ofstream out(approvalFile);out<<browser->auth_url;wrote=true;}
            SDL_Delay(5);
        }
        REQUIRE(wrote&&p.zmap.Loaded());bool named=false;for(const auto &entry:p.player_info)if(entry.p_id==p.p_id)named=entry.name==expected;
        REQUIRE(named);player.reset();SDL_Quit();std::cout<<"native-portal-join-ok";
    }

    static void browser(const string &image = "") {
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        GMMMultiplayer menu;
        menu.address_entry.SetSelected(true);
        for(char c:string("127.0.0.1:2301")) menu.KeyPress(c);
        REQUIRE(menu.address_entry.GetText()=="127.0.0.1:2301");
        REQUIRE(menu.KeyPress(13));
        menu.Process(); menu.Process();
        REQUIRE(menu.GetGMMFlags().join_match && menu.GetGMMFlags().join_port==2301);
        menu.SetError("Connection refused"); REQUIRE(!menu.joining);
        GMMMultiplayer::RefreshResult result; result.ok=true;
        MatchInfo full; full.host="127.0.0.1"; full.port=2301; full.players=2; full.capacity=2; full.map="test.map";
        result.matches={full}; menu.ApplyRefresh(result);
        REQUIRE(!menu.match_list.GetEntryList()[0].disabled); // Full/active games permit spectator connections.
        full.players=1; full.started=true; result.matches={full}; menu.ApplyRefresh(result);
        REQUIRE(!menu.match_list.GetEntryList()[0].disabled); // Full/active games permit spectator connections.
        full.started=false; result.matches={full}; menu.ApplyRefresh(result);
        REQUIRE(!menu.match_list.GetEntryList()[0].disabled);
        full.region="Europe";full.game_mode="Co-op";full.ping_ms=45;result.matches={full};menu.ApplyRefresh(result);
        menu.search_entry.SetText("Co-op");menu.region_entry.SetText("Europe");menu.max_ping=100;menu.Refilter();REQUIRE(menu.match_list.GetEntryList().size()==1);
        menu.max_ping=25;menu.Refilter();REQUIRE(menu.match_list.GetEntryList().empty());menu.max_ping=0;
        menu.players_filter=3;menu.Refilter();REQUIRE(menu.match_list.GetEntryList().empty());menu.players_filter=0;
        ZServerToggleFavorite(full);REQUIRE(ZServerFavorite(full));menu.view=1;menu.Refilter();REQUIRE(menu.match_list.GetEntryList().size()==1);
        ZServerRemember(full);ZServerRemember(full);REQUIRE(ZServerBookmarks(true).size()==1);menu.view=2;menu.Refilter();REQUIRE(menu.match_list.GetEntryList().size()==1);
        ZServerToggleFavorite(full);REQUIRE(!ZServerFavorite(full));menu.view=0;menu.search_entry.SetText("");menu.region_entry.SetText("");menu.Refilter();
        {GMMMultiplayer recovery;recovery.SetReconnectTarget("127.0.0.1",2301,"retained-server");
         REQUIRE(recovery.has_reconnect_target&&!recovery.lan_mode&&recovery.reconnect_target.id=="retained-server");
         full.id="retained-server";full.port=2302;result.matches={full};recovery.ApplyRefresh(result);
         REQUIRE(recovery.reconnect_target.port==2302&&recovery.auth_device.empty());}
        menu.GetGMMFlags().clear();menu.HandleWidgetEvent(GMM_UNCLICK_EVENT,&menu.back_button);
        REQUIRE(menu.KillMe() && !menu.GetGMMFlags().open_main_menu);
        // Let the asynchronous scan release its own socket before process exit.
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(menu.refresh_task.Pending() && std::chrono::steady_clock::now()<deadline) { menu.Process(); SDL_Delay(5); }
        REQUIRE(!menu.refresh_task.Pending());
        if(!image.empty()) {
            REQUIRE(ZVideo_SetMode(1280,960,false));
            ZFontEngine::Init(); ZGuiMainMenuBase::Init(); GMMWButton::Init(); GMMWList::Init();
            SDL_Surface *screen=SDL_CreateSurface(640,480,SDL_PIXELFORMAT_ARGB8888);
            REQUIRE(screen); ZSDL_Surface::SetMainSoftwareSurface(screen);
            menu.sticky_error=false;
            full.host="192.168.1.44"; full.port=2300; full.map="p02_bb_orig01.map";
            MatchInfo busy=full; busy.host="192.168.1.51"; busy.port=2137; busy.players=2;
            result.matches={full,busy}; menu.ApplyRefresh(result);
            menu.address_entry.SetText("192.168.1.44:2300"); menu.address_entry.SetSelected(true);
            menu.SetCenterCoords(320,240);
            SDL_FillSurfaceRect(screen,nullptr,0xff171b20);
            auto *renderer=ZVideo_GetRenderer();ZMenuTheme::beginText(renderer);
            ZMap background;menu.DoRender(background,screen);ZMenuTheme::endText();
            ZVideo_SetCanvasMode(640,480);
            SDL_Texture *frame=SDL_CreateTextureFromSurface(renderer,screen);REQUIRE(frame);
            SDL_RenderTexture(renderer,frame,nullptr,nullptr);ZMenuTheme::presentText(renderer,640,480);
            SDL_Surface *output=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(output);
            REQUIRE(IMG_SavePNG(output,image.c_str()));SDL_DestroySurface(output);SDL_DestroyTexture(frame);
            ZSDL_Surface::SetMainSoftwareSurface(nullptr); SDL_DestroySurface(screen);
        }
    }
};
static void battle_menu(const string &image) {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO)); REQUIRE(ZVideo_SetMode(1280,800,false));
    ZFontEngine::Init();ZGuiMainMenuBase::Init();GMMWButton::Init();
    SDL_Surface *surface=SDL_CreateSurface(1280,800,SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(surface);ZSDL_Surface::SetMainSoftwareSurface(surface);
    SDL_FillSurfaceRect(surface,nullptr,0xff101a20);
    GMMMainMenu menu(false,true,true);menu.SetCenterCoords(640,400);menu.SetRenderScale(2);
    ZMap map;auto *renderer=ZVideo_GetRenderer();
    ZMenuTheme::beginText(renderer);menu.DoRender(map,surface);ZMenuTheme::endText();
    SDL_Texture *frame=SDL_CreateTextureFromSurface(renderer,surface);REQUIRE(frame);
    SDL_RenderTexture(renderer,frame,nullptr,nullptr);ZMenuTheme::presentText(renderer,1280,800);
    SDL_Surface *output=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(output);
    REQUIRE(IMG_SavePNG(output,image.c_str()));SDL_DestroySurface(output);SDL_DestroyTexture(frame);
    // Native menu is 164x253; scale pivots about its centre.
    int w,h;menu.GetDimensions(w,h);
    int top=400-h;
    menu.Click(640,top+2*(GMM_TITLE_MARGIN+7));menu.UnClick(640,top+2*(GMM_TITLE_MARGIN+7));
    REQUIRE(menu.KillMe());
    gmm_warning_flag flags;flags.quit_game=true;flags.text1="Are you sure you want";flags.text2="to quit the game?";
    for(double requested:{1.0,1.5,2.0,3.0})for(bool confirm:{false,true}){
        GMMWarning warning(flags);warning.SetCenterCoords(640,400);warning.SetRenderScale(requested);warning.GetDimensions(w,h);double scale=warning.GetRenderScale();
        SDL_Surface *line=ZMenuTheme::text(flags.text1.c_str(),11);REQUIRE(line&&line->w<=w-24&&line->h<=16);SDL_DestroySurface(line);
        SDL_FillSurfaceRect(surface,nullptr,0xff101a20);ZMenuTheme::beginText(renderer);warning.DoRender(map,surface);ZMenuTheme::endText();
        SDL_Texture *dialog=SDL_CreateTextureFromSurface(renderer,surface);REQUIRE(dialog);SDL_RenderTexture(renderer,dialog,nullptr,nullptr);ZMenuTheme::presentText(renderer,1280,800);SDL_DestroyTexture(dialog);
        if(requested==3.0&&confirm){SDL_Surface *capture=SDL_RenderReadPixels(renderer,nullptr);REQUIRE(capture);REQUIRE(IMG_SavePNG(capture,(image+".warning.png").c_str()));SDL_DestroySurface(capture);}
        int buttonX=(int)(640-w*scale/2+(confirm?150:50)*scale),buttonY=(int)(400-h*scale/2+87*scale);
        warning.Click(buttonX,buttonY);warning.UnClick(buttonX,buttonY);REQUIRE(warning.KillMe()&&warning.GetGMMFlags().quit_game==confirm);
    }
    ZSDL_Surface::SetMainSoftwareSurface(nullptr);SDL_DestroySurface(surface);SDL_Quit();
    std::cout<<"battle-menu-resume-ok";
}
static void dial_server(const string &host,int port) {
    string error;
    if(!ZNetCheckVersion(host,port,error,1500))throw std::runtime_error(error);
    auto socket=ZNetConnect(host,port,error,2000);
    if(!socket) throw std::runtime_error(error);
    list<Event*> events;
    ClientSocket client; client.SetEventList(&events);
    REQUIRE(client.Adopt(*socket));
    REQUIRE(client.SendMessage(REQUEST_PLAYER_ID,nullptr,0));
    bool received=false;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!received && std::chrono::steady_clock::now()<deadline) {
        client.Process();
        for(auto *event:events) { if(event->event_number==GIVE_PLAYER_ID && event->event_type==TCP_EVENT) received=true; delete event; }
        events.clear(); SDL_Delay(1);
    }
    REQUIRE(received); client.ClearConnection(); REQUIRE(!client.GetHandler());
}

static void verify_editor_server(const string &map,const string &id){ZServerRegression::verify_editor(map,id);}

static void feature_checks(const string &root){
 REQUIRE(SDL_Init(SDL_INIT_VIDEO));ZMap::ServerInit();string error;
 REQUIRE(ZCampaign_CompareVersion("0.6","0.6.0")==0);REQUIRE(ZCampaign_CompareVersion("1.2.0","1.10.0")<0);REQUIRE(ZCampaign_CompareVersion("bad","1")==-2);
 ZCampaign pack;pack.id="test";pack.title="Test";pack.version="1.0";pack.engineMin="99";REQUIRE(!ZCampaign_Compatibility(pack,{}).empty());pack.engineMin="0.1";pack.requires={"base@2"};REQUIRE(!ZCampaign_Compatibility(pack,{}).empty());ZCampaign base;base.id="base";base.version="2";REQUIRE(ZCampaign_Compatibility(pack,{base}).empty());base.requires={"test@1"};REQUIRE(!ZCampaign_Compatibility(pack,{base}).empty());
 ZMissionEvent event;REQUIRE(ZMission_ParseEvent("time|3|0|reinforce|1|5|0|8|8|2|Hello",event));REQUIRE(!ZMission_ParseEvent("time|-1|0|dialogue|1|5|0|8|8|2|bad",event));REQUIRE(ZMission_ParseEvent("time|3|0|reinforce|1|5|0|8|8|2|Hello",event));ZMissionRuntime runtime;runtime.events={event};ZMissionWorld world;world.time=10;REQUIRE(runtime.DueEvents(world).empty());world.time=13;REQUIRE(runtime.DueEvents(world).size()==1&&runtime.DueEvents(world).empty());runtime={};runtime.objectives={{"escort","Reach zone",0,2}};world.objectOwners={1};world.objectZones={1};vector<string> messages;REQUIRE(runtime.Update(world,messages)==0);world.objectZones[0]=2;REQUIRE(runtime.Update(world,messages)==1);runtime={};runtime.objectives={{"escort","Reach zone",0,2}};world.objectOwners[0]=-1;REQUIRE(runtime.Update(world,messages)==-1);
 zod_damage_alerts=zod_production_alerts=false;zod_alert_sounds=true;zod_alert_cooldown_ms=6500;ZPrefs_Save();zod_damage_alerts=zod_production_alerts=true;zod_alert_sounds=false;zod_alert_cooldown_ms=500;ZPrefs_Load();REQUIRE(!zod_damage_alerts&&!zod_production_alerts&&zod_alert_sounds&&zod_alert_cooldown_ms==6500);ZPrefs_ResetCategory(6);
 for(int i=0;i<4;++i){ZPrefs_ApplyPreset(i);REQUIRE(zod_access_preset==i);}ZPrefs_ResetCategory(5);REQUIRE(!zod_high_contrast&&zod_access_preset==-1);
 auto map=bytes("maps/p02_bb_orig01.map");vector<char> payload(map.size()+4);int index=0;memcpy(payload.data(),&index,4);memcpy(payload.data()+4,map.data(),map.size());string file=root+"/test.zreplay";ZReplayRecorder recorder;REQUIRE(recorder.Start(file,1));REQUIRE(recorder.Record(0,STORE_MAP,payload.data(),payload.size()));index=-1;REQUIRE(recorder.Record(1,STORE_MAP,(char*)&index,4));REQUIRE(recorder.Finish());ZReplayTape tape;REQUIRE(tape.Load(file,error)&&tape.packets.size()==2);auto intact=bytes(file);{std::ofstream partial(root+"/interrupted.zreplay.part",std::ios::binary);partial.write(intact.data(),intact.size());partial<<"short tail";}string recovered;REQUIRE(ZReplay_Recover(root+"/interrupted.zreplay.part",recovered,error));ZReplayTape restored;REQUIRE(restored.Load(recovered,error)&&restored.packets.size()==2);REQUIRE(!ZReplay_Recover(root+"/interrupted.zreplay.part",recovered,error));auto damaged=bytes(file);damaged.back()^=1;{std::ofstream out(file,std::ios::binary);out.write(damaged.data(),damaged.size());}REQUIRE(!tape.Load(file,error)&&tape.packets.size()==2);{std::ofstream partial(root+"/corrupt.zreplay.part",std::ios::binary);partial.write(damaged.data(),damaged.size());}REQUIRE(!ZReplay_Recover(root+"/corrupt.zreplay.part",recovered,error));REQUIRE(!ZReplay_Allowed(SET_NAME));REQUIRE(recorder.Start(root+"/partial.zreplay",1));REQUIRE(!recorder.Finish());SDL_Quit();std::cout<<"feature-checks-ok";
}

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    try {
        REQUIRE(argc >= 2);
        if(std::string(argv[1]) == "campaign-menu-resume"){ZServerRegression::campaign_menu_resume();}
        else if(std::string(argv[1]) == "objective-server"){ZServerRegression::objective_round();}
        else if(std::string(argv[1]) == "-d" || std::string(argv[1]) == "-private") {
            REQUIRE(argc >= 4 && std::string(argv[2]) == "-m");
            ZServer server;
            server.SetMapName(argv[3]); server.SetAllowRemoteClients(string(argv[1])=="-d");
            for(int i = 4; i + 1 < argc; ++i) if(string(argv[i]) == "-D") zod_bot_difficulty = atoi(argv[i + 1]);
            server.Setup();
            if(const char *path = getenv("ZOD_TEST_SETTINGS_FILE")) {
                std::ofstream settings(path); settings << zod_bot_difficulty << " " << (getenv("ZOD_GAMESPEED") ? getenv("ZOD_GAMESPEED") : "unset");
            }
            server.Run();
        }
        else if(std::string(argv[1]) == "palette-compatibility") {
            for(int height : {16,17}) {
                std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(
                    SDL_CreateSurface(2,height,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
                REQUIRE(surface);
                for(int row=0;row<height;++row) {
                    SDL_Rect a{0,row,1,1}, b{1,row,1,1};
                    SDL_FillSurfaceRect(surface.get(),&a,SDL_MapRGBA(SDL_GetPixelFormatDetails(surface->format),nullptr,row<16?row+1:0,0,0,255));
                    SDL_FillSurfaceRect(surface.get(),&b,SDL_MapRGBA(SDL_GetPixelFormatDetails(surface->format),nullptr,0,20+row,0,255));
                }
                ZTeam_Palette palette{}; REQUIRE(palette.LoadSurfacePalette(surface.get()));
                Uint8 red=16,green=0,blue=0;
                SDL_Color colour{}; REQUIRE(palette.GetReplacement(red,green,blue,colour));
                REQUIRE(colour.r==0 && colour.g==35 && colour.b==0);
                red=green=blue=0; REQUIRE(!palette.GetReplacement(red,green,blue,colour));
            }
            std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> invalid(
                SDL_CreateSurface(2,15,SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
            ZTeam_Palette palette{}; REQUIRE(!palette.LoadSurfacePalette(invalid.get()));
            std::cout<<"palette-compatible";
        }
        else if(std::string(argv[1]) == "import-levels"){REQUIRE(argc==3);string message;REQUIRE(ZAsset_ImportLevels(argv[2],message));REQUIRE(!ZAsset_OverlayPath("maps/p02_bb_orig01.map").empty());std::cout<<"levels-imported";}
        else if(std::string(argv[1]) == "next-features"){REQUIRE(argc==3);ZFrontEndRegression::next_features(argv[2]);}
        else if(std::string(argv[1]) == "display-matrix"){REQUIRE(argc==3);ZFrontEndRegression::display_matrix(argv[2]);}
        else if(std::string(argv[1]) == "content-health"){REQUIRE(argc==3);REQUIRE(SDL_Init(0));ZAsset_RecordDiagnostic(ZDiagnosticEvent::DeploymentFailed);std::string report;REQUIRE(ZAsset_ExportDiagnostics(report,argv[2]));std::cout<<report;SDL_Quit();}
        else if(std::string(argv[1]) == "asset-ready") { std::cout<<(ZAsset_RuntimeReady()?"ready":"missing"); }
        else if(std::string(argv[1]) == "records-preview"||std::string(argv[1]) == "gallery-preview"||std::string(argv[1]) == "campaigns-preview"||std::string(argv[1]) == "settings-preview"||std::string(argv[1]) == "timeline-preview"){REQUIRE(argc==3);ZFrontEndRegression::polish_preview(argv[1],argv[2]);}
        else if(std::string(argv[1]) == "multiplayer-reconnect"){REQUIRE(argc==3);ZPlayerUIRegression::multiplayer_reconnect(std::stoi(argv[2]));}
        else if(std::string(argv[1]) == "reconnect-failure"){REQUIRE(argc==3);ZPlayerUIRegression::reconnect_failure(std::stoi(argv[2]));}
        else if(std::string(argv[1]) == "objectives-summary"){ZPlayerUIRegression::objectives_and_summary();}
        else if(std::string(argv[1]) == "polish"){REQUIRE(argc==3);ZFrontEndRegression::polish(argv[2]);}
        else if(std::string(argv[1]) == "enhancements"){REQUIRE(argc==3);ZFrontEndRegression::enhancements(argv[2]);}
        else if(std::string(argv[1]) == "movie"){REQUIRE(argc==3);ZFrontEndRegression::movie(argv[2]);}
        else if(std::string(argv[1]) == "content-polish"){REQUIRE(argc==3);ZFrontEndRegression::content_polish(argv[2]);}
        else if(std::string(argv[1]) == "inspect-pack"){REQUIRE(argc==3);ZCampaign summary;string error;bool ok=ZCampaign_InspectPack(argv[2],summary,error);std::cout<<(ok?"pack-ok":error);return ok?0:2;}
        else if(std::string(argv[1]) == "campaign-audio") {REQUIRE(argc==3);ZFrontEndRegression::campaigns_and_audio(argv[2]);}
        else if(std::string(argv[1]) == "preview-palette") ZFrontEndRegression::imported_preview_palette();
        else if(std::string(argv[1]) == "workspace" || std::string(argv[1]) == "workspace-preview") { REQUIRE(argc==3);ZFrontEndRegression::workspace(argv[2],std::string(argv[1])=="workspace"); }
        else if(std::string(argv[1]) == "battle-menu") { REQUIRE(argc==3);battle_menu(argv[2]); }
        else if(std::string(argv[1]) == "disc-menu" || std::string(argv[1]) == "disc-menu-preview" || std::string(argv[1]) == "menu-options-preview") {
            REQUIRE(argc==3);ZFrontEndRegression::disc_menu(argv[2],std::string(argv[1])=="disc-menu",std::string(argv[1])=="menu-options-preview");
        }
        else if(std::string(argv[1]) == "asset-bootstrap") {
            REQUIRE(SDL_Init(SDL_INIT_VIDEO));REQUIRE(ZVideo_SetMode(1280,800,false));
            {ZFrontEnd frontend;frontend.ShowImport(true);frontend.Render();}
            std::cout<<"import-screen-rendered";SDL_Quit();
        }
        else if(std::string(argv[1]) == "asset-helper") { REQUIRE(argc==3); std::cout<<ZAsset_FindHelper(argv[2]); }
        else if(std::string(argv[1]) == "asset-path") { REQUIRE(argc==3); std::cout<<ZAsset_OverlayPath(argv[2]); }
        else if(std::string(argv[1]) == "offline-policy") { ZServerRegression::offline_policy(); }
        else if(std::string(argv[1]) == "address") {
            REQUIRE(argc==3); string host,error; int port;
            if(!ZMP_ParseAddress(argv[2],host,port,error)) { std::cerr<<error; return 2; }
            std::cout<<host<<"\n"<<port;
        }
        else if(std::string(argv[1]) == "discover") {
            vector<int> ports; for(int i=2;i<argc;++i) ports.push_back(atoi(argv[i]));
            vector<MatchInfo> matches; string error;
            REQUIRE(ZLAN_Discover(matches,error,350,{"127.0.0.1"},ports));
            std::cout<<"["; bool comma=false;
            for(const auto &m:matches) {
                if(comma) std::cout<<","; comma=true;
                std::cout<<"{\"host\":"<<std::quoted(m.host)<<",\"port\":"<<m.port<<",\"players\":"<<m.players<<",\"capacity\":"<<m.capacity<<",\"started\":"<<(m.started?"true":"false")<<"}";
            }
            std::cout<<"]";
        }
        else if(std::string(argv[1]) == "dial") { REQUIRE(argc==4); dial_server(argv[2],atoi(argv[3])); }
        else if(std::string(argv[1]) == "browser") ZLANRegression::browser(argc>2 ? argv[2] : "");
        else if(std::string(argv[1]) == "portal-join"){REQUIRE(argc==5);ZLANRegression::portal_join(atoi(argv[2]),argv[3],argv[4]);}
        else if(std::string(argv[1]) == "portal-request") {REQUIRE(argc==3||argc==4);auto response=ZPortal_Request(argv[2],argc==4?argv[3]:"");if(!response.ok){std::cerr<<response.error;return 2;}std::cout<<response.status<<"\n"<<response.body;}
        else if(std::string(argv[1]) == "list-online") {
            vector<MatchInfo> matches; REQUIRE(ZMP_ListMatches(matches));
            std::cout<<"[";bool comma=false;for(const auto &m:matches){if(comma)std::cout<<",";comma=true;std::cout<<"{\"id\":"<<std::quoted(m.id)<<",\"started\":"<<(m.started?"true":"false")<<"}";}std::cout<<"]";
        }
        else if(std::string(argv[1]) == "persistent-round") { ZServerRegression::persistent_round(); }
        else if(std::string(argv[1]) == "cloud-report") { REQUIRE(argc==4); ZServerRegression::cloud_report(argv[2],argv[3]); }
        else if(std::string(argv[1]) == "lobby") { REQUIRE(argc==4); ZLobbyRegression::run(argv[2],argv[3]); }
        else if(std::string(argv[1]) == "menu-navigation") ZPlayerUIRegression::menu_navigation();
        else if(std::string(argv[1]) == "match-ui") { REQUIRE(argc==3); ZPlayerUIRegression::run(argv[2]); }
        else if(std::string(argv[1]) == "random-module") { REQUIRE(argc==3); random_module_tests(argv[2]); }
        else if(std::string(argv[1]) == "random-prepare" || std::string(argv[1]) == "random-restore") { REQUIRE(argc==3); ZSkirmishRegression::random_setup(argv[2],string(argv[1])=="random-restore"); }
        else if(std::string(argv[1]) == "skirmish") { REQUIRE(argc == 3); ZSkirmishRegression::run(argv[2]); }
        else if(std::string(argv[1]) == "local-host") { REQUIRE(argc == 3); local_match(argv[2]); }
        else if(std::string(argv[1]) == "movement"){REQUIRE(argc==3);ZServerRegression::movement(argv[2]);}
        else if(std::string(argv[1]) == "recovery"){REQUIRE(argc==3);ZServerRegression::recovery(argv[2]);}
        else if(std::string(argv[1]) == "replay-feedback"){REQUIRE(argc==3);ZPlayerUIRegression::replay_feedback(argv[2]);}
        else if(std::string(argv[1]) == "authoring-tools"){REQUIRE(argc==3);ZFrontEndRegression::authoring_tools(argv[2]);}
        else if(std::string(argv[1]) == "feature-checks"){REQUIRE(argc==3);feature_checks(argv[2]);}
        else if(std::string(argv[1]) == "script-save"){REQUIRE(argc==3);ZServerRegression::script_save(argv[2]);}
        else if(std::string(argv[1]) == "stress"){REQUIRE(argc==5);ZServerRegression::scripted_stress(argv[2],atoi(argv[3]),atoi(argv[4]));}
        else if(std::string(argv[1]) == "save") { REQUIRE(argc == 3); ZServerRegression::roundtrip(argv[2]); }
        else if(std::string(argv[1]) == "packets") packets();
        else if(std::string(argv[1]) == "protocol") {
#define EMIT(value) std::cout << "\"" #value "\":" << (int)value << ",";
            std::cout << "{";
            EMIT(SET_LPLAYER_NAME) EMIT(CLOUD_SERVER_ROLE) EMIT(SET_GAME_SPEED) EMIT(UPDATE_GAME_SPEED) EMIT(CLOUD_REQUIRED) EMIT(CLOUD_TICKET) EMIT(CLOUD_RESULT) EMIT(REQUEST_VERSION) EMIT(GIVE_VERSION) EMIT(SET_LPLAYER_READY) EMIT(LOBBY_COUNTDOWN) EMIT(RESET_MAP) EMIT(RESET_GAME) EMIT(REQUEST_MAP) EMIT(STORE_MAP) EMIT(REQUEST_OBJECTS) EMIT(ADD_NEW_OBJECT)
            EMIT(REQUEST_ZONES) EMIT(SET_ZONE_INFO) EMIT(REQUEST_PLAYER_ID) EMIT(GIVE_PLAYER_ID)
            EMIT(SET_READY) EMIT(SET_LPLAYER_TEAM) EMIT(SET_LPLAYER_MODE) EMIT(REQUEST_PLAYER_LIST) EMIT(BOT_MODE) EMIT(SET_NAME) EMIT(SET_TEAM) EMIT(SET_PLAYER_MODE) EMIT(PLAYER_MODE)
            EMIT(GET_GAME_PAUSED) EMIT(UPDATE_GAME_PAUSED) EMIT(SET_GAME_PAUSED)
            EMIT(REQUEST_SETTINGS) EMIT(SET_SETTINGS) EMIT(RED_TEAM) EMIT(BLUE_TEAM)
            std::cout << "\"object_init_size\":" << sizeof(object_init_packet) << "}";
        } else REQUIRE(false);
        return 0;
    } catch(const std::exception &error) { std::cerr << error.what() << "\n"; return 1; }
}
