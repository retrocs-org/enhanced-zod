#ifndef _ZFRONTEND_H_
#define _ZFRONTEND_H_

// ZFrontEnd - command console with shared launch and battle-menu materials.
// Imported WARDATA/MANE.LBM supplies an optional subdued backdrop. Operations,
// saved battles, settings and import controls are drawn and handled by code.
//
// Enhancements over the original menu: Load Game resumes the campaign where you
// left off, Replay Level is a planet-by-planet level picker that shows locked /
// beaten levels and your best score, a High Score table, mission briefing cards
// with a radar display ("Scenes"), live gamma, fullscreen, volume, music, bot
// difficulty, mouse mode, render smoothing and unit limit, plus full keyboard
// navigation.
//
// It is an immediate-mode UI: ZPlayer feeds it raw SDL events and calls Render()
// once per frame while no map is loaded; anything that needs the game client
// (start a map, open a multiplayer window, change volume, quit) is returned as a
// queued Action for ZPlayer to carry out.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include "zmap_workspace.h"
#include "zcampaign.h"
#include "zportrait.h"

#include "zaudio.h"
#include "zasset_import.h"

class ZFrontEnd
{
public:
	enum ActionType
	{
		FE_NONE, FE_PLAY_RECORDING,
		FE_START_CAMPAIGN_MAP,
        FE_START_MAP,        //value = campaign map index
		FE_LOAD_SAVE,        //restore a saved battle; value = slot
		FE_START_MAP_FILE,   //text = path of a custom map to play
		FE_OPEN_SKIRMISH,    //open the random-map generator window
		FE_OPEN_MULTIPLAYER, //open the multiplayer browser window
		FE_SET_VOLUME,       //value = sound_setting index
		FE_SET_MUSIC,        //value = 0/1
		FE_NAME_CHANGED,     //zod_player_name was edited
		FE_QUIT
	};

	struct Action
	{
		ActionType type;
		int value;
		std::string text;
		Action(ActionType t = FE_NONE, int v = 0) : type(t), value(v) {}
	};

	ZFrontEnd();
	~ZFrontEnd();

	// call when the menu (re)appears: refreshes campaign progress and scores
	void Show();
	void ShowImport(bool required = false);
    void ReviewDiagnostics(bool inGame=false);
    bool DiagnosticOverlay() const{return visible&&screen==SCR_DIAGNOSTICS&&diagnostic_return_game;}
	// the map we asked for has arrived (or the menu is otherwise leaving)
	void Hide();
	bool Visible() const { return visible; }

	//original-game cutscenes (assets/videos), only used when the clips are present
	void PlayLevelIntro(int index);
	void PlayLevelResult(bool won);
    void RecordCampaignResult(bool won,int seconds=-1,int kills=0,int lost=0,int difficulty=1);
    bool CommunityBattle() const {return community_running;}
    void LoadCheckpoint();
    void OpenRecordings();void RecordingError(const std::string &message);
    bool EditorTesting() const {return editor_testing;}
    void PrepareEditorReturn(bool advance){editor_continue_pending=editor_testing&&workspace&&!workspace->TestCampaign().root.empty()&&advance&&community_next>=0;}
    void ContinueCommunity();
    void ResumeSavedCampaign(const std::string &map,const std::string &id="",int mission=-1,const std::string &uid="");

	// true while waiting for a requested map to load (shows the loading card)
	bool Loading() const { return screen == SCR_LOADING; }
    void DeploymentFailed(const std::string &message,bool force=false);
    void RetryDeployment();
    void DeploymentStage(const std::string &message){if(Loading())deployment_stage=message;}

	void HandleEvent(const SDL_Event &ev);
	void Render();

	bool PopAction(Action &out);

	// window size changed etc. - drop GPU resources so they are rebuilt
	void InvalidateGraphics();

	static const int CANVAS_W = 1280;
	static const int CANVAS_H = 800;

private:
	friend struct ZFrontEndRegression;
	enum Screen { SCR_MAIN, SCR_REPLAY, SCR_SCORES, SCR_BRIEFING, SCR_LOADING, SCR_QUIT, SCR_LOAD, SCR_CUSTOM, SCR_KEYS, SCR_IMPORT, SCR_SETTINGS, SCR_EDITOR, SCR_CAMPAIGNS, SCR_RECORDINGS, SCR_ALERTS, SCR_MULTIPLAYER_GUIDE, SCR_BUILD_INFO, SCR_DIAGNOSTICS, SCR_SINGLE_PLAYER, SCR_MULTIPLAYER_MENU };

	struct CustomMap
	{
		std::string file;    //file name inside the custom maps folder
		std::string title;
		int planet, w, h;
		int teams;           //teams that have a fort
		bool red_fort;       //the player's side has a fort
		bool readable;
	};

	struct LevelInfo
	{
		std::string file;    //as written in map_list.txt
		std::string base;    //file name without directory
		std::string title;   //map name stored in the map, or "Mission N"
		int planet;          //DESERT .. CITY
		int w, h;
	};

	struct TextTex
	{
		SDL_Texture *tex;
		float w, h;          //size in canvas units
		Uint64 last_used;
	};

	struct Font
	{
		std::string file;
		std::map<int, TTF_Font*> sizes;   //by pixel size (already scaled)
	};

	ZAssetImport asset_import;
	bool import_only = false;
	int focus_button=-1,focus_count=0,focus_index=0;bool focus_activate=false;Screen focus_screen=SCR_MAIN;
    int campaign_dependency_scroll=0,campaign_backup_choice=0;
    std::string recordingDetails,recordingError;int recordingInspected=-1;
    std::vector<std::string> recordingFiles;int recordingChoice=0,recordingScroll=0;
    void RenderDiagnostics();std::string diagnostic_preview,diagnostic_message;int diagnostic_scroll=0,diagnostic_return_screen=SCR_SETTINGS;bool diagnostic_return_game=false;
    void RenderRecordings();void RenderAlerts();void RenderMultiplayerGuide();void RenderBuildInfo();std::string build_info_message;bool guide_completed_session=false;int multiplayer_guide_step=0;Screen guide_return=SCR_MAIN;
    void RenderImport();

	// ---- state ----
	bool visible;
	bool initialised;
	Screen screen;
	Screen screen_before_quit;
	std::vector<Action> actions;

	float mouse_x, mouse_y;
	bool mouse_down;
	float press_x, press_y;
	bool clicked;              //a full click happened this frame
	float click_x, click_y;
	float wheel;               //accumulated wheel this frame

	int kb_item;               //keyboard-highlighted main item (-1 = mouse)
	bool kb_activate;          //Enter pressed this frame
	bool kb_back;              //Esc pressed this frame
	bool kb_delete;            //Delete key pressed this frame
	int kb_dx, kb_dy;          //arrow keys this frame

	bool editing_name;
	std::string name_edit;
	bool name_fresh;            //editing just started: typing replaces the old name

	int settings_scroll;
    int settings_category=0;
    int audio_drag=-1;
    bool editor_fullscreen_previous=false,editor_testing=false,editor_continue_pending=false;
    ZCampaign editor_previous_campaign;
    std::vector<LevelInfo> editor_previous_levels;int editor_previous_level=-1,editor_previous_current=-1,editor_previous_unlocked=-1;
    ZCampaign selected_campaign;
    std::vector<ZCampaign> campaigns;
    std::vector<LevelInfo> original_levels;
    bool community_running=false,mission_replay=false;
    int campaign_mission_choice=-1,campaign_mission_scroll=0,campaign_previous_choice=-2;bool campaign_mission_focus=false,history_briefing=false;
    std::vector<int> CampaignVisibleMissions(const ZCampaign *pack,int unlocked) const;
    std::string CampaignMoviePath(const ZCampaign *pack,int index,bool victory) const;
    void OpenHistoryBriefing(int index);
    int community_next=-1,campaign_scroll=0,briefing_scroll=0,campaign_choice=-1,settings_focus=0;
    SDL_Texture *campaign_cover=nullptr;std::string campaign_cover_path;
    std::string campaign_pack_path;ZCampaign campaign_pack_summary;bool campaign_import_preview=false,campaign_manage=false,campaign_records=false,campaign_gallery=false;int gallery_scroll=0;
    std::string campaign_error;
    ZPortrait campaign_portrait;
    void ScanCampaigns();void SelectCampaign(int index);void RenderCampaigns();void RenderCommunityBriefing();
    std::unique_ptr<ZMapWorkspace> workspace;
	int hover_item;            //main item under the mouse
	std::string info_text;     //help readout

	int replay_planet;
	int replay_selected;       //campaign index
	int briefing_level;
	int current_level;      //campaign level being played, -1 if none (custom map, save, multiplayer)
	Uint64 loading_since;
	Action deployment_action;bool deployment_failed=false,has_deployment_retry=false;
    std::string status_message,deployment_stage="Connecting to the game server";
	Uint64 status_until;

	// campaign data
	std::vector<LevelInfo> levels;
	int progress_current;      //level Load Game resumes (-1 = none)
	int progress_unlocked;     //highest unlocked index

	// saved battles: the autosave and the player's slots
	struct SaveSlot
	{
		int slot;bool recovery=false;
        std::string map,campaign;
		std::string label;     //"Autosave" / "Slot 2"
		std::string title;     //"Desert - Level 04 Desert Islands"
		std::string detail;    //"12:34 played - Normal - saved 16 Nov 16:40"
	};
	bool have_save;
	std::vector<SaveSlot> saves;   //only slots that hold a save
	int load_selected;             //row in the Load Game list
	int delete_pending;            //save slot waiting for "delete?" confirmation, -1 = none

	// key bindings screen
	int keys_scroll=0;
    int keys_selected;             //row highlighted with the keyboard
	int keys_waiting;              //action waiting for its new key, -1 = none

	// custom maps
	std::string custom_dir;
	std::vector<CustomMap> custom_maps;
	int custom_selected, custom_scroll;
	Uint64 custom_scanned;
	bool newmap_open;
	bool editing_mapname;
	std::string newmap_name;
	int newmap_planet, newmap_size;

	// ---- graphics ----
	SDL_Renderer *renderer;
	float scale;               //canvas -> pixels
	SDL_Texture *target;       //canvas-sized render target
	SDL_Texture *background;   //static procedural console
	SDL_Texture *glow;         //soft radial blob for lamps / LEDs
	SDL_Texture *title_tex;    //embossed title plate lettering
	SDL_Texture *disc_background = nullptr;
	std::map<unsigned char, TextTex> disc_glyphs;
	int disc_save_scroll = 0;
	int tex_w, tex_h;
	Font font_ui, font_ui_medium, font_led;
	std::map<std::string, TextTex> text_cache;

	ZAudio_Sound *snd_hover;
	ZAudio_Sound *snd_click;
	ZAudio_Sound *snd_toggle;
	ZAudio_Sound *snd_start;

	// ---- setup ----
	void Init();
	void LoadLevels();
	void LoadProgress();
	bool EnsureGraphics();
	void FreeGraphics();
	void BuildBackground();
	void BuildTitle();
	void BuildGlow();

	// ---- drawing helpers (canvas units) ----
	TTF_Font *GetFont(Font &f, float size);
	TextTex *Text(Font &f, float size, const std::string &text, SDL_Color c);
	float TextWidth(Font &f, float size, const std::string &text);
	void DrawLabel(Font &f, float size, const std::string &text, float x, float y, SDL_Color c, int align = 0);
	void DrawGlow(float cx, float cy, float radius, SDL_Color c, float alpha);
	void FillRect(float x, float y, float w, float h, SDL_Color c, Uint8 a = 255);
	void FrameRect(float x, float y, float w, float h, SDL_Color c, Uint8 a = 255, float t = 1.0f);
	void DrawPanel(float x, float y, float w, float h, float alpha = 1.0f);
	void DimScreen(Uint8 a);
	bool Hover(float x, float y, float w, float h) const;
	bool Clicked(float x, float y, float w, float h);
	bool Button(const std::string &label, float x, float y, float w, float h, bool enabled = true, bool primary = false);
	void Sound(ZAudio_Sound *s);

	// ---- screens ----
	void RenderMain(bool interactive);
    void RenderPlayModes(bool multiplayer);void OpenCampaignGroup(int scope);
    int campaign_scope=0,mode_choice=0;bool browser_return_pending=false;
	void RenderCommandMain(bool interactive);
	void RenderControlPanel(bool interactive);
	int settings_return_screen=SCR_MAIN,import_return_screen=SCR_MAIN;
    void RenderOptions();
	TextTex &DiscGlyph(unsigned char character);
	void DrawDiscText(const std::string &text, float x, float y, SDL_Color colour, int align = 0);
	void RenderSettings(bool interactive);
	void RenderReplay();
	void RenderScores();
	void RenderBriefing();
	void RenderLoading();
	void RenderQuit();
	SDL_Texture *map_preview_texture=nullptr;std::string map_preview_path;
    void DrawMapPreview(const std::string &path,float x,float y,float w,float h);
    void RenderLoad();
	void RenderKeys();
	void CaptureKey(int keycode);
	void RenderCustom();
	void RenderNewMap();
	void LoadSaveInfo();
	void ScanCustomMaps(bool force);
	std::string NewMapFileName() const;
	bool LaunchEditor(const std::vector<std::string> &args);
	void RenderStatus();

	void ActivateItem(int item);
	void StartLevel(int index, bool via_briefing);
	void CommitName();

	int  SettingCount() const;
	std::string SettingLabel(int i) const;
	std::string SettingHelp(int i) const;
	void AdjustSetting(int i, int dir, int absolute = -1000);

	bool LevelUnlocked(int i) const;
	std::string PlanetName(int p) const;
	std::string LevelLabel(int i) const;
};

#endif
