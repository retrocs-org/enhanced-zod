#include "zsavegame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;
namespace {
unsigned save_crc(const vector<unsigned char> &data){unsigned crc=~0u;for(auto byte:data){crc^=byte;for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&-(int)(crc&1));}return ~crc;}
bool copy_atomic(const string &from,const string &to){ifstream in(from,ios::binary);if(!in)return false;ofstream out(to+".copy",ios::binary|ios::trunc);out<<in.rdbuf();out.close();if(!out||in.bad()){remove((to+".copy").c_str());return false;}
#ifdef _WIN32
bool ok=MoveFileExA((to+".copy").c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
bool ok=rename((to+".copy").c_str(),to.c_str())==0;
#endif
if(!ok)remove((to+".copy").c_str());return ok;}
bool valid_save(const string &path,ZSaveInfo &info){ZSaveReader reader;return reader.ReadFile(path)&&ZSave_ReadInfo(reader,info)&&reader.Remaining()>0;}
}


string ZSave_Path(int slot)
{
	char name[48];
	if(slot==ZSAVE_CHECKPOINT_SLOT) snprintf(name,sizeof(name),".zod_checkpoint");
	else if(slot <= ZSAVE_AUTO_SLOT) snprintf(name, sizeof(name), ".zod_savegame_auto");
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
	if(good&&buf.size()>=12&&memcmp(buf.data()+4,ZSAVE_MAGIC,8)==0){unsigned crc=save_crc(buf);unsigned char tail[12]={'Z','O','D','C','R','C','1','!',0,0,0,0};for(int i=0;i<4;++i)tail[8+i]=(crc>>(i*8))&255;good=fwrite(tail,1,12,fp)==12;}
    if(fflush(fp) != 0) good = false;
#ifndef _WIN32
	if(good && fsync(fileno(fp)) != 0) good = false;
#endif
	if(fclose(fp) != 0) good = false;

	if(!good) { remove(tmp.c_str()); return false; }

    // Keep three previous valid generations for autosaves and manual slots.
    // Generic atomic writers (e.g. the outbox) never receive save backups.
    bool slotPath=false;for(int slot=0;slot<=ZSAVE_CHECKPOINT_SLOT;++slot)slotPath|=path==ZSave_Path(slot);
    ZSaveInfo previous;if(slotPath&&valid_save(path,previous)){
        for(int generation=3;generation>1;--generation){ifstream exists(path+".bak"+to_string(generation-1),ios::binary);if(exists&&!copy_atomic(path+".bak"+to_string(generation-1),path+".bak"+to_string(generation))){remove(tmp.c_str());return false;}}
        if(!copy_atomic(path,path+".bak1")){remove(tmp.c_str());return false;}
    }
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
    if(fseek(fp,0,SEEK_END)!=0||ftell(fp)<0||ftell(fp)>64*1024*1024){fclose(fp);ok=false;return false;}rewind(fp);

	unsigned char chunk[65536];
	size_t got;
	while((got = fread(chunk, 1, sizeof(chunk), fp)) > 0) buf.insert(buf.end(), chunk, chunk + got);
    bool readError=ferror(fp)!=0;fclose(fp);if(readError){ok=false;return false;}
    bool checksumRequired=buf.size()>=12&&(memcmp(buf.data()+4,ZSAVE_MAGIC,8)==0||memcmp(buf.data()+4,"ZODSAVE5",8)==0||memcmp(buf.data()+4,"ZODSAVE6",8)==0);
    if(checksumRequired&&(buf.size()<24||memcmp(buf.data()+buf.size()-12,"ZODCRC1!",8)!=0)){ok=false;return false;}
    if(buf.size()>=12&&memcmp(buf.data()+buf.size()-12,"ZODCRC1!",8)==0){unsigned stored=0;for(int i=0;i<4;++i)stored|=(unsigned)buf[buf.size()-4+i]<<(i*8);buf.resize(buf.size()-12);if(save_crc(buf)!=stored){ok=false;return false;}}

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
    w.Str(info.campaign_id);w.Int(info.mission_index);w.Str(info.mission_uid);w.Int(info.session_seed);
}

bool ZSave_ReadInfo(ZSaveReader &r, ZSaveInfo &info)
{
	vector<char> magic = r.Blob();
	if(!r.Ok()||magic.size()!=8)return false;bool modern=memcmp(magic.data(),ZSAVE_MAGIC,8)==0||memcmp(magic.data(),"ZODSAVE4",8)==0||memcmp(magic.data(),"ZODSAVE5",8)==0||memcmp(magic.data(),"ZODSAVE6",8)==0;if(!modern&&memcmp(magic.data(),"ZODSAVE3",8)!=0)return false;

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

    info.mission_uid.clear();info.session_seed=0;if(memcmp(magic.data(),ZSAVE_MAGIC,8)==0){info.mission_uid=r.Str();info.session_seed=(unsigned)r.Int();if(info.mission_uid.size()>64)return false;}
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
	for(int generation=1;generation<=3;++generation)remove((path+".bak"+to_string(generation)).c_str());
    if(remove(path.c_str()) == 0) return true;
	ZSaveInfo still_there;
	return !ZSave_ReadInfo(slot, still_there);   //already gone counts as deleted
}

bool ZSave_RecoveryInfo(int slot,ZSaveInfo &info){for(int generation=1;generation<=3;++generation)if(valid_save(ZSave_Path(slot)+".bak"+to_string(generation),info))return true;return false;}
bool ZSave_Recover(int slot){const auto path=ZSave_Path(slot);ZSaveInfo info;for(int generation=1;generation<=3;++generation){auto backup=path+".bak"+to_string(generation);if(!valid_save(backup,info))continue;ifstream current(path,ios::binary);if(current&&!copy_atomic(path,path+".damaged"))return false;return copy_atomic(backup,path);}return false;}
