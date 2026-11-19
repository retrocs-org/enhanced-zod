#include "zgamepad.h"
#include "zvideo.h"
#include "zprefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <map>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace std;

namespace
{
	const float STICK_DEADZONE = 0.22f;
	const float CURSOR_SPEEDS[MAX_STICK_SPEEDS] = { 0.55f, 0.85f, 1.25f };   // screens per second at full deflection (Settings -> Stick)
	const float SCROLL_SPEED = 24.0f;      // mouse-wheel units per second at full deflection
	const float TRIGGER_ON = 0.55f;

	map<SDL_JoystickID, SDL_Gamepad*> g_pads;
	bool g_active = false;

	float g_axis[4] = { 0, 0, 0, 0 };      // left x/y, right x/y (-1..1)
	bool g_trigger_l = false, g_trigger_r = false;
	float g_scroll_x = 0, g_scroll_y = 0;  // fractional wheel units not yet sent
	Uint64 g_last_tick = 0;
	bool g_mode_frontend = false;          // mapping used when a button went down, so its release matches

	enum { AX_LX, AX_LY, AX_RX, AX_RY };

	// what each held button was turned into, so the release undoes the same thing
	map<int, int> g_held;                  // button -> action
	enum { ACT_NONE, ACT_LCLICK, ACT_RCLICK, ACT_KEY };
	map<int, SDL_Keycode> g_held_key;

	float stick_value(Sint16 v)
	{
		float f = v < 0 ? v / 32768.0f : v / 32767.0f;
		float a = fabsf(f);
		if(a < STICK_DEADZONE) return 0;
		a = (a - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
		a = a * a;                          // fine control near the centre
		return f < 0 ? -a : a;
	}

	void push_key(SDL_Keycode key, bool down)
	{
		SDL_Event e;
		SDL_zero(e);
		e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
		e.key.timestamp = SDL_GetTicksNS();
		e.key.windowID = SDL_GetWindowID(ZVideo_GetWindow());
		e.key.key = key;
		e.key.scancode = SDL_GetScancodeFromKey(key, NULL);
		e.key.mod = SDL_KMOD_NONE;
		e.key.down = down;
		e.key.repeat = false;
		SDL_PushEvent(&e);
	}

	void push_click(Uint8 button, bool down)
	{
		float mx = 0, my = 0;
		SDL_GetMouseState(&mx, &my);     // window coordinates; the poll loop converts them

		SDL_Event e;
		SDL_zero(e);
		e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		e.button.timestamp = SDL_GetTicksNS();
		e.button.windowID = SDL_GetWindowID(ZVideo_GetWindow());
		e.button.button = button;
		e.button.down = down;
		e.button.clicks = 1;
		e.button.x = mx;
		e.button.y = my;
		SDL_PushEvent(&e);
	}

	void push_wheel(float x, float y)
	{
		float mx = 0, my = 0;
		SDL_GetMouseState(&mx, &my);

		SDL_Event e;
		SDL_zero(e);
		e.type = SDL_EVENT_MOUSE_WHEEL;
		e.wheel.timestamp = SDL_GetTicksNS();
		e.wheel.windowID = SDL_GetWindowID(ZVideo_GetWindow());
		e.wheel.x = x;
		e.wheel.y = y;
		e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
		e.wheel.mouse_x = mx;
		e.wheel.mouse_y = my;
		SDL_PushEvent(&e);
	}

	void do_action(int action, SDL_Keycode key, bool down)
	{
		switch(action)
		{
		case ACT_LCLICK: push_click(SDL_BUTTON_LEFT, down); break;
		case ACT_RCLICK: push_click(SDL_BUTTON_RIGHT, down); break;
		case ACT_KEY:    push_key(key, down); break;
		default: break;
		}
	}

	// Map a button press to an action for the current screen
	void button(int btn, bool down, bool in_frontend)
	{
		g_active = true;

		if(!down)
		{
			// release exactly what the press did, even if the screen changed meanwhile
			map<int, int>::iterator h = g_held.find(btn);
			if(h == g_held.end()) return;
			do_action(h->second, g_held_key[btn], false);
			g_held.erase(h);
			g_held_key.erase(btn);
			return;
		}

		int action = ACT_NONE;
		SDL_Keycode key = SDLK_UNKNOWN;

		switch(btn)
		{
		case SDL_GAMEPAD_BUTTON_DPAD_UP:    action = ACT_KEY; key = SDLK_UP; break;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:  action = ACT_KEY; key = SDLK_DOWN; break;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:  action = ACT_KEY; key = SDLK_LEFT; break;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: action = ACT_KEY; key = SDLK_RIGHT; break;
		case SDL_GAMEPAD_BUTTON_SOUTH:      // A
			if(in_frontend) { action = ACT_KEY; key = SDLK_RETURN; }
			else action = ACT_LCLICK;
			break;
		case SDL_GAMEPAD_BUTTON_EAST:       // B
			if(in_frontend) { action = ACT_KEY; key = SDLK_ESCAPE; }
			else action = ACT_RCLICK;
			break;
		case SDL_GAMEPAD_BUTTON_START:      action = ACT_KEY; key = SDLK_ESCAPE; break;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
			if(!in_frontend) { action = ACT_KEY; key = SDLK_MINUS; }
			break;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
			if(!in_frontend) { action = ACT_KEY; key = SDLK_EQUALS; }
			break;
		default: break;
		}

		if(action == ACT_NONE) return;
		g_held[btn] = action;
		g_held_key[btn] = key;
		do_action(action, key, true);
	}

	// triggers act as a second pair of mouse buttons: right = left click, left = right click
	void trigger(bool &state, float value, Uint8 mouse_button)
	{
		bool on = value > TRIGGER_ON;
		if(on == state) return;
		state = on;
		g_active = true;
		push_click(mouse_button, on);
	}

	void axis(int which, Sint16 raw)
	{
		g_active = true;
		switch(which)
		{
		case SDL_GAMEPAD_AXIS_LEFTX:  g_axis[AX_LX] = stick_value(raw); break;
		case SDL_GAMEPAD_AXIS_LEFTY:  g_axis[AX_LY] = stick_value(raw); break;
		case SDL_GAMEPAD_AXIS_RIGHTX: g_axis[AX_RX] = stick_value(raw); break;
		case SDL_GAMEPAD_AXIS_RIGHTY: g_axis[AX_RY] = stick_value(raw); break;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: trigger(g_trigger_r, raw / 32767.0f, SDL_BUTTON_LEFT); break;
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  trigger(g_trigger_l, raw / 32767.0f, SDL_BUTTON_RIGHT); break;
		default: break;
		}
	}

	void open_pad(SDL_JoystickID id)
	{
		if(g_pads.find(id) != g_pads.end()) return;
		SDL_Gamepad *pad = SDL_OpenGamepad(id);
		if(!pad) { SDL_Log("gamepad: could not open %u: %s", (unsigned)id, SDL_GetError()); return; }
		g_pads[id] = pad;
		SDL_Log("gamepad: %s connected", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "controller");
	}

	void close_pad(SDL_JoystickID id)
	{
		map<SDL_JoystickID, SDL_Gamepad*>::iterator i = g_pads.find(id);
		if(i == g_pads.end()) return;
		SDL_Log("gamepad: %s disconnected", SDL_GetGamepadName(i->second) ? SDL_GetGamepadName(i->second) : "controller");
		SDL_CloseGamepad(i->second);
		g_pads.erase(i);
		// nothing may stay held or pushed when the pad goes away
		for(int a=0;a<4;a++) g_axis[a] = 0;
		for(map<int, int>::iterator h = g_held.begin(); h != g_held.end(); ++h) do_action(h->second, g_held_key[h->first], false);
		g_held.clear();
		g_held_key.clear();
		if(g_trigger_r) { push_click(SDL_BUTTON_LEFT, false); g_trigger_r = false; }
		if(g_trigger_l) { push_click(SDL_BUTTON_RIGHT, false); g_trigger_l = false; }
	}

	// ----- test facility ---------------------------------------------------
	// ZOD_PAD_FIFO=<path to a fifo> lets a script drive the same code the real
	// controller does:   "b a 1" (button a down), "b a 0", "a lx 0.8" (axis), "q" ignored
	//   buttons: a b x y lb rb start back up down left right      axes: lx ly rx ry lt rt
	int g_fifo = -1;
	bool g_fifo_tried = false;
	string g_fifo_buf;

	int button_by_name(const string &n)
	{
		static const struct { const char *name; int id; } table[] = {
			{ "a", SDL_GAMEPAD_BUTTON_SOUTH }, { "b", SDL_GAMEPAD_BUTTON_EAST },
			{ "x", SDL_GAMEPAD_BUTTON_WEST }, { "y", SDL_GAMEPAD_BUTTON_NORTH },
			{ "lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
			{ "start", SDL_GAMEPAD_BUTTON_START }, { "back", SDL_GAMEPAD_BUTTON_BACK },
			{ "up", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
			{ "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
		};
		for(size_t i=0;i<sizeof(table)/sizeof(table[0]);i++) if(n == table[i].name) return table[i].id;
		return -1;
	}

	int axis_by_name(const string &n)
	{
		if(n == "lx") return SDL_GAMEPAD_AXIS_LEFTX;
		if(n == "ly") return SDL_GAMEPAD_AXIS_LEFTY;
		if(n == "rx") return SDL_GAMEPAD_AXIS_RIGHTX;
		if(n == "ry") return SDL_GAMEPAD_AXIS_RIGHTY;
		if(n == "lt") return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
		if(n == "rt") return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
		return -1;
	}

	void poll_fifo(bool in_frontend)
	{
#ifndef _WIN32
		if(!g_fifo_tried)
		{
			g_fifo_tried = true;
			const char *path = getenv("ZOD_PAD_FIFO");
			if(path && *path) g_fifo = open(path, O_RDONLY | O_NONBLOCK);
		}
		if(g_fifo < 0) return;

		char chunk[256];
		ssize_t n;
		while((n = read(g_fifo, chunk, sizeof(chunk))) > 0) g_fifo_buf.append(chunk, (size_t)n);

		size_t nl;
		while((nl = g_fifo_buf.find('\n')) != string::npos)
		{
			string line = g_fifo_buf.substr(0, nl);
			g_fifo_buf.erase(0, nl + 1);

			char kind = 0, name[32] = { 0 };
			float value = 0;
			if(sscanf(line.c_str(), " %c %31s %f", &kind, name, &value) < 2) continue;

			if(kind == 'b')
			{
				int id = button_by_name(name);
				if(id >= 0) button(id, value != 0, in_frontend);
			}
			else if(kind == 'a')
			{
				int id = axis_by_name(name);
				if(id >= 0) axis(id, (Sint16)(value * 32767.0f));
			}
		}
#else
		(void)in_frontend;
#endif
	}
}

void ZGamepad_Init()
{
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
	{
		SDL_Log("gamepad: not available: %s", SDL_GetError());
		return;
	}

	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);
	for(int i=0; ids && i<count; i++) open_pad(ids[i]);
	SDL_free(ids);
	g_last_tick = SDL_GetTicks();
}

void ZGamepad_Shutdown()
{
	for(map<SDL_JoystickID, SDL_Gamepad*>::iterator i = g_pads.begin(); i != g_pads.end(); ++i) SDL_CloseGamepad(i->second);
	g_pads.clear();
}

bool ZGamepad_Active() { return g_active; }

bool ZGamepad_HandleEvent(const SDL_Event &event, bool in_frontend)
{
	switch(event.type)
	{
	case SDL_EVENT_GAMEPAD_ADDED:    open_pad(event.gdevice.which); return true;
	case SDL_EVENT_GAMEPAD_REMOVED:  close_pad(event.gdevice.which); return true;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN: button(event.gbutton.button, true, in_frontend); return true;
	case SDL_EVENT_GAMEPAD_BUTTON_UP:   button(event.gbutton.button, false, in_frontend); return true;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION: axis(event.gaxis.axis, event.gaxis.value); return true;
	default:
		// the rest of the controller's event types (touchpad, sensors, ...) are of no use here
		return event.type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && event.type <= SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED;
	}
}

void ZGamepad_Update(bool in_frontend)
{
	g_mode_frontend = in_frontend;
	poll_fifo(in_frontend);

	Uint64 now = SDL_GetTicks();
	float dt = g_last_tick ? (now - g_last_tick) / 1000.0f : 0;
	g_last_tick = now;
	if(dt > 0.1f) dt = 0.1f;          // a stall must not fling the cursor across the screen
	if(dt <= 0) return;

	SDL_Window *window = ZVideo_GetWindow();
	SDL_Renderer *renderer = ZVideo_GetRenderer();
	if(!window) return;

	// left stick: the cursor, in the game's own (logical) coordinates
	if(g_axis[AX_LX] != 0 || g_axis[AX_LY] != 0)
	{
		float mx = 0, my = 0;
		SDL_GetMouseState(&mx, &my);

		float lx = mx, ly = my;
		int lw = 0, lh = 0;
		SDL_RendererLogicalPresentation mode;
		if(renderer)
		{
			SDL_RenderCoordinatesFromWindow(renderer, mx, my, &lx, &ly);
			SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
		}
		if(lw <= 0 || lh <= 0) { int ww = 0, wh = 0; SDL_GetWindowSize(window, &ww, &wh); lw = ww; lh = wh; }

		float speed = CURSOR_SPEEDS[zod_stick_speed >= 0 && zod_stick_speed < MAX_STICK_SPEEDS ? zod_stick_speed : STICK_NORMAL] * (float)lw;
		lx += g_axis[AX_LX] * speed * dt;
		ly += g_axis[AX_LY] * speed * dt;
		if(lx < 0) lx = 0; if(lx > lw - 1) lx = (float)(lw - 1);
		if(ly < 0) ly = 0; if(ly > lh - 1) ly = (float)(lh - 1);

		ZVideo_WarpMouse((int)lx, (int)ly);
	}

	// right stick: scroll (pans the map in the game, scrolls lists in the menus)
	if(g_axis[AX_RX] != 0 || g_axis[AX_RY] != 0)
	{
		g_scroll_x += g_axis[AX_RX] * SCROLL_SPEED * dt;
		g_scroll_y += -g_axis[AX_RY] * SCROLL_SPEED * dt;

		// send whole-ish steps: the game turns a wheel unit into 30 pixels
		const float STEP = 0.1f;
		if(fabsf(g_scroll_x) >= STEP || fabsf(g_scroll_y) >= STEP)
		{
			push_wheel(g_scroll_x, g_scroll_y);
			g_scroll_x = g_scroll_y = 0;
		}
	}
	else
		g_scroll_x = g_scroll_y = 0;
	(void)in_frontend;
}
