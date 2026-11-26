// Gamepad support.
//
// The game is driven by mouse and keyboard, so a controller is translated into
// those: the left stick moves the cursor, A/B click (or Enter/Esc in the menus),
// the right stick scrolls, the d-pad sends arrow keys, the shoulder buttons zoom
// and Start opens the menu. Everything it produces goes through the normal event
// queue, so the game and the front-end need no controller-specific code.
#ifndef _ZGAMEPAD_H_
#define _ZGAMEPAD_H_

#include <SDL3/SDL.h>

void ZGamepad_Init();       // after SDL_Init: starts the gamepad subsystem, opens connected pads
void ZGamepad_Shutdown();

// Offer every polled event first. Returns true for gamepad events (they are
// consumed here and turned into mouse/key events). in_frontend picks the menu
// mapping (A = Enter, B = Esc) over the in-game one (A = left click, B = right click).
bool ZGamepad_HandleEvent(const SDL_Event &event, bool in_frontend);

// Once per frame: moves the cursor and scrolls from the sticks.
void ZGamepad_Update(bool in_frontend);

// True once a controller has been used, so the game can hide hints that only
// make sense for a mouse.
bool ZGamepad_Active();
void ZGamepad_SetEditor(bool enabled,bool modal=false);
float ZGamepad_FilterAxis(Sint16 value);

#endif
