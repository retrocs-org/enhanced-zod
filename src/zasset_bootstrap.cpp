#include "zasset_import.h"
#include "zfrontend.h"
#include "zvideo.h"
#include <SDL3_ttf/SDL_ttf.h>

// This path deliberately does not construct ZPlayer, load classic sprite assets,
// or start a game server. It must work on a clean source checkout.
int ZAsset_RunSetup()
{
    if(!SDL_Init(SDL_INIT_VIDEO)) return 1;
    if(!ZVideo_SetMode(1280,800,false)) {SDL_Quit();return 1;}
    ZVideo_SetCaption("zod-ee - import original game media");
    {
        ZFrontEnd menu;
        menu.ShowImport(true);
        bool running=true;
        while(running) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_EVENT_QUIT) running=false;
                ZVideo_ConvertEventCoords(&event);
                menu.HandleEvent(event);
            }
            menu.Render();
            ZFrontEnd::Action action;
            while(menu.PopAction(action)) if(action.type==ZFrontEnd::FE_QUIT) running=false;
            SDL_Delay(10);
        }
    }
    TTF_Quit();SDL_Quit();return 0;
}
