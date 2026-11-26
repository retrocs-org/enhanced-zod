#include <cmath>
// Saving and restoring a single-player battle, and starting a map from any
// file (custom maps). See zsavegame.h for what a save contains.

#include "zserver.h"
#include "zsavegame.h"
#include "zcampaign.h"
#include "zprefs.h"
#include "zpath_debug.h"

#include <time.h>

using namespace COMMON;

namespace
{
	struct saved_object
	{
		int ot, oid, x, y, owner, level, extra_links, health, direction;
		int ref_id, leader_ref_id;
		int driver_type;
		vector<int> driver_health;
		int grenades;
		vector<waypoint> waypoints, rallypoints;
		vector<char> build_state, build_queue, built_cannons;
	};

	string temp_path(const char *name)
	{
		const char *tmp = getenv("TMPDIR");
		if(!tmp || !tmp[0]) tmp = getenv("TEMP");
		if(!tmp || !tmp[0]) tmp = getenv("TMP");
		if(tmp && tmp[0]) return string(tmp) + "/" + name;
#ifdef _WIN32
		return name;
#else
		return string("/tmp/") + name;
#endif
	}

	void write_waypoints(ZSaveWriter &w, vector<waypoint> &list)
	{
		w.Int((int)list.size());
		for(vector<waypoint>::iterator i=list.begin(); i!=list.end(); ++i)
		{
			w.Int(i->mode);
			w.Int(i->ref_id);
			w.Int(i->x);
			w.Int(i->y);
			w.Int(i->attack_to ? 1 : 0);
			w.Int(i->player_given ? 1 : 0);
		}
	}

	bool read_waypoints(ZSaveReader &r, vector<waypoint> &list)
	{
		int n = r.Int();
		if(!r.Ok() || n < 0 || n > 10000) return false;
		for(int i=0;i<n;i++)
		{
			waypoint wp;
			wp.mode = (char)r.Int();
			wp.ref_id = r.Int();
			wp.x = r.Int();
			wp.y = r.Int();
			wp.attack_to = r.Int() != 0;
			wp.player_given = r.Int() != 0;
			list.push_back(wp);
		}
		return r.Ok();
	}

	void write_packet(ZSaveWriter &w, char *data, int size)
	{
		w.Blob(data, data ? size : 0);
		if(data) free(data);
	}
}

// Start a battle on any map file (the Custom Maps screen). The human keeps
// their team; every other team that has a fort on the map gets a bot.
void ZServer::StartMapFile(string path, int player)
{
	if(allow_remote_clients) return;   //local single-player only

	{
		ZMap probe;
		if(!probe.Read(path.c_str()))
		{
			SendNews(player, ("could not read map " + path).c_str(), 0, 0, 0);
			return;
		}
	}

	DoResetGame(path);

	//a one-off map: go back to the menu when it ends instead of walking a list
	playing_generated_map = true;
	game_repro_desc = "custom map: " + path;

	FillEmptySlotsWithBots();

	BroadCastNews("custom map loaded");
}

bool ZServer::SaveGame(int player, int kills, int lost, int elapsed, int slot)
{
	//the autosave is the game's own doing: it never talks to the player
	bool quiet = slot == ZSAVE_AUTO_SLOT||slot==ZSAVE_CHECKPOINT_SLOT;

	if(slot < ZSAVE_AUTO_SLOT || slot > ZSAVE_CHECKPOINT_SLOT) return false;

	if(allow_remote_clients)
	{
		if(!quiet) SendNews(player, "saving is only available in single-player games", 0, 0, 0);
		return false;
	}
	if(!game_on || !zmap.GetMapBasics().width)
	{
		if(!quiet) SendNews(player, "there is no battle in progress to save", 0, 0, 0);
		return false;
	}

	ZSaveWriter w;

	ZSaveInfo info;
	info.map_name = map_name;info.campaign_id=mission_campaign_id;info.mission_index=mission_campaign_index;info.session_seed=session_seed;for(const auto &pack:ZCampaign_List())if(pack.id==mission_campaign_id&&mission_campaign_index>=0&&mission_campaign_index<(int)pack.missions.size())info.mission_uid=pack.missions[mission_campaign_index].uid;
	{
		char title[sizeof(zmap.GetMapBasics().map_name) + 1];
		memcpy(title, zmap.GetMapBasics().map_name, sizeof(zmap.GetMapBasics().map_name));
		title[sizeof(zmap.GetMapBasics().map_name)] = 0;
		info.map_title = title;
	}
	info.one_off_map = playing_generated_map;
	info.campaign_index = -1;
	if(!playing_generated_map && !load_maps_randomly)
		for(int i=0;i<(int)map_list.size();i++)
			if(map_list[i] == map_name) { info.campaign_index = i; break; }
	info.elapsed = elapsed;
	info.kills = kills;
	info.lost = lost;
	info.difficulty = zod_bot_difficulty;
    for(int t=0;t<MAX_TEAM_TYPES;++t)info.team_difficulties.push_back(bot[t].Difficulty());
	info.when = (long long)time(NULL);
	ZSave_WriteInfo(w, info);

	//the map exactly as it was loaded (tiles + zones)
	{
		char *data = NULL;
		int size = 0;
		zmap.GetMapData(data, size);
		if(!data || size <= 0)
		{
			if(!quiet) SendNews(player, "could not save: no map data", 0, 0, 0);
			return false;
		}
		w.Blob(data, size);
	}

	//which teams are run by a bot
	for(int t=0;t<MAX_TEAM_TYPES;t++) w.Int(bot_thread[t] ? 1 : 0);

	w.Int(next_ref_id);

	//objects, in list order (the list is sorted by ref id and must stay so)
	int count = 0;
	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); ++i)
	{
		unsigned char ot, oid;
		(*i)->GetObjectID(ot, oid);
		//dead units are only still listed until the server sweeps them
		if(ot != BUILDING_OBJECT && (*i)->IsDestroyed()) continue;
		count++;
	}
	w.Int(count);

	for(vector<ZObject*>::iterator i=object_list.begin(); i!=object_list.end(); ++i)
	{
		ZObject *o = *i;
		unsigned char ot, oid;
		int x, y;

		o->GetObjectID(ot, oid);
		if(ot != BUILDING_OBJECT && o->IsDestroyed()) continue;
		o->GetCords(x, y);

		w.Int(ot);
		w.Int(oid);
		w.Int(x);
		w.Int(y);
		w.Int(o->GetOwner());
		w.Int(o->GetLevel());
		w.Int(o->GetExtraLinks());
		w.Int(o->GetHealth());
		w.Int(o->GetDirection());
		w.Int(o->GetRefID());
		w.Int(o->GetGroupLeader() ? o->GetGroupLeader()->GetRefID() : -1);

		w.Int(o->GetDriverType());
		vector<driver_info_s> &drivers = o->GetDrivers();
		w.Int((int)drivers.size());
		for(size_t d=0; d<drivers.size(); d++) w.Int(drivers[d].driver_health);

		w.Int(o->GetGrenadeAmount());

		write_waypoints(w, o->GetWayPointList());
		write_waypoints(w, o->GetRallyPointList());

		//factories: what is being built and how far along, the queue, stored cannons
		char *data;
		int size;
		if(ot == BUILDING_OBJECT)
		{
			o->CreateBuildingStateData(data, size);  write_packet(w, data, size);
			o->CreateBuildingQueueData(data, size);  write_packet(w, data, size);
			o->CreateBuiltCannonData(data, size);    write_packet(w, data, size);
		}
		else
		{
			w.Blob(NULL, 0);
			w.Blob(NULL, 0);
			w.Blob(NULL, 0);
		}
	}

    w.Str("zod-ee_MISSION1");w.Int(mission_runtime.objectives.size());
    for(auto &o:mission_runtime.objectives)w.Str(ZMission_Encode(o));
    w.Int(mission_runtime.step);w.Double(!mission_runtime.started?-1:std::max(0.0,ztime.ztime-mission_runtime.stepStarted));w.Int(mission_runtime.announced?1:0);
	w.Str("zod-ee_SCRIPT2");w.Int(mission_runtime.events.size());for(auto &event:mission_runtime.events)w.Str(ZMission_EncodeEvent(event));for(size_t i=0;i<mission_runtime.events.size();++i)w.Int(i<mission_runtime.fired.size()&&mission_runtime.fired[i]);w.Double(mission_runtime.missionStarted<0?-1:ztime.ztime-mission_runtime.missionStarted);w.Int(mission_object_refs.size());for(int ref:mission_object_refs)w.Int(ref);
	if(!w.WriteFile(ZSave_Path(slot)))
	{
		ZDiag("save FAILED: could not write %s", ZSave_Path(slot).c_str());
		if(!quiet) SendNews(player, ("could not write " + ZSave_Path(slot)).c_str(), 0, 0, 0);
		return false;
	}

	ZDiag("game saved to slot %d: %s, %d objects, %ds played", slot, map_name.c_str(), count, elapsed);
	if(!quiet)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "game saved in slot %d - Load Game in the main menu returns to it", slot);
		SendNews(player, msg, 0, 0, 0);
	}
	return true;
}

bool ZServer::LoadSavedGame(int player, int slot)
{
	if(allow_remote_clients) return false;
	if(slot < ZSAVE_AUTO_SLOT || slot > ZSAVE_CHECKPOINT_SLOT) return false;

	//---- read and validate everything before touching the running world ----
	ZSaveReader r;
	ZSaveInfo info;
	if(!r.ReadFile(ZSave_Path(slot)) || !ZSave_ReadInfo(r, info))
	{
		SendNews(player, "there is no saved game to load", 0, 0, 0);
		return false;
	}

	vector<char> map_blob = r.Blob();
	bool bot_team[MAX_TEAM_TYPES];
	for(int t=0;t<MAX_TEAM_TYPES;t++) bot_team[t] = r.Int() != 0;
	int saved_next_ref_id = r.Int();
	int count = r.Int();

	vector<saved_object> objs;
	bool good = r.Ok() && (info.team_difficulties.empty()||info.team_difficulties.size()==MAX_TEAM_TYPES) && !map_blob.empty() && count >= 0 && count < 100000;
	for(int i=0; good && i<count; i++)
	{
		saved_object s;
		s.ot = r.Int(); s.oid = r.Int(); s.x = r.Int(); s.y = r.Int();
		s.owner = r.Int(); s.level = r.Int(); s.extra_links = r.Int();
		s.health = r.Int(); s.direction = r.Int();
		s.ref_id = r.Int(); s.leader_ref_id = r.Int();
		s.driver_type = r.Int();
		int nd = r.Int();
		if(!r.Ok() || nd < 0 || nd > 64) { good = false; break; }
		for(int d=0;d<nd;d++) s.driver_health.push_back(r.Int());
		s.grenades = r.Int();
		if(!read_waypoints(r, s.waypoints) || !read_waypoints(r, s.rallypoints)) { good = false; break; }
		s.build_state = r.Blob();
		s.build_queue = r.Blob();
		s.built_cannons = r.Blob();

		if(!r.Ok() || s.ot < 0 || s.ot >= MAX_MAP_OBJECT_TYPES || s.owner < 0 || s.owner >= MAX_TEAM_TYPES ||
			(i && s.ref_id <= objs.back().ref_id))
			good = false;
		else
			objs.push_back(s);
	}

    std::vector<int> savedRefs;double eventAge=-1;ZMissionRuntime saved_mission;double saved_step_age=-1;bool has_mission_state=r.Remaining()>0;
    if(good&&has_mission_state){
        good=r.Str()=="zod-ee_MISSION1";int count=r.Int();good=good&&count>=0&&count<=64;
        for(int i=0;good&&i<count;++i){ZMissionObjective o;good=ZMission_Parse(r.Str(),o);if(good)saved_mission.objectives.push_back(o);}
        saved_mission.step=r.Int();saved_step_age=r.Double();saved_mission.announced=r.Int()!=0;
        good=good&&r.Ok()&&saved_mission.step>=0&&saved_mission.step<=(int)saved_mission.objectives.size()&&std::isfinite(saved_step_age)&&saved_step_age>=-1&&saved_step_age<=864000;
        if(good&&r.Remaining()){good=r.Str()=="zod-ee_SCRIPT2";int count=r.Int();good=good&&count>=0&&count<=64;for(int i=0;good&&i<count;++i){ZMissionEvent event;good=ZMission_ParseEvent(r.Str(),event);if(good)saved_mission.events.push_back(event);}for(int i=0;good&&i<count;++i){int fired=r.Int();good=fired==0||fired==1;saved_mission.fired.push_back(fired!=0);}eventAge=r.Double();int refs=r.Int();good=good&&std::isfinite(eventAge)&&eventAge>=-1&&eventAge<=864000&&refs>=0&&refs<=100000;for(int i=0;good&&i<refs;++i)savedRefs.push_back(r.Int());}good=good&&r.Ok()&&r.Remaining()==0;
    }
	//the map goes through a file: that is how ZMap keeps the bytes it later
	//sends to clients
	string map_file = temp_path("zod_savegame.map");
	if(good)
	{
		FILE *fp = fopen(map_file.c_str(), "wb");
		good = fp && fwrite(&map_blob[0], 1, map_blob.size(), fp) == map_blob.size();
		if(fp) fclose(fp);
	}
	if(good)
	{
		ZMap probe;
		good = probe.Read(map_file.c_str()) != 0;
	}

	if(!good)
	{
		SendNews(player, "the saved game is damaged and could not be loaded", 0, 0, 0);
		ZDiag("load game FAILED: bad save file %s", ZSave_Path(slot).c_str());
		return false;
	}

	//---- rebuild the world (mirrors LoadNextMap) ----
	menu_first = false;user_continue_requested=false;
	playing_generated_map = info.one_off_map;
	max_units_per_team = zod_max_units_per_team;
	reload_same_map = false;
	do_reset_game = false;
	end_return_to_menu = false;
	start_pause_at_ztime = -1;

	ResetGame();

	map_fingerprint.clear();zmap.Read(map_file.c_str());

	map_name = info.map_name;
	game_repro_desc = info.one_off_map ? "custom map: " + map_name : map_name;   //high scores key on this
	if(!info.one_off_map && !load_maps_randomly &&
		info.campaign_index >= 0 && info.campaign_index < (int)map_list.size())
	{
		current_map_i = info.campaign_index;
		if(current_map_i > max_unlocked_i) max_unlocked_i = current_map_i;
		SaveCampaignProgress();
	}

	zod_bot_difficulty = info.difficulty;
    bot_difficulty_settings=info.team_difficulties;if(bot_difficulty_settings.empty())bot_difficulty_settings.assign(MAX_TEAM_TYPES,info.difficulty);
    for(int t=0;t<MAX_TEAM_TYPES;++t)bot[t].SetDifficulty(bot_difficulty_settings[t]);

	//objects, with their original ref ids so saved orders still point at the
	//right targets
	vector<ZObject*> made(objs.size(), (ZObject*)NULL);
	for(size_t i=0;i<objs.size();i++)
	{
		saved_object &s = objs[i];

		ZObject *o = CreateObject(s.ot, s.oid, s.x, s.y, s.owner, NULL, s.level, s.extra_links, 100);
		if(!o) continue;

		o->SetRefID(s.ref_id);
		o->SetDirection(s.direction);

		//whoever was at the wheel
		o->ClearDrivers();
		o->SetDriverType(s.driver_type);
		for(size_t d=0; d<s.driver_health.size(); d++) o->AddDriver(s.driver_health[d]);

		if(o->CanHaveGrenades()) o->SetGrenadeAmount(s.grenades);

		o->SetHealth(s.health, zmap);

		made[i] = o;
	}
	next_ref_id = saved_next_ref_id;

	//squads
	for(size_t i=0;i<objs.size();i++)
	{
		if(!made[i] || objs[i].leader_ref_id < 0) continue;
		ZObject *leader = GetObjectFromID(objs[i].leader_ref_id, object_list);
		if(!leader || leader == made[i]) continue;
		leader->AddGroupMinion(made[i]);
		made[i]->SetGroupLeader(leader);
	}

	MakeAllFortTurretsUnEjectable();
	buildlist.SetMapHasBridges(MapHasBridges());

	//zones follow the flags / forts as they were owned at the time of saving
	InitZones();

	//factories pick up where they were (InitZones only gave them defaults)
	for(size_t i=0;i<objs.size();i++)
	{
		if(!made[i] || objs[i].ot != BUILDING_OBJECT) continue;
		saved_object &s = objs[i];
		if(s.build_state.size()) made[i]->ProcessSetBuildingStateData(&s.build_state[0], (int)s.build_state.size());
		if(s.build_queue.size()) made[i]->ProcessBuildingQueueData(&s.build_queue[0], (int)s.build_queue.size());
		if(s.built_cannons.size()) made[i]->ProcessSetBuiltCannonData(&s.built_cannons[0], (int)s.built_cannons.size());
	}

	//orders
	for(size_t i=0;i<objs.size();i++)
	{
		if(!made[i]) continue;
		made[i]->GetWayPointList() = objs[i].waypoints;
		made[i]->GetRallyPointList() = objs[i].rallypoints;
	}

	ResetZoneOwnagePercentages();

	//hand it over paused, like the start of a level
	//don't judge the match for a few seconds: the clients are still being sent the
	//world, and one that has not seen its units yet would call a won battle lost
	next_end_game_check_time = ztime.ztime + 3.0;

	PauseGame();
    session_seed=info.session_seed?info.session_seed:session_seed;srand(session_seed);ConfigureCampaignMission();if(!info.mission_uid.empty())for(const auto &pack:ZCampaign_List())if(pack.id==info.campaign_id)for(size_t i=0;i<pack.missions.size();++i)if(pack.missions[i].uid==info.mission_uid)info.mission_index=i;if(!info.campaign_id.empty()){mission_campaign_id=info.campaign_id;mission_campaign_index=info.mission_index;}if(has_mission_state){mission_runtime=saved_mission;mission_runtime.started=saved_step_age>=0;mission_runtime.stepStarted=saved_step_age<0?0:ztime.ztime-saved_step_age;mission_runtime.missionStarted=eventAge<0?-1:ztime.ztime-eventAge;if(!savedRefs.empty())mission_object_refs=savedRefs;}
    if(savedRefs.empty()){mission_object_refs.clear();for(const auto &initial:zmap.GetObjectList()){int reference=-1;for(auto *object:object_list){unsigned char type,id;int x,y;object->GetObjectID(type,id);object->GetCords(x,y);if(type==initial.object_type&&id==initial.object_id&&x==initial.x*16&&y==initial.y*16){reference=object->GetRefID();break;}}mission_object_refs.push_back(reference);}}
	game_on = true;

	ZDiag("game loaded: %s, %d objects", map_name.c_str(), (int)objs.size());

	BroadCastNews("saved game loaded");
	server_socket.SendMessageAll(RESET_GAME, NULL, 0);

	for(int t=0;t<MAX_TEAM_TYPES;t++)
		if(bot_team[t] && !bot_thread[t]) StartBot(t);

	return true;
}

void ZServer::save_game_event(ZServer *p, char *data, int size, int player)
{
	save_game_packet *pi = (save_game_packet*)data;
	if(size != sizeof(save_game_packet)) return;

	p->SaveGame(player, pi->kills, pi->lost, pi->elapsed, pi->slot);
}

void ZServer::load_game_event(ZServer *p, char *data, int size, int player)
{
	int_packet *pi = (int_packet*)data;
	if(size != sizeof(int_packet)) return;

	p->LoadSavedGame(player, pi->team);   //int_packet's one int carries the slot
}

void ZServer::load_map_file_event(ZServer *p, char *data, int size, int player)
{
	if(size <= 1 || size > 4096) return;
	data[size - 1] = 0;

	p->StartMapFile(data, player);
}
