#ifndef _ZGMM_MULTIPLAYER_CREATE_H_
#define _ZGMM_MULTIPLAYER_CREATE_H_
#include "zgui_main_menu_base.h"
#include "zmpclient.h"
#include "zmp_async.h"
#include "zskirmish.h"

class GMMMultiplayerCreate : public ZGuiMainMenuBase
{
public:
    GMMMultiplayerCreate(const SkirmishPreferences *setup = nullptr, bool rematch = false);
    SkirmishPreferences GetSetupPreferences() { Remember(); return preferences; }
    ~GMMMultiplayerCreate() override;
    bool KeyPress(int c) override;
    void Process() override;
    bool UnClick(int px, int py) override { return (Busy()) && WithinDimensions(px, py) ? true : ZGuiMainMenuBase::UnClick(px, py); }
    void RenderContents(ZMap &map, SDL_Surface *dest) override;
    void SetError(const string &message);
private:
    friend class ZSkirmishRegression;
    GMMWList map_list;
    GMMWButton source_button, generate_button, reroll_button, size_button, terrain_button, tech_button, vehicles_button;
    GMMWTextEntry seed_entry;
    GMMWLabel seed_hint;
    GMMWButton filter_button, human_button, difficulty_button, speed_button;
    GMMWButton bot_button[MAX_TEAM_TYPES - 1], start_button, back_button;
    GMMWLabel map_label, size_label, slots_label, status_label, error_label;
    bool bot_on[MAX_TEAM_TYPES] = {};
    MatchOptions options;
    SkirmishPreferences preferences;
    RandomMapOptions random;
    bool random_mode = false, random_dirty = true, auto_start = false;
    string generated_key;
    Uint64 remember_at = 0,preview_at=0;
    struct GeneratedResult { MapPreview preview; string recipe; };
    ZMPAsync<GeneratedResult> generation_task;
    bool Busy() const { return create_task.Pending() || generation_task.Pending() || connect_next_frame; }
    void GenerateRandom(bool new_seed);
    void ApplyPreview(MapPreview loaded);
    void Remember();
    string StockMap();
    vector<MapMeta> map_names;
    vector<int> filter_options;
    int filter_i = 0, speed_i = 1;
    bool connect_next_frame = false, sticky_error = false;
    string selected_preview, status;
    MapPreview preview;
    ZSDL_Surface preview_image;
    struct MapsResult { bool ok = false; vector<MapMeta> maps; };
    struct CreateResult { bool ok = false; MatchInfo match; string error; };
    ZMPAsync<MapsResult> maps_task;
    ZMPAsync<MapPreview> preview_task;
    ZMPAsync<CreateResult> create_task;
    MatchInfo created_match;
    void SetupLayout1();
    void BuildFilters();
    void PopulateMapList();
    void RefreshSelection();
    void UpdateLabels();
    void StartMatch();
    void HandleWidgetEvent(int type, ZGMMWidget *widget) override;
    string SelectedMap();
};
#endif
