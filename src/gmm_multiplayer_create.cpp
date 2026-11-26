#include "gmm_multiplayer_create.h"
#include "zprefs.h"
#include <cstdio>
#include <algorithm>
#include <random>

namespace {
const double speeds[] = {0.5, 1.0, 1.5, 2.0};
string shorten(const string &text, size_t length) { return text.size() <= length ? text : text.substr(0, length - 3) + "..."; }
}
GMMMultiplayerCreate::GMMMultiplayerCreate(const SkirmishPreferences *setup, bool rematch, bool offline)
{
    offline=offline||(setup&&setup->match.local_only);offline_mode=offline;
    menu_type = GMM_MULTIPLAYER_CREATE;
    title = "Create Match"; w = 450; h = 348;
    preferences=setup ? *setup : ZSkirmish_Load(zod_bot_difficulty);
    auto_start=rematch;
    options=preferences.match; random=preferences.random; random_mode=preferences.random_mode;
    options.local_only=offline;
    if(offline&&preferences.map.empty()&&!preferences.bots)preferences.bots=1u<<BLUE_TEAM;
    if(offline) {title="Play against AI";options.team_difficulties.resize(MAX_TEAM_TYPES,options.difficulty);}
    else options.team_difficulties.clear();
    for(int t=1;t<MAX_TEAM_TYPES;++t) bot_on[t]=(preferences.bots&(1u<<t))!=0;
    for(int i=0;i<4;++i) if(speeds[i]==options.game_speed) speed_i=i;
    filter_options.push_back(0);
    SetupLayout1(); status = "Loading maps...";
    maps_task.Start([offline]() { MapsResult r; r.ok = ZMP_ListMaps(r.maps,offline); return r; });
    if(random_mode) GenerateRandom(false);
}
GMMMultiplayerCreate::~GMMMultiplayerCreate() { Remember(); }
void GMMMultiplayerCreate::Remember()
{
    string stock=StockMap(); if(!stock.empty()) preferences.map=stock;
    preferences.random_mode=random_mode; preferences.random=random;
    auto difficulties=preferences.match.team_difficulties;
    preferences.match=options;if(!offline_mode)preferences.match.team_difficulties=difficulties; preferences.match.game_speed=speeds[speed_i];
    preferences.bots=0;
    for(int t=1;t<MAX_TEAM_TYPES;++t) if(bot_on[t] && t!=options.human_team) preferences.bots|=1u<<t;
    if(!ZSkirmish_Save(preferences) && !Busy()) SetError("Could not remember setup choices; check your user folder");
    remember_at=0;
}
bool GMMMultiplayerCreate::KeyPress(int c)
{
    if(Busy()) return true;
    bool enter=random_mode && seed_entry.IsSelected() && c==13;
    bool result=ZGuiMainMenuBase::KeyPress(c);
    if(enter) GenerateRandom(false);
    return enter || result;
}
void GMMMultiplayerCreate::GenerateRandom(bool new_seed)
{
    if(Busy()) return;
    preview_at=0;
    if(!ZSkirmish_ParseSeed(seed_entry.GetText(),random.seed)) { SetError("Seed must be a whole number from 0 to 4294967295"); return; }
    if(new_seed) {
        std::random_device entropy; auto seed=(std::uint32_t)entropy();
        if(seed==random.seed) ++seed;
        random.seed=seed; seed_entry.SetText(to_string(seed));
    }
    random_dirty=true; sticky_error=false; error_label.SetText(""); status="Generating random-map preview...";
    seed_entry.TakeChanged();
    auto recipe=random;
    generation_task.Start([recipe]() {
        GeneratedResult result; result.recipe=ZSkirmish_RandomKey(recipe);
        ZSkirmish_Generate(recipe,result.preview); return result;
    });
    Remember(); UpdateLabels();
}
void GMMMultiplayerCreate::ApplyPreview(MapPreview loaded)
{
    preview=std::move(loaded); preview_image.Unload();
    if(preview.valid) {
        if(find(preview.teams.begin(),preview.teams.end(),options.human_team)==preview.teams.end()) options.human_team=preview.teams[0];
        for(int t=1;t<MAX_TEAM_TYPES;++t) if(t==options.human_team || find(preview.teams.begin(),preview.teams.end(),t)==preview.teams.end()) bot_on[t]=false;
        SDL_Surface *surface=SDL_CreateSurface(preview.preview_width,preview.preview_height,SDL_PIXELFORMAT_ARGB8888);
        if(surface) {
            for(int row=0;row<surface->h;++row) memcpy((char*)surface->pixels+row*surface->pitch,preview.pixels.data()+row*surface->w,surface->w*4);
            preview_image.LoadBaseImage(surface);
        }
        Remember();
    }
}
void GMMMultiplayerCreate::SetupLayout1()
{
    auto button = [&](GMMWButton &b, int px, int py, int width) {
        b.SetType(MMGENERIC_BUTTON); b.SetCoords(px, py); b.SetDimensions(width, 18); AddWidget(&b);
    };
    button(source_button,10,24,110); button(filter_button,126,24,112);
    seed_entry.SetCoords(10,48); seed_entry.SetDimensions(228,18); seed_entry.SetLabel("Seed: ");
    seed_entry.SetDigitsOnly(true); seed_entry.SetMaxText(10); seed_entry.SetText(to_string(random.seed)); AddWidget(&seed_entry);
    button(generate_button,10,70,110); generate_button.SetText("Preview seed");
    button(reroll_button,128,70,110); reroll_button.SetText("New seed");
    button(size_button,10,92,228); button(terrain_button,10,114,228);
    button(tech_button,10,136,110); button(vehicles_button,128,136,110);
    seed_hint.SetCoords(10,158); seed_hint.SetText("Preview updates automatically"); AddWidget(&seed_hint);
    map_list.SetCoords(10, 46); map_list.SetDimensions(228, 125); map_list.SetVisibleEntries(8); AddWidget(&map_list);
    auto label = [&](GMMWLabel &l, int px, int py) { l.SetCoords(px, py); AddWidget(&l); };
    label(map_label, 250, 155); label(size_label, 250, 171);
    button(human_button, 10, 192, 228);
    button(difficulty_button, 250, 192, 190);
    for(int team = 1; team < MAX_TEAM_TYPES; ++team) button(bot_button[team - 1], 10, 215 + (team - 1) * 20, 228);
    button(speed_button, 250, 215, 190);
    label(slots_label, 250, 239);
    button(start_button, 250, 260, 190); button(back_button, 250, 282, 190);
    back_button.SetText("Back");
    label(status_label, 10, 310); label(error_label, 10, 327);
    UpdateLabels(); UpdateDimensions();
}
void GMMMultiplayerCreate::BuildFilters()
{
    filter_options = {0};
    for(const auto &map : map_names) if(map.players > 0 && find(filter_options.begin(), filter_options.end(), map.players) == filter_options.end()) filter_options.push_back(map.players);
    sort(filter_options.begin(), filter_options.end()); filter_i = 0; PopulateMapList();
}
void GMMMultiplayerCreate::PopulateMapList()
{
    map_list.GetEntryList().clear();
    for(size_t i = 0; i < map_names.size(); ++i) {
        if(filter_options[filter_i] && map_names[i].players != filter_options[filter_i]) continue;
        string title = map_names[i].name;
        if(title.size() > 4 && title.substr(title.size() - 4) == ".map") title.resize(title.size() - 4);
        map_list.GetEntryList().push_back(mmlist_entry(shorten(title, 29) + "  (" + to_string(map_names[i].players) + "P)", (int)i, (int)i));
    }
    for(auto &entry:map_list.GetEntryList()) if(map_names[entry.ref_id].name==preferences.map) entry.state=MMLIST_PRESSED;
    map_list.CheckViewI(); selected_preview.clear(); RefreshSelection();
}
string GMMMultiplayerCreate::StockMap()
{
    auto &entries = map_list.GetEntryList();
    if(entries.empty()) return "";
    int selected = map_list.GetFirstSelected(); if(selected < 0) selected = 0;
    if(selected >= (int)entries.size()) return "";
    int index = entries[selected].ref_id;
    return index >= 0 && index < (int)map_names.size() ? map_names[index].name : "";
}
string GMMMultiplayerCreate::SelectedMap() { return random_mode ? generated_key : StockMap(); }
void GMMMultiplayerCreate::RefreshSelection()
{
    if(random_mode) return;
    string map = SelectedMap();
    if(map == selected_preview || preview_task.Pending()) return;
    selected_preview = map; preview = MapPreview(); preview_image.Unload();
    if(!map.empty()) {
        if(!sticky_error) { status = "Loading map preview..."; error_label.SetText(""); }
        preview_task.Start([map]() { MapPreview result; ZMP_ReadMapPreview(map, result); return result; });
    }
}
void GMMMultiplayerCreate::SetError(const string &message)
{
    connect_next_frame=false; gmm_flags.join_match=false;
    sticky_error = true; status = shorten(message, 73);
    error_label.SetText(message.size() > 73 ? shorten(message.substr(73), 73) : "");
}
void GMMMultiplayerCreate::UpdateLabels()
{
    source_button.SetText(random_mode ? "Browse levels" : "Generate random");
    filter_button.SetText(random_mode ? "Players: "+to_string(random.players) : filter_options[filter_i] ? to_string(filter_options[filter_i])+" players" : "All sizes");
    map_list.SetActive(!random_mode);
    for(ZGMMWidget *widget:std::vector<ZGMMWidget*>{&seed_entry,&generate_button,&reroll_button,&size_button,&terrain_button,&tech_button,&vehicles_button,&seed_hint}) widget->SetActive(random_mode);
    const char *sizes[]={"Small (64 x 80)","Medium (80 x 100)","Large (112 x 140)"};
    size_button.SetText(string("Size: ")+sizes[random.size]);
    terrain_button.SetText("Terrain: "+planet_type_string[random.terrain]);
    tech_button.SetText("Tech: "+to_string(random.tech));
    vehicles_button.SetText(random.vehicles<0 ? "Vehicles: auto" : "Vehicles: "+to_string(random.vehicles));
    human_button.SetText("You: " + team_type_string[options.human_team]);
    human_button.SetGreen(true);
    difficulty_button.SetText(string("Difficulty: ") + bot_difficulty_name[options.difficulty]);
    speed_button.SetText("Game speed: " + to_string((int)(speeds[speed_i] * 100)) + "%");
    int bots = 0, row = 0;
    for(int team = 1; team < MAX_TEAM_TYPES; ++team) {
        bool available = !preview.valid || find(preview.teams.begin(), preview.teams.end(), team) != preview.teams.end();
        bot_button[team - 1].SetActive(available && !SelectedMap().empty());
        if(available) bot_button[team - 1].SetCoords(10, 215 + row++ * 20);
        string value = !available ? "no fort" : team == options.human_team ? "you" : bot_on[team] ? (offline_mode?string(bot_difficulty_name[options.team_difficulties[team]]):string("bot")) : "open";
        bot_button[team - 1].SetText(team_type_string[team] + ": " + value);
        bot_button[team - 1].SetGreen(bot_on[team] || team == options.human_team);
        bots += bot_on[team] ? 1 : 0;
    }
    int new_height = 348 + std::max(0, row - 4) * 20;
    y += (h - new_height) / 2; h = new_height;
    status_label.SetCoords(10, h - 38); error_label.SetCoords(10, h - 21); UpdateDimensions();
    int slots = preview.valid ? (int)preview.teams.size() : 0;
    for(const auto &map : map_names) if(!slots && map.name == SelectedMap()) slots = map.players;
    slots_label.SetText(slots ? to_string(slots) + " slots: you + " + to_string(bots) + (bots == 1 ? " bot" : " bots") : "Choose a map");
    map_label.SetText(random_mode && random_dirty ? "Preview needs refresh" : preview.valid ? shorten(preview.title.empty() ? preview.name : preview.title, 29) : preview_task.Pending() ? "Loading preview..." : "Preview unavailable");
    size_label.SetText(preview.valid ? to_string(preview.width) + " x " + to_string(preview.height) + " - " + planet_type_string[preview.terrain] : shorten(preview.error, 29));
    bool busy = Busy();
    start_button.SetText(generation_task.Pending() ? "Generating map..." : busy ? "Starting match..." : "Create and Join");
    status_label.SetText(status + ((create_task.Pending() || generation_task.Pending()) ? string((SDL_GetTicks() / 400) % 4, '.') : ""));
}
void GMMMultiplayerCreate::Process()
{
    if(connect_next_frame) {
        gmm_flags.join_match = true; gmm_flags.join_host = created_match.host;
        gmm_flags.join_port = created_match.port; gmm_flags.join_team = options.human_team;
    }
    MapsResult maps;
    if(maps_task.Poll(maps)) {
        map_names = std::move(maps.maps); BuildFilters();
        if(!sticky_error && !random_mode) status = maps.ok ? "Choose a map and teams" : "Could not load maps. Go Back and retry.";
    }
    MapPreview loaded;
    if(preview_task.Poll(loaded) && !random_mode && loaded.name==SelectedMap()) {
        ApplyPreview(std::move(loaded));
        if(!sticky_error) status=preview.valid ? "Choose opponents, then Create and Join" : "Preview unavailable; server will validate the map";
    }
    GeneratedResult generated;
    if(generation_task.Poll(generated) && random_mode) {
        if(generated.recipe==ZSkirmish_RandomKey(random) && generated.preview.valid) {
            generated_key=generated.preview.name; random_dirty=false; ApplyPreview(std::move(generated.preview));
            if(!sticky_error) status=preview.details+". Ready for LAN play.";
        } else if(generated.recipe!=ZSkirmish_RandomKey(random))preview_at=SDL_GetTicks()+250;else SetError(generated.preview.error.empty() ? "Generation failed. Preview again to retry." : generated.preview.error);
    }
    if(random_mode && seed_entry.TakeChanged()) {
        random_dirty=true; remember_at=SDL_GetTicks()+400;
        std::uint32_t seed;
        if(ZSkirmish_ParseSeed(seed_entry.GetText(),seed)) random.seed=seed;
        status="Updating preview...";preview_at=SDL_GetTicks()+450;
    }
    if(preview_at&&SDL_GetTicks()>=preview_at&&!Busy()){preview_at=0;if(random_mode)GenerateRandom(false);}
    if(remember_at && SDL_GetTicks()>=remember_at) Remember();
    RefreshSelection();
    if(auto_start && !maps_task.Pending() && !preview_task.Pending() && !generation_task.Pending()) {
        auto_start=false;
        StartMatch();
    }
    CreateResult result;
    if(create_task.Poll(result)) {
        if(result.ok) { created_match = result.match; connect_next_frame = true; status = "Starting match... connecting to server"; }
        else SetError(result.error.empty() ? "Could not start match. Check the server and retry." : result.error);
    }
    UpdateLabels(); ProcessWidgets();
}
void GMMMultiplayerCreate::RenderContents(ZMap &map, SDL_Surface *dest)
{
    ZGuiMainMenuBase::RenderContents(map, dest);
    if(!(random_mode&&random_dirty) && preview_image.GetBaseSurface()) preview_image.BlitSurface(nullptr, x + 250 + (190 - preview.preview_width) / 2, y + 44 + (108 - preview.preview_height) / 2);
}
void GMMMultiplayerCreate::StartMatch()
{
    string map = SelectedMap();
    if(random_mode && (random_dirty || generation_task.Pending())) { SetError("Preview the current random-map settings before starting"); return; }
    if(map.empty() || (!random_mode && (maps_task.Pending() || preview_task.Pending()))) { SetError("Choose a map and wait for its preview"); return; }
    vector<string> bots;
    for(int t = 1; t < MAX_TEAM_TYPES; ++t) if(bot_on[t]) bots.push_back(team_type_string[t]);
    for(const auto &entry : map_names) if(entry.name == map && entry.players > 0 && bots.size() + 1 > (size_t)entry.players) {
        SetError("Too many opponents for this map"); return;
    }
    if(offline_mode&&bots.empty()){SetError("Choose at least one AI opponent by clicking its team row");return;}
    options.game_speed = speeds[speed_i];
    string error;
    if(!ZMP_ValidateSetup(preview, bots, options, error)) { SetError(error); return; }
    sticky_error = false; error_label.SetText(""); status = "Starting match... launching server";
    Remember();
    MatchOptions settings = options;
    create_task.Start([map, bots, settings]() {
        CreateResult result; result.ok = ZMP_CreateMatch(map, bots, result.match, settings); result.error = ZMP_LastError(); return result;
    });
    UpdateLabels();
}
void GMMMultiplayerCreate::HandleWidgetEvent(int type, ZGMMWidget *widget)
{
    if(!widget || Busy()) return;
    if(type == GMM_CLICK_EVENT && widget == &map_list) {
        sticky_error = false; error_label.SetText("");
        int selected = map_list.GetGMMWFlags().mmlist_entry_selected;
        if(selected >= 0) map_list.UnSelectAll(selected);
        preferences.map=StockMap(); Remember();
        return;
    }
    if(type != GMM_UNCLICK_EVENT && type != GMM_WHEELUP_EVENT && type != GMM_WHEELDOWN_EVENT) return;
    sticky_error = false; error_label.SetText("");
    if(widget==&source_button) {
        random_mode=!random_mode;
        if(random_mode) GenerateRandom(false);
        else { selected_preview.clear(); preview=MapPreview(); preview_image.Unload(); RefreshSelection(); }
    }
    else if(widget==&generate_button) GenerateRandom(false);
    else if(widget==&reroll_button) GenerateRandom(true);
    else if(widget==&filter_button && random_mode) { random.players=random.players%4+1; if(random.players<2) random.players=2; random_dirty=true; }
    else if(widget==&size_button) { random.size=(random.size+1)%3; random_dirty=true; }
    else if(widget==&terrain_button) { random.terrain=(random.terrain+1)%5; random_dirty=true; }
    else if(widget==&tech_button) { random.tech=(random.tech+1)%6; random_dirty=true; }
    else if(widget==&vehicles_button) { const int choices[]={-1,0,2,6}; for(int i=0;i<4;++i) if(random.vehicles==choices[i]) { random.vehicles=choices[(i+1)%4]; break; } random_dirty=true; }
    else if(widget == &filter_button) { filter_i = (filter_i + 1) % filter_options.size(); PopulateMapList(); }
    else if(widget == &human_button) {
        for(int i = 0; i < MAX_TEAM_TYPES; ++i) {
            options.human_team = options.human_team % (MAX_TEAM_TYPES - 1) + 1;
            if(!preview.valid || find(preview.teams.begin(), preview.teams.end(), options.human_team) != preview.teams.end()) break;
        }
        bot_on[options.human_team] = false;
    }
    else if(widget == &difficulty_button) {options.difficulty = (options.difficulty + 1) % MAX_BOT_DIFFICULTY;if(offline_mode)std::fill(options.team_difficulties.begin(),options.team_difficulties.end(),options.difficulty);}
    else if(widget == &speed_button) speed_i = (speed_i + 1) % 4;
    else if(widget == &start_button) StartMatch();
    else if(widget == &back_button) { if(offline_mode)killme=true;else {gmm_flags.open_main_menu = true; gmm_flags.open_main_menu_type = GMM_MULTIPLAYER;} }
    else for(int t = 1; t < MAX_TEAM_TYPES; ++t) if(widget == &bot_button[t - 1] && t != options.human_team
             && (!preview.valid || find(preview.teams.begin(), preview.teams.end(), t) != preview.teams.end())) {if(!offline_mode)bot_on[t]=!bot_on[t];else if(!bot_on[t]){bot_on[t]=true;options.team_difficulties[t]=0;}else if(options.team_difficulties[t]<MAX_BOT_DIFFICULTY-1)++options.team_difficulties[t];else bot_on[t]=false;}
    if(random_mode && random_dirty && !generation_task.Pending() && !sticky_error){status="Updating preview...";preview_at=SDL_GetTicks()+350;}
    Remember(); RefreshSelection(); UpdateLabels();
}
