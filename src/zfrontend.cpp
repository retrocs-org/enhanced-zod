#include "zperf.h"
#include "zgamepad.h"
// Command console front end. Shared materials keep the launch and battle menus
// visually consistent; imported disc artwork remains an optional backdrop.

#include "zmovie.h"
#include "zcontent.h"
#include "zmenu_theme.h"
#include "zfrontend.h"
#include "zbuild.h"
#include "zplaytest.h"
#include "zportal.h"
#include <fstream>
#include "zreplay.h"
#include "zmpclient.h"
#include "zvideo.h"
#include "zprefs.h"
#include "zhighscore.h"
#include "zsdl.h"
#include "zmap.h"
#include "zmusic_engine.h"
#include "zsavegame.h"
#include "constants.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <sstream>

using namespace std;

extern bool zod_render_smoothing;   //zobject.cpp

#ifndef ZOD_VERSION
#define ZOD_VERSION "dev"
#endif

// ---------------------------------------------------------------------------
// palette

static const SDL_Color C_STEEL      = { 206, 211, 222, 255 };
static const SDL_Color C_STEEL_HI   = { 240, 244, 255, 255 };
static const SDL_Color C_STEEL_DIM  = {  96, 101, 112, 255 };
static const SDL_Color C_HOVER_GLOW = { 120, 150, 255, 255 };
static const SDL_Color C_AMBER      = { 255, 154,  44, 255 };
static const SDL_Color C_AMBER_HI   = { 255, 205, 120, 255 };
static const SDL_Color C_AMBER_DIM  = { 112,  56,  16, 255 };
static const SDL_Color C_LED_BG     = {  14,   9,   6, 255 };
static const SDL_Color C_BLACK      = {   0,   0,   0, 255 };
static const SDL_Color C_RED        = { 230,  70,  50, 255 };
static const SDL_Color C_GREEN      = { 120, 220, 120, 255 };

// main menu items
enum
{
	ITEM_NEW, ITEM_LOAD, ITEM_REPLAY, ITEM_SKIRMISH, ITEM_CUSTOM, ITEM_MULTI, ITEM_SCORES, MAX_ITEMS
};

static const char *item_label[MAX_ITEMS] =
{
	"Single Player", "Load Game", "Replay Level", "Skirmish", "Map Editor", "Multiplayer", "High Score"
};

static const char *item_help[MAX_ITEMS] =
{
	"Choose the Original Campaign or an imported campaign.",
	"Return to your saved battle, or continue the campaign.",
	"Play any level you have already reached, planet by planet.",
	"Battle on a freshly generated random map.",
	"Play maps you made, or build one in the map editor.",
	"Public dedicated servers, local network games and direct connect.",
	"Your best victories."
};

// settings rows on the LED display
enum
{
	SET_FULLSCREEN, SET_SCENES, SET_GAMMA, SET_VOLUME, SET_MUSIC,
	SET_DIFFICULTY, SET_MOUSE, SET_SMOOTHING, SET_UNITS,
	SET_PICTURE, SET_STICK, SET_KEYS, SET_IMPORT, MAX_SETTINGS
};

static const int SETTINGS_VISIBLE = 4;

// layout (canvas units)
static const float LP_X = 70, LP_Y = 236, LP_W = 492, LP_H = 412;      //left panel
static const float RP_X = 640, RP_Y = 236, RP_W = 570, RP_H = 470;     //right panel
static const float ITEM_X = 92, ITEM_Y0 = 256, ITEM_STEP = 54, ITEM_W = 450, ITEM_H = 48;
static const float NAME_X = 832, NAME_Y = 258, NAME_W = 350, NAME_H = 44;
static const float LED_X = 668, LED_Y = 324, LED_W = 466, LED_H = 236;
static const float LED_ROW_H = 56;
static const float SCR_X = 1146, SCR_W = 40;                             //scroll column
static const float INFO_X = 668, INFO_Y = 584, INFO_W = 518, INFO_H = 96;
static const float QUIT_X = 150, QUIT_Y = 688, QUIT_W = 270, QUIT_H = 60;
static const float KNOB_X = 474, KNOB_Y = 718, KNOB_R = 26;

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---------------------------------------------------------------------------
// procedural rasteriser for the static console art

namespace
{
	struct Raster
	{
		int W, H;
		float s;                  //pixels per canvas unit
		vector<float> rgb;        //W*H*3, linear-ish 0..1

		void init(int w, int h, float sc) { W = w; H = h; s = sc; rgb.assign((size_t)W * H * 3, 0.0f); }
		float *px(int x, int y) { return &rgb[((size_t)y * W + x) * 3]; }
	};

	inline unsigned int hash2(int x, int y)
	{
		unsigned int h = (unsigned int)x * 374761393u + (unsigned int)y * 668265263u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return h ^ (h >> 16);
	}

	inline float rnd2(int x, int y) { return (hash2(x, y) & 0xffffff) / 16777215.0f; }

	inline float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

	float vnoise(float x, float y)
	{
		int xi = (int)floorf(x), yi = (int)floorf(y);
		float fx = smooth(x - xi), fy = smooth(y - yi);
		float a = rnd2(xi, yi), b = rnd2(xi + 1, yi), c = rnd2(xi, yi + 1), d = rnd2(xi + 1, yi + 1);
		return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
	}

	float fbm(float x, float y)
	{
		return vnoise(x, y) * 0.55f + vnoise(x * 2.1f, y * 2.1f) * 0.3f + vnoise(x * 4.3f, y * 4.3f) * 0.15f;
	}

	struct Pt { float x, y; };

	// signed distance info for a convex polygon (canvas units)
	struct PolyHit { float d; float nx, ny; };   //d>0 inside; n = outward normal of nearest edge

	PolyHit poly_dist(const vector<Pt> &p, float x, float y, float orient)
	{
		PolyHit best; best.d = 1e9f; best.nx = 0; best.ny = -1;
		float worst_out = -1e9f;
		PolyHit out_hit = best;
		size_t n = p.size();
		for(size_t i=0;i<n;i++)
		{
			const Pt &a = p[i], &b = p[(i + 1) % n];
			float ex = b.x - a.x, ey = b.y - a.y;
			float len = sqrtf(ex * ex + ey * ey);
			if(len <= 0) continue;
			//inward normal
			float nx = -ey / len * orient, ny = ex / len * orient;
			float d = (x - a.x) * nx + (y - a.y) * ny;
			if(d < best.d) { best.d = d; best.nx = -nx; best.ny = -ny; }
			if(-d > worst_out) { worst_out = -d; out_hit.nx = -nx; out_hit.ny = -ny; }
		}
		if(best.d < 0) { out_hit.d = -worst_out; return out_hit; }
		return best;
	}

	float poly_orient(const vector<Pt> &p)
	{
		float a = 0;
		for(size_t i=0;i<p.size();i++)
		{
			const Pt &u = p[i], &v = p[(i + 1) % p.size()];
			a += u.x * v.y - v.x * u.y;
		}
		return a >= 0 ? 1.0f : -1.0f;
	}

	vector<Pt> chamfer_rect(float x, float y, float w, float h, float c_tl, float c_tr, float c_br, float c_bl)
	{
		vector<Pt> p;
		Pt q;
		q.x = x + c_tl; q.y = y; p.push_back(q);
		q.x = x + w - c_tr; q.y = y; p.push_back(q);
		if(c_tr > 0) { q.x = x + w; q.y = y + c_tr; p.push_back(q); }
		q.x = x + w; q.y = y + h - c_br; p.push_back(q);
		if(c_br > 0) { q.x = x + w - c_br; q.y = y + h; p.push_back(q); }
		q.x = x + c_bl; q.y = y + h; p.push_back(q);
		if(c_bl > 0) { q.x = x; q.y = y + h - c_bl; p.push_back(q); }
		q.x = x; q.y = y + c_tl; p.push_back(q);
		return p;
	}

	struct PlateStyle
	{
		float r, g, b;        //base metal colour
		float bevel;          //canvas units
		float shadow;         //drop shadow offset (0 = none)
		bool recessed;        //inset panel (lit from below)
		float grain;          //brushed grain strength
		float wear;           //dark wear spots
	};

	void draw_plate(Raster &R, const vector<Pt> &poly, const PlateStyle &st)
	{
		float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
		for(size_t i=0;i<poly.size();i++)
		{
			minx = min(minx, poly[i].x); maxx = max(maxx, poly[i].x);
			miny = min(miny, poly[i].y); maxy = max(maxy, poly[i].y);
		}
		float pad = st.shadow > 0 ? st.shadow * 3.0f : 2.0f;
		int x0 = max(0, (int)((minx - pad) * R.s)), x1 = min(R.W - 1, (int)((maxx + pad) * R.s) + 1);
		int y0 = max(0, (int)((miny - pad) * R.s)), y1 = min(R.H - 1, (int)((maxy + pad) * R.s) + 1);
		float orient = poly_orient(poly);
		const float Lx = -0.62f, Ly = -0.78f;     //light from the top left
		float inv = 1.0f / R.s;

		for(int py=y0;py<=y1;py++)
			for(int px=x0;px<=x1;px++)
			{
				float x = (px + 0.5f) * inv, y = (py + 0.5f) * inv;
				float *c = R.px(px, py);

				//drop shadow underneath
				if(st.shadow > 0)
				{
					PolyHit sh = poly_dist(poly, x - st.shadow * 0.6f, y - st.shadow, orient);
					float k = clampf((sh.d + st.shadow * 1.6f) / (st.shadow * 2.4f), 0, 1);
					k = smooth(k) * 0.6f;
					c[0] *= 1 - k; c[1] *= 1 - k; c[2] *= 1 - k;
				}

				PolyHit h = poly_dist(poly, x, y, orient);
				float cover = clampf(h.d * R.s + 0.5f, 0, 1);
				if(cover <= 0) continue;

				//metal surface
				float n1 = fbm(x * 0.018f, y * 0.018f);
				float grain = (rnd2(0, (int)(y * 3.0f)) - 0.5f) * st.grain + (rnd2(px, py) - 0.5f) * 0.035f;
				float streak = (vnoise(x * 0.004f, y * 0.9f) - 0.5f) * st.grain * 1.4f;
				float shade = 0.84f + 0.3f * n1 + grain + streak;
				//soft top-to-bottom light falloff across the plate
				shade *= 1.08f - 0.22f * clampf((y - miny) / max(1.0f, maxy - miny), 0, 1);
				if(st.wear > 0)
				{
					float w = fbm(x * 0.05f + 17.0f, y * 0.05f + 3.0f);
					if(w > 0.68f) shade *= 1.0f - (w - 0.68f) * st.wear * 2.2f;
				}

				//bevel
				if(h.d < st.bevel)
				{
					float t = 1.0f - h.d / st.bevel;
					float l = (h.nx * Lx + h.ny * Ly);
					if(st.recessed) l = -l;
					shade *= 1.0f + 0.65f * l * t;
				}
				else if(h.d < st.bevel + 1.2f)
					shade *= st.recessed ? 1.06f : 0.86f;   //machined groove inside the bevel

				float r = st.r * shade, g = st.g * shade, b = st.b * shade;
				c[0] = c[0] + (r - c[0]) * cover;
				c[1] = c[1] + (g - c[1]) * cover;
				c[2] = c[2] + (b - c[2]) * cover;
			}
	}

	void draw_rivet(Raster &R, float cx, float cy, float rad)
	{
		int x0 = max(0, (int)((cx - rad - 2) * R.s)), x1 = min(R.W - 1, (int)((cx + rad + 2) * R.s));
		int y0 = max(0, (int)((cy - rad - 2) * R.s)), y1 = min(R.H - 1, (int)((cy + rad + 2) * R.s));
		for(int py=y0;py<=y1;py++)
			for(int px=x0;px<=x1;px++)
			{
				float x = (px + 0.5f) / R.s - cx, y = (py + 0.5f) / R.s - cy;
				float d = sqrtf(x * x + y * y);
				float *c = R.px(px, py);
				//contact shadow
				if(d < rad + 1.8f && d >= rad)
				{
					float k = 0.45f * (1 - (d - rad) / 1.8f);
					c[0] *= 1 - k; c[1] *= 1 - k; c[2] *= 1 - k;
				}
				if(d >= rad) continue;
				float nz = sqrtf(max(0.0f, 1 - (d / rad) * (d / rad)));
				float l = (-x / rad * 0.6f - y / rad * 0.75f) + nz * 0.5f;
				float v = 0.18f + 0.55f * clampf(l, 0, 1.3f);
				float cover = clampf((rad - d) * R.s, 0, 1);
				c[0] += (v * 0.95f - c[0]) * cover;
				c[1] += (v * 0.97f - c[1]) * cover;
				c[2] += (v * 1.02f - c[2]) * cover;
			}
	}

	// vertical or horizontal pipe with cylindrical shading
	void draw_pipe(Raster &R, float x, float y, float w, float h, bool vertical, float tr, float tg, float tb)
	{
		int x0 = max(0, (int)(x * R.s)), x1 = min(R.W - 1, (int)((x + w) * R.s));
		int y0 = max(0, (int)(y * R.s)), y1 = min(R.H - 1, (int)((y + h) * R.s));
		for(int py=y0;py<=y1;py++)
			for(int px=x0;px<=x1;px++)
			{
				float t = vertical ? (((px + 0.5f) / R.s - x) / w) * 2 - 1 : (((py + 0.5f) / R.s - y) / h) * 2 - 1;
				float nz = sqrtf(max(0.0f, 1 - t * t));
				float l = 0.25f + 0.75f * nz + (vertical ? -t : -t) * 0.25f;
				float spec = powf(clampf(nz * 0.9f - t * 0.35f, 0, 1), 24.0f) * 0.6f;
				float *c = R.px(px, py);
				c[0] = tr * l + spec; c[1] = tg * l + spec; c[2] = tb * l + spec;
			}
	}

	void darken_box(Raster &R, float x, float y, float w, float h, float k, float soft)
	{
		int x0 = max(0, (int)((x - soft) * R.s)), x1 = min(R.W - 1, (int)((x + w + soft) * R.s));
		int y0 = max(0, (int)((y - soft) * R.s)), y1 = min(R.H - 1, (int)((y + h + soft) * R.s));
		for(int py=y0;py<=y1;py++)
			for(int px=x0;px<=x1;px++)
			{
				float cx = (px + 0.5f) / R.s, cy = (py + 0.5f) / R.s;
				float dx = max(max(x - cx, cx - (x + w)), 0.0f);
				float dy = max(max(y - cy, cy - (y + h)), 0.0f);
				float d = sqrtf(dx * dx + dy * dy);
				float f = soft > 0 ? clampf(1 - d / soft, 0, 1) : (d <= 0 ? 1 : 0);
				float m = 1 - k * smooth(f);
				float *c = R.px(px, py);
				c[0] *= m; c[1] *= m; c[2] *= m;
			}
	}

	SDL_Surface *raster_to_surface(Raster &R)
	{
		SDL_Surface *s = SDL_CreateSurface(R.W, R.H, SDL_PIXELFORMAT_ARGB8888);
		if(!s) return NULL;
		for(int y=0;y<R.H;y++)
		{
			Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
			for(int x=0;x<R.W;x++)
			{
				float *c = R.px(x, y);
				//tiny dither against banding in the dark gradients
				float d = (rnd2(x * 7 + 3, y * 13 + 1) - 0.5f) / 255.0f;
				Uint32 r = (Uint32)(clampf(c[0] + d, 0, 1) * 255.0f + 0.5f);
				Uint32 g = (Uint32)(clampf(c[1] + d, 0, 1) * 255.0f + 0.5f);
				Uint32 b = (Uint32)(clampf(c[2] + d, 0, 1) * 255.0f + 0.5f);
				row[x] = 0xff000000u | (r << 16) | (g << 8) | b;
			}
		}
		return s;
	}
}

// ---------------------------------------------------------------------------

ZFrontEnd::ZFrontEnd()
{
	visible = false;
	initialised = false;
	screen = SCR_MAIN;
	screen_before_quit = SCR_MAIN;
	mouse_x = mouse_y = -1;
	mouse_down = false;
	press_x = press_y = -1;
	clicked = false;
	click_x = click_y = -1;
	wheel = 0;
	kb_item = -1;
	kb_activate = kb_back = kb_delete = false;
	kb_dx = kb_dy = 0;
	editing_name = false;
	name_fresh = false;
	settings_scroll = 0;
	hover_item = -1;
	replay_planet = 0;
	replay_selected = 0;
	briefing_level = 0;
	current_level = -1;
	loading_since = 0;
	status_until = 0;
	progress_current = -1;
	progress_unlocked = 0;
	have_save = false;
	load_selected = 0;
	delete_pending = -1;
	keys_selected = 0;
	keys_waiting = -1;
	custom_selected = 0;
	custom_scroll = 0;
	custom_scanned = 0;
	newmap_open = false;
	editing_mapname = false;
	newmap_planet = 0;
	newmap_size = 1;

	renderer = NULL;
	scale = 0;
SDL_DestroyTexture(map_preview_texture);map_preview_texture=nullptr;map_preview_path.clear();
	target = background = glow = title_tex = NULL;
	tex_w = tex_h = 0;

	font_ui.file = "assets/frontend/fonts/Rajdhani-SemiBold.ttf";
	font_ui_medium.file = "assets/frontend/fonts/Rajdhani-Medium.ttf";
	font_led.file = "assets/frontend/fonts/Orbitron.ttf";

	snd_hover = snd_click = snd_toggle = snd_start = NULL;
}

ZFrontEnd::~ZFrontEnd()
{
	//GPU objects die with the renderer at exit; nothing to do here
}

void ZFrontEnd::Init()
{
	if(initialised) return;
	initialised = true;

	if(!TTF_WasInit()) TTF_Init();

	snd_hover  = ZAudio_LoadSound(ZSDL_DataPath("assets/sounds/BEEP1L.wav").c_str());
	snd_click  = ZAudio_LoadSound(ZSDL_DataPath("assets/sounds/CLICK1L.wav").c_str());
	snd_toggle = ZAudio_LoadSound(ZSDL_DataPath("assets/sounds/SWITCHL.wav").c_str());
	snd_start  = ZAudio_LoadSound(ZSDL_DataPath("assets/sounds/CONNECTL.wav").c_str());

	LoadLevels();

	//restore the display settings remembered from last time
	ZVideo_SetGamma(zod_gamma);
	if(zod_fullscreen != ZVideo_GetFullscreen()) ZVideo_SetFullscreen(zod_fullscreen);
}

void ZFrontEnd::LoadLevels()
{
	levels.clear();

	FILE *fp = fopen(ZSDL_DataPath("map_list.txt").c_str(), "r");
	if(!fp) return;

	char line[512];
	bool first = true;
	while(fgets(line, sizeof(line), fp))
	{
		size_t n = strlen(line);
		while(n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
		if(!n) continue;

		//first line of a map list is the "load randomly" flag
		if(first) { first = false; if(n <= 2 && (line[0] == '0' || line[0] == '1')) continue; }

		LevelInfo li;
		li.file = line;
		size_t slash = li.file.find_last_of("/\\");
		li.base = slash == string::npos ? li.file : li.file.substr(slash + 1);
		li.planet = DESERT;
		li.w = li.h = 0;

		//only the header is needed: size, name and planet
		FILE *mf = fopen(ZSDL_DataPath(li.file).c_str(), "rb");
		if(mf)
		{
			map_basics mb;
			if(fread(&mb, sizeof(mb), 1, mf) == 1)
			{
				mb.map_name[sizeof(mb.map_name) - 1] = 0;
				li.title = mb.map_name;
				li.planet = mb.terrain_type < MAX_PLANET_TYPES ? mb.terrain_type : DESERT;
				li.w = mb.width;
				li.h = mb.height;
			}
			fclose(mf);
		}

		//"L07: Pyro Technics" -> "Pyro Technics"
		if(li.title.size() > 4 && li.title[0] == 'L' && li.title.find(':') != string::npos && li.title.find(':') < 5)
		{
			li.title = li.title.substr(li.title.find(':') + 1);
			while(li.title.size() && li.title[0] == ' ') li.title.erase(0, 1);
		}
		if(li.title.empty())
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "Mission %d", (int)levels.size() + 1);
			li.title = buf;
		}

		levels.push_back(li);
	}
	fclose(fp);
    original_levels=levels;
}

void ZFrontEnd::LoadProgress()
{
    if(!selected_campaign.id.empty()) {ZCampaign_Progress(selected_campaign.id,progress_current,progress_unlocked);progress_current=std::min(progress_current,(int)levels.size()-1);progress_unlocked=std::min(progress_unlocked,(int)levels.size()-1);return;}

	//written by the (embedded) server: line 1 = where you are, line 2 = unlocked
	progress_current = -1;
	progress_unlocked = 0;

	const char *home = getenv("HOME");
	if(!home || !*home) home = getenv("USERPROFILE");
	string path = (home && *home) ? string(home) + "/.zod_campaign_progress" : ".zod_campaign_progress";

	FILE *fp = fopen(path.c_str(), "r");
	if(!fp)fp=fopen((path+".bak").c_str(),"r");
	if(!fp)return;

	int cur = -1, unl = -1;
	int got = fscanf(fp, "%d %d", &cur, &unl);
    if(got<1||cur<0||cur>=(int)levels.size()){fclose(fp);fp=fopen((path+".bak").c_str(),"r");if(!fp)return;got=fscanf(fp,"%d %d",&cur, &unl);}
	fclose(fp);

	if(got >= 1 && cur >= 0 && cur < (int)levels.size()) progress_current = cur;
	if(got >= 2 && unl >= 0) progress_unlocked = unl;
	else if(progress_current >= 0) progress_unlocked = progress_current;
	if(progress_unlocked >= (int)levels.size()) progress_unlocked = (int)levels.size() - 1;
}

void ZFrontEnd::ReviewDiagnostics(bool inGame){Init();diagnostic_return_screen=screen;diagnostic_return_game=inGame;diagnostic_preview=ZAsset_DiagnosticPreview(asset_import.phase);diagnostic_message.clear();diagnostic_scroll=0;focus_button=-1;focus_activate=false;visible=true;screen=SCR_DIAGNOSTICS;}
void ZFrontEnd::ShowImport(bool required)
{
    Show();
    import_only = required;
    screen = SCR_IMPORT;
    if(required) asset_import.message = "Original game media is not installed. Choose your Z DOS disc to enable play.";
}

void ZFrontEnd::Show()
{
	Init();
    if(editor_testing&&workspace){if(editor_continue_pending){editor_continue_pending=false;visible=true;screen=SCR_MAIN;return;}bool campaignTest=!workspace->TestCampaign().root.empty();workspace->ResumeAfterTest();if(campaignTest){selected_campaign=editor_previous_campaign;levels=editor_previous_levels;current_level=editor_previous_level;progress_current=editor_previous_current;progress_unlocked=editor_previous_unlocked;community_running=false;community_next=-1;}editor_testing=false;visible=true;screen=SCR_EDITOR;ZVideo_SetFullscreen(true);return;}
	LoadProgress();
	LoadSaveInfo();
	ZHighScore_All(true);
	newmap_open = false;
	editing_mapname = false;

	visible = true;
	screen = browser_return_pending?SCR_MULTIPLAYER_MENU:SCR_MAIN;browser_return_pending=false;
	editing_name = false;
	name_edit = zod_player_name;
	kb_item = -1;
	clicked = false;

	if(ZVideo_GetWindow()) SDL_StopTextInput(ZVideo_GetWindow());
}

void ZFrontEnd::Hide()
{
	visible = false;
	editing_name = false;
	if(screen == SCR_LOADING) screen = SCR_MAIN;
	if(ZVideo_GetWindow()) SDL_StopTextInput(ZVideo_GetWindow());
}

bool ZFrontEnd::PopAction(Action &out)
{
	if(actions.empty()) return false;
	out = actions.front();
	actions.erase(actions.begin());
    if(out.type==FE_OPEN_MULTIPLAYER){std::ifstream seen(ZAsset_Profile()+"multiplayer-guide-seen");if(!seen&&!guide_completed_session){guide_return=screen;screen=SCR_MULTIPLAYER_GUIDE;multiplayer_guide_step=0;return false;}}

    if(out.type==FE_OPEN_MULTIPLAYER)browser_return_pending=true;
    if(out.type==FE_START_MAP||out.type==FE_START_CAMPAIGN_MAP||out.type==FE_START_MAP_FILE||out.type==FE_LOAD_SAVE){deployment_action=out;has_deployment_retry=true;deployment_failed=false;}
	//anything but a campaign level (custom map, save, multiplayer...) is not a
	//level whose cutscenes we should show
	if(out.type != FE_START_CAMPAIGN_MAP && out.type != FE_START_MAP && out.type != FE_SET_VOLUME && out.type != FE_SET_MUSIC && out.type != FE_NAME_CHANGED)
		current_level = -1;
    if(out.type!=FE_START_CAMPAIGN_MAP && out.type!=FE_SET_VOLUME && out.type!=FE_SET_MUSIC && out.type!=FE_NAME_CHANGED)community_running=false;
	return true;
}

// The cutscene clips are named E_<planet letter><kind>: A(rctic) C(ity) D(esert)
// J(ungle) V(olcanic) + PLNT (arrive), TRAV1-3 (travel), WINT1-3 (sector won),
// WINPL (planet won), LOST1-4; plus E_OUTRO / E_CREDS for the end of the war.
static char planet_letter(int planet)
{
	switch(planet)
	{
	case ARCTIC: return 'A';
	case CITY: return 'C';
	case JUNGLE: return 'J';
	case VOLCANIC: return 'V';
	default: return 'D';
	}
}

void ZFrontEnd::PlayLevelIntro(int index)
{
	if(index < 0 || index >= (int)levels.size()) return;

	char L = planet_letter(levels[index].planet);
	char name[32];

	//first level on a new planet: the arrival clip; otherwise a travel clip
	if(index == 0 || levels[index - 1].planet != levels[index].planet)
	{
		snprintf(name, sizeof(name), "E_%cPLNT", L);
		if(ZMovie_PlayClip(name) != ZMOVIE_MISSING) return;
	}
	snprintf(name, sizeof(name), "E_%cTRAV%d", L, index % 3 + 1);
	ZMovie_PlayClip(name);
}

void ZFrontEnd::PlayLevelResult(bool won)
{
    if(community_running && current_level>=0 && current_level<(int)selected_campaign.missions.size()) {
        const auto &video=selected_campaign.missions[current_level].victoryVideo;
        if(won&&!video.empty()) {if(editor_testing)ZPlaytest_Event(selected_campaign,current_level,"victory-video-attempt");if(video.compare(0,9,"original:")==0)ZMovie_PlayClip(video.substr(9));else {string file;if(ZCampaign_SafeAsset(selected_campaign.root,video,file)){ZAudio_PauseMusic(true);ZMovie_Play(file);ZAudio_PauseMusic(false);}}}return;
    }

	int index = current_level;
	current_level = -1;   //once per level
	if(index < 0 || index >= (int)levels.size()) return;

	char L = planet_letter(levels[index].planet);
	char name[32];

	if(won)
	{
		if(index == (int)levels.size() - 1)
		{
			//the war is won
			if(ZMovie_PlayClip("E_OUTRO") != ZMOVIE_SKIPPED) ZMovie_PlayClip("E_CREDS");
			return;
		}

		if(levels[index + 1].planet != levels[index].planet)
			snprintf(name, sizeof(name), "E_%cWINPL", L);
		else
			snprintf(name, sizeof(name), "E_%cWINT%d", L, index % 3 + 1);
		ZMovie_PlayClip(name);
		return;
	}

	//lost: not every planet has all four clips, so take the first that exists
	for(int k = 0; k < 4; k++)
	{
		snprintf(name, sizeof(name), "E_%cLOST%d", L, (index + k) % 4 + 1);
		if(ZMovie_PlayClip(name) != ZMOVIE_MISSING) return;
	}
}

// ---------------------------------------------------------------------------
// input

void ZFrontEnd::HandleEvent(const SDL_Event &ev)
{
    if(ev.type==SDL_EVENT_KEY_DOWN&&ev.key.key==SDLK_TAB&&!editing_name&&!editing_mapname){int count=max(1,focus_count),step=(ev.key.mod&SDL_KMOD_SHIFT)?-1:1;if(focus_button<0&&step<0)focus_button=0;focus_button=(focus_button+step+count)%count;focus_activate=false;if(screen!=SCR_EDITOR)return;}
    if(ev.type==SDL_EVENT_KEY_DOWN&&(ev.key.key==SDLK_RETURN||ev.key.key==SDLK_KP_ENTER||ev.key.key==SDLK_SPACE)&&focus_button>=0&&screen!=SCR_EDITOR&&!editing_name&&!editing_mapname&&!(screen==SCR_KEYS&&keys_waiting>=0)){focus_activate=true;return;}
    if(screen==SCR_EDITOR && workspace) {workspace->Event(ev);return;}
	switch(ev.type)
	{
	case SDL_EVENT_MOUSE_MOTION:
		focus_button=-1;mouse_x = ev.motion.x;
		mouse_y = ev.motion.y;
		kb_item = -1;
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		focus_button=-1;mouse_x = ev.button.x;
		mouse_y = ev.button.y;
		if(ev.button.button == SDL_BUTTON_LEFT)
		{
			mouse_down = true;
			press_x = mouse_x;
			press_y = mouse_y;
		}
		else if(ev.button.button == SDL_BUTTON_RIGHT)
			kb_back = true;
		break;
	case SDL_EVENT_MOUSE_BUTTON_UP:
		focus_button=-1;mouse_x = ev.button.x;
		mouse_y = ev.button.y;
		if(ev.button.button == SDL_BUTTON_LEFT && mouse_down)
		{
			mouse_down = false;
			clicked = true;
			click_x = mouse_x;
			click_y = mouse_y;
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		wheel += ev.wheel.y * (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f);
		break;
	case SDL_EVENT_TEXT_INPUT:
		if(editing_mapname && ev.text.text)
		{
			//becomes a file name: letters, digits, space, dash and underscore
			for(const char *c = ev.text.text; *c; c++)
				if((isalnum((unsigned char)*c) || *c == ' ' || *c == '-' || *c == '_') && newmap_name.size() < 24)
					newmap_name += *c;
		}
		else if(editing_name && ev.text.text)
		{
			if(name_fresh) { name_edit.clear(); name_fresh = false; }
			for(const char *c = ev.text.text; *c; c++)
			{
				//names travel through the engine's comma separated packets, so
				//keep them to plain printable ASCII without commas
				if(*c >= 32 && *c < 127 && *c != ',' && name_edit.size() < 16)
					name_edit += *c;
			}
		}
		break;
	case SDL_EVENT_KEY_DOWN:
		{
			SDL_Keycode k = ev.key.key;

			//the Keys screen is waiting for the new key of an action
			if(screen == SCR_KEYS && keys_waiting >= 0)
			{
				CaptureKey(k);
				break;
			}

			if(editing_mapname)
			{
				if(k == SDLK_BACKSPACE && newmap_name.size()) newmap_name.erase(newmap_name.size() - 1);
				else if(k == SDLK_RETURN || k == SDLK_KP_ENTER) kb_activate = true;
				else if(k == SDLK_ESCAPE) kb_back = true;
				break;
			}

			if(editing_name)
			{
				if(k == SDLK_BACKSPACE && name_fresh) { name_edit.clear(); name_fresh = false; }
				else if(k == SDLK_BACKSPACE && name_edit.size()) name_edit.erase(name_edit.size() - 1);
				else if(k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_TAB) CommitName();
				else if(k == SDLK_ESCAPE) { name_edit = zod_player_name; editing_name = false; SDL_StopTextInput(ZVideo_GetWindow()); }
				break;
			}

			switch(k)
			{
			case SDLK_TAB: if(screen==SCR_SETTINGS){settings_category=(settings_category+1)%6;settings_focus=0;}break;
			case SDLK_UP: focus_button=-1;   kb_dy = -1; break;
			case SDLK_DOWN: focus_button=-1; kb_dy = 1; break;
			case SDLK_LEFT: focus_button=-1; kb_dx = -1; break;
			case SDLK_RIGHT: focus_button=-1;kb_dx = 1; break;
			case SDLK_DELETE: kb_delete = true; break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
			case SDLK_SPACE: kb_activate = true; break;
			case SDLK_ESCAPE:
			case SDLK_BACKSPACE: kb_back = true; break;
			default: break;
			}
		}
		break;
	default:
		break;
	}
}

void ZFrontEnd::CommitName()
{
	editing_name = false;
	if(ZVideo_GetWindow()) SDL_StopTextInput(ZVideo_GetWindow());

	while(name_edit.size() && name_edit[name_edit.size() - 1] == ' ') name_edit.erase(name_edit.size() - 1);
	while(name_edit.size() && name_edit[0] == ' ') name_edit.erase(0, 1);
	if(name_edit.empty()) { name_edit = zod_player_name; return; }

	if(name_edit != zod_player_name)
	{
		zod_player_name = name_edit;
		ZPrefs_Save();
		actions.push_back(Action(FE_NAME_CHANGED));
	}
}

// ---------------------------------------------------------------------------
// graphics resources

bool ZFrontEnd::EnsureGraphics()
{
	renderer = ZVideo_GetRenderer();
	if(!renderer) return false;

	int ow = 0, oh = 0;
	if(!ZVideo_GetOutputSize(ow, oh) || ow <= 0 || oh <= 0) { ow = CANVAS_W; oh = CANVAS_H; }

	float s = min((float)ow / CANVAS_W, (float)oh / CANVAS_H);
	s = clampf(s, 1.0f, 3.0f);
	s = floorf(s * 8.0f + 0.5f) / 8.0f;   //quantise so tiny size jitters don't rebuild

	if(target && background && fabsf(s - scale) < 0.01f) return true;

	FreeGraphics();
	scale = s;
	tex_w = (int)(CANVAS_W * scale + 0.5f);
	tex_h = (int)(CANVAS_H * scale + 0.5f);

	target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tex_w, tex_h);
	if(!target) { printf("ZFrontEnd: render target failed: %s\n", SDL_GetError()); return false; }
	SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);

	BuildBackground();
	BuildGlow();
	BuildTitle();
	// Optional until import completes. Do not log a missing-art error on first run.
	if(SDL_Surface *art = IMG_Load(ZSDL_DataPath("assets/frontend/disc/main_menu.png").c_str()))
	{
		disc_background = SDL_CreateTextureFromSurface(renderer, art);
		SDL_DestroySurface(art);
		if(disc_background) {
			SDL_SetTextureScaleMode(disc_background, SDL_SCALEMODE_NEAREST);
			SDL_SetTextureBlendMode(disc_background, SDL_BLENDMODE_NONE);
		}
	}

	return background != NULL;
}

void ZFrontEnd::FreeGraphics()
{
    if(workspace)workspace->ReleaseGraphics();
    SDL_DestroyTexture(campaign_cover);campaign_cover=nullptr;campaign_cover_path.clear();
	if(target) SDL_DestroyTexture(target);
	if(background) SDL_DestroyTexture(background);
	if(glow) SDL_DestroyTexture(glow);
	if(title_tex) SDL_DestroyTexture(title_tex);
	if(disc_background) SDL_DestroyTexture(disc_background);
	disc_background = nullptr;
	for(auto &glyph : disc_glyphs) if(glyph.second.tex) SDL_DestroyTexture(glyph.second.tex);
	disc_glyphs.clear();
SDL_DestroyTexture(map_preview_texture);map_preview_texture=nullptr;map_preview_path.clear();
	target = background = glow = title_tex = NULL;

	for(map<string, TextTex>::iterator i=text_cache.begin(); i!=text_cache.end(); ++i)
		if(i->second.tex) SDL_DestroyTexture(i->second.tex);
	text_cache.clear();

	Font *fonts[3] = { &font_ui, &font_ui_medium, &font_led };
	for(int f=0;f<3;f++)
	{
		for(map<int, TTF_Font*>::iterator i=fonts[f]->sizes.begin(); i!=fonts[f]->sizes.end(); ++i)
			if(i->second) TTF_CloseFont(i->second);
		fonts[f]->sizes.clear();
	}
}

void ZFrontEnd::InvalidateGraphics()
{
	FreeGraphics();
	scale = 0;
}

void ZFrontEnd::BuildGlow()
{
	const int N = 128;
	SDL_Surface *s = SDL_CreateSurface(N, N, SDL_PIXELFORMAT_ARGB8888);
	if(!s) return;
	for(int y=0;y<N;y++)
	{
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
		for(int x=0;x<N;x++)
		{
			float dx = (x + 0.5f) / N * 2 - 1, dy = (y + 0.5f) / N * 2 - 1;
			float d = sqrtf(dx * dx + dy * dy);
			float a = clampf(1 - d, 0, 1);
			a = powf(a, 2.2f);
			row[x] = ((Uint32)(a * 255) << 24) | 0x00ffffffu;
		}
	}
	glow = SDL_CreateTextureFromSurface(renderer, s);
	SDL_DestroySurface(s);
	if(glow) SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
}

void ZFrontEnd::BuildBackground()
{
	Raster R;
	R.init(tex_w, tex_h, scale);

	//backdrop: dark blue-grey bulkhead with a soft warm light from the title
	for(int py=0;py<R.H;py++)
		for(int px=0;px<R.W;px++)
		{
			float x = (px + 0.5f) / R.s, y = (py + 0.5f) / R.s;
			float n = fbm(x * 0.01f, y * 0.01f);
			float grain = (rnd2(px, py) - 0.5f) * 0.03f;
			float v = 0.10f + 0.07f * n + grain;
			//vertical panel seams every 160 units
			float seam = fmodf(x + 40.0f, 160.0f);
			if(seam < 1.5f) v *= 0.55f; else if(seam < 3.0f) v *= 1.25f;
			//vignette
			float vx = (x - CANVAS_W * 0.5f) / (CANVAS_W * 0.62f), vy = (y - CANVAS_H * 0.45f) / (CANVAS_H * 0.75f);
			float vig = clampf(1.15f - (vx * vx + vy * vy) * 0.75f, 0.25f, 1.1f);
			//warm pool of light under the title lamp
			float lx = (x - 640) / 520.0f, ly = (y - 120) / 260.0f;
			float warm = clampf(1 - (lx * lx + ly * ly), 0, 1);
			float *c = R.px(px, py);
			c[0] = v * vig * (0.92f + 0.9f * warm * warm);
			c[1] = v * vig * (0.98f + 0.55f * warm * warm);
			c[2] = v * vig * (1.10f + 0.15f * warm * warm);
		}

	PlateStyle beam = { 0.20f, 0.21f, 0.24f, 3.0f, 6.0f, false, 0.06f, 0.6f };
	PlateStyle gunmetal = { 0.27f, 0.285f, 0.31f, 5.0f, 9.0f, false, 0.07f, 0.5f };
	PlateStyle title_plate = { 0.46f, 0.37f, 0.27f, 6.0f, 10.0f, false, 0.08f, 0.8f };
	PlateStyle inset = { 0.035f, 0.03f, 0.03f, 3.0f, 0.0f, true, 0.0f, 0.0f };
	PlateStyle led = { 0.07f, 0.045f, 0.03f, 3.0f, 0.0f, true, 0.0f, 0.0f };
	PlateStyle button = { 0.33f, 0.34f, 0.36f, 4.0f, 5.0f, false, 0.06f, 0.4f };

	//top girder with angled braces running into the title plate
	draw_plate(R, chamfer_rect(-10, 70, CANVAS_W + 20, 58, 0, 0, 0, 0), beam);
	{
		vector<Pt> brace;
		Pt p;
		p.x = 300; p.y = 128; brace.push_back(p);
		p.x = 360; p.y = 128; brace.push_back(p);
		p.x = 430; p.y = 200; brace.push_back(p);
		p.x = 370; p.y = 200; brace.push_back(p);
		draw_plate(R, brace, beam);
		for(size_t i=0;i<brace.size();i++) brace[i].x = CANVAS_W - brace[i].x;
		draw_plate(R, brace, beam);
	}
	for(float x=24; x<CANVAS_W; x+=48)
	{
		draw_rivet(R, x, 80, 3.0f);
		draw_rivet(R, x, 118, 3.0f);
	}

	//title plate
	draw_plate(R, chamfer_rect(392, 26, 496, 180, 36, 36, 36, 36), title_plate);
	draw_plate(R, chamfer_rect(420, 52, 440, 128, 22, 22, 22, 22), PlateStyle{ 0.40f, 0.31f, 0.22f, 4.0f, 0.0f, true, 0.07f, 0.6f });
	draw_rivet(R, 432, 40, 4.0f); draw_rivet(R, 848, 40, 4.0f);
	draw_rivet(R, 432, 192, 4.0f); draw_rivet(R, 848, 192, 4.0f);

	//left console
	draw_pipe(R, 104, LP_Y + LP_H - 10, 20, CANVAS_H - LP_Y - LP_H + 20, true, 0.24f, 0.25f, 0.30f);
	draw_pipe(R, 130, LP_Y + LP_H - 10, 14, CANVAS_H - LP_Y - LP_H + 20, true, 0.20f, 0.22f, 0.30f);
	draw_plate(R, chamfer_rect(LP_X, LP_Y, LP_W, LP_H, 0, 26, 0, 26), gunmetal);
	for(int i=0;i<MAX_ITEMS;i++)
	{
		float y = ITEM_Y0 + i * ITEM_STEP;
		//lamp socket
		draw_plate(R, chamfer_rect(ITEM_X + 6, y + 7, 18, 34, 3, 3, 3, 3), inset);
	}
	draw_rivet(R, LP_X + 12, LP_Y + 12, 3.5f);
	draw_rivet(R, LP_X + LP_W - 14, LP_Y + LP_H - 12, 3.5f);
	draw_rivet(R, LP_X + 12, LP_Y + LP_H - 34, 3.5f);
	draw_rivet(R, LP_X + LP_W - 34, LP_Y + 12, 3.5f);

	//conduit between the consoles (lit dynamically)
	for(int i=0;i<3;i++)
		draw_plate(R, chamfer_rect(LP_X + LP_W - 4, ITEM_Y0 + 15 + i * ITEM_STEP, RP_X - LP_X - LP_W + 8, 18, 0, 0, 0, 0), inset);

	//right console
	draw_plate(R, chamfer_rect(RP_X, RP_Y, RP_W, RP_H, 26, 0, 26, 0), gunmetal);
	draw_plate(R, chamfer_rect(NAME_X, NAME_Y, NAME_W, NAME_H, 4, 4, 4, 4), inset);
	draw_plate(R, chamfer_rect(LED_X - 8, LED_Y - 8, LED_W + 16, LED_H + 16, 6, 6, 6, 6), PlateStyle{ 0.18f, 0.12f, 0.08f, 3.0f, 0.0f, true, 0.05f, 0.0f });
	draw_plate(R, chamfer_rect(LED_X, LED_Y, LED_W, LED_H, 3, 3, 3, 3), led);
	draw_plate(R, chamfer_rect(SCR_X, LED_Y, SCR_W, 54, 4, 4, 4, 4), button);
	draw_plate(R, chamfer_rect(SCR_X, LED_Y + LED_H - 54, SCR_W, 54, 4, 4, 4, 4), button);
	draw_plate(R, chamfer_rect(SCR_X + 13, LED_Y + 62, 14, LED_H - 124, 2, 2, 2, 2), inset);
	draw_plate(R, chamfer_rect(INFO_X, INFO_Y, INFO_W, INFO_H, 4, 4, 4, 4), inset);
	draw_rivet(R, RP_X + RP_W - 14, RP_Y + 12, 3.5f);
	draw_rivet(R, RP_X + 12, RP_Y + RP_H - 34, 3.5f);
	draw_rivet(R, RP_X + RP_W - 14, RP_Y + RP_H - 14, 3.5f);
	draw_rivet(R, RP_X + 36, RP_Y + 12, 3.5f);

	//quit plate, lever housing and its cable
	draw_plate(R, chamfer_rect(QUIT_X - 10, QUIT_Y - 10, QUIT_W + 20, QUIT_H + 20, 8, 8, 8, 8), PlateStyle{ 0.16f, 0.17f, 0.19f, 3.0f, 6.0f, false, 0.05f, 0.3f });
	draw_plate(R, chamfer_rect(QUIT_X, QUIT_Y, QUIT_W, QUIT_H, 5, 5, 5, 5), button);
	draw_pipe(R, KNOB_X - 4, LP_Y + LP_H - 10, 8, KNOB_Y - (LP_Y + LP_H) - KNOB_R + 14, true, 0.42f, 0.30f, 0.16f);
	draw_rivet(R, KNOB_X, KNOB_Y, KNOB_R);
	darken_box(R, 0, CANVAS_H - 18, CANVAS_W, 18, 0.35f, 18);

	SDL_Surface *s = raster_to_surface(R);
	if(!s) return;
	background = SDL_CreateTextureFromSurface(renderer, s);
	SDL_DestroySurface(s);
	if(background) SDL_SetTextureBlendMode(background, SDL_BLENDMODE_NONE);
}

void ZFrontEnd::BuildTitle()
{
	//embossed steel lettering for the title plate, built from a font mask
	TTF_Font *big = GetFont(font_led, 84 * scale);
	if(!big) return;

	SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface *mask = TTF_RenderText_Blended(big, "ZOD", 0, white);
	if(!mask) return;
	SDL_Surface *m = SDL_ConvertSurface(mask, SDL_PIXELFORMAT_ARGB8888);
	SDL_DestroySurface(mask);
	if(!m) return;

	int pad = (int)(8 * scale);
	int W = m->w + pad * 2, H = m->h + pad * 2;
	SDL_Surface *out = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_ARGB8888);
	if(!out) { SDL_DestroySurface(m); return; }

	#define MASK_AT(xx, yy) (((xx) < 0 || (yy) < 0 || (xx) >= m->w || (yy) >= m->h) ? 0.0f : \
		(((((Uint32 *)((Uint8 *)m->pixels + (yy) * m->pitch))[xx]) >> 24) / 255.0f))

	int sh = (int)(4 * scale), hl = (int)(max(1.0f, 1.5f * scale));
	for(int y=0;y<H;y++)
	{
		Uint32 *row = (Uint32 *)((Uint8 *)out->pixels + y * out->pitch);
		for(int x=0;x<W;x++)
		{
			int mx = x - pad, my = y - pad;
			float a = MASK_AT(mx, my);
			float shadow = MASK_AT(mx - sh, my - sh) * 0.75f;
			float up = MASK_AT(mx + hl, my + hl);      //edge facing the light
			float dn = MASK_AT(mx - hl, my - hl);
			float t = (float)my / max(1, m->h);
			//brushed chrome gradient with a horizon line
			float face = t < 0.48f ? 0.95f - t * 0.5f : 0.55f + (t - 0.48f) * 0.7f;
			face += (rnd2(0, my) - 0.5f) * 0.05f;
			float bevel = (a - up) * 0.55f - (a - dn) * 0.45f;
			float v = clampf(face + bevel, 0, 1);
			float r = v * 0.98f, g = v * 0.97f, b = v * 0.93f;
			float alpha = a + shadow * (1 - a);
			if(alpha > 0)
			{
				r = (r * a) / alpha; g = (g * a) / alpha; b = (b * a) / alpha;   //shadow is black
			}
			row[x] = ((Uint32)(clampf(alpha, 0, 1) * 255) << 24) |
				((Uint32)(clampf(r, 0, 1) * 255) << 16) | ((Uint32)(clampf(g, 0, 1) * 255) << 8) | (Uint32)(clampf(b, 0, 1) * 255);
		}
	}
	#undef MASK_AT

	title_tex = SDL_CreateTextureFromSurface(renderer, out);
	SDL_DestroySurface(out);
	SDL_DestroySurface(m);
	if(title_tex) SDL_SetTextureBlendMode(title_tex, SDL_BLENDMODE_BLEND);
}

TTF_Font *ZFrontEnd::GetFont(Font &f, float size)
{
	int px = (int)(size + 0.5f);
	map<int, TTF_Font*>::iterator i = f.sizes.find(px);
	if(i != f.sizes.end()) return i->second;

	TTF_Font *font = TTF_OpenFont(ZSDL_DataPath(f.file).c_str(), (float)px);
	if(!font) font = TTF_OpenFont(ZSDL_DataPath("assets/arial.ttf").c_str(), (float)px);
	if(!font) printf("ZFrontEnd: could not open font %s\n", f.file.c_str());
	f.sizes[px] = font;
	return font;
}

ZFrontEnd::TextTex *ZFrontEnd::Text(Font &f, float size, const string &text, SDL_Color c)
{
	if(text.empty()) return NULL;
    if(zod_high_contrast&&c.r+c.g+c.b<500)c={255,255,255,255};

	char key[96];
	snprintf(key, sizeof(key), "%p|%.1f|%02x%02x%02x|", (void *)&f, size*zod_text_percent/100.0f, c.r, c.g, c.b);
	string k = string(key) + text;

	map<string, TextTex>::iterator i = text_cache.find(k);
	if(i != text_cache.end())
	{
		i->second.last_used = SDL_GetTicks();
		return &i->second;
	}

	//keep the cache bounded (the name field and scores churn entries)
	if(text_cache.size() > 600)
	{
		Uint64 now = SDL_GetTicks();
		for(map<string, TextTex>::iterator j=text_cache.begin(); j!=text_cache.end();)
		{
			if(now - j->second.last_used > 5000) { SDL_DestroyTexture(j->second.tex); text_cache.erase(j++); }
			else ++j;
		}
	}

	TTF_Font *font = GetFont(f, size * scale * zod_text_percent / 100.0f);
	if(!font) return NULL;

	SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface *s = TTF_RenderText_Blended(font, text.c_str(), 0, white);
	if(!s) return NULL;

	TextTex t;
	t.tex = SDL_CreateTextureFromSurface(renderer, s);
	t.w = s->w / scale;
	t.h = s->h / scale;
	t.last_used = SDL_GetTicks();
	SDL_DestroySurface(s);
	if(!t.tex) return NULL;

	SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(t.tex, SDL_SCALEMODE_LINEAR);
	SDL_SetTextureColorMod(t.tex, c.r, c.g, c.b);
	text_cache[k] = t;
	return &text_cache[k];
}

float ZFrontEnd::TextWidth(Font &f, float size, const string &text)
{
	TextTex *t = Text(f, size, text, C_STEEL);
	return t ? t->w : 0;
}

void ZFrontEnd::DrawLabel(Font &f, float size, const string &text, float x, float y, SDL_Color c, int align)
{
	TextTex *t = Text(f, size, text, c);
	if(!t) return;
	if(align == 1) x -= t->w * 0.5f;
	else if(align == 2) x -= t->w;
	float fit=min(1.0f,max(1.0f,CANVAS_W-24-x)/max(1.0f,t->w));
	SDL_FRect dst = { x, y, t->w*fit, t->h*fit };
	SDL_SetTextureAlphaMod(t->tex, c.a);
	SDL_RenderTexture(renderer, t->tex, NULL, &dst);
}

void ZFrontEnd::DrawGlow(float cx, float cy, float radius, SDL_Color c, float alpha)
{
	if(zod_reduced_motion || !glow || alpha <= 0) return;
	SDL_FRect dst = { cx - radius, cy - radius, radius * 2, radius * 2 };
	SDL_SetTextureColorMod(glow, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(glow, (Uint8)(clampf(alpha, 0, 1) * 255));
	SDL_RenderTexture(renderer, glow, NULL, &dst);
}

void ZFrontEnd::FillRect(float x, float y, float w, float h, SDL_Color c, Uint8 a)
{
	SDL_FRect r = { x, y, w, h };
	SDL_SetRenderDrawBlendMode(renderer, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, a);
	SDL_RenderFillRect(renderer, &r);
}

void ZFrontEnd::FrameRect(float x, float y, float w, float h, SDL_Color c, Uint8 a, float t)
{
	FillRect(x, y, w, t, c, a);
	FillRect(x, y + h - t, w, t, c, a);
	FillRect(x, y + t, t, h - 2 * t, c, a);
	FillRect(x + w - t, y + t, t, h - 2 * t, c, a);
}

void ZFrontEnd::DrawPanel(float x,float y,float w,float h,float alpha)
{
    string key="panel:"+to_string(zod_high_contrast)+":"+to_string((int)w)+":"+to_string((int)h);
    auto it=text_cache.find(key);
    if(it==text_cache.end()) {
        SDL_Surface *s=SDL_CreateSurface((int)w,(int)h,SDL_PIXELFORMAT_RGBA32);
        if(!s) return;
        ZMenuTheme::panel(s,0,0,(int)w,(int)h,3);
        TextTex t; t.tex=SDL_CreateTextureFromSurface(renderer,s);t.w=w;t.h=h;t.last_used=SDL_GetTicks();SDL_DestroySurface(s);
        if(!t.tex) return;
        SDL_SetTextureBlendMode(t.tex,SDL_BLENDMODE_BLEND);it=text_cache.emplace(key,t).first;
    }
    it->second.last_used=SDL_GetTicks();SDL_SetTextureAlphaMod(it->second.tex,(Uint8)(alpha*255));
    SDL_FRect dst{x,y,w,h};SDL_RenderTexture(renderer,it->second.tex,nullptr,&dst);
}

void ZFrontEnd::DimScreen(Uint8 a)
{
	FillRect(0, 0, CANVAS_W, CANVAS_H, C_BLACK, a);
}

bool ZFrontEnd::Hover(float x, float y, float w, float h) const
{
	return mouse_x >= x && mouse_x < x + w && mouse_y >= y && mouse_y < y + h;
}

bool ZFrontEnd::Clicked(float x, float y, float w, float h)
{
	if(!clicked) return false;
	bool in = click_x >= x && click_x < x + w && click_y >= y && click_y < y + h;
	bool pressed_in = press_x >= x && press_x < x + w && press_y >= y && press_y < y + h;
	if(in && pressed_in) { clicked = false; return true; }
	return false;
}

bool ZFrontEnd::Button(const string &label, float x, float y, float w, float h, bool enabled, bool primary)
{
	int index=enabled?focus_index++:-1;bool focused=index>=0&&focus_button==index;
	bool hot = enabled && (focused||Hover(x, y, w, h));
	bool down = hot && mouse_down;

    string key="button:"+to_string(zod_high_contrast)+":"+to_string((int)w)+":"+to_string((int)h)+":"+to_string(hot)+to_string(down)+to_string(primary);
    auto it=text_cache.find(key);
    if(it==text_cache.end()) {
        SDL_Surface *s=SDL_CreateSurface((int)w,(int)h,SDL_PIXELFORMAT_RGBA32);
        if(s) {
            ZMenuTheme::button(s,0,0,(int)w,(int)h,hot,down,primary,2);
            TextTex t;t.tex=SDL_CreateTextureFromSurface(renderer,s);t.w=w;t.h=h;t.last_used=SDL_GetTicks();SDL_DestroySurface(s);
            if(t.tex) { SDL_SetTextureBlendMode(t.tex,SDL_BLENDMODE_BLEND);it=text_cache.emplace(key,t).first; }
        }
    }
    if(it!=text_cache.end()) {
        it->second.last_used=SDL_GetTicks();SDL_SetTextureAlphaMod(it->second.tex,enabled?255:100);
        SDL_FRect dst{x,y,w,h};SDL_RenderTexture(renderer,it->second.tex,nullptr,&dst);
    }

	SDL_Color tc = !enabled ? C_STEEL_DIM : (hot ? C_STEEL_HI : C_STEEL);
	DrawLabel(font_ui, h * 0.56f, label, x + w * 0.5f, y + h * 0.5f - h * 0.36f + (down ? 1 : 0), tc, 1);

	if(enabled&&focused&&focus_activate){focus_activate=false;return true;}return enabled && Clicked(x, y, w, h);
}

void ZFrontEnd::Sound(ZAudio_Sound *s)
{
	if(!s || zod_effects_volume <= 0) return;
	int ch = ZAudio_PlayChannel(-1, s, 0);
	if(ch >= 0) ZAudio_SetChannelVolume(ch, ZAUDIO_MAX_VOLUME);
}

// ---------------------------------------------------------------------------
// campaign helpers

bool ZFrontEnd::LevelUnlocked(int i) const
{
	if(i < 0 || i >= (int)levels.size()) return false;
	return i <= max(progress_unlocked, progress_current);
}

string ZFrontEnd::PlanetName(int p) const
{
	static const char *names[MAX_PLANET_TYPES] = { "Desert", "Volcanic", "Arctic", "Jungle", "City" };
	if(p < 0 || p >= MAX_PLANET_TYPES) return "Unknown";
	return names[p];
}

string ZFrontEnd::LevelLabel(int i) const
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%02d", i + 1);
	return buf;
}

static string format_time(int secs)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
	return buf;
}

static string format_score(int v)
{
	char buf[32];
	if(v >= 1000) snprintf(buf, sizeof(buf), "%d,%03d", v / 1000, v % 1000);
	else snprintf(buf, sizeof(buf), "%d", v);
	return buf;
}

static string base_name(const string &f)
{
	size_t slash = f.find_last_of("/\\");
	return slash == string::npos ? f : f.substr(slash + 1);
}

void ZFrontEnd::StartLevel(int index, bool via_briefing)
{
    history_briefing=false;
    if(!selected_campaign.id.empty()&&!editor_testing&&!ZCampaign_MigrateIdentity(selected_campaign,campaign_error)){screen=SCR_CAMPAIGNS;return;}
    if(!selected_campaign.id.empty()&&!editor_testing){auto errors=ZCampaign_Compatibility(selected_campaign,campaigns);if(!errors.empty()){campaign_error=errors.front();screen=SCR_CAMPAIGNS;return;}}
	if(index < 0 || index >= (int)levels.size()) return;

    auto completed=ZCampaign_Completed(selected_campaign.id.empty()?"original":selected_campaign.id);
    string key=selected_campaign.id.empty()?levels[index].base:ZCampaign_MissionKey(selected_campaign,index);
    mission_replay=std::find(completed.begin(),completed.end(),key)!=completed.end();
	current_level = index;

	if(via_briefing && zod_scenes)
	{
        briefing_scroll=0;
        if(selected_campaign.id.empty())PlayLevelIntro(index);
        else {
            const auto &mission=selected_campaign.missions[index];
            if(!mission.video.empty()) {
                if(editor_testing)ZPlaytest_Event(selected_campaign,index,"intro-video-attempt");
                if(mission.video.compare(0,9,"original:")==0)ZMovie_PlayClip(mission.video.substr(9));
                else {string file;if(ZCampaign_SafeAsset(selected_campaign.root,mission.video,file)){ZAudio_PauseMusic(true);ZMovie_Play(file);ZAudio_PauseMusic(false);SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);SDL_FlushEvents(SDL_EVENT_MOUSE_BUTTON_DOWN,SDL_EVENT_MOUSE_WHEEL);}}
            }
            ZPortrait::Init();campaign_portrait.SetRobotID(GRUNT);
            for(int i=0;i<MAX_ROBOT_TYPES;++i)if(robot_type_string[i]==mission.character)campaign_portrait.SetRobotID(i);
            campaign_portrait.SetTeam(RED_TEAM);campaign_portrait.SetCords(0,0);campaign_portrait.SetOverMap(false);campaign_portrait.SetTerrainType((planet_type)levels[index].planet);campaign_portrait.SetDoRandomAnims(!zod_reduced_motion);campaign_portrait.StartAnim(UNIT_REPORTING1_ANIM,false);
        }
        if(editor_testing)ZPlaytest_Event(selected_campaign,index,"briefing");
		briefing_level = index;
		screen = SCR_BRIEFING;
		Sound(snd_click);
		return;
	}

    if(editor_testing&&!selected_campaign.id.empty())ZPlaytest_Event(selected_campaign,index,"start");
	Sound(snd_start);
	if(selected_campaign.id.empty())actions.push_back(Action(FE_START_MAP,index));
    else {Action action(FE_START_CAMPAIGN_MAP,index);action.text=levels[index].file;actions.push_back(action);community_running=true;community_next=-1;if(!mission_replay)progress_current=index;if(!editor_testing&&!mission_replay)ZCampaign_Progress(selected_campaign.id,progress_current,progress_unlocked,true);}
	screen = SCR_LOADING;
	loading_since = SDL_GetTicks();deployment_stage="Connecting to the game server";
}

void ZFrontEnd::ActivateItem(int item)
{
	//the key press that got us here must not also press a button on the
	//screen this opens (it is drawn later in the same frame)
	kb_activate = false;
	clicked = false;
	switch(item)
	{
	case ITEM_NEW:
        mode_choice=0;screen=SCR_SINGLE_PLAYER;
		break;
	case ITEM_LOAD:
		LoadSaveInfo();
		if(have_save) { Sound(snd_click); load_selected = 0; delete_pending = -1; screen = SCR_LOAD; }
		else if(progress_current >= 0) StartLevel(progress_current, true);
		break;
	case ITEM_CUSTOM:
		Sound(snd_click);
		ScanCustomMaps(true);
		newmap_open=false;
        LaunchEditor({});
		break;
	case ITEM_REPLAY:
		Sound(snd_click);
		screen = SCR_REPLAY;
		replay_selected = progress_current >= 0 ? progress_current : 0;
		if(replay_selected < (int)levels.size()) replay_planet = levels[replay_selected].planet;
		break;
	case ITEM_SKIRMISH:
		Sound(snd_click);
		actions.push_back(Action(FE_OPEN_SKIRMISH));
		break;
	case ITEM_MULTI:
		Sound(snd_click);
        screen=SCR_MULTIPLAYER_MENU;
		break;
	case ITEM_SCORES:
		Sound(snd_click);
		ZHighScore_All(true);
		screen = SCR_SCORES;
		break;
	}
}

// ---------------------------------------------------------------------------
// settings

int ZFrontEnd::SettingCount() const { return MAX_SETTINGS; }

string ZFrontEnd::SettingLabel(int i) const
{
	static const char *labels[MAX_SETTINGS] =
		{ "Fullscreen", "Scenes", "Gamma", "Volume", "Music", "Difficulty", "Mouse", "Motion smoothing", "Units", "Image filter", "Controller speed", "Keys", "Import disc" };
	return (i >= 0 && i < MAX_SETTINGS) ? labels[i] : "";
}

string ZFrontEnd::SettingHelp(int i) const
{
	switch(i)
	{
	case SET_FULLSCREEN: return "Fill the whole screen, or play in a window. (Alt+Enter in game.)";
	case SET_SCENES:     return "Show the intro splash, the original cutscene videos (if installed) and a mission briefing before each level.";
	case SET_GAMMA:      return "Brighten or darken the picture.";
	case SET_VOLUME:     return "Sound effect volume.";
	case SET_IMPORT: return "Import graphics, music, speech and videos from your original Z ISO or BIN/CUE. No account or internet required.";
	case SET_MUSIC:
		if(zod_music && zod_music_style == MUSIC_STYLE_MIDI && !ZMusicEngine::MidiPossible())
			return "The original MIDI score needs a SoundFont in assets/soundfonts. Playing the recorded tracks.";
		return "Original MIDI: the 1996 score played with a SoundFont. Recorded: audio tracks, including a rendered CD score if imported.";
	case SET_DIFFICULTY: return "How smart and aggressive the enemy commander is. Harder scores more.";
	case SET_MOUSE:      return "Classic: left click orders, like 1996. Modern: right click orders.";
	case SET_SMOOTHING:  return "Eases movement corrections while units move. Does not blur textures; use Image filter for pixel softness.";
	case SET_UNITS:      return "Maximum combat units per team, from the next level on.";
	case SET_PICTURE:    return "Smooth: soft scaling. Sharp: crisp pixels. Pixel-perfect: whole-number scale, with black bars if it does not fit.";
	case SET_STICK:      return "How fast a controller's left stick moves the cursor.";
	case SET_KEYS:       return "Change the pan, pause and other letter keys.";
	}
	return "";
}

void ZFrontEnd::AdjustSetting(int i, int dir, int absolute)
{
	switch(i)
	{
	case SET_FULLSCREEN:
		zod_fullscreen = absolute != -1000 ? absolute != 0 : !zod_fullscreen;
		ZVideo_SetFullscreen(zod_fullscreen);
		break;
	case SET_SCENES:
		zod_scenes = absolute != -1000 ? absolute != 0 : !zod_scenes;
		break;
	case SET_GAMMA:
		zod_gamma = absolute != -1000 ? absolute : zod_gamma + dir;
		if(zod_gamma < -ZVIDEO_GAMMA_STEPS) zod_gamma = -ZVIDEO_GAMMA_STEPS;
		if(zod_gamma > ZVIDEO_GAMMA_STEPS) zod_gamma = ZVIDEO_GAMMA_STEPS;
		ZVideo_SetGamma(zod_gamma);
		break;
	case SET_VOLUME:
		{
			int v = absolute != -1000 ? absolute : zod_volume_setting + dir;
			if(v < 0) v = 0;
			if(v > MAX_SOUND_SETTINGS - 1) v = MAX_SOUND_SETTINGS - 1;
			zod_volume_setting = v;
			actions.push_back(Action(FE_SET_VOLUME, v));
		}
		break;
	case SET_MUSIC:
		{
			//Off -> Original (MIDI) -> Recorded
			int state = !zod_music ? 0 : (zod_music_style == MUSIC_STYLE_MIDI ? 1 : 2);
			state = (state + dir + 3) % 3;
			zod_music = state != 0;
			if(state == 1) zod_music_style = MUSIC_STYLE_MIDI;
			else if(state == 2) zod_music_style = MUSIC_STYLE_RECORDED;
			actions.push_back(Action(FE_SET_MUSIC, zod_music ? 1 : 0));
		}
		break;
	case SET_DIFFICULTY:
		zod_bot_difficulty = (zod_bot_difficulty + dir + MAX_BOT_DIFFICULTY) % MAX_BOT_DIFFICULTY;
		break;
	case SET_MOUSE:
		zod_classic_mouse = absolute != -1000 ? absolute != 0 : !zod_classic_mouse;
		break;
	case SET_SMOOTHING:
		zod_render_smoothing = absolute != -1000 ? absolute != 0 : !zod_render_smoothing;
		break;
	case SET_UNITS:
		zod_max_units_per_team += dir * 10;
		if(zod_max_units_per_team < UNIT_LIMIT_MIN) zod_max_units_per_team = UNIT_LIMIT_MIN;
		if(zod_max_units_per_team > UNIT_LIMIT_MAX) zod_max_units_per_team = UNIT_LIMIT_MAX;
		break;
	case SET_PICTURE:
		zod_picture_mode = (zod_picture_mode + dir + MAX_PICTURE_MODES) % MAX_PICTURE_MODES;
		ZVideo_SetPictureMode(zod_picture_mode);
		break;
	case SET_STICK:
		zod_stick_speed = (zod_stick_speed + dir + MAX_STICK_SPEEDS) % MAX_STICK_SPEEDS;
		break;
	case SET_KEYS:
		return;   //opened from its own button
	}

	ZPrefs_Save();
	Sound(snd_toggle);
}

// ---------------------------------------------------------------------------
// rendering

void ZFrontEnd::Render()
{
    ZPerfScope profile("frontend_frame");
    ZGamepad_SetEditor(screen==SCR_EDITOR);
    if(focus_screen!=screen){focus_screen=screen;focus_button=-1;}focus_index=0;
	asset_import.Poll();
	if(!visible) return;
	if(!EnsureGraphics()) return;

	Uint64 now = SDL_GetTicks();

	ZVideo_SetCanvasMode(CANVAS_W, CANVAS_H);
	SDL_SetRenderTarget(renderer, target);
	SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
	SDL_SetRenderViewport(renderer, nullptr);
	SDL_SetRenderClipRect(renderer, nullptr);
	SDL_SetRenderScale(renderer, scale, scale);

	SDL_FRect full = { 0, 0, (float)CANVAS_W, (float)CANVAS_H };
	bool disc_skin = disc_background && screen != SCR_IMPORT;
	SDL_SetTextureScaleMode(target, SDL_SCALEMODE_LINEAR);
	SDL_RenderTexture(renderer, disc_skin ? disc_background : background, NULL, &full);

	bool main_interactive = screen == SCR_MAIN&&!deployment_failed;

	//escape on the main screen asks to quit
	if(screen == SCR_MAIN && kb_back && !editing_name&&!deployment_failed)
	{
		kb_back = false;
		screen_before_quit = SCR_MAIN;
		screen = SCR_QUIT;
		main_interactive = false;
	}

	RenderMain(main_interactive);

	switch(screen)
	{
	case SCR_REPLAY:   RenderReplay(); break;
	case SCR_SCORES:   RenderScores(); break;
	case SCR_BRIEFING: RenderBriefing(); break;
	case SCR_LOADING:  RenderLoading(); break;
	case SCR_QUIT:     RenderQuit(); break;
	case SCR_LOAD:     RenderLoad(); break;
	case SCR_CUSTOM:   RenderCustom(); break;
	case SCR_KEYS:     RenderKeys(); break;
	case SCR_IMPORT:   RenderImport(); break;
	case SCR_SETTINGS: RenderOptions(); break;
    case SCR_CAMPAIGNS: RenderCampaigns();break;
    case SCR_RECORDINGS:RenderRecordings();break;
    case SCR_ALERTS:RenderAlerts();break;
    case SCR_MULTIPLAYER_GUIDE:RenderMultiplayerGuide();break;
    case SCR_BUILD_INFO:RenderBuildInfo();break;
    case SCR_DIAGNOSTICS:RenderDiagnostics();break;
    case SCR_SINGLE_PLAYER:RenderPlayModes(false);break;
    case SCR_MULTIPLAYER_MENU:RenderPlayModes(true);break;
    case SCR_EDITOR: if(workspace && workspace->Render()) {
        string play=workspace->PlayPath();editor_testing=!play.empty();if(!editor_testing)workspace.reset();ZVideo_SetFullscreen(editor_fullscreen_previous);screen=SCR_MAIN;ScanCustomMaps(true);
        if(editor_testing&&!workspace->TestCampaign().root.empty()){editor_previous_campaign=selected_campaign;editor_previous_levels=levels;editor_previous_level=current_level;editor_previous_current=progress_current;editor_previous_unlocked=progress_unlocked;selected_campaign=workspace->TestCampaign();levels.clear();for(const auto &mission:selected_campaign.missions){LevelInfo info;info.title=mission.title;info.base=mission.map;ZCampaign_SafeAsset(selected_campaign.root,mission.map,info.file);ZMap map;info.planet=DESERT;info.w=info.h=0;if(map.Read(info.file.c_str())){info.planet=map.GetMapBasics().terrain_type;info.w=map.GetMapBasics().width;info.h=map.GetMapBasics().height;}levels.push_back(info);}StartLevel(workspace->TestMission(),true);break;}
        if(!play.empty()) {Action action(FE_START_MAP_FILE);action.text=play;actions.push_back(action);screen=SCR_LOADING;loading_since=SDL_GetTicks();}
    } break;
	default: break;
	}

    focus_count=focus_index;focus_activate=false;
    if(ZGamepad_Active())DrawLabel(font_ui_medium,15,screen==SCR_EDITOR?"Controller: stick pointer / A paint / B back / X undo / Y redo / shoulders focus":"Controller: A confirm / B back / triggers click / shoulders focus",24,782,C_STEEL_DIM);
	RenderStatus();

	//one-shot inputs are consumed by now
	clicked = false;
	wheel = 0;
	kb_activate = kb_back = kb_delete = false;
	kb_dx = kb_dy = 0;

	SDL_SetRenderScale(renderer, 1.0f, 1.0f);
	SDL_SetRenderTarget(renderer, NULL);

	ZVideo_PresentTexture(target);
	(void)now;
}

void ZFrontEnd::RenderMain(bool interactive)
{
    RenderCommandMain(interactive);
}

ZFrontEnd::TextTex &ZFrontEnd::DiscGlyph(unsigned char character)
{
    auto found = disc_glyphs.find(character);
    if(found != disc_glyphs.end()) return found->second;
    TextTex glyph{};
    char path[128]; snprintf(path,sizeof(path),"assets/fonts/small_white/char_%03u.png",(unsigned)character);
    if(SDL_Surface *surface = IMG_Load(ZSDL_DataPath(path).c_str())) {
        glyph.w = (float)surface->w; glyph.h = (float)surface->h;
        glyph.tex = SDL_CreateTextureFromSurface(renderer,surface);
        SDL_DestroySurface(surface);
        if(glyph.tex) { SDL_SetTextureScaleMode(glyph.tex,SDL_SCALEMODE_NEAREST); SDL_SetTextureBlendMode(glyph.tex,SDL_BLENDMODE_BLEND); }
    }
    return disc_glyphs.emplace(character,glyph).first->second;
}

void ZFrontEnd::DrawDiscText(const string &value,float x,float y,SDL_Color colour,int align)
{
    float width=0;
    for(unsigned char c : value) {
        TextTex &glyph=DiscGlyph(c);
        if(!glyph.tex) { DrawLabel(font_ui,28,value,x*4,y*4,colour,align); return; }
        width+=glyph.w;
    }
    if(align==1) x-=width*0.5f;
    else if(align==2) x-=width;
    for(unsigned char c : value) {
        TextTex &glyph=DiscGlyph(c);
        SDL_SetTextureColorMod(glyph.tex,colour.r,colour.g,colour.b);
        SDL_SetTextureAlphaMod(glyph.tex,colour.a);
        SDL_FRect dst{x*4,y*4,glyph.w*4,glyph.h*4};
        SDL_RenderTexture(renderer,glyph.tex,nullptr,&dst);
        x+=glyph.w;
    }
}

void ZFrontEnd::RenderCommandMain(bool interactive)
{
    DimScreen(235);
    DrawLabel(font_led,48,"zod-enhanced",72,40,C_AMBER);
    DrawLabel(font_ui,22,"ZOD ENGINE ENHANCED",74,105,C_STEEL_DIM);
    DrawLabel(font_ui,22,"SINGLE PLAYER / PUBLIC / LAN / DIRECT",1208,66,C_STEEL,2);
    FillRect(72,148,1136,2,C_STEEL_DIM,100);
    DrawPanel(72,190,438,490); DrawPanel(536,190,672,490);
    DrawLabel(font_ui,20,"01 / OPERATIONS",98,210,C_AMBER);
    DrawLabel(font_ui,20,"02 / CURRENT OPERATION",564,210,C_AMBER);
    const int order[]={ITEM_NEW,ITEM_LOAD,ITEM_REPLAY,ITEM_SKIRMISH,ITEM_CUSTOM,ITEM_MULTI,ITEM_SCORES};
    auto enabled=[&](int item) { return item!=ITEM_LOAD || have_save || progress_current>=0; };
    if(interactive && kb_dy) {
        int position=-1;
        for(int i=0;i<MAX_ITEMS;++i) if(order[i]==kb_item) position=i;
        if(position<0) position=kb_dy>0?0:MAX_ITEMS-1;
        else position=(position+kb_dy+MAX_ITEMS)%MAX_ITEMS;
        while(!enabled(order[position])) position=(position+kb_dy+MAX_ITEMS)%MAX_ITEMS;
        kb_item=order[position]; Sound(snd_hover);
    }
    for(int i=0;i<MAX_ITEMS;++i) {
        int item=order[i]; float y=258+i*55;
        bool selected=interactive && kb_item==item;
        if(Button(item_label[item],98,y,386,44,interactive && enabled(item),selected)) ActivateItem(item);
    }
    if(interactive && kb_activate && kb_item>=0 && enabled(kb_item)) ActivateItem(kb_item);
    bool campaign=progress_current>=0 && progress_current<(int)levels.size();
    string mission=campaign?levels[progress_current].title:"READY FOR DEPLOYMENT";
    while(mission.size()>3 && TextWidth(font_led,29,mission)>610) mission.erase(mission.size()-1);
    DrawLabel(font_led,29,mission,566,267,C_STEEL_HI);
    DrawLabel(font_ui_medium,22,"Take command. Capture territory. Outbuild the enemy.",566,314,C_STEEL);
    FillRect(564,369,616,1,C_STEEL_DIM,100);
    DrawLabel(font_ui,18,"SAVED BATTLES",566,386,C_AMBER);
    int count=(int)saves.size();
    if(load_selected>=count) load_selected=-1;
    if(interactive && wheel && Hover(564,420,616,138)) disc_save_scroll-=wheel>0?1:-1;
    disc_save_scroll=max(0,min(disc_save_scroll,max(0,count-3)));
    if(count==0) DrawLabel(font_ui_medium,23,"Your next campaign begins here.",580,444,C_STEEL_DIM);
    for(int i=0;i<3 && i+disc_save_scroll<count;++i) {
        int row=i+disc_save_scroll; float y=420+i*46;
        bool selected=row==load_selected;
        FillRect(564,y,616,40,selected?SDL_Color{47,61,72,255}:SDL_Color{14,23,30,255});
        if(selected) FillRect(564,y,3,40,C_AMBER);
        DrawLabel(font_ui,23,saves[row].label.substr(0,40),580,y+4,C_STEEL);
        if(interactive && Clicked(564,y,616,40)) { load_selected=row; Sound(snd_click); }
    }
    if(count>3) DrawLabel(font_ui_medium,16,"Scroll for more saves / Load Game to manage",1178,563,C_STEEL_DIM,2);
    bool saved=load_selected>=0 && load_selected<count;
    if(Button(saved?"Resume saved battle":campaign?"Continue campaign":"Start campaign",564,598,616,48,interactive,true)) {
        if(saved) { Sound(snd_start); actions.push_back(Action(FE_LOAD_SAVE,saves[load_selected].slot)); screen=SCR_LOADING;loading_since=SDL_GetTicks(); }
        else if(campaign)StartLevel(progress_current,true);else {mode_choice=0;screen=SCR_SINGLE_PLAYER;}
    }
    if(Button("Quit",72,714,150,46,interactive)) { screen_before_quit=SCR_MAIN;screen=SCR_QUIT;Sound(snd_click); }
    DrawLabel(font_ui_medium,18,"Build "+ZBuild_ID(),258,727,C_STEEL_DIM);
    if(Button("Import disc",868,714,160,46,interactive)) { screen=SCR_IMPORT;Sound(snd_click); }
    if(Button("Options",1048,714,160,46,interactive)) { screen=SCR_SETTINGS;Sound(snd_click); }
}

void ZFrontEnd::RenderOptions()
{
    DimScreen(190);DrawPanel(220,82,840,650);
    DrawLabel(font_ui,32,"SETTINGS",252,108,C_AMBER);
    DrawLabel(font_ui_medium,20,"Display, audio and controls",252,149,C_STEEL_DIM);
    RenderControlPanel(true);
    if(Button("Back",874,670,150,42) || (kb_back && !editing_name)) {
        if(editing_name) CommitName();kb_back=false;screen=(Screen)settings_return_screen;Sound(snd_click);
    }
}

void ZFrontEnd::RenderControlPanel(bool interactive)
{
	const float NAME_X=470,NAME_Y=192,NAME_W=554,NAME_H=42;
    FillRect(NAME_X,NAME_Y,NAME_W,NAME_H,SDL_Color{14,23,30,255});
    // Editable commander identity.
    DrawLabel(font_ui,23,"Commander",254,200,C_STEEL);

	bool name_hot = interactive && Hover(NAME_X, NAME_Y, NAME_W, NAME_H);
	if(interactive && Clicked(NAME_X, NAME_Y, NAME_W, NAME_H) && !editing_name)
	{
		editing_name = true;
		name_fresh = true;
		name_edit = zod_player_name;
		if(ZVideo_GetWindow()) SDL_StartTextInput(ZVideo_GetWindow());
		Sound(snd_click);
	}
	else if(editing_name && clicked && !Hover(NAME_X, NAME_Y, NAME_W, NAME_H))
		CommitName();

	if(editing_name || name_hot) FrameRect(NAME_X + 2, NAME_Y + 2, NAME_W - 4, NAME_H - 4, C_AMBER_DIM, 255, 1.5f);
	{
		string shown = editing_name ? name_edit : zod_player_name;
		if(shown.empty() && !editing_name) shown = "Player";
		if(editing_name && name_fresh && !shown.empty())
		{
			//whole name selected: the first key replaces it
			FillRect(NAME_X + 12, NAME_Y + 8, TextWidth(font_ui, 20, shown) + 8, 28, C_AMBER);
			DrawLabel(font_ui, 20, shown, NAME_X + 16, NAME_Y + 10, C_LED_BG);
		}
		else DrawLabel(font_ui, 20, shown, NAME_X + 16, NAME_Y + 10, C_AMBER);
		if(editing_name && !name_fresh && ((SDL_GetTicks() / 450) & 1))
		{
			float w = shown.empty() ? 0 : TextWidth(font_ui, 20, shown);
			FillRect(NAME_X + 18 + w, NAME_Y + 10, 3, 24, C_AMBER);
		}
	}

	RenderSettings(interactive);

}

void ZFrontEnd::RenderSettings(bool interactive)
{
    const char *categories[]={"Graphics","Audio","Game","Controls","Media","Access"};
    const vector<vector<int>> groups={{SET_FULLSCREEN,SET_GAMMA,SET_PICTURE,SET_SMOOTHING},{SET_VOLUME,SET_MUSIC},{SET_SCENES,SET_DIFFICULTY,SET_UNITS},{SET_MOUSE,SET_STICK,SET_KEYS},{SET_IMPORT},{}};
    for(int i=0;i<6;++i) if(Button(categories[i],252+i*130,254,122,40,interactive,settings_category==i)) {settings_category=i;settings_focus=0;}
    if(settings_category==5&&kb_dy)settings_focus=(settings_focus+kb_dy+4)%4;
    if(kb_dy&&!groups[settings_category].empty()){settings_focus=max(0,min(settings_focus+kb_dy,(int)groups[settings_category].size()-1));}
    if(kb_dx){if(settings_category==1){int *v=settings_focus==0?&zod_effects_volume:&zod_music_volume;*v=max(0,min(100,*v+kb_dx*5));ZAudio_ApplyVolumes();ZPrefs_Save();}else if(!groups[settings_category].empty())AdjustSetting(groups[settings_category][min(settings_focus,(int)groups[settings_category].size()-1)],kb_dx);}
    if(Button("Reset category",252,618,220,40,interactive)){ZPrefs_ResetCategory(settings_category);ZVideo_SetFullscreen(zod_fullscreen);ZVideo_SetGamma(zod_gamma);ZVideo_SetPictureMode(zod_picture_mode);ZAudio_ApplyVolumes();actions.push_back(Action(FE_SET_MUSIC,zod_music?1:0));}
    DrawLabel(font_ui_medium,14,"Arrows adjust settings; Tab navigates.",490,682,C_STEEL_DIM);
    if(Button("About / build information",650,618,358,40,interactive)){build_info_message.clear();screen=SCR_BUILD_INFO;}
    if(settings_category==3){
        if(Button("Stick dead zone: "+to_string(zod_stick_deadzone)+"%",268,494,740,40,interactive)){zod_stick_deadzone=zod_stick_deadzone>=40?5:min(40,zod_stick_deadzone+5);ZPrefs_Save();}
        if(Button("Edge scrolling: "+to_string(zod_edge_scroll_percent)+"%",268,544,740,40,interactive)){zod_edge_scroll_percent=(zod_edge_scroll_percent+25)%225;ZPrefs_Save();}
        DrawLabel(font_ui_medium,17,"Controller: stick moves pointer, triggers click, shoulders focus buttons.",268,590,C_STEEL_DIM);
    }
    if(settings_category==2&&Button(ZPerf_Enabled()?"Profiling: On":"Profiling: Off",268,502,354,40,interactive))ZPerf_Enable(!ZPerf_Enabled());
    if(settings_category==2){if(Button(zod_record_replays?"Record replays: On":"Record replays: Off",650,502,358,40,interactive)){zod_record_replays=!zod_record_replays;ZPrefs_Save();}if(Button("Replay recordings",268,554,354,40,interactive))OpenRecordings();if(Button("Battle alerts",650,554,358,40,interactive))screen=SCR_ALERTS;}
    if(settings_category==4){if(Button("Subtitle size: "+to_string(zod_subtitle_percent)+"%",268,494,354,36,interactive)){zod_subtitle_percent=zod_subtitle_percent>=200?75:min(200,zod_subtitle_percent+25);ZPrefs_Save();}if(Button(zod_subtitle_background?"Subtitle background: On":"Subtitle background: Off",650,494,358,36,interactive)){zod_subtitle_background=!zod_subtitle_background;ZPrefs_Save();}}
    if(settings_category==5&&Button(string("Apply preset: ")+ZPrefs_PresetName((zod_access_preset+1)%4),268,554,740,40,interactive)){ZPrefs_ApplyPreset((zod_access_preset+1)%4);ZVideo_SetGamma(zod_gamma);return;}
    if(settings_category==5){
        const string labels[]={"Text size: "+to_string(zod_text_percent)+"%","Battle cursor: "+to_string(zod_cursor_percent)+"%",string("Team symbols: ")+(zod_team_markers?"On":"Off"),string("Reduced motion: ")+(zod_reduced_motion?"On":"Off")};
        for(int i=0;i<4;++i)if(Button(labels[i],268,316+i*60,740,44,interactive,settings_focus==i)||((kb_activate||kb_dx)&&settings_focus==i)){kb_activate=false;settings_focus=i;if(i==0)zod_text_percent=100+(zod_text_percent-100+(kb_dx<0?20:10))%30;if(i==1)zod_cursor_percent=100+(zod_cursor_percent-100+(kb_dx<0?100:50))%150;if(i==2)zod_team_markers=!zod_team_markers;if(i==3)zod_reduced_motion=!zod_reduced_motion;ZPrefs_Save();}
        DrawLabel(font_ui_medium,17,"Presets change related settings together; individual options remain editable.",268,600,C_STEEL_DIM);
    }
    if(settings_category==4&&Button("Multiplayer guide",268,554,354,40,interactive)){guide_return=screen;screen=SCR_MULTIPLAYER_GUIDE;multiplayer_guide_step=0;}
    if(settings_category==4&&Button("Export diagnostics",650,554,358,40,interactive)){ReviewDiagnostics();}
    if(settings_category==4){DrawLabel(font_ui,23,"Video volume",268,398,C_STEEL);if(Button("-",650,390,40,40,interactive)){zod_video_volume=max(0,zod_video_volume-5);ZPrefs_Save();}DrawLabel(font_ui,22,to_string(zod_video_volume)+"%",848,398,C_STEEL_HI,1);if(Button("+",968,390,40,40,interactive)){zod_video_volume=min(100,zod_video_volume+5);ZPrefs_Save();}if(Button(zod_video_subtitles?"Subtitles: On":"Subtitles: Off",650,448,358,40,interactive)){zod_video_subtitles=!zod_video_subtitles;ZPrefs_Save();}}
    if(settings_category==0){
        DrawLabel(font_ui_medium,16,"LIVE FILTER / MOVEMENT PREVIEW",548,558,C_STEEL_DIM);
        SDL_Surface *sample=SDL_CreateSurface(8,8,SDL_PIXELFORMAT_RGBA32);if(sample){for(int y=0;y<8;++y)for(int x=0;x<8;++x){SDL_Rect pixel{x,y,1,1};auto c=(x+y)%2?C_AMBER:C_STEEL_DIM;SDL_FillSurfaceRect(sample,&pixel,SDL_MapSurfaceRGBA(sample,c.r,c.g,c.b,255));}SDL_Texture *tex=SDL_CreateTextureFromSurface(renderer,sample);SDL_DestroySurface(sample);if(tex){SDL_SetTextureScaleMode(tex,zod_picture_mode==PICTURE_SMOOTH?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);SDL_FRect dst{904,558,48,48};SDL_RenderTexture(renderer,tex,nullptr,&dst);SDL_DestroyTexture(tex);}}
        float t=fmod(SDL_GetTicks()/1000.0,2.0);float pos=zod_render_smoothing?t:floor(t*4)/4;FillRect(548+pos*104,588,16,12,C_AMBER);
    }
    if(settings_category==1) {
        const char *names[]={"Sound effects","Music"};int *values[]={&zod_effects_volume,&zod_music_volume};
        for(int channel=0;channel<2;++channel) {
            float y=322+channel*92,trackX=600,trackY=y+34,width=405;
            DrawLabel(font_ui,24,names[channel],268,y,C_STEEL);
            DrawLabel(font_ui,22,to_string(*values[channel])+"%",1008,y,C_STEEL_HI,2);
            FillRect(trackX,trackY,width,8,SDL_Color{14,23,30,255});FillRect(trackX,trackY,width*(*values[channel])/100,8,C_AMBER);
            FillRect(trackX+(width-12)*(*values[channel])/100,trackY-8,12,24,C_STEEL_HI);
            bool pressed=mouse_down&&press_x>=trackX-12&&press_x<trackX+width+12&&press_y>=trackY-14&&press_y<trackY+24;
            if(mouse_down&&(audio_drag==channel||pressed)) {
                audio_drag=channel;int value=max(0,min(100,(int)((mouse_x-trackX)*100/width)));
                if(value!=*values[channel]) {*values[channel]=value;ZAudio_ApplyVolumes();ZPrefs_Save();}
            }
        }
        if(!mouse_down)audio_drag=-1;
        DrawLabel(font_ui,23,"Soundtrack",268,522,C_STEEL);
        string value=!zod_music?"Off":zod_music_style==MUSIC_STYLE_MIDI?"Original MIDI":"Recorded";
        if(Button("-",650,518,40,40,interactive))AdjustSetting(SET_MUSIC,-1);
        DrawLabel(font_ui,22,value,848,526,C_STEEL_HI,1);
        if(Button("+",968,518,40,40,interactive))AdjustSetting(SET_MUSIC,1);
        return;
    }
    const auto &rows=groups[settings_category];
    string help=settings_category==0?"Image filter changes pixel softness. Motion smoothing eases moving units.":"Changes apply immediately unless the option says otherwise.";
    for(int row=0;row<(int)rows.size();++row) {
        int i=rows[row];float y=316+row*58;
        if(i>=MAX_SETTINGS) break;
        FillRect(252,y,772,52,row==settings_focus?SDL_Color{32,43,52,255}:SDL_Color{20,28,35,255});
        DrawLabel(font_ui,24,SettingLabel(i),268,y+10,C_STEEL);
        if(Hover(252,y,772,52)) help=SettingHelp(i);
        string value;
        bool toggle=true;
        switch(i) {
        case SET_FULLSCREEN:value=zod_fullscreen?"On":"Off";break;
        case SET_SCENES:value=zod_scenes?"On":"Off";break;
        case SET_MOUSE:value=zod_classic_mouse?"Classic":"Modern";break;
        case SET_SMOOTHING:value=zod_render_smoothing?"On":"Off";break;
        case SET_KEYS:value="Key bindings...";break;
        case SET_IMPORT:value="Import disc...";break;
        default:toggle=false;break;
        }
        if(toggle) {
            if(Button(value,726,y+6,282,40,interactive)) {
                if(i==SET_KEYS || i==SET_IMPORT) { keys_selected=0;keys_waiting=-1;import_return_screen=SCR_SETTINGS;screen=i==SET_KEYS?SCR_KEYS:SCR_IMPORT; }
                else AdjustSetting(i,1);
            }
        } else {
            switch(i) {
            case SET_GAMMA:value=to_string(zod_gamma);break;
            case SET_VOLUME:value=to_string(zod_volume_setting*100/(MAX_SOUND_SETTINGS-1))+"%";break;
            case SET_DIFFICULTY:value=bot_difficulty_name[zod_bot_difficulty];break;
            case SET_MUSIC:value=!zod_music?"Off":zod_music_style==MUSIC_STYLE_MIDI?"Original MIDI":"Recorded";break;
            case SET_PICTURE:{ const char *v[]={"Smooth","Sharp","Pixel-perfect"};value=v[zod_picture_mode];break; }
            case SET_STICK:{ const char *v[]={"Slow","Normal","Fast"};value=v[zod_stick_speed];break; }
            default:value=to_string(zod_max_units_per_team);break;
            }
            if(Button("-",650,y+6,40,40,interactive)) AdjustSetting(i,-1);
            DrawLabel(font_ui,22,value,848,y+12,C_STEEL_HI,1);
            if(Button("+",968,y+6,40,40,interactive)) AdjustSetting(i,1);
        }
    }
    if(!help.empty()) {
        while(help.size()>3 && TextWidth(font_ui_medium,17,help)>595) help.erase(help.size()-1);
        DrawLabel(font_ui_medium,17,help,252,681,C_STEEL_DIM);
    }
}

void ZFrontEnd::RenderReplay()
{
	DimScreen(170);

	const float PX = 110, PY = 86, PW = 1060, PH = 640;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 28, "REPLAY LEVEL", PX + 36, PY + 26, C_AMBER);

	//planet tabs (only planets that actually have campaign levels)
	vector<int> planets;
	for(int p=0;p<MAX_PLANET_TYPES;p++)
		for(size_t i=0;i<levels.size();i++)
			if(levels[i].planet == p) { planets.push_back(p); break; }

	if(planets.empty())
	{
		DrawLabel(font_ui, 28, "No campaign levels were found (map_list.txt).", PX + PW * 0.5f, PY + 280, C_STEEL, 1);
		if(Button("Back", PX + PW - 190, PY + PH - 74, 150, 48) || kb_back) { screen = SCR_MAIN; kb_back = false; }
		return;
	}
	if(find(planets.begin(), planets.end(), replay_planet) == planets.end()) replay_planet = planets[0];

	float tab_w = (PW - 72 - 12 * (planets.size() - 1)) / planets.size();
	for(size_t k=0;k<planets.size();k++)
	{
		int p = planets[k];
		float tx = PX + 36 + k * (tab_w + 12), ty = PY + 76;
		bool sel = p == replay_planet;
		bool hot = Hover(tx, ty, tab_w, 46);

		//is anything on this planet reachable yet?
		bool any = false;
		for(size_t i=0;i<levels.size();i++) if(levels[i].planet == p && LevelUnlocked((int)i)) any = true;

		FillRect(tx, ty, tab_w, 46, sel ? SDL_Color{ 78, 52, 22, 255 } : (hot ? SDL_Color{ 56, 60, 68, 255 } : SDL_Color{ 44, 47, 54, 255 }));
		FrameRect(tx, ty, tab_w, 46, sel ? C_AMBER : C_AMBER_DIM, 255, sel ? 2.0f : 1.0f);
		if(sel) DrawGlow(tx + tab_w * 0.5f, ty + 23, tab_w * 0.6f, C_AMBER, 0.15f);
		//planet colour chip
		static const SDL_Color planet_col[MAX_PLANET_TYPES] =
			{ { 214, 170, 96, 255 }, { 210, 70, 40, 255 }, { 170, 210, 240, 255 }, { 90, 170, 70, 255 }, { 150, 150, 160, 255 } };
		float tw = TextWidth(font_ui, 26, PlanetName(p));
		float chip_x = tx + (tab_w - tw) * 0.5f - 22;
		FillRect(chip_x, ty + 17, 12, 12, planet_col[p], any ? 255 : 90);
		DrawLabel(font_ui, 26, PlanetName(p), tx + tab_w * 0.5f + 8, ty + 7, any ? (sel ? C_STEEL_HI : C_STEEL) : C_STEEL_DIM, 1);
		if(Clicked(tx, ty, tab_w, 46) && !sel) { replay_planet = p; Sound(snd_click); }
	}
	if(kb_dx && kb_dy == 0 && false) {}

	//levels on this planet
	vector<int> lv;
	for(size_t i=0;i<levels.size();i++) if(levels[i].planet == replay_planet) lv.push_back((int)i);

	const int cols = 4;
	const float TW = 230, TH = 128, GAP = 18;
	float gx = PX + (PW - (cols * TW + (cols - 1) * GAP)) * 0.5f, gy = PY + 146;

	//keyboard: arrows move between tiles, crossing planets at the ends
	if(kb_dx || kb_dy)
	{
		int cur = (int)(find(lv.begin(), lv.end(), replay_selected) - lv.begin());
		if(cur >= (int)lv.size()) cur = 0;
		else
		{
			cur += kb_dx + kb_dy * cols;
			if(cur < 0 || cur >= (int)lv.size())
			{
				size_t pk = find(planets.begin(), planets.end(), replay_planet) - planets.begin();
				pk = (pk + (cur < 0 ? planets.size() - 1 : 1)) % planets.size();
				replay_planet = planets[pk];
				lv.clear();
				for(size_t i=0;i<levels.size();i++) if(levels[i].planet == replay_planet) lv.push_back((int)i);
				cur = cur < 0 ? (int)lv.size() - 1 : 0;
			}
		}
		if(!lv.empty()) replay_selected = lv[max(0, min(cur, (int)lv.size() - 1))];
		Sound(snd_hover);
	}

	int hovered = -1;
	for(size_t k=0;k<lv.size();k++)
	{
		int idx = lv[k];
		float tx = gx + (k % cols) * (TW + GAP), ty = gy + (k / cols) * (TH + GAP);
		bool unlocked = LevelUnlocked(idx);
		bool hot = Hover(tx, ty, TW, TH);
		bool sel = idx == replay_selected;
		if(hot) hovered = idx;

		const ZHighScoreEntry *best = ZHighScore_BestFor(levels[idx].base);

		FillRect(tx, ty, TW, TH, unlocked ? (hot ? SDL_Color{ 62, 67, 78, 255 } : SDL_Color{ 48, 52, 60, 255 }) : SDL_Color{ 30, 32, 36, 255 });
		FrameRect(tx, ty, TW, TH, sel ? C_AMBER : (hot ? C_AMBER_DIM : SDL_Color{ 70, 74, 82, 255 }), 255, sel ? 2.0f : 1.0f);
		if(sel || hot) DrawGlow(tx + TW * 0.5f, ty + TH * 0.5f, TW * 0.6f, unlocked ? C_AMBER : C_STEEL_DIM, sel ? 0.16f : 0.08f);

		DrawLabel(font_ui, 54, LevelLabel(idx), tx + 14, ty + 0, unlocked ? (best ? C_AMBER : C_STEEL) : C_STEEL_DIM);
		DrawLabel(font_ui, 22, levels[idx].title, tx + 16, ty + 66, unlocked ? C_STEEL : C_STEEL_DIM);

		if(!unlocked)
		{
			//padlock
			float lx = tx + TW - 46, ly = ty + 20;
			FrameRect(lx + 6, ly, 18, 18, C_STEEL_DIM, 255, 3);
			FillRect(lx, ly + 14, 30, 22, C_STEEL_DIM);
			FillRect(lx + 13, ly + 20, 4, 9, SDL_Color{ 30, 32, 36, 255 });
			DrawLabel(font_ui_medium, 17, "Locked", tx + 16, ty + 96, C_STEEL_DIM);
		}
		else if(best)
		{
			DrawLabel(font_ui_medium, 17, "Best " + format_score(best->score) + "   " + format_time(best->seconds), tx + 16, ty + 96, C_AMBER_HI);
			//victory star
			DrawGlow(tx + TW - 30, ty + 30, 22, C_AMBER, 0.6f);
			FillRect(tx + TW - 36, ty + 24, 12, 12, C_AMBER_HI);
		}
		else
			DrawLabel(font_ui_medium, 17, "Not yet won", tx + 16, ty + 96, C_STEEL_DIM);

		if(Clicked(tx, ty, TW, TH))
		{
			if(unlocked)
			{
				if(replay_selected == idx) { StartLevel(idx, true); kb_activate = false; }   //second click plays
				else { replay_selected = idx; Sound(snd_click); }
			}
			else
			{
				status_message = "Win the earlier levels to unlock this one.";
				status_until = SDL_GetTicks() + 2500;
			}
		}
	}

	//detail line
	int show = hovered >= 0 ? hovered : replay_selected;
	if(show >= 0 && show < (int)levels.size())
	{
		const LevelInfo &li = levels[show];
		char buf[160];
		snprintf(buf, sizeof(buf), "Level %s  -  %s  -  planet %s  -  %d x %d", LevelLabel(show).c_str(), li.title.c_str(), PlanetName(li.planet).c_str(), li.w, li.h);
		DrawLabel(font_ui_medium, 22, buf, PX + 40, PY + PH - 112, C_STEEL);
	}

	bool can_play = LevelUnlocked(replay_selected);
	if(Button("Back", PX + 40, PY + PH - 74, 160, 48) || kb_back) { screen = SCR_MAIN; kb_back = false; Sound(snd_click); }
	if(screen == SCR_REPLAY && (Button("Play Level " + LevelLabel(replay_selected), PX + PW - 300, PY + PH - 74, 260, 48, can_play, true) || (kb_activate && can_play)))
	{
		kb_activate = false;
		StartLevel(replay_selected, true);
	}
}

void ZFrontEnd::RenderScores()
{
	DimScreen(170);

	const float PX = 150, PY = 86, PW = 980, PH = 640;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 28, "HIGH SCORE", PX + 36, PY + 26, C_AMBER);

	const vector<ZHighScoreEntry> &all = ZHighScore_All();

	//summary on the right of the header
	{
		int won = 0;
		for(size_t i=0;i<levels.size();i++) if(ZHighScore_BestFor(levels[i].base)) won++;
		char buf[96];
		snprintf(buf, sizeof(buf), "Campaign: %d of %d levels won", won, (int)levels.size());
		DrawLabel(font_ui_medium, 22, buf, PX + PW - 40, PY + 32, C_STEEL, 2);
	}

	float cols[] = { PX + 40, PX + 100, PX + 330, PX + 640, PX + 750, PX + PW - 40 };
	float hy = PY + 92;
	DrawLabel(font_ui, 15, "#", cols[0], hy, C_AMBER_DIM);
	DrawLabel(font_ui, 15, "COMMANDER", cols[1], hy, C_AMBER_DIM);
	DrawLabel(font_ui, 15, "LEVEL", cols[2], hy, C_AMBER_DIM);
	DrawLabel(font_ui, 15, "TIME", cols[3], hy, C_AMBER_DIM);
	DrawLabel(font_ui, 15, "ENEMY", cols[4], hy, C_AMBER_DIM);
	DrawLabel(font_ui, 15, "SCORE", cols[5], hy, C_AMBER_DIM, 2);
	FillRect(PX + 36, hy + 28, PW - 72, 1, C_AMBER_DIM);

	if(all.empty())
	{
		DrawLabel(font_ui, 30, "No victories yet.", PX + PW * 0.5f, PY + 260, C_STEEL, 1);
		DrawLabel(font_ui_medium, 22, "Win a level and your result is recorded here.", PX + PW * 0.5f, PY + 304, C_STEEL_DIM, 1);
	}

	for(size_t r=0;r<all.size() && r<10;r++)
	{
		const ZHighScoreEntry &e = all[r];
		float y = hy + 42 + r * 42;
		SDL_Color c = r == 0 ? C_AMBER_HI : C_STEEL;
		if(r % 2 == 0) FillRect(PX + 36, y - 6, PW - 72, 40, C_BLACK, 50);

		//level name from the campaign list when we know the map
		string level = base_name(e.map_file);
		for(size_t i=0;i<levels.size();i++)
			if(levels[i].base == base_name(e.map_file)) { level = LevelLabel((int)i) + "  " + levels[i].title; break; }

		char rank[8]; snprintf(rank, sizeof(rank), "%d", (int)r + 1);
		DrawLabel(font_ui, 25, rank, cols[0], y - 3, c);
		DrawLabel(font_ui, 25, e.player, cols[1], y - 3, c);
		DrawLabel(font_ui, 25, level, cols[2], y - 3, c);
		DrawLabel(font_ui, 25, format_time(e.seconds), cols[3], y - 3, c);
		int d = e.difficulty >= 0 && e.difficulty < MAX_BOT_DIFFICULTY ? e.difficulty : 1;
		DrawLabel(font_ui, 25, bot_difficulty_name[d], cols[4], y - 3, c);
		DrawLabel(font_ui, 27, format_score(e.score), cols[5], y - 4, c, 2);
	}

	if(Button("Back", PX + PW - 200, PY + PH - 74, 160, 48) || kb_back || kb_activate) { screen = SCR_MAIN; kb_back = false; Sound(snd_click); }
}

void ZFrontEnd::RenderBriefing()
{
    if(!selected_campaign.id.empty()){RenderCommunityBriefing();return;}
	DimScreen(185);

	const float PX = 230, PY = 120, PW = 820, PH = 540;
	DrawPanel(PX, PY, PW, PH);

	int idx = briefing_level;
	if(idx < 0 || idx >= (int)levels.size()) { screen = history_briefing?SCR_CAMPAIGNS:SCR_MAIN; return; }
	const LevelInfo &li = levels[idx];

	//radar scope
	float rcx = PX + 190, rcy = PY + 250, rr = 140;
	float t = SDL_GetTicks() / 1000.0f;
	DrawGlow(rcx, rcy, rr * 1.15f, SDL_Color{ 40, 255, 90, 255 }, 0.12f);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for(int ring=1; ring<=4; ring++)
	{
		float r = rr * ring / 4.0f;
		SDL_SetRenderDrawColor(renderer, 60, 220, 110, ring == 4 ? 200 : 90);
		const int SEG = 96;
		SDL_FPoint pts[SEG + 1];
		for(int k=0;k<=SEG;k++)
		{
			float a = k * 6.2831853f / SEG;
			pts[k].x = rcx + cosf(a) * r;
			pts[k].y = rcy + sinf(a) * r;
		}
		SDL_RenderLines(renderer, pts, SEG + 1);
	}
	SDL_RenderLine(renderer, rcx - rr, rcy, rcx + rr, rcy);
	SDL_RenderLine(renderer, rcx, rcy - rr, rcx, rcy + rr);
	//sweep with a fading trail
	float sweep = fmodf(t * 1.6f, 6.2831853f);
	for(int k=0;k<24;k++)
	{
		float a = sweep - k * 0.03f;
		SDL_SetRenderDrawColor(renderer, 90, 255, 140, (Uint8)(220 * (1 - k / 24.0f)));
		SDL_RenderLine(renderer, rcx, rcy, rcx + cosf(a) * rr, rcy + sinf(a) * rr);
	}
	//blips: deterministic per level, brightening as the sweep passes
	unsigned int seed = 1234u + idx * 77u;
	for(int b=0;b<7;b++)
	{
		seed = seed * 1103515245u + 12345u;
		float a = (seed % 6283) / 1000.0f;
		seed = seed * 1103515245u + 12345u;
		float d = rr * (0.25f + (seed % 700) / 1000.0f);
		float diff = fmodf(sweep - a + 12.566f, 6.2831853f);
		float bright = clampf(1 - diff / 2.5f, 0.15f, 1);
		SDL_Color bc = b < 3 ? C_RED : SDL_Color{ 120, 160, 255, 255 };
		DrawGlow(rcx + cosf(a) * d, rcy + sinf(a) * d, 12, bc, bright);
	}

	//text
	float tx = PX + 380;
	DrawLabel(font_ui, 18, "MISSION BRIEFING", tx, PY + 40, C_AMBER_DIM);
	{ string pn = PlanetName(li.planet); for(size_t c=0;c<pn.size();c++) pn[c] = (char)toupper(pn[c]); DrawLabel(font_ui, 34, "PLANET " + pn, tx, PY + 70, C_AMBER); }
	char buf[96];
	snprintf(buf, sizeof(buf), "Level %s of %d", LevelLabel(idx).c_str(), (int)levels.size());
	DrawLabel(font_ui, 28, buf, tx, PY + 122, C_STEEL);
	DrawLabel(font_ui, 40, li.title, tx, PY + 156, C_STEEL_HI);

	const char *lines[] =
	{
		"Capture the enemy fort, or destroy every",
		"enemy unit on the map.",
		"Take territory flags to seize factories,",
		"radar and repair stations in each zone."
	};
	for(int i=0;i<4;i++) DrawLabel(font_ui_medium, 22, lines[i], tx, PY + 226 + i * 28, C_STEEL);

	snprintf(buf, sizeof(buf), "Enemy commander: %s", bot_difficulty_name[zod_bot_difficulty]);
	DrawLabel(font_ui_medium, 22, buf, tx, PY + 352, C_AMBER);
	const ZHighScoreEntry *best = ZHighScore_BestFor(li.base);
	if(best)
	{
		string s = "Your best: " + format_score(best->score) + " in " + format_time(best->seconds);
		DrawLabel(font_ui_medium, 22, s, tx, PY + 380, C_AMBER_HI);
	}

	if(Button("Back", PX + 40, PY + PH - 78, 160, 50) || kb_back)
	{
		kb_back = false;
		screen = history_briefing?SCR_CAMPAIGNS:SCR_MAIN;
		Sound(snd_click);
	}
	if(Button("Begin Mission", PX + PW - 290, PY + PH - 78, 250, 50, true, true) || kb_activate)
	{
		kb_activate = false;
		StartLevel(idx, false);
	}
}

void ZFrontEnd::DeploymentFailed(const string &message,bool force){
 if(!visible||(!force&&screen!=SCR_LOADING))return;if(workspace&&(editor_testing||screen==SCR_EDITOR)){workspace->ResumeAfterTest();editor_testing=false;screen=SCR_EDITOR;ZVideo_SetFullscreen(true);}else {screen=SCR_MAIN;deployment_failed=has_deployment_retry;if(deployment_failed)ZAsset_RecordDiagnostic(ZDiagnosticEvent::DeploymentFailed);}status_message=message;status_until=SDL_GetTicks()+12000;
}

void ZFrontEnd::RenderLoading()
{
	DimScreen(200);

	Uint64 now = SDL_GetTicks();
	if(now - loading_since > 25000)
	{
        DeploymentFailed("The level did not start. Check the connection and installed map files.");
		return;
	}

	const float PX = 390, PY = 330, PW = 500, PH = 140;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 24, "DEPLOYING", PX + PW * 0.5f, PY + 32, C_AMBER, 1);

	DrawLabel(font_ui_medium,18,deployment_stage,PX+PW*0.5f,PY+62,C_STEEL,1);
	//indeterminate progress: a lit block running along a segmented bar
	int segs = 20;
	float sw = (PW - 80 - (segs - 1) * 4) / segs;
	int lit = zod_reduced_motion ? segs/2 : (int)((now / 70) % (segs + 6)) - 3;
	for(int k=0;k<segs;k++)
	{
		bool on = abs(k - lit) <= 2;
		FillRect(PX + 40 + k * (sw + 4), PY + 84, sw, 20, on ? C_AMBER : SDL_Color{ 50, 24, 8, 255 });
	}
}

void ZFrontEnd::RenderQuit()
{
	DimScreen(170);

	const float PX = 440, PY = 300, PW = 400, PH = 190;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 34, "Quit zod-enhanced?", PX + PW * 0.5f, PY + 36, C_STEEL_HI, 1);

	if(Button("Stay", PX + 40, PY + PH - 76, 140, 50) || kb_back)
	{
		kb_back = false;
		screen = screen_before_quit;
		Sound(snd_click);
	}
	if(Button("Quit", PX + PW - 180, PY + PH - 76, 140, 50, true, true) || kb_activate)
	{
		kb_activate = false;
		actions.push_back(Action(FE_QUIT));
	}
}

void ZFrontEnd::RetryDeployment(){if(!has_deployment_retry)return;deployment_failed=false;status_message.clear();actions.push_back(deployment_action);screen=SCR_LOADING;loading_since=SDL_GetTicks();deployment_stage="Reconnecting to the game server";}
void ZFrontEnd::RenderStatus()
{
    if(deployment_failed){DrawPanel(360,260,560,280);DrawLabel(font_ui,26,"MISSION COULD NOT START",390,284,C_AMBER);std::istringstream words(status_message);string word,row;int line=0;while(words>>word){if(!row.empty()&&TextWidth(font_ui,20,row+" "+word)>500){DrawLabel(font_ui,20,row,390,336+line*28,C_STEEL);row.clear();if(++line==3)break;}if(!row.empty())row+=' ';row+=word;}if(line<3&&!row.empty())DrawLabel(font_ui,20,row,390,336+line*28,C_STEEL);if(Button("Retry",390,470,230,44)||kb_activate){kb_activate=false;RetryDeployment();}if(Button("Back",650,470,230,44)||kb_back){deployment_failed=false;status_message.clear();kb_back=false;}return;}

	if(status_message.empty()) return;
	Uint64 now = SDL_GetTicks();
	if(now > status_until) { status_message.clear(); return; }

	float w = TextWidth(font_ui_medium, 24, status_message) + 60;
	float x = (CANVAS_W - w) * 0.5f, y = CANVAS_H - 84;
	FillRect(x, y, w, 44, SDL_Color{ 20, 12, 6, 255 }, 230);
	FrameRect(x, y, w, 44, C_AMBER, 255, 1.0f);
	DrawLabel(font_ui_medium, 24, status_message, CANVAS_W * 0.5f, y + 8, C_AMBER_HI, 1);
}

// ---------------------------------------------------------------------------
// saved battle

static string format_when(long long when)
{
	time_t t = (time_t)when;
	struct tm *lt = localtime(&t);
	char buf[64] = "";
	if(lt) strftime(buf, sizeof(buf), "%d %b %H:%M", lt);
	return buf;
}

void ZFrontEnd::LoadSaveInfo()
{
	saves.clear();

	for(int slot=ZSAVE_AUTO_SLOT; slot<=ZSAVE_USER_SLOTS; slot++)
	{
		ZSaveInfo info;
		bool recovery=!ZSave_ReadInfo(slot,info);if(recovery&&!ZSave_RecoveryInfo(slot,info))continue;

		SaveSlot s;s.recovery=recovery;
        s.map=info.map_name;s.campaign=info.campaign_id=="original"?"Original Campaign":info.campaign_id.empty()?"Standalone battle":info.campaign_id;
        if(info.campaign_id!="original")for(const auto &pack:ZCampaign_List())if(pack.id==info.campaign_id)s.campaign=pack.title;
        if(info.mission_index>=0)s.campaign+=" / Mission "+to_string(info.mission_index+1);
		s.slot = slot;
		if(slot == ZSAVE_AUTO_SLOT) s.label = "Autosave";
		else { char buf[16]; snprintf(buf, sizeof(buf), "Slot %d", slot); s.label = buf; }

		if(recovery)s.label+=" / Recover backup";
		//campaign levels by their place in the campaign, anything else by name
		for(size_t i=0;i<levels.size();i++)
			if(levels[i].file == info.map_name)
				s.title = PlanetName(levels[i].planet) + "  -  Level " + LevelLabel((int)i) + "  " + levels[i].title;
		if(s.title.empty()) s.title = info.map_title.size() ? info.map_title : base_name(info.map_name);

		int d = info.difficulty >= 0 && info.difficulty < MAX_BOT_DIFFICULTY ? info.difficulty : 1;
		s.detail = format_time((int)info.elapsed) + " played  -  " + bot_difficulty_name[d] + "  -  saved " + format_when(info.when);

		saves.push_back(s);
	}

	have_save = !saves.empty();
}

void ZFrontEnd::DrawMapPreview(const string &path,float x,float y,float w,float h){
    if(path!=map_preview_path){SDL_DestroyTexture(map_preview_texture);map_preview_texture=nullptr;map_preview_path=path;MapPreview preview;if(!path.empty()&&ZMP_ReadMapFilePreview(path,preview)&&preview.valid){SDL_Surface *surface=SDL_CreateSurface(preview.preview_width,preview.preview_height,SDL_PIXELFORMAT_ARGB8888);if(surface){for(int row=0;row<surface->h;++row)memcpy((char*)surface->pixels+row*surface->pitch,preview.pixels.data()+row*surface->w,surface->w*4);map_preview_texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);}}}
    FillRect(x,y,w,h,SDL_Color{10,17,23,255});if(map_preview_texture){float tw,th;SDL_GetTextureSize(map_preview_texture,&tw,&th);float fit=min(w/tw,h/th);SDL_FRect dst{x+(w-tw*fit)/2,y+(h-th*fit)/2,tw*fit,th*fit};SDL_RenderTexture(renderer,map_preview_texture,nullptr,&dst);}else DrawLabel(font_ui_medium,18,"Preview unavailable",x+12,y+12,C_STEEL_DIM);
}
void ZFrontEnd::RenderLoad(){
    DimScreen(190);DrawPanel(120,78,1040,650);DrawLabel(font_ui,30,"LOAD SAVED BATTLE",152,102,C_AMBER);
    bool campaign=progress_current>=0&&progress_current<(int)levels.size();int rows=(int)saves.size()+1;
    if(delete_pending<0&&kb_dy)load_selected=(load_selected+kb_dy+rows)%rows;load_selected=max(0,min(load_selected,rows-1));
    int chosen=-1,remove=-1;
    for(int row=0;row<rows;++row){bool level=row==(int)saves.size(),enabled=!level||campaign;float y=164+row*62;string label=level?"Restart current mission":saves[row].label;string title=level?(campaign?levels[progress_current].title:"No campaign in progress"):saves[row].title;
        if(Button("",152,y,450,54,enabled&&delete_pending<0,load_selected==row))load_selected=row;DrawLabel(font_ui,21,label,166,y+3,enabled?C_STEEL_HI:C_STEEL_DIM);
        string clipped=title;while(clipped.size()>3&&TextWidth(font_ui_medium,17,clipped)>420)clipped.pop_back();DrawLabel(font_ui_medium,17,clipped,166,y+31,enabled?C_STEEL_DIM:C_AMBER_DIM);
    }
    bool level=load_selected==(int)saves.size(),enabled=!level||campaign;string map=level?(campaign?levels[progress_current].file:""):saves[load_selected].map;
    DrawMapPreview(map,638,164,482,260);
    string title=level?(campaign?levels[progress_current].title:"No mission selected"):saves[load_selected].title;while(title.size()>3&&TextWidth(font_ui,24,title)>480)title.pop_back();DrawLabel(font_ui,24,title,638,442,C_STEEL_HI);
    string context=level?"Current campaign / Start from the beginning":saves[load_selected].campaign;while(context.size()>3&&TextWidth(font_ui_medium,19,context)>480)context.pop_back();DrawLabel(font_ui_medium,19,context,638,486,C_AMBER);
    if(!level){string detail=saves[load_selected].detail;size_t split=detail.find("saved ");DrawLabel(font_ui_medium,18,detail.substr(0,split),638,522,C_STEEL);if(split!=string::npos)DrawLabel(font_ui_medium,18,detail.substr(split),638,552,C_STEEL_DIM);}
    if(!level&&delete_pending<0){ZSaveInfo backup;if(ZSave_RecoveryInfo(saves[load_selected].slot,backup)&&Button("Restore previous backup",638,578,300,28)){if(ZSave_Recover(saves[load_selected].slot))LoadSaveInfo();else{status_message="Backup could not be restored; files kept.";status_until=SDL_GetTicks()+5000;}return;}}
    if(delete_pending>=0){DrawPanel(280,250,720,270);DrawLabel(font_ui,28,"DELETE SAVED BATTLE?",312,278,C_AMBER);DrawLabel(font_ui_medium,22,"This cannot be undone. Your campaign progress is kept.",312,338,C_STEEL);if(Button("Delete",312,426,300,48)||kb_activate)remove=delete_pending;if(Button("Keep save",648,426,300,48)||kb_back){delete_pending=-1;kb_back=false;}kb_activate=false;}
    else {if(Button(level?"Restart mission":saves[load_selected].recovery?"Recover backup":"Load battle",638,614,300,48,enabled,true)||(kb_activate&&enabled)){chosen=load_selected;kb_activate=false;}if(!level&&(Button("Delete save",954,614,166,48)||kb_delete))delete_pending=saves[load_selected].slot;}
    if(remove>=0){ZSave_Delete(remove);delete_pending=-1;LoadSaveInfo();load_selected=max(0,min(load_selected,(int)saves.size()));return;}
    if(Button("Back",152,658,170,44,delete_pending<0)||kb_back){kb_back=false;screen=SCR_MAIN;return;}
    if(chosen<0)return;if(chosen==(int)saves.size())StartLevel(progress_current,true);else{if(saves[chosen].recovery){if(!ZSave_Recover(saves[chosen].slot)){status_message="Backup recovery failed; your files were kept.";status_until=SDL_GetTicks()+5000;return;}LoadSaveInfo();return;}actions.push_back(Action(FE_LOAD_SAVE,saves[chosen].slot));screen=SCR_LOADING;loading_since=SDL_GetTicks();deployment_stage="Connecting to the game server";}
}

void ZFrontEnd::CaptureKey(int k)
{
	int action = keys_waiting;
	keys_waiting = -1;

	if(action<0||action>=MAX_ZKEYS||k == SDLK_ESCAPE) return;   //cancelled

	//letters, punctuation and the like; digits are control groups and / opens chat
	bool ok = k > 32 && k < 127 && !(k >= '0' && k <= '9') && k != '/' && k != '\\' && k != ',';
	if(!ok)
	{
		status_message = "That key can not be used here.";
		status_until = SDL_GetTicks() + 2500;
		return;
	}
	if(k >= 'A' && k <= 'Z') k += 32;

	for(int a=0;a<MAX_ZKEYS;a++)
		if(a != action && zod_keys[a] == k)
		{
			status_message = ZPrefs_KeyLabel(k) + " is already used for " + ZPrefs_KeyActionName(a) + ".";
			status_until = SDL_GetTicks() + 3000;
			return;
		}

	zod_keys[action] = k;
	ZPrefs_Save();
	Sound(snd_toggle);
}

void ZFrontEnd::RenderKeys()
{
	DimScreen(170);

	const float ROW_H = 46, ROW_GAP = 6;
	const float PW = 700, PH = 100 + min(8,(int)MAX_ZKEYS) * (ROW_H + ROW_GAP) + 84;
	const float PX = (CANVAS_W - PW) * 0.5f, PY = (CANVAS_H - PH) * 0.5f;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 28, "KEYS", PX + 36, PY + 26, C_AMBER);
	DrawLabel(font_ui_medium, 18, keys_waiting >= 0 ? "Press the new key  (Esc cancels)" : "Pick an action, then press the key you want for it.",
		PX + 36, PY + 66, C_STEEL_DIM);

	if(keys_waiting < 0)
	{
		if(kb_dy) { keys_selected = (keys_selected + kb_dy + MAX_ZKEYS) % MAX_ZKEYS; Sound(snd_hover); }
		if(kb_activate) { keys_waiting = keys_selected; kb_activate = false; }
	}

	if(wheel)keys_scroll=max(0,min(keys_scroll-(int)wheel,max(0,MAX_ZKEYS-8)));
    if(kb_dy){keys_scroll=min(keys_scroll,keys_selected);if(keys_selected>=keys_scroll+8)keys_scroll=keys_selected-7;}
    for(int row=0;row<8&&row+keys_scroll<MAX_ZKEYS;++row)
	{int a=row+keys_scroll;
		float rx = PX + 36, ry = PY + 100 + row * (ROW_H + ROW_GAP), rw = PW - 72;
		bool waiting = keys_waiting == a;
		bool hot = keys_waiting < 0 && Hover(rx, ry, rw, ROW_H);
		bool sel = a == keys_selected;

		FillRect(rx, ry, rw, ROW_H, waiting ? SDL_Color{ 82, 52, 20, 255 } : (hot ? SDL_Color{ 62, 67, 78, 255 } : SDL_Color{ 48, 52, 60, 255 }));
		FrameRect(rx, ry, rw, ROW_H, (hot || sel || waiting) ? C_AMBER : C_AMBER_DIM, 255, (hot || sel || waiting) ? 2.0f : 1.0f);

		DrawLabel(font_ui, 24, ZPrefs_KeyActionName(a), rx + 18, ry + 8, C_STEEL_HI);

		string label = waiting ? "press a key..." : ZPrefs_KeyLabel(zod_keys[a]);
		bool changed = zod_keys[a] != ZPrefs_KeyDefault(a);
		DrawLabel(font_ui, 20, label, rx + rw - 22, ry + 12, waiting ? C_AMBER_HI : (changed ? C_AMBER_HI : C_AMBER), 2);

		if(keys_waiting < 0 && Clicked(rx, ry, rw, ROW_H)) { keys_selected = a; keys_waiting = a; Sound(snd_click); }
	}

	float by = PY + PH - 64;
    DrawLabel(font_ui_medium,16,to_string(keys_scroll+1)+"-"+to_string(min(keys_scroll+8,(int)MAX_ZKEYS))+" / "+to_string(MAX_ZKEYS)+" actions / scroll for more",PX+36,by+14,C_STEEL_DIM);
	if(Button("Reset", PX + PW - 36 - 160 - 16 - 160, by, 160, 46, keys_waiting < 0))
	{
		ZPrefs_ResetKeys();
		ZPrefs_Save();
		Sound(snd_toggle);
	}
	if(Button("Back", PX + PW - 36 - 160, by, 160, 46, keys_waiting < 0) || (kb_back && keys_waiting < 0))
	{
		kb_back = false;
		screen = SCR_SETTINGS;
		Sound(snd_click);
	}
}

// ---------------------------------------------------------------------------
// custom maps

void ZFrontEnd::ScanCustomMaps(bool force)
{
	Uint64 now = SDL_GetTicks();
	if(!force && now - custom_scanned < 1500) return;
	custom_scanned = now;

	if(custom_dir.empty())
	{
		//"Zod Maps" in the Documents folder (or the home folder as a fallback)
		const char *docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
		if(!docs || !docs[0]) docs = SDL_GetUserFolder(SDL_FOLDER_HOME);
		string base = (docs && docs[0]) ? docs : "./";
		if(base[base.size() - 1] != '/' && base[base.size() - 1] != '\\') base += "/";
		custom_dir = base + "Zod Maps";
	}
	SDL_CreateDirectory(custom_dir.c_str());

	string keep = custom_selected >= 0 && custom_selected < (int)custom_maps.size() ? custom_maps[custom_selected].file : "";

	vector<string> files;
	int count = 0;
	char **list = SDL_GlobDirectory(custom_dir.c_str(), "*.map", SDL_GLOB_CASEINSENSITIVE, &count);
	for(int i=0; list && i<count; i++) files.push_back(list[i]);
	SDL_free(list);
	sort(files.begin(), files.end());

	//only re-read the maps when the folder's listing or a file changed
	vector<CustomMap> fresh;
	for(size_t i=0;i<files.size();i++)
	{
		CustomMap m;
		m.file = files[i];
		m.title = files[i];
		m.planet = DESERT;
		m.w = m.h = m.teams = 0;
		m.red_fort = false;
		m.readable = false;

		//data-only load (no graphics) to learn the size, planet and teams
		ZMap probe;
		if(probe.Read((custom_dir + "/" + files[i]).c_str()))
		{
			m.readable = true;
			map_basics &mb = probe.GetMapBasics();
			mb.map_name[sizeof(mb.map_name) - 1] = 0;
			if(mb.map_name[0]) m.title = mb.map_name;
			m.planet = mb.terrain_type < MAX_PLANET_TYPES ? mb.terrain_type : DESERT;
			m.w = mb.width;
			m.h = mb.height;

			bool fort[MAX_TEAM_TYPES] = { false };
			for(vector<map_object>::iterator o=probe.GetObjectList().begin(); o!=probe.GetObjectList().end(); ++o)
				if(o->object_type == BUILDING_OBJECT && (o->object_id == FORT_FRONT || o->object_id == FORT_BACK) &&
					o->owner > NULL_TEAM && o->owner < MAX_TEAM_TYPES)
					fort[(int)o->owner] = true;
			for(int t=0;t<MAX_TEAM_TYPES;t++) if(fort[t]) m.teams++;
			m.red_fort = fort[RED_TEAM];
		}
		fresh.push_back(m);
	}
	custom_maps.swap(fresh);

	custom_selected = 0;
	for(size_t i=0;i<custom_maps.size();i++)
		if(custom_maps[i].file == keep) custom_selected = (int)i;
}

bool ZFrontEnd::LaunchEditor(const vector<string> &args)
{
    ScanCustomMaps(true);
    editor_fullscreen_previous=ZVideo_GetFullscreen();
    workspace.reset(new ZMapWorkspace(renderer));
    bool create=false;string file,name="Untitled";int terrain=0,width=80,height=100;
    for(size_t i=0;i<args.size();++i) {
        if(args[i]=="-n")create=true;
        else if(args[i]=="-f"&&i+1<args.size())file=args[++i];
        else if(args[i]=="-m"&&i+1<args.size())name=args[++i];
        else if(args[i]=="-d"&&i+1<args.size()){string size=args[++i];sscanf(size.c_str(),"%dx%d",&width,&height);}
        else if(args[i]=="-p"&&i+1<args.size()){string planet=args[++i];for(int t=0;t<MAX_PLANET_TYPES;++t)if(planet_type_string[t]==planet)terrain=t;}
        else if(!args[i].empty()&&args[i][0]!='-')file=args[i];
    }
    if(args.empty()){create=true;file=custom_dir+"/Untitled.map";}
    if(create) workspace->Create(file,name,terrain,width,height);
    else if(!workspace->Open(file,custom_dir)) {workspace.reset();status_message="Could not open the level";status_until=SDL_GetTicks()+5000;return false;}
    vector<pair<string,string>> entries;
    for(const auto &level:(original_levels.empty()?levels:original_levels))entries.push_back({level.title,ZSDL_DataPath(level.file)});
    for(const auto &map:custom_maps)entries.push_back({"Custom / "+map.title,custom_dir+"/"+map.file});
    for(const auto &campaign:ZCampaign_List())for(const auto &mission:campaign.missions){string file;if(ZCampaign_SafeAsset(campaign.root,mission.map,file))entries.push_back({campaign.title+" / "+mission.title,file});}
    workspace->SetFiles(std::move(entries));
    if(args.empty())workspace->Welcome();ZVideo_SetFullscreen(true);
    screen=SCR_EDITOR;clicked=false;return true;
}

string ZFrontEnd::NewMapFileName() const
{
	string f;
	for(size_t i=0;i<newmap_name.size();i++)
	{
		char c = newmap_name[i];
		f += c == ' ' ? '_' : (char)tolower((unsigned char)c);
	}
	while(f.size() && f[f.size() - 1] == '_') f.erase(f.size() - 1);
	return f.empty() ? "" : f + ".map";
}

void ZFrontEnd::RenderCustom()
{
	DimScreen(170);
	ScanCustomMaps(false);

	const float PX = 110, PY = 86, PW = 1060, PH = 640;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 28, "CUSTOM MAPS", PX + 36, PY + 26, C_AMBER);
	DrawLabel(font_ui_medium,16,custom_dir.substr(0,70),PX+36,PY+62,C_STEEL_DIM);

	bool modal = newmap_open;
    if(Button("Edit existing level",PX+PW-326,PY+20,290,42,!modal&&!levels.empty())) {
        if(LaunchEditor({ZSDL_DataPath(levels[0].file)}))workspace->Browse();
        return;
    }


	//---- list ----
	const float LX = PX + 36, LY = PY + 84, LW = 600, ROW_H = 52;
	const int ROWS = 8;
	int n = (int)custom_maps.size();
	int max_scroll = n > ROWS ? n - ROWS : 0;

	if(!modal)
	{
		if(wheel != 0 && Hover(LX, LY, LW, ROWS * ROW_H)) custom_scroll -= wheel > 0 ? 1 : -1;
		if(kb_dy && n)
		{
			custom_selected = (custom_selected + kb_dy + n) % n;
			if(custom_selected < custom_scroll) custom_scroll = custom_selected;
			if(custom_selected >= custom_scroll + ROWS) custom_scroll = custom_selected - ROWS + 1;
			Sound(snd_hover);
		}
	}
	if(custom_scroll > max_scroll) custom_scroll = max_scroll;
	if(custom_scroll < 0) custom_scroll = 0;
	if(custom_selected >= n) custom_selected = n - 1;
	if(custom_selected < 0) custom_selected = 0;

	FillRect(LX, LY, LW, ROWS * ROW_H, SDL_Color{ 24, 26, 30, 255 });
	FrameRect(LX, LY, LW, ROWS * ROW_H, C_AMBER_DIM, 255, 1.0f);

	if(!n)
	{
		DrawLabel(font_ui, 26, "No maps here yet.", LX + LW * 0.5f, LY + 150, C_STEEL, 1);
		DrawLabel(font_ui_medium, 20, "Press New Map to build one in the editor,", LX + LW * 0.5f, LY + 190, C_STEEL_DIM, 1);
		DrawLabel(font_ui_medium, 20, "or copy .map files into the folder above.", LX + LW * 0.5f, LY + 216, C_STEEL_DIM, 1);
	}

	for(int row=0; row<ROWS; row++)
	{
		int i = custom_scroll + row;
		if(i >= n) break;
		const CustomMap &m = custom_maps[i];
		float ry = LY + row * ROW_H;
		bool sel = i == custom_selected;
		bool hot = !modal && Hover(LX, ry, LW, ROW_H);

		if(sel) FillRect(LX + 1, ry + 1, LW - 2, ROW_H - 2, SDL_Color{ 78, 52, 22, 255 });
		else if(hot) FillRect(LX + 1, ry + 1, LW - 2, ROW_H - 2, SDL_Color{ 44, 47, 54, 255 });

		DrawLabel(font_ui, 25, m.title, LX + 16, ry + 3, m.readable ? (sel ? C_STEEL_HI : C_STEEL) : C_STEEL_DIM);
		string sub = m.file;
		if(m.readable)
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "  -  %s  -  %d x %d  -  %d team%s", PlanetName(m.planet).c_str(), m.w, m.h, m.teams, m.teams == 1 ? "" : "s");
			sub += buf;
		}
		else sub += "  -  not a readable map";
		DrawLabel(font_ui_medium, 16, sub, LX + 16, ry + 29, sel ? C_AMBER_HI : C_STEEL_DIM);

		if(!modal && Clicked(LX, ry, LW, ROW_H)) { custom_selected = i; Sound(snd_click); }
	}
	if(max_scroll)
	{
		float th = ROWS * ROW_H * ROWS / (float)n;
		float ty = LY + (ROWS * ROW_H - th) * custom_scroll / (float)max_scroll;
		FillRect(LX + LW - 6, ty, 5, th, C_AMBER, 180);
	}

	//---- details and what is needed to play ----
	const float DX = LX + LW + 28, DW = PX + PW - 36 - DX;
	const CustomMap *cur = n ? &custom_maps[custom_selected] : NULL;
	bool can_play = cur && cur->readable && cur->red_fort && cur->teams >= 2;

	if(cur)
	{
		DrawLabel(font_ui, 30, cur->title, DX, LY - 2, C_STEEL_HI);
		float y = LY + 44;
		if(!cur->readable)
			DrawLabel(font_ui_medium, 20, "This file could not be read as a map.", DX, y, C_RED);
		else
		{
			DrawLabel(font_ui_medium, 20, "Planet " + PlanetName(cur->planet), DX, y, C_STEEL); y += 26;
			char buf[96];
			snprintf(buf, sizeof(buf), "%d x %d tiles", cur->w, cur->h);
			DrawLabel(font_ui_medium, 20, buf, DX, y, C_STEEL); y += 26;
			snprintf(buf, sizeof(buf), "%d team%s with a fort", cur->teams, cur->teams == 1 ? "" : "s");
			DrawLabel(font_ui_medium, 20, buf, DX, y, C_STEEL); y += 38;

			if(can_play)
			{
				DrawLabel(font_ui_medium, 20, "You command the red team.", DX, y, C_GREEN); y += 26;
				DrawLabel(font_ui_medium, 20, "Bots take the other forts.", DX, y, C_GREEN);
			}
			else
			{
				DrawLabel(font_ui_medium, 20, "Not playable yet. A map needs:", DX, y, C_AMBER); y += 28;
				DrawLabel(font_ui_medium, 20, cur->red_fort ? "+ a red fort (yours)" : "- a red fort (yours)", DX, y, cur->red_fort ? C_GREEN : C_RED); y += 26;
				DrawLabel(font_ui_medium, 20, cur->teams >= 2 ? "+ an enemy fort" : "- an enemy fort", DX, y, cur->teams >= 2 ? C_GREEN : C_RED); y += 26;
				DrawLabel(font_ui_medium, 20, "Zones with flags make factories work.", DX, y, C_STEEL_DIM);
			}
		}
	}

	//---- buttons ----
	float by = PY + PH - 74;
	bool act_back = false, act_play = false;
	if(Button("Back", PX + 36, by, 130, 48, !modal)) act_back = true;
	if(Button("Open Folder", PX + 182, by, 170, 48, !modal))
	{
		Sound(snd_click);
		string url = "file://" + custom_dir;
		for(size_t c = 0; c < url.size(); c++) if(url[c] == ' ') { url.replace(c, 1, "%20"); c += 2; }
		if(!SDL_OpenURL(url.c_str()))
		{
			status_message = "Could not open the folder: " + custom_dir;
			status_until = SDL_GetTicks() + 5000;
		}
	}
	if(Button("New Map", PX + 368, by, 150, 48, !modal))
	{
		Sound(snd_click);
		newmap_open = true;
		editing_mapname = true;
		newmap_name.clear();
		if(ZVideo_GetWindow()) SDL_StartTextInput(ZVideo_GetWindow());
		clicked = false;
		return;
	}
	if(Button("Edit", PX + PW - 36 - 190 - 16 - 130, by, 130, 48, !modal && cur && cur->readable))
	{
		Sound(snd_click);
		vector<string> args;
		args.push_back(custom_dir + "/" + cur->file);
		LaunchEditor(args);
	}
	if(Button("Play", PX + PW - 36 - 190, by, 190, 48, !modal && can_play, true)) act_play = true;

	if(!modal)
	{
		if(kb_back) { kb_back = false; act_back = true; }
		if(kb_activate && can_play) { kb_activate = false; act_play = true; }
	}
	if(act_back) { screen = SCR_MAIN; Sound(snd_click); return; }
	if(act_play)
	{
		Sound(snd_start);
		Action a(FE_START_MAP_FILE);
		a.text = custom_dir + "/" + cur->file;
		actions.push_back(a);
		screen = SCR_LOADING;
		loading_since = SDL_GetTicks();deployment_stage="Connecting to the game server";
		return;
	}

	if(modal) RenderNewMap();
}

void ZFrontEnd::RenderNewMap()
{
	DimScreen(150);

	static const char *size_name[] = { "Small  60 x 60", "Medium  80 x 100", "Large  128 x 128" };
	static const char *size_arg[] = { "60x60", "80x100", "128x128" };

	const float PX = 360, PY = 230, PW = 560, PH = 350;
	DrawPanel(PX, PY, PW, PH);
	DrawLabel(font_ui, 24, "NEW MAP", PX + 32, PY + 24, C_AMBER);

	//name
	DrawLabel(font_ui_medium, 20, "Name", PX + 32, PY + 82, C_STEEL);
	FillRect(PX + 150, PY + 74, PW - 182, 40, C_LED_BG);
	FrameRect(PX + 150, PY + 74, PW - 182, 40, C_AMBER_DIM, 255, 1.5f);
	DrawLabel(font_ui, 19, newmap_name.empty() ? string("type a name") : newmap_name, PX + 162, PY + 83, newmap_name.empty() ? C_AMBER_DIM : C_AMBER);
	if(!newmap_name.empty() && ((SDL_GetTicks() / 450) & 1))
		FillRect(PX + 164 + TextWidth(font_ui, 19, newmap_name), PY + 82, 3, 24, C_AMBER);

	//planet and size steppers
	for(int row=0; row<2; row++)
	{
		float y = PY + 136 + row * 56;
		DrawLabel(font_ui_medium, 20, row == 0 ? "Planet" : "Size", PX + 32, y + 8, C_STEEL);
		float cx = PX + 150, cw = PW - 182, bw = 40;
		for(int k=0;k<2;k++)
		{
			float bx = k == 0 ? cx : cx + cw - bw;
			if(Button(k == 0 ? "<" : ">", bx, y, bw, 40))
			{
				Sound(snd_toggle);
				int d = k == 0 ? -1 : 1;
				if(row == 0) newmap_planet = (newmap_planet + d + MAX_PLANET_TYPES) % MAX_PLANET_TYPES;
				else newmap_size = (newmap_size + d + 3) % 3;
			}
		}
		DrawLabel(font_ui, 24, row == 0 ? PlanetName(newmap_planet) : string(size_name[newmap_size]), cx + cw * 0.5f, y + 4, C_STEEL_HI, 1);
	}

	string file = NewMapFileName();
	bool exists = false;
	for(size_t i=0;i<custom_maps.size();i++) if(custom_maps[i].file == file) exists = true;
	if(exists) DrawLabel(font_ui_medium, 18, "A map with this name already exists.", PX + 32, PY + 250, C_RED);
	else if(!file.empty()) DrawLabel(font_ui_medium, 18, "Will be saved as " + file, PX + 32, PY + 250, C_STEEL_DIM);

	bool close = false;
	if(Button("Cancel", PX + 32, PY + PH - 70, 150, 46) || kb_back) { kb_back = false; close = true; Sound(snd_click); }
	bool ok = !file.empty() && !exists;
	if(Button("Create and Edit", PX + PW - 32 - 240, PY + PH - 70, 240, 46, ok, true) || (kb_activate && ok))
	{
		kb_activate = false;
		Sound(snd_click);
		vector<string> args;
		args.push_back("-n");
		args.push_back("-f"); args.push_back(custom_dir + "/" + file);
		args.push_back("-d"); args.push_back(size_arg[newmap_size]);
		args.push_back("-p"); args.push_back(planet_type_string[newmap_planet]);
		args.push_back("-m"); args.push_back(newmap_name);
		LaunchEditor(args);
		close = true;
	}
	kb_activate = false;

	if(close)
	{
		newmap_open = false;
		editing_mapname = false;
		if(ZVideo_GetWindow()) SDL_StopTextInput(ZVideo_GetWindow());
	}
}

void ZFrontEnd::RenderImport()
{
    DimScreen(190);
    const float x=220,y=120,w=840,h=560;
    DrawPanel(x,y,w,h);
    DrawLabel(font_ui,28,"IMPORT GAME CONTENT",x+32,y+28,C_AMBER);
    DrawLabel(font_ui_medium,20,"Bring your original media into zod-enhanced. Works entirely offline.",x+32,y+78,C_STEEL);
    std::string filename=asset_import.source;
    const size_t slash=filename.find_last_of("/\\");if(slash!=std::string::npos)filename=filename.substr(slash+1);
    if(filename.size()>65)filename=filename.substr(0,62)+"...";
    DrawLabel(font_ui_medium,20,filename.empty()?"ISO / CUE / BIN":filename,x+32,y+125,C_AMBER_HI);
    std::string line;std::vector<std::string> lines;std::string text=asset_import.message;
    for(size_t at=0;at<text.size();) {
        size_t end=text.find(' ',at);if(end==std::string::npos)end=text.size();
        std::string word=text.substr(at,end-at),test=line.empty()?word:line+" "+word;
        if(TextWidth(font_ui_medium,20,test)>w-64&&!line.empty()){lines.push_back(line);line=word;}else line=test;
        at=end+1;
    }
    if(!line.empty())lines.push_back(line);
    for(size_t i=0;i<lines.size()&&i<5;i++)DrawLabel(font_ui_medium,20,lines[i],x+32,y+175+i*27,C_STEEL);
    if(asset_import.Busy()) {
        if(kb_back){asset_import.Cancel();kb_back=false;}
        FrameRect(x+32,y+325,w-64,16,C_AMBER_DIM);
        FillRect(x+34,y+327,(w-68)*asset_import.percent/100.0f,12,C_AMBER);
        if(Button("Cancel import",x+32,y+h-74,200,46))asset_import.Cancel();
    } else {
        if(Button("Import levels",x+510,y+h-74,154,46))asset_import.ChooseLevels(ZVideo_GetWindow());
        if(Button("Choose disc...",x+32,y+h-74,200,46) || (kb_activate && !asset_import.Ready()))asset_import.Choose(ZVideo_GetWindow());
        if(asset_import.Ready()&&(Button("Import all media",x+250,y+h-74,235,46,true,true)||kb_activate))asset_import.Start();
        if(Button(import_only?"Quit":"Back",x+w-160,y+h-74,128,46)||kb_back){kb_back=false;if(import_only)actions.push_back(Action(FE_QUIT));else screen=(Screen)import_return_screen;}
    }
    DrawLabel(font_ui_medium,18,"Imported files stay in your game profile. The installed app stays intact.",x+32,y+378,C_STEEL_DIM);
    auto checks=ZAsset_CheckContent();
    for(size_t i=0;i<checks.size();++i){const auto &check=checks[i];DrawLabel(font_ui_medium,16,check.category+": "+to_string(check.present)+"/"+to_string(check.total),x+32+(i%3)*255,y+405+(i/3)*24,check.present==check.total?C_STEEL:C_AMBER);}
    if(!asset_import.Busy()&&Button("Repair / reimport",x+32,y+340,210,32))asset_import.Choose(ZVideo_GetWindow());
    if(!asset_import.Busy()&&Button("Export diagnostics",x+260,y+340,220,32)){ReviewDiagnostics();}
    if(!asset_import.Busy()&&Button("Open reports folder",x+500,y+340,260,32)){SDL_CreateDirectory((ZAsset_Profile()+"diagnostics").c_str());SDL_OpenURL(("file://"+ZAsset_Profile()+"diagnostics").c_str());}
    if(asset_import.Busy())DrawLabel(font_ui_medium,18,to_string(asset_import.percent)+"%",x+w-90,y+345,C_AMBER);
    if(asset_import.phase=="complete") {
        if(Button("Original MIDI",x+32,y+458,210,28)) {zod_music=true;zod_music_style=MUSIC_STYLE_MIDI;ZPrefs_Save();actions.push_back(Action(FE_SET_MUSIC,1));}
        if(Button("Recorded music",x+260,y+458,230,28)) {zod_music=true;zod_music_style=MUSIC_STYLE_RECORDED;ZPrefs_Save();actions.push_back(Action(FE_SET_MUSIC,1));}
    }
}

void ZFrontEnd::ScanCampaigns(){vector<string> errors;campaigns=ZCampaign_List(&errors);for(const auto &pack:campaigns){string issue;if(!ZCampaign_MigrateIdentity(pack,issue))errors.push_back(issue);}campaign_error=errors.empty()?"":errors.front();}
void ZFrontEnd::SelectCampaign(int index){
    selected_campaign=index<0?ZCampaign():campaigns[index];levels=original_levels;community_running=false;current_level=-1;community_next=-1;
    if(index>=0){levels.clear();for(const auto &mission:selected_campaign.missions){LevelInfo info;info.title=mission.title;info.planet=DESERT;info.w=info.h=0;ZCampaign_SafeAsset(selected_campaign.root,mission.map,info.file);info.base=mission.map;ZMap map;if(map.Read(info.file.c_str())){info.planet=map.GetMapBasics().terrain_type;info.w=map.GetMapBasics().width;info.h=map.GetMapBasics().height;}levels.push_back(info);}}
    LoadProgress();screen=SCR_MAIN;
}
std::vector<int> ZFrontEnd::CampaignVisibleMissions(const ZCampaign *pack,int unlocked) const {
 auto completed=ZCampaign_Completed(pack?pack->id:"original");vector<int> visible;bool next=false;int count=pack?(int)pack->missions.size():(int)original_levels.size();
 for(int i=0;i<count;++i){string key=pack?ZCampaign_MissionKey(*pack,i):original_levels[i].base;bool done=!pack&&i<unlocked||find(completed.begin(),completed.end(),key)!=completed.end();if(!pack)for(const auto &score:ZHighScore_All())done|=score.map_file==original_levels[i].file;if(done)visible.push_back(i);else if(!next&&(!pack||i==unlocked)){visible.push_back(i);next=true;}}return visible;
}
string ZFrontEnd::CampaignMoviePath(const ZCampaign *pack,int index,bool victory) const {
 if(pack){if(index<0||index>=(int)pack->missions.size())return "";string video=victory?pack->missions[index].victoryVideo:pack->missions[index].video;if(video.empty())return "";if(video.compare(0,9,"original:")==0){string clip=video.substr(9);return ZMovie_HasClip(clip)?ZSDL_DataPath("assets/videos/"+clip+".mp4"):"";}string path;return ZCampaign_SafeAsset(pack->root,video,path)?path:"";}
 if(index<0||index>=(int)original_levels.size())return "";char clip[32];char letter=planet_letter(original_levels[index].planet);
 if(victory){if(index+1==(int)original_levels.size())snprintf(clip,sizeof(clip),"E_OUTRO");else if(original_levels[index+1].planet!=original_levels[index].planet)snprintf(clip,sizeof(clip),"E_%cWINPL",letter);else snprintf(clip,sizeof(clip),"E_%cWINT%d",letter,index%3+1);}
 else {if(index==0||original_levels[index-1].planet!=original_levels[index].planet){snprintf(clip,sizeof(clip),"E_%cPLNT",letter);if(ZMovie_HasClip(clip))return ZSDL_DataPath("assets/videos/"+string(clip)+".mp4");}snprintf(clip,sizeof(clip),"E_%cTRAV%d",letter,index%3+1);}
 return ZMovie_HasClip(clip)?ZSDL_DataPath("assets/videos/"+string(clip)+".mp4"):"";
}
void ZFrontEnd::OpenHistoryBriefing(int index){
 if(index<0||index>=(int)levels.size())return;history_briefing=true;briefing_level=index;briefing_scroll=0;screen=SCR_BRIEFING;
 if(!selected_campaign.id.empty()){const auto &mission=selected_campaign.missions[index];ZPortrait::Init();campaign_portrait.SetRobotID(GRUNT);for(int i=0;i<MAX_ROBOT_TYPES;++i)if(robot_type_string[i]==mission.character)campaign_portrait.SetRobotID(i);campaign_portrait.SetTeam(RED_TEAM);campaign_portrait.SetCords(0,0);campaign_portrait.SetOverMap(false);campaign_portrait.SetTerrainType((planet_type)levels[index].planet);campaign_portrait.SetDoRandomAnims(!zod_reduced_motion);campaign_portrait.StartAnim(UNIT_REPORTING1_ANIM,false);}
}
void ZFrontEnd::OpenCampaignGroup(int scope){
 campaign_scope=scope;ScanCampaigns();campaign_choice=scope==2&&!campaigns.empty()?0:-1;campaign_previous_choice=-2;campaign_scroll=0;campaign_mission_focus=false;campaign_import_preview=campaign_manage=campaign_records=campaign_gallery=false;screen=SCR_CAMPAIGNS;clicked=kb_activate=focus_activate=false;
}
void ZFrontEnd::RenderPlayModes(bool multiplayer){
 DimScreen(190);DrawPanel(180,110,920,580);DrawLabel(font_ui,30,multiplayer?"MULTIPLAYER":"SINGLE PLAYER",216,142,C_AMBER);
 if(!multiplayer){
  if(kb_dy)mode_choice=(mode_choice+kb_dy+2)%2;
  if(Button("Original Campaign",216,224,848,52,true,mode_choice==0)||kb_activate&&mode_choice==0){OpenCampaignGroup(1);return;}
  DrawLabel(font_ui_medium,20,"The original missions, completion history, briefings and cinematics.",232,292,C_STEEL);
  if(Button("Imported Campaigns",216,350,848,52,true,mode_choice==1)||kb_activate&&mode_choice==1){OpenCampaignGroup(2);return;}
  DrawLabel(font_ui_medium,20,"Install and play community campaigns with their own saved progress.",232,418,C_STEEL);
 }else{
  Button("Planetary Conquest",216,216,848,48,false);
  DrawLabel(font_ui_medium,19,"Planned: online planetary territory war through our public infrastructure.",232,274,C_STEEL_DIM);
  if(Button("Server Browser",216,326,848,52,true,true)||kb_activate){kb_activate=false;actions.push_back(Action(FE_OPEN_MULTIPLAYER));return;}
  DrawLabel(font_ui_medium,20,"Public dedicated servers, Local Area Network and private direct connect.",232,390,C_STEEL);
  Button("Co-op",216,446,848,48,false);
  DrawLabel(font_ui_medium,19,"Future mode. Planetary Conquest and Co-op are not yet playable.",232,506,C_STEEL_DIM);
 }
 if(Button("Back",860,606,204,44)||kb_back){kb_back=false;screen=SCR_MAIN;}
}
void ZFrontEnd::RenderCampaigns(){
    string picked;if(ZCampaign_PollPack(picked)){campaign_manage=campaign_records=campaign_gallery=false;campaign_dependency_scroll=0;campaign_pack_path=picked;campaign_import_preview=ZCampaign_InspectPack(picked,campaign_pack_summary,campaign_error);}
    bool modal=campaign_import_preview||campaign_manage||campaign_records||campaign_gallery;
    DimScreen(190);DrawPanel(100,80,1080,650);DrawLabel(font_ui,30,campaign_scope==1?"SINGLE PLAYER / ORIGINAL CAMPAIGN":campaign_scope==2?"SINGLE PLAYER / IMPORTED CAMPAIGNS":"SINGLE PLAYER / CAMPAIGNS",132,104,C_AMBER);
    bool emptyImported=campaign_scope==2&&campaigns.empty();int first=campaign_scope==2&&!emptyImported?0:-1;campaign_choice=campaign_scope==1?-1:max(first,min(campaign_choice,(int)campaigns.size()-1));
    if(kb_dx&&!modal)campaign_mission_focus=kb_dx>0;
    if(kb_dy&&!modal&&!campaign_mission_focus)campaign_choice=campaign_scope==1?-1:max(first,min(campaign_choice+kb_dy,(int)campaigns.size()-1));
    if(campaign_scope!=2&&Button("Original Campaign",132,166,380,52,!modal,campaign_choice==-1))campaign_choice=-1;
    if(campaign_scope==2)DrawLabel(font_ui,22,emptyImported?"No imported campaigns":"INSTALLED CAMPAIGNS",132,180,C_STEEL);
    if(wheel&&!modal&&Hover(132,230,380,330))campaign_scroll-=wheel>0?1:-1;
    campaign_scroll=max(0,min(campaign_scroll,max(0,(int)campaigns.size()-5)));
    for(int row=0;campaign_scope!=1&&row<5&&row+campaign_scroll<(int)campaigns.size();++row){int i=row+campaign_scroll;if(Button(campaigns[i].title.substr(0,30),132,234+row*60,380,52,!modal,campaign_choice==i))campaign_choice=i;}
    const ZCampaign *pack=campaign_choice<0?nullptr:&campaigns[campaign_choice];
    string title=emptyImported?"Import a campaign pack to begin":pack?pack->title:"Original Campaign",description=pack?pack->description:"The original Z campaign, preserved with its own progress.";
    DrawLabel(font_ui,28,title.substr(0,40),548,166,C_AMBER);
    int current=0,unlocked=0,count=emptyImported?0:pack?(int)pack->missions.size():(int)original_levels.size();
    if(pack)ZCampaign_Progress(pack->id,current,unlocked);else {const char *home=getenv("HOME");if(!home)home=getenv("USERPROFILE");if(home){FILE *f=fopen((string(home)+"/.zod_campaign_progress").c_str(),"r");if(f){int ignored=fscanf(f,"%d %d",&current,&unlocked);(void)ignored;fclose(f);}}}
    current=pack&&current==-2?-2:max(0,min(current,max(0,count-1)));unlocked=max(0,min(unlocked,max(0,count-1)));
    DrawLabel(font_ui_medium,20,emptyImported?"No campaign installed":to_string(count)+" missions / "+(pack?pack->difficulty:"Original difficulty"),548,212,C_STEEL);
    DrawLabel(font_ui_medium,19,emptyImported?"Use Import pack below to install a campaign.":"Completed missions and the next mission in line",548,246,C_STEEL);
    auto completed=ZCampaign_Completed(pack?pack->id:"original");auto done=[&](int i){string key=pack?ZCampaign_MissionKey(*pack,i):original_levels[i].base;bool finished=!pack&&i<unlocked||find(completed.begin(),completed.end(),key)!=completed.end();if(!pack)for(const auto &score:ZHighScore_All())finished|=score.map_file==original_levels[i].file;return finished;};
    auto visible=emptyImported?vector<int>{}:CampaignVisibleMissions(pack,pack?current:unlocked);int next=-1,finished=0;for(int i=0;i<count;++i){if(done(i))++finished;else if(next<0&&(!pack||i==current))next=i;}
    if(pack&&ZCampaign_Branched(*pack))next=current>=0?current:-1;
    if(campaign_previous_choice!=campaign_choice){campaign_mission_choice=next>=0?next:count-1;campaign_mission_scroll=max(0,(int)visible.size()-6);campaign_previous_choice=campaign_choice;}
    if(find(visible.begin(),visible.end(),campaign_mission_choice)==visible.end())campaign_mission_choice=visible.empty()?-1:visible.back();
    DrawLabel(font_ui_medium,18,emptyImported?"Your Original Campaign remains separate.":to_string(finished)+" completed / "+(next>=0?"next mission "+to_string(next+1):"campaign complete"),548,274,C_AMBER);
    FillRect(548,298,600,4,C_STEEL_DIM,140);if(count)FillRect(548,298,600.0f*finished/count,4,C_AMBER);
    if(wheel&&Hover(548,302,600,216)&&!modal)campaign_mission_scroll-=wheel>0?1:-1;
    if(!modal&&kb_dy&&campaign_mission_focus&&!visible.empty()){auto found=find(visible.begin(),visible.end(),campaign_mission_choice);int row=found-visible.begin();row=max(0,min(row+kb_dy,(int)visible.size()-1));campaign_mission_choice=visible[row];campaign_mission_scroll=max(0,min(campaign_mission_scroll,row));if(row>=campaign_mission_scroll+6)campaign_mission_scroll=row-5;}
    campaign_mission_scroll=max(0,min(campaign_mission_scroll,max(0,(int)visible.size()-6)));
    for(int row=0;row<6&&row+campaign_mission_scroll<(int)visible.size();++row){int i=visible[row+campaign_mission_scroll];string name=pack?pack->missions[i].title:original_levels[i].title;string label=to_string(i+1)+". "+(done(i)?"Completed / ":"Next / ")+name;
        while(label.size()>3&&TextWidth(font_ui,22,label)>568)label.pop_back();
        if(Button(label,548,306+row*35,600,32,!modal,campaign_mission_choice==i)){campaign_mission_choice=i;campaign_mission_focus=true;}}
    if(visible.size()>6)DrawLabel(font_ui_medium,16,"Scroll for more completed missions",548,518,C_STEEL_DIM);
    string selectedTitle=campaign_mission_choice<0?"No missions":pack?pack->missions[campaign_mission_choice].title:original_levels[campaign_mission_choice].title;
    DrawLabel(font_ui_medium,18,selectedTitle.substr(0,60),548,526,C_STEEL);
    if(campaign_mission_choice>=0){string key=pack?ZCampaign_MissionKey(*pack,campaign_mission_choice):original_levels[campaign_mission_choice].base;for(auto &record:ZCampaign_Records(pack?pack->id:"original"))if(record.key==key){DrawLabel(font_ui_medium,16,"Best "+format_time(record.bestSeconds)+" / "+bot_difficulty_name[record.difficulty]+" / "+to_string(record.wins)+" wins / "+ZCampaign_Medal(record),548,548,C_AMBER);}}

    auto choose=[&](){SelectCampaign(campaign_choice);history_briefing=false;};
    if(Button(next>=0?"Continue":"Replay last",548,574,180,40,!modal&&count>0)||(!modal&&kb_activate&&!campaign_mission_focus&&count)){kb_activate=false;choose();StartLevel(next>=0?next:visible.empty()?-1:visible.back(),true);history_briefing=screen==SCR_BRIEFING;}
    if(Button("Restart",744,574,170,40,!modal&&count>0)){choose();StartLevel(0,true);history_briefing=screen==SCR_BRIEFING;}
    bool selected=campaign_mission_choice>=0;
    if(Button(selected&&done(campaign_mission_choice)?"Replay mission":"Play next mission",930,574,218,40,!modal&&selected)||(!modal&&kb_activate&&campaign_mission_focus&&selected)){kb_activate=false;int i=campaign_mission_choice;choose();StartLevel(i,true);history_briefing=screen==SCR_BRIEFING;}
    if(Button("Briefing",548,624,180,36,!modal&&selected)){int i=campaign_mission_choice;choose();OpenHistoryBriefing(i);}
    string intro=CampaignMoviePath(pack,campaign_mission_choice,false),victory=selected&&done(campaign_mission_choice)?CampaignMoviePath(pack,campaign_mission_choice,true):"";
    auto watch=[&](const string &file){ZAudio_PauseMusic(true);int result=ZMovie_Play(file);ZAudio_PauseMusic(false);SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_WHEEL);clicked=kb_activate=false;if(result==ZMOVIE_MISSING)campaign_error="Video playback is unavailable for this file on this platform.";};
    if(Button("Intro video",744,624,170,36,!modal&&!intro.empty()))watch(intro);
    if(Button("Victory video",930,624,218,36,!modal&&!victory.empty()))watch(victory);
    if(!emptyImported&&!pack&&intro.empty()&&victory.empty())DrawLabel(font_ui_medium,16,"Import disc media for cinematics.",132,536,C_STEEL_DIM);
    if(selected&&(pack||campaign_scope==1||campaigns.empty())){string preview=pack?pack->root+"/"+pack->missions[campaign_mission_choice].map:original_levels[campaign_mission_choice].file;DrawMapPreview(preview,pack?1068:132,pack?520:238,pack?80:380,pack?48:260);}
    if(selected){string media="Media: ";if(pack){const auto &m=pack->missions[campaign_mission_choice];int missing=0;for(const auto &video:{m.video,m.victoryVideo})if(!video.empty()){string path;if(video.compare(0,9,"original:")==0){if(!ZMovie_HasClip(video.substr(9)))++missing;}else if(!ZCampaign_SafeAsset(pack->root,video,path))++missing;}media+=missing?to_string(missing)+" optional videos missing":"ready / briefing fallback supported";}else media+=intro.empty()?"import disc for optional cinematics":"cinematics installed";DrawLabel(font_ui_medium,15,media,548,562,C_STEEL_DIM);}
    if(!emptyImported)DrawLabel(font_ui_medium,15,"Estimated campaign length: "+to_string(count*10)+"-"+to_string(count*25)+" min",132,600,C_STEEL_DIM);
    if(pack){if(campaign_cover_path!=pack->root+"/"+pack->cover){SDL_DestroyTexture(campaign_cover);campaign_cover=nullptr;campaign_cover_path=pack->root+"/"+pack->cover;string path;if(!pack->cover.empty()&&ZCampaign_SafeAsset(pack->root,pack->cover,path)){SDL_Surface *image=IMG_Load(path.c_str());if(image){campaign_cover=SDL_CreateTextureFromSurface(renderer,image);SDL_DestroySurface(image);}}}if(campaign_cover){float w,h;SDL_GetTextureSize(campaign_cover,&w,&h);float scale=min(160.0f/w,58.0f/h);SDL_FRect dst{330,548,w*scale,h*scale};SDL_RenderTexture(renderer,campaign_cover,nullptr,&dst);}}
    if(pack){DrawLabel(font_ui_medium,17,(pack->author.empty()?"Community campaign":"By "+pack->author).substr(0,46),132,526,C_STEEL_DIM);float tx=132,width=180;std::istringstream text(pack->description);string word,line;int row=0;while(text>>word){if(!line.empty()&&TextWidth(font_ui_medium,16,line+" "+word)>width){DrawLabel(font_ui_medium,16,line,tx,545+row*16,C_STEEL_DIM);line.clear();if(++row>=1)break;}if(!line.empty())line+=' ';line+=word;}if(row<1&&!line.empty())DrawLabel(font_ui_medium,16,line,tx,545+row*16,C_STEEL_DIM);}
    if(!campaign_error.empty())DrawLabel(font_ui_medium,15,campaign_error.substr(0,110),132,712,C_RED);
    if(Button("Video gallery",132,620,174,36,!modal&&count>0)){campaign_gallery=true;gallery_scroll=0;}
    if(Button("Manage pack",322,620,174,36,!modal&&pack))campaign_manage=true;
    if(Button("Mission details",132,574,174,36,!modal&&campaign_mission_choice>=0))campaign_records=true;
    if(campaign_scope!=1&&Button("Import pack",132,670,174,40,!modal))ZCampaign_PickPack(SDL_GetKeyboardFocus());
    if(campaign_scope!=1&&Button("Open folder",322,670,174,40,!modal))SDL_OpenURL(("file://"+ZCampaign_Root()).c_str());
    if(Button("Refresh",512,670,144,40,!modal))ScanCampaigns();
    if(Button("Back",1004,670,144,40,!modal)||kb_back){if(modal){campaign_import_preview=campaign_manage=campaign_records=campaign_gallery=false;}else screen=campaign_scope?SCR_SINGLE_PLAYER:SCR_MAIN;kb_back=false;}
    if(campaign_import_preview){auto requirements=ZCampaign_Compatibility(campaign_pack_summary,campaigns);bool existing=false,newer=true,author=false;string installedVersion;for(auto &pack:campaigns)if(pack.id==campaign_pack_summary.id){existing=true;installedVersion=pack.version;newer=ZCampaign_CompareVersion(campaign_pack_summary.version,pack.version)>0;string marker;author=ZCampaign_SafeAsset(pack.root,".zod-authoring",marker);}
        DrawPanel(240,150,800,530);DrawLabel(font_ui,28,"INSTALL CAMPAIGN PACK",272,172,C_AMBER);DrawLabel(font_ui,25,campaign_pack_summary.title.substr(0,48)+" / v"+campaign_pack_summary.version,272,220,C_STEEL);DrawLabel(font_ui_medium,18,"Engine: "+(campaign_pack_summary.engineMin.empty()?string("any"):campaign_pack_summary.engineMin)+" to "+(campaign_pack_summary.engineMax.empty()?string("latest"):campaign_pack_summary.engineMax)+" / running "+ZOD_VERSION,272,260,C_STEEL);
        auto &dependencies=campaign_pack_summary.requires;if(wheel)campaign_dependency_scroll-=wheel>0?1:-1;campaign_dependency_scroll=max(0,min(campaign_dependency_scroll,max(0,(int)dependencies.size()-6)));if(dependencies.empty())DrawLabel(font_ui_medium,19,"No additional campaign packs required",272,306,C_STEEL);for(int row=0;row<6&&row+campaign_dependency_scroll<(int)dependencies.size();++row){auto dependency=dependencies[row+campaign_dependency_scroll];string id=dependency.substr(0,dependency.find('@')),status="missing";for(auto &pack:campaigns)if(pack.id==id)status="installed v"+pack.version;DrawLabel(font_ui_medium,18,(dependency+" / "+status).substr(0,82),272,304+row*32,C_STEEL);}
        string conflict=!requirements.empty()?requirements.front():author?"Authoring projects must be updated in the editor":existing&&!newer?"Update requires a newer version than installed v"+installedVersion:existing?"Updating v"+installedVersion+"; current version will be backed up":"Requirements satisfied";DrawLabel(font_ui_medium,17,conflict.substr(0,88),272,522,C_AMBER);DrawLabel(font_ui_medium,16,"Scroll to review dependencies. Progress and records are retained.",272,558,C_STEEL_DIM);
        if(!campaign_error.empty())DrawLabel(font_ui_medium,16,campaign_error.substr(0,88),272,582,C_AMBER);
        if(Button(existing?"Update":"Install",272,610,240,44,requirements.empty()&&!author&&(!existing||newer))&&ZCampaign_ImportPack(campaign_pack_path,campaign_error,existing)){campaign_import_preview=false;ScanCampaigns();}if(Button("Cancel",768,610,240,44))campaign_import_preview=false;
    }
    if(campaign_manage&&pack){auto backups=ZCampaign_Backups(pack->id);campaign_backup_choice=backups.empty()?0:campaign_backup_choice%backups.size();DrawPanel(260,190,760,420);DrawLabel(font_ui,28,"MANAGE CAMPAIGN",292,214,C_AMBER);DrawLabel(font_ui,24,pack->title.substr(0,36)+" / v"+pack->version,292,266,C_STEEL);DrawLabel(font_ui_medium,18,"Uninstall and restore retain mission progress and records.",292,310,C_STEEL);DrawLabel(font_ui_medium,17,"Restore also checks other installed packs for dependency conflicts.",292,342,C_STEEL_DIM);
        if(backups.empty())DrawLabel(font_ui_medium,18,"No previous update backups",292,390,C_STEEL_DIM);else {if(Button("Backup v"+backups[campaign_backup_choice].version+" (change)",292,382,380,42))campaign_backup_choice=(campaign_backup_choice+1)%backups.size();if(Button("Restore backup",690,382,298,42)&&ZCampaign_RestoreBackup(pack->id,backups[campaign_backup_choice].folder,campaign_error)){campaign_manage=false;ScanCampaigns();}}
        if(!campaign_error.empty())DrawLabel(font_ui_medium,16,campaign_error.substr(0,80),292,472,C_AMBER);if(Button("Uninstall pack",292,530,280,44)){string id=pack->id;if(ZCampaign_Uninstall(id,campaign_error)){campaign_manage=false;campaign_choice=-1;ScanCampaigns();}}if(Button("Cancel",708,530,280,44))campaign_manage=false;
    }
    if(campaign_records){DrawPanel(160,150,960,450);string previewPath;if(pack)ZCampaign_SafeAsset(pack->root,pack->missions[campaign_mission_choice].map,previewPath);else previewPath=original_levels[campaign_mission_choice].file;DrawMapPreview(previewPath,192,248,240,224);DrawLabel(font_ui,28,"MISSION DETAILS & RECORD",192,176,C_AMBER);string key=pack?ZCampaign_MissionKey(*pack,campaign_mission_choice):original_levels[campaign_mission_choice].base;bool found=false;for(auto&r:ZCampaign_Records(pack?pack->id:"original"))if(r.key==key){found=true;time_t when=r.completedAt;char date[64]={};if(auto *local=localtime(&when))strftime(date,sizeof(date),"%Y-%m-%d %H:%M",local);else snprintf(date,sizeof(date),"Unknown date");DrawLabel(font_ui,24,"Best time: "+format_time(r.bestSeconds)+" / "+to_string(r.wins)+" wins",464,248,C_STEEL);DrawLabel(font_ui,22,"Last victory: "+string(date),464,296,C_STEEL);DrawLabel(font_ui,22,"Last battle: "+to_string(r.kills)+" kills / "+to_string(r.lost)+" losses",464,338,C_STEEL);DrawLabel(font_ui,22,"Highest difficulty: "+string(bot_difficulty_name[r.difficulty])+" / "+ZCampaign_Medal(r),464,380,C_AMBER);}DrawLabel(font_ui_medium,16,"Gold: no losses on Hard/Expert. Silver: no losses. Bronze: victory.",192,496,C_STEEL_DIM);if(!found)DrawLabel(font_ui,22,"Detailed records will appear after your next victory.",464,300,C_STEEL);if(Button("Close",908,530,180,44))campaign_records=false;}
    if(campaign_gallery){DrawPanel(220,140,840,500);DrawLabel(font_ui,28,"UNLOCKED VIDEO GALLERY",252,166,C_AMBER);vector<pair<string,string>> clips;for(int i:visible){string title=pack?pack->missions[i].title:original_levels[i].title;string intro=CampaignMoviePath(pack,i,false),win=done(i)?CampaignMoviePath(pack,i,true):"";if(!intro.empty())clips.push_back({to_string(i+1)+". "+title+" / Intro",intro});if(!win.empty())clips.push_back({to_string(i+1)+". "+title+" / Victory",win});}if(wheel)gallery_scroll-=wheel>0?1:-1;gallery_scroll=max(0,min(gallery_scroll,max(0,(int)clips.size()-7)));for(int row=0;row<7&&row+gallery_scroll<(int)clips.size();++row){auto &clip=clips[row+gallery_scroll];if(Button(clip.first.substr(0,60),252,218+row*44,776,38))watch(clip.second);}if(clips.empty())DrawLabel(font_ui,22,"No unlocked videos installed for this campaign.",252,250,C_STEEL);if(Button("Close",848,574,180,42))campaign_gallery=false;}

}


void ZFrontEnd::RecordCampaignResult(bool won,int seconds,int kills,int lost,int difficulty){if(editor_testing){{const auto *mission=!selected_campaign.id.empty()&&current_level>=0&&current_level<(int)selected_campaign.missions.size()?&selected_campaign.missions[current_level]:nullptr;int branch=mission?(won?mission->nextWin:mission->nextLoss):-1;community_next=branch!=-1?branch:won?(current_level+1<(int)levels.size()?current_level+1:-2):current_level;}ZPlaytest_Event(selected_campaign,current_level,won?"victory":"defeat",community_next);return;}if(won&&seconds>=0&&current_level>=0&&current_level<(int)levels.size())ZCampaign_RecordWin(selected_campaign.id.empty()?"original":selected_campaign.id,selected_campaign.id.empty()?levels[current_level].base:ZCampaign_MissionKey(selected_campaign,current_level),seconds,kills,lost,difficulty);if(won&&current_level>=0&&current_level<(int)levels.size())ZCampaign_MarkCompleted(selected_campaign.id.empty()?"original":selected_campaign.id,selected_campaign.id.empty()?levels[current_level].base:ZCampaign_MissionKey(selected_campaign,current_level));if(mission_replay){community_next=-1;return;}if(!community_running||current_level<0)return;{const auto *mission=!selected_campaign.id.empty()&&current_level>=0&&current_level<(int)selected_campaign.missions.size()?&selected_campaign.missions[current_level]:nullptr;int branch=mission?(won?mission->nextWin:mission->nextLoss):-1;community_next=branch!=-1?branch:won?(current_level+1<(int)levels.size()?current_level+1:-2):current_level;}progress_current=community_next==-2&&ZCampaign_Branched(selected_campaign)?-2:community_next==-2?(int)levels.size()-1:community_next;progress_unlocked=max(progress_unlocked,progress_current);ZCampaign_Progress(selected_campaign.id,progress_current,progress_unlocked,true);}
void ZFrontEnd::ContinueCommunity(){if(workspace&&screen==SCR_EDITOR)return;int next=community_next;community_running=false;community_next=-1;if(next>=0)StartLevel(next,true);else{ScanCampaigns();screen=SCR_CAMPAIGNS;}}
void ZFrontEnd::RenderCommunityBriefing(){
    if(briefing_level<0||briefing_level>=(int)selected_campaign.missions.size()){screen=SCR_MAIN;return;}
    const auto &mission=selected_campaign.missions[briefing_level];DimScreen(190);DrawPanel(140,100,1000,610);
    DrawLabel(font_ui,22,selected_campaign.title,176,126,C_AMBER);DrawLabel(font_ui,32,mission.title,176,166,C_STEEL_HI);
    SDL_Surface *surface=SDL_CreateSurface(86,74,SDL_PIXELFORMAT_RGBA32);if(surface){SDL_FillSurfaceRect(surface,nullptr,0);auto *previous=ZSDL_Surface::GetMainSoftwareSurface();ZSDL_Surface::SetMainSoftwareSurface(surface);if(!zod_reduced_motion){if(!campaign_portrait.DoingAnim())campaign_portrait.StartAnim(UNIT_REPORTING1_ANIM,false);campaign_portrait.Process();}campaign_portrait.DoRender();ZSDL_Surface::SetMainSoftwareSurface(previous);SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);if(texture){SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_NEAREST);SDL_FRect dst{176,244,258,222};SDL_RenderTexture(renderer,texture,nullptr,&dst);SDL_DestroyTexture(texture);}}
    DrawLabel(font_ui,25,mission.speaker,176,488,C_AMBER);
    vector<string> lines;std::istringstream input(mission.briefing.empty()?"Capture the opposing forts to complete this mission.":mission.briefing);string line,word;
    while(std::getline(input,line)){std::istringstream words(line);string row;while(words>>word){if(!row.empty()&&TextWidth(font_ui,23,row+" "+word)>624){lines.push_back(row);row.clear();}if(!row.empty())row+=' ';row+=word;}lines.push_back(row);}
    if(wheel)briefing_scroll-=wheel>0?1:-1;briefing_scroll=max(0,min(briefing_scroll,max(0,(int)lines.size()-11)));
    for(int i=0;i<11&&i+briefing_scroll<(int)lines.size();++i)DrawLabel(font_ui,23,lines[i+briefing_scroll],472,236+i*30,C_STEEL);
    if(Button(editor_testing?"Return to editor":"Back",176,636,190,44)||kb_back){kb_back=false;if(editor_testing){editor_continue_pending=false;Show();return;}screen=SCR_CAMPAIGNS;}
    if(Button("Start mission",868,636,236,44,true,true)||kb_activate){kb_activate=false;StartLevel(briefing_level,false);}
}

void ZFrontEnd::ResumeSavedCampaign(const string &map,const string &id,int mission,const string &uid){
 Screen previous=screen;ScanCampaigns();
 for(size_t c=0;c<campaigns.size();++c){if(!id.empty()&&id!=campaigns[c].id)continue;vector<int> matches;for(size_t m=0;m<campaigns[c].missions.size();++m){string file;if(ZCampaign_SafeAsset(campaigns[c].root,campaigns[c].missions[m].map,file)&&file==map)matches.push_back(m);}if(matches.empty()&&uid.empty())continue;int current=0,unlocked=0;ZCampaign_Progress(campaigns[c].id,current,unlocked);int desired=mission>=0?mission:current;for(size_t i=0;i<campaigns[c].missions.size();++i)if(!uid.empty()&&campaigns[c].missions[i].uid==uid){desired=i;matches.clear();matches.push_back(i);break;}if(matches.empty())continue;int index=find(matches.begin(),matches.end(),desired)!=matches.end()?desired:matches[0];SelectCampaign(c);current_level=index;community_running=true;community_next=-1;screen=previous;return;}
 SelectCampaign(-1);for(int i=0;i<(int)levels.size();++i)if(levels[i].file==map){current_level=i;break;}screen=previous;
}

void ZFrontEnd::LoadCheckpoint(){actions.push_back(Action(FE_LOAD_SAVE,ZSAVE_CHECKPOINT_SLOT));visible=true;screen=SCR_LOADING;loading_since=SDL_GetTicks();}
void ZFrontEnd::OpenRecordings(){recordingFiles.clear();auto folder=ZAsset_Profile()+"replays";SDL_CreateDirectory(folder.c_str());int count=0;char **files=SDL_GlobDirectory(folder.c_str(),"*.zreplay",0,&count);for(int i=0;files&&i<count&&i<512;++i){string path;if(ZCampaign_SafeAsset(folder,files[i],path))recordingFiles.push_back(files[i]);}SDL_free(files);std::sort(recordingFiles.rbegin(),recordingFiles.rend());recordingChoice=recordingScroll=0;recordingInspected=-1;recordingDetails.clear();screen=SCR_RECORDINGS;visible=true;}
void ZFrontEnd::RecordingError(const string &message){screen=SCR_RECORDINGS;visible=true;recordingError=recordingDetails=message;recordingInspected=recordingChoice;}
void ZFrontEnd::RenderRecordings(){
 DimScreen(190);DrawPanel(180,100,920,620);DrawLabel(font_ui,30,"BATTLE RECORDINGS",212,126,C_AMBER);DrawLabel(font_ui_medium,18,"Local world recordings / matching engine version required",212,174,C_STEEL);
 if(wheel)recordingScroll=max(0,min(recordingScroll-(int)wheel,max(0,(int)recordingFiles.size()-6)));if(kb_dy&&!recordingFiles.empty()){recordingChoice=max(0,min(recordingChoice+kb_dy,(int)recordingFiles.size()-1));recordingScroll=max(0,min(recordingScroll,recordingChoice));if(recordingChoice>=recordingScroll+6)recordingScroll=recordingChoice-5;}
 for(int row=0;row<6&&row+recordingScroll<(int)recordingFiles.size();++row){int i=row+recordingScroll;if(Button(recordingFiles[i],212,218+row*44,856,38,true,recordingChoice==i))recordingChoice=i;}
 if(recordingFiles.empty())DrawLabel(font_ui,22,"Enable Record replays in Game settings before a battle.",212,250,C_STEEL);
 if(recordingChoice!=recordingInspected&&!recordingFiles.empty()){recordingInspected=recordingChoice;ZReplayTape tape;recordingError.clear();string path=ZAsset_Profile()+"replays/"+recordingFiles[recordingChoice];if(tape.Load(path,recordingError)){SDL_PathInfo info{};SDL_GetPathInfo(path.c_str(),&info);recordingDetails=tape.mapName.substr(0,24)+" / "+to_string(tape.duration/1000)+" seconds / "+to_string(tape.packets.size())+" updates / team "+to_string(tape.team)+" / "+to_string(info.size/1024)+" KiB / engine "+ZOD_VERSION;}else recordingDetails=recordingError;}
 DrawLabel(font_ui_medium,17,recordingDetails.substr(0,92),212,506,C_STEEL);
 if(Button("Recover interrupted",212,548,400,42)){auto folder=ZAsset_Profile()+"replays";int count=0;char **files=SDL_GlobDirectory(folder.c_str(),"*.zreplay.part",0,&count);int recovered=0;string error;for(int i=0;files&&i<count&&i<64;++i){string path,output;if(ZCampaign_SafeAsset(folder,files[i],path)&&ZReplay_Recover(path,output,error))++recovered;}SDL_free(files);OpenRecordings();recordingDetails=recovered?to_string(recovered)+" recordings recovered; originals kept":error.empty()?"No interrupted recordings found":error;}
 if(Button("Play recording",212,620,400,48,!recordingFiles.empty()&&recordingError.empty(),true)||(!recordingFiles.empty()&&recordingError.empty()&&kb_activate)){kb_activate=false;Action action(FE_PLAY_RECORDING);action.text=ZAsset_Profile()+"replays/"+recordingFiles[recordingChoice];actions.push_back(action);}if(Button("Back",868,620,200,48)||kb_back){kb_back=false;screen=SCR_SETTINGS;}
}

void ZFrontEnd::RenderAlerts(){int before=(zod_damage_alerts?1:0)+(zod_production_alerts?2:0)+(zod_alert_sounds?4:0)+zod_alert_cooldown_ms*8;DimScreen(190);DrawPanel(240,150,800,520);DrawLabel(font_ui,30,"BATTLE ALERTS",272,174,C_AMBER);if(Button(zod_damage_alerts?"Damage alerts: On":"Damage alerts: Off",272,246,736,46))zod_damage_alerts=!zod_damage_alerts;if(Button(zod_production_alerts?"Production alerts: On":"Production alerts: Off",272,306,736,46))zod_production_alerts=!zod_production_alerts;if(Button(zod_alert_sounds?"Alert sounds: On":"Alert sounds: Off",272,366,736,46))zod_alert_sounds=!zod_alert_sounds;if(Button("Repeat cooldown: "+to_string(zod_alert_cooldown_ms)+" ms",272,426,736,46))zod_alert_cooldown_ms=zod_alert_cooldown_ms>=10000?500:zod_alert_cooldown_ms+500;DrawLabel(font_ui_medium,17,"Nearby repeated alerts merge. History retains the latest 32 notices.",272,500,C_STEEL);if(Button("Reset alerts",272,584,300,44))ZPrefs_ResetCategory(6);if(Button("Back",768,584,240,44)||kb_back){kb_back=false;screen=SCR_SETTINGS;}if(before!=(zod_damage_alerts?1:0)+(zod_production_alerts?2:0)+(zod_alert_sounds?4:0)+zod_alert_cooldown_ms*8)ZPrefs_Save();}

void ZFrontEnd::RenderMultiplayerGuide(){
 DimScreen(190);DrawPanel(140,100,1000,610);DrawLabel(font_ui,28,"MULTIPLAYER / GETTING STARTED",176,132,C_AMBER);
 const char *titles[]={"1. Import your game data","2. Public servers use your cloud account","3. Private games stay independent","4. Join friends and check compatibility"};
 const char *lines[4][4]={{"Use Import disc to install your own original graphics and sounds.","The engine download contains our code and tools, not original game media.","Map data is downloaded from the host and verified before loading.","If assets are missing, return to Import disc from the main menu."},{"Open Multiplayer > Server Browser > Public Servers, then approve in your browser.","Only sign in on your configured HTTPS portal; the game never needs your password.","Approval is short lived and can be used once for the selected server.","If it expires or the connection drops, request fresh browser approval."},{"Private and LAN hosts support guests without a cloud account.","Share the host address and game port with people you invite.","Local campaign and private play do not submit public account statistics.","The public server owner controls administrators, moderators and temporary bans."},{"Use the portal community page for friends and server invitations.","Match the host engine version; check map and capacity in the browser.","Different local maps use the host copy; your saved maps are not replaced.","If a join fails, read the connection message before retrying."}};
 DrawLabel(font_ui,25,titles[multiplayer_guide_step],176,220,C_STEEL);for(int i=0;i<4;++i)DrawLabel(font_ui_medium,20,lines[multiplayer_guide_step][i],176,282+i*60,C_STEEL);
 if(multiplayer_guide_step==0&&Button("Open importer",176,544,270,42)){import_return_screen=SCR_MULTIPLAYER_GUIDE;screen=SCR_IMPORT;}
 if(multiplayer_guide_step==3&&Button("Friends on the portal",176,544,300,42))SDL_OpenURL((ZPortal_URL()+"/community").c_str());
 if(Button("Back",176,636,190,44)){if(multiplayer_guide_step) --multiplayer_guide_step;else screen=guide_return;}
 if(kb_back){kb_back=false;screen=guide_return;}
 if(Button(multiplayer_guide_step==3?"Open server browser":"Next",780,636,324,44,true,true)||kb_activate){kb_activate=false;if(multiplayer_guide_step<3)++multiplayer_guide_step;else {SDL_CreateDirectory(ZAsset_Profile().c_str());std::ofstream seen(ZAsset_Profile()+"multiplayer-guide-seen");seen<<"1\n";seen.close();guide_completed_session=true;screen=SCR_MULTIPLAYER_MENU;actions.push_back(Action(FE_OPEN_MULTIPLAYER));}}
}

void ZFrontEnd::RenderBuildInfo(){
 DimScreen(190);DrawPanel(140,100,1000,610);DrawLabel(font_ui,28,"ABOUT / BUILD INFORMATION",176,132,C_AMBER);auto json=ZBuild_JSON();std::string value;
 const char *keys[]={"version","commit","configuration","compiler","platform"};const char *labels[]={"Version: ","Commit: ","Configuration: ","Compiler: ","Platform: "};DrawLabel(font_ui_medium,20,"Build: "+ZBuild_ID(),176,204,C_STEEL);
 for(int i=0;i<5;++i)if(ZPortal_String(json,keys[i],value))DrawLabel(font_ui_medium,19,std::string(labels[i])+value,176,250+i*42,C_STEEL);
 if(ZPortal_String(json,"source_sha256",value)){DrawLabel(font_ui_medium,17,"Source SHA-256: "+value.substr(0,32),176,474,C_STEEL_DIM);DrawLabel(font_ui_medium,17,value.substr(32),332,502,C_STEEL_DIM);}
 DrawLabel(font_ui_medium,18,"Based on Zod Engine by Michael Bok / Nighsoft. SDL3 foundation by fenio.",176,546,C_STEEL_DIM);
 if(Button("Copy build JSON",176,594,300,42))build_info_message=SDL_SetClipboardText(json.c_str())?"Build information copied":"Clipboard unavailable; use --build-info or export diagnostics";
 DrawLabel(font_ui_medium,16,build_info_message,176,650,C_AMBER);if(Button("Back",900,636,204,44)||kb_back){kb_back=false;screen=SCR_SETTINGS;}
}

void ZFrontEnd::RenderDiagnostics(){
 DimScreen(190);DrawPanel(140,80,1000,650);DrawLabel(font_ui,27,"REVIEW SUPPORT DIAGNOSTICS",176,108,C_AMBER);
 DrawLabel(font_ui_medium,18,"Review before exporting. Nothing is uploaded; share the ZIP only if you choose.",176,156,C_STEEL_DIM);
 std::vector<std::string> lines;std::istringstream input(diagnostic_preview);std::string line;while(std::getline(input,line)){while(line.size()>92){lines.push_back(line.substr(0,92));line.erase(0,92);}lines.push_back(line);}
 int last=std::max(0,(int)lines.size()-12);diagnostic_scroll=std::max(0,std::min(last,diagnostic_scroll));
 for(int i=0;i<12&&i+diagnostic_scroll<(int)lines.size();++i)DrawLabel(font_ui_medium,17,lines[i+diagnostic_scroll],176,204+i*28,C_STEEL);
 if(Button("Previous",176,558,200,40,diagnostic_scroll>0))diagnostic_scroll=std::max(0,diagnostic_scroll-12);
 if(Button("Next",392,558,200,40,diagnostic_scroll<last))diagnostic_scroll=std::min(last,diagnostic_scroll+12);
 if(Button("Export reviewed ZIP",176,620,350,44)){std::string path;diagnostic_message=ZAsset_ExportDiagnosticBundle(diagnostic_preview,path)?"Saved in profile diagnostics. Use Open folder to find the ZIP.":"Export failed. Check profile folder permissions.";}
 if(Button("Open folder",548,620,260,44)){auto folder=ZAsset_Profile()+"diagnostics";if(!SDL_CreateDirectory(folder.c_str())||!SDL_OpenURL(("file://"+folder).c_str()))diagnostic_message="Could not open the diagnostics folder.";}
 if(Button("Back",850,620,254,44)||kb_back){kb_back=false;screen=(Screen)diagnostic_return_screen;if(diagnostic_return_game){Hide();diagnostic_return_game=false;}diagnostic_preview.clear();}
 DrawLabel(font_ui_medium,16,diagnostic_message,176,686,C_AMBER);
}
