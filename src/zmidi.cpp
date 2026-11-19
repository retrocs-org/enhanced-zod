#include "zmidi.h"

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <map>

using namespace std;

namespace
{
	struct Reader
	{
		const unsigned char *p, *end;
		bool ok;

		Reader(const unsigned char *b, const unsigned char *e) : p(b), end(e), ok(true) {}

		unsigned int byte()
		{
			if(p >= end) { ok = false; return 0; }
			return *p++;
		}

		unsigned int be(int n)
		{
			unsigned int v = 0;
			for(int i=0;i<n;i++) v = (v << 8) | byte();
			return v;
		}

		unsigned int varlen()
		{
			unsigned int v = 0;
			for(int i=0;i<4;i++)
			{
				unsigned int b = byte();
				v = (v << 7) | (b & 0x7f);
				if(!(b & 0x80)) break;
			}
			return v;
		}
	};

	void put_varlen(vector<unsigned char> &out, unsigned int v)
	{
		unsigned char buf[5];
		int n = 0;
		buf[n++] = v & 0x7f;
		while((v >>= 7)) buf[n++] = 0x80 | (v & 0x7f);
		while(n) out.push_back(buf[--n]);
	}

	void put_be32(vector<unsigned char> &out, unsigned int v)
	{
		out.push_back((v >> 24) & 0xff);
		out.push_back((v >> 16) & 0xff);
		out.push_back((v >> 8) & 0xff);
		out.push_back(v & 0xff);
	}

	struct RawEvent
	{
		unsigned int tick;
		unsigned int order;   //keeps file order among simultaneous events
		vector<unsigned char> data;
	};

	bool raw_before(const RawEvent &a, const RawEvent &b)
	{
		if(a.tick != b.tick) return a.tick < b.tick;
		return a.order < b.order;
	}

	inline bool is_meta(const vector<unsigned char> &d, int type)
	{
		return d.size() >= 2 && d[0] == 0xff && d[1] == type;
	}
}

bool ZMidiFile::Load(const string &filename)
{
	events.clear();
	division = 0;
	length_seconds = 0;

	FILE *fp = fopen(filename.c_str(), "rb");
	if(!fp) return false;

	vector<unsigned char> file;
	unsigned char buf[4096];
	size_t got;
	while((got = fread(buf, 1, sizeof(buf), fp)) > 0) file.insert(file.end(), buf, buf + got);
	fclose(fp);

	if(file.size() < 14 || memcmp(&file[0], "MThd", 4)) return false;

	Reader hdr(&file[0] + 4, &file[0] + file.size());
	unsigned int hdr_len = hdr.be(4);
	hdr.be(2);                              //format (0 or 1; both are handled the same)
	unsigned int ntracks = hdr.be(2);
	unsigned int div = hdr.be(2);
	if(!hdr.ok || hdr_len < 6 || (div & 0x8000) || !div) return false;   //SMPTE time is not supported
	division = (int)div;

	vector<RawEvent> raw;
	unsigned int order = 0;
	size_t pos = 8 + hdr_len;

	for(unsigned int t=0; t<ntracks && pos + 8 <= file.size(); t++)
	{
		Reader th(&file[pos], &file[0] + file.size());
		bool is_track = !memcmp(&file[pos], "MTrk", 4);
		th.p += 4;
		unsigned int len = th.be(4);
		pos += 8;
		if(pos + len > file.size()) len = (unsigned int)(file.size() - pos);

		if(is_track)
		{
			Reader r(&file[pos], &file[pos] + len);
			unsigned int tick = 0;
			unsigned int running = 0;

			while(r.p < r.end && r.ok)
			{
				tick += r.varlen();
				unsigned int status = r.byte();
				if(!r.ok) break;

				RawEvent e;
				e.tick = tick;
				e.order = order++;

				if(status == 0xff)
				{
					unsigned int type = r.byte();
					unsigned int mlen = r.varlen();
					if(!r.ok || r.p + mlen > r.end) break;
					e.data.push_back(0xff);
					e.data.push_back((unsigned char)type);
					put_varlen(e.data, mlen);
					e.data.insert(e.data.end(), r.p, r.p + mlen);
					r.p += mlen;
					if(type == 0x2f) continue;   //end of track: we write our own
				}
				else if(status == 0xf0 || status == 0xf7)
				{
					unsigned int slen = r.varlen();
					if(!r.ok || r.p + slen > r.end) break;
					e.data.push_back((unsigned char)status);
					put_varlen(e.data, slen);
					e.data.insert(e.data.end(), r.p, r.p + slen);
					r.p += slen;
				}
				else
				{
					unsigned int first;
					if(status & 0x80) { running = status; first = r.byte(); }
					else { first = status; status = running; }
					if(status < 0x80 || status >= 0xf0) break;   //malformed

					e.data.push_back((unsigned char)status);
					e.data.push_back((unsigned char)first);
					unsigned int kind = status & 0xf0;
					if(kind != 0xc0 && kind != 0xd0) e.data.push_back((unsigned char)r.byte());
					if(!r.ok) break;
				}

				raw.push_back(e);
			}
		}

		pos += len;
	}

	if(raw.empty()) return false;

	stable_sort(raw.begin(), raw.end(), raw_before);

	//tempo map -> seconds
	double us_per_quarter = 500000.0;
	double seconds = 0;
	unsigned int last_tick = 0;

	events.reserve(raw.size());
	for(size_t i=0;i<raw.size();i++)
	{
		seconds += (raw[i].tick - last_tick) * us_per_quarter / division / 1000000.0;
		last_tick = raw[i].tick;

		Event e;
		e.tick = raw[i].tick;
		e.seconds = seconds;
		e.data.swap(raw[i].data);

		if(is_meta(e.data, 0x51) && e.data.size() >= 6)
			us_per_quarter = (e.data[3] << 16) | (e.data[4] << 8) | e.data[5];

		events.push_back(e);
	}

	length_seconds = seconds;
	return true;
}

vector<unsigned char> ZMidiFile::Slice(double start_seconds) const
{
	vector<unsigned char> track;
	if(events.empty()) return track;

	//first event at or after the start time
	size_t first = 0;
	if(start_seconds > 0)
	{
		while(first < events.size() && events[first].seconds < start_seconds - 0.0005) first++;
		if(first >= events.size()) first = 0;   //past the end: play from the top
	}
	unsigned int start_tick = events[first].tick;

	//"chase" everything before the cut so the slice sounds like the full song
	//would at that point: the last tempo, time signature, instrument,
	//controller values, pitch bend and pressure of every channel
	vector<unsigned char> tempo, timesig;
	vector<vector<unsigned char> > sysex;
	map<int, vector<unsigned char> > program, bend, pressure;   //by channel
	map<int, vector<unsigned char> > controller;                //by channel*128 + cc
	vector<int> controller_order;                               //first-seen order (bank/RPN sequences)

	for(size_t i=0;i<first;i++)
	{
		const vector<unsigned char> &d = events[i].data;
		unsigned int status = d[0];

		if(status == 0xff)
		{
			if(is_meta(d, 0x51)) tempo = d;
			else if(is_meta(d, 0x58)) timesig = d;
		}
		else if(status == 0xf0)
			sysex.push_back(d);
		else if(status < 0xf0)
		{
			int ch = status & 0x0f;
			switch(status & 0xf0)
			{
			case 0xc0: program[ch] = d; break;
			case 0xe0: bend[ch] = d; break;
			case 0xd0: pressure[ch] = d; break;
			case 0xb0:
				{
					int cc = d[1];
					if(cc >= 120) break;   //channel mode messages (all notes off, ...)
					int key = ch * 128 + cc;
					if(controller.find(key) == controller.end()) controller_order.push_back(key);
					controller[key] = d;
				}
				break;
			}
		}
	}

	#define EMIT0(v) do { track.push_back(0); track.insert(track.end(), (v).begin(), (v).end()); } while(0)

	if(!tempo.empty()) EMIT0(tempo);
	if(!timesig.empty()) EMIT0(timesig);
	for(size_t i=0;i<sysex.size();i++) EMIT0(sysex[i]);
	for(size_t i=0;i<controller_order.size();i++) EMIT0(controller[controller_order[i]]);
	for(map<int, vector<unsigned char> >::iterator i=program.begin(); i!=program.end(); ++i) EMIT0(i->second);
	for(map<int, vector<unsigned char> >::iterator i=bend.begin(); i!=bend.end(); ++i) EMIT0(i->second);
	for(map<int, vector<unsigned char> >::iterator i=pressure.begin(); i!=pressure.end(); ++i) EMIT0(i->second);

	#undef EMIT0

	//the rest of the song, shifted to start at tick 0
	unsigned int last_tick = start_tick;
	for(size_t i=first;i<events.size();i++)
	{
		put_varlen(track, events[i].tick - last_tick);
		last_tick = events[i].tick;
		track.insert(track.end(), events[i].data.begin(), events[i].data.end());
	}

	//end of track
	track.push_back(0);
	track.push_back(0xff);
	track.push_back(0x2f);
	track.push_back(0);

	vector<unsigned char> out;
	out.insert(out.end(), (const unsigned char *)"MThd", (const unsigned char *)"MThd" + 4);
	put_be32(out, 6);
	out.push_back(0); out.push_back(0);   //format 0
	out.push_back(0); out.push_back(1);   //one track
	out.push_back((division >> 8) & 0xff);
	out.push_back(division & 0xff);
	out.insert(out.end(), (const unsigned char *)"MTrk", (const unsigned char *)"MTrk" + 4);
	put_be32(out, (unsigned int)track.size());
	out.insert(out.end(), track.begin(), track.end());

	return out;
}
