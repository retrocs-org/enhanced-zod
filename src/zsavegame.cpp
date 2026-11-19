#include "zsavegame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;

string ZSave_Path(int slot)
{
	char name[48];
	if(slot <= ZSAVE_AUTO_SLOT) snprintf(name, sizeof(name), ".zod_savegame_auto");
	else snprintf(name, sizeof(name), ".zod_savegame_%d", slot > ZSAVE_USER_SLOTS ? ZSAVE_USER_SLOTS : slot);

	const char *home = getenv("HOME");
	if(!home || !home[0]) home = getenv("USERPROFILE");
	if(!home || !home[0]) return name;
	return string(home) + "/" + name;
}

void ZSaveWriter::Int(int v)
{
	unsigned int u = (unsigned int)v;
	for(int i=0;i<4;i++) buf.push_back((unsigned char)((u >> (i * 8)) & 0xff));
}

void ZSaveWriter::Double(double v)
{
	unsigned char b[sizeof(double)];
	memcpy(b, &v, sizeof(double));
	buf.insert(buf.end(), b, b + sizeof(double));
}

void ZSaveWriter::Str(const string &s)
{
	Blob(s.data(), (int)s.size());
}

void ZSaveWriter::Blob(const void *data, int size)
{
	if(size < 0 || !data) size = 0;
	Int(size);
	if(size) buf.insert(buf.end(), (const unsigned char *)data, (const unsigned char *)data + size);
}

void ZSaveWriter::Raw(const void *data,int size)
{
    if(data && size>0) buf.insert(buf.end(),(const unsigned char*)data,(const unsigned char*)data+size);
}

bool ZSaveWriter::WriteFile(const string &path)
{
	//write to a temporary file first so a failed save never eats the old one
	string tmp = path + ".tmp";
	FILE *fp = fopen(tmp.c_str(), "wb");
	if(!fp) return false;

	bool good = buf.empty() || fwrite(&buf[0], 1, buf.size(), fp) == buf.size();
	if(fflush(fp) != 0) good = false;
#ifndef _WIN32
	if(good && fsync(fileno(fp)) != 0) good = false;
#endif
	if(fclose(fp) != 0) good = false;

	if(!good) { remove(tmp.c_str()); return false; }

#ifdef _WIN32
	bool replaced = MoveFileExA(tmp.c_str(), path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	bool replaced = rename(tmp.c_str(), path.c_str()) == 0;
#endif
	if(!replaced) remove(tmp.c_str());
	return replaced;
}

bool ZSaveReader::ReadFile(const string &path)
{
	buf.clear();
	pos = 0;
	ok = true;

	FILE *fp = fopen(path.c_str(), "rb");
	if(!fp) { ok = false; return false; }

	unsigned char chunk[65536];
	size_t got;
	while((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) buf.insert(buf.end(), chunk, chunk + got);
	fclose(fp);

	return true;
}

int ZSaveReader::Int()
{
	if(!ok || pos + 4 > buf.size()) { ok = false; return 0; }
	unsigned int u = 0;
	for(int i=0;i<4;i++) u |= (unsigned int)buf[pos + i] << (i * 8);
	pos += 4;
	return (int)u;
}

double ZSaveReader::Double()
{
	if(!ok || pos + sizeof(double) > buf.size()) { ok = false; return 0; }
	double v;
	memcpy(&v, &buf[pos], sizeof(double));
	pos += sizeof(double);
	return v;
}

vector<char> ZSaveReader::Blob()
{
	vector<char> out;
	int size = Int();
	if(!ok || size < 0 || pos + (size_t)size > buf.size()) { ok = false; return out; }
	if(size) out.assign((const char *)&buf[pos], (const char *)&buf[pos] + size);
	pos += size;
	return out;
}

string ZSaveReader::Str()
{
	vector<char> b = Blob();
	return b.empty() ? string() : string(&b[0], b.size());
}

void ZSave_WriteInfo(ZSaveWriter &w, const ZSaveInfo &info)
{
	w.Blob(ZSAVE_MAGIC, 8);
	w.Str(info.map_name);
	w.Str(info.map_title);
	w.Int(info.campaign_index);
	w.Int(info.one_off_map ? 1 : 0);
	w.Double(info.elapsed);
	w.Int(info.kills);
	w.Int(info.lost);
	w.Int(info.difficulty);
	w.Double((double)info.when);
    w.Str(info.campaign_id);w.Int(info.mission_index);
}

bool ZSave_ReadInfo(ZSaveReader &r, ZSaveInfo &info)
{
	vector<char> magic = r.Blob();
	if(!r.Ok()||magic.size()!=8)return false;bool modern=memcmp(magic.data(),ZSAVE_MAGIC,8)==0;if(!modern&&memcmp(magic.data(),"ZODSAVE3",8)!=0)return false;

	info.map_name = r.Str();
	info.map_title = r.Str();
	info.campaign_index = r.Int();
	info.one_off_map = r.Int() != 0;
	info.elapsed = r.Double();
	info.kills = r.Int();
	info.lost = r.Int();
	info.difficulty = r.Int();
	info.when = (long long)r.Double();
    info.campaign_id.clear();info.mission_index=-1;if(modern){info.campaign_id=r.Str();info.mission_index=r.Int();if(info.campaign_id.size()>64||info.mission_index< -1||info.mission_index>4096)return false;}

	return r.Ok();
}

bool ZSave_ReadInfo(int slot, ZSaveInfo &info)
{
	ZSaveReader r;
	if(!r.ReadFile(ZSave_Path(slot))) return false;
	return ZSave_ReadInfo(r, info);
}

bool ZSave_Delete(int slot)
{
	string path = ZSave_Path(slot);
	if(remove(path.c_str()) == 0) return true;
	ZSaveInfo still_there;
	return !ZSave_ReadInfo(slot, still_there);   //already gone counts as deleted
}
