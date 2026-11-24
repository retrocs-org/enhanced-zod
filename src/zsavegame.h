#ifndef _ZSAVEGAME_H_
#define _ZSAVEGAME_H_

// Saved battles (single-player). One slot, written by the embedded server when
// the player picks "Save Game" in the in-game menu and restored from "Load
// Game" in the start menu.
//
// A save is a snapshot of the server's world: the map, every object (position,
// owner, health, drivers, grenades, squad links, orders), what each factory is
// building and how far along it is, stored cannons, plus the campaign position
// and the player's running tally. In-flight shots and the momentary state of
// fights are not kept - units pick their targets again after loading.

#include <string>
#include <vector>

#define ZSAVE_MAGIC "ZODSAVE7"

// Slot 0 is the autosave (written by the game itself at the start of a battle
// and every few minutes); slots 1..ZSAVE_USER_SLOTS are the player's own.
#define ZSAVE_AUTO_SLOT 0
#define ZSAVE_USER_SLOTS 5
#define ZSAVE_CHECKPOINT_SLOT 6
#define ZSAVE_AUTOSAVE_INTERVAL 180.0   //game seconds between autosaves

struct ZSaveInfo
{
	std::string campaign_id,mission_uid;unsigned session_seed=0;int mission_index=-1;
	std::string map_name;     //map file the battle was started from
	std::string map_title;    //the map's own name, for menus
	int campaign_index;       //index in the campaign list, -1 for one-off maps
	bool one_off_map;         //skirmish / custom map: return to the menu afterwards
	double elapsed;           //game seconds played so far
	int kills, lost;          //the player's tally so far
	int difficulty;           //bot difficulty the battle was played at
	long long when;           //unix time of the save

	ZSaveInfo() : campaign_index(-1), one_off_map(false), elapsed(0), kills(0), lost(0), difficulty(1), when(0) {}
};

// ~/.zod_savegame_auto, ~/.zod_savegame_1 ... (or the working directory when
// there is no home)
std::string ZSave_Path(int slot);

// Read just the header of a slot. False when it holds no (valid) save.
bool ZSave_ReadInfo(int slot, ZSaveInfo &info);
bool ZSave_RecoveryInfo(int slot,ZSaveInfo &info);
bool ZSave_Recover(int slot);
bool ZSave_Delete(int slot);   //remove a saved battle; true if the file is gone

// ---- tiny binary reader / writer used by the server -------------------------
class ZSaveWriter
{
public:
	void Int(int v);
	void Double(double v);
	void Str(const std::string &s);
	void Blob(const void *data, int size);
    void Raw(const void *data, int size); //unframed bytes for atomic JSON outbox files
	bool WriteFile(const std::string &path);
private:
	std::vector<unsigned char> buf;
};

class ZSaveReader
{
public:
	ZSaveReader() : pos(0), ok(true) {}
	bool ReadFile(const std::string &path);
	int Int();
	double Double();
	std::string Str();
	std::vector<char> Blob();
	bool Ok() const { return ok; }
    size_t Remaining() const {return pos<=buf.size()?buf.size()-pos:0;}
private:
	std::vector<unsigned char> buf;
	size_t pos;
	bool ok;
};

void ZSave_WriteInfo(ZSaveWriter &w, const ZSaveInfo &info);
bool ZSave_ReadInfo(ZSaveReader &r, ZSaveInfo &info);

#endif
