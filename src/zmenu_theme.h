#ifndef ZMENU_THEME_H
#define ZMENU_THEME_H
#include <SDL3/SDL.h>
#include "zsdl.h"
#include "zprefs.h"
// Shared command-console materials for the renderer and software game menus.
namespace ZMenuTheme {
// Capture only menu typography; the world keeps its independent pixel scaling.
void beginText(SDL_Renderer *renderer);
void textTransform(float scale,float offsetX,float offsetY);
void endText();
bool queueText(const char *value,float x,float y,float size,SDL_Color colour,int maxWidth=0,int maxHeight=0);
void presentText(SDL_Renderer *renderer,int logicalWidth,int logicalHeight);

inline SDL_Surface *text(const char *value,float size=12,SDL_Color colour={218,226,232,255}) {
 if(!TTF_WasInit() && !TTF_Init()) return nullptr;
 TTF_Font *font=TTF_OpenFont(ZSDL_DataPath("assets/frontend/fonts/Rajdhani-SemiBold.ttf").c_str(),size*zod_text_percent/100.0f);
 if(!font) return nullptr;
 SDL_Surface *result=TTF_RenderText_Blended(font,value,0,colour);
 TTF_CloseFont(font);return result;
}
inline void rect(SDL_Surface *s,int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b) {
 if(!s || w<=0 || h<=0) return;
 SDL_Rect a{x,y,w,h}; SDL_FillSurfaceRect(s,&a,SDL_MapRGBA(SDL_GetPixelFormatDetails(s->format),nullptr,r,g,b,255));
}
inline void panel(SDL_Surface *s,int x,int y,int w,int h,int u=1) {
 rect(s,x,y,w,h,12,17,22); rect(s,x+u,y+u,w-2*u,h-2*u,43,53,62);
 rect(s,x+2*u,y+2*u,w-4*u,h-4*u,24,32,39);
 rect(s,x+2*u,y+2*u,w-4*u,u,83,98,109);
 rect(s,x+2*u,y+2*u,2*u,12*u,244,164,65);
 for(int yy=18*u;yy<h-4*u;yy+=4*u) rect(s,x+4*u,y+yy,w-8*u,1,27,35,42);
 for(int i=0;i<2;i++) for(int j=0;j<2;j++) rect(s,x+(i?w-5*u:4*u),y+(j?h-5*u:4*u),u,u,100,115,125);
}
inline void button(SDL_Surface *s,int x,int y,int w,int h,bool hot,bool down,bool primary,int u=1) {
 rect(s,x,y,w,h,10,16,21);
 rect(s,x+u,y+u,w-2*u,h-2*u,hot?89:55,hot?105:69,hot?115:79);
 rect(s,x+u,y+2*u,w-2*u,h-3*u,down?29:(hot?54:37),down?38:(hot?69:49),down?46:(hot?80:59));
 if(primary || hot) rect(s,x+u,y+u,2*u,h-2*u,244,164,65);
 rect(s,x+3*u,y+h-2*u,w-6*u,u,down?18:30,down?25:40,down?31:48);
}
inline void field(SDL_Surface *s,int x,int y,int w,int h,bool selected=false) {
 rect(s,x,y,w,h,selected?47:14,selected?61:23,selected?72:30);
 if(selected) rect(s,x,y,2,h,244,164,65);
}
}
#endif
