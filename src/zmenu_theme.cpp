#include "zmenu_theme.h"
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
namespace ZMenuTheme {
namespace {
struct Command { std::string value;float x,y,size,maxWidth,maxHeight;SDL_Color colour; };
std::vector<Command> commands;
bool capturing=false;
float factor=1,offsetX=0,offsetY=0;
struct Cached { SDL_Texture *texture;float width,height; };
std::map<std::string,Cached> cache;
SDL_Renderer *cacheRenderer=nullptr;
}
void beginText(SDL_Renderer *renderer) { commands.clear();capturing=renderer!=nullptr;factor=1;offsetX=offsetY=0; }
void textTransform(float scale,float x,float y) { factor=scale;offsetX=x;offsetY=y; }
void endText() { capturing=false; }
bool queueText(const char *value,float x,float y,float size,SDL_Color colour,int maxWidth,int maxHeight) {
 if(!capturing) return false;
 if(value && *value) commands.push_back({value,x*factor+offsetX,y*factor+offsetY,size*factor,maxWidth*factor,maxHeight*factor,colour});
 return true;
}
void presentText(SDL_Renderer *renderer,int logicalWidth,int logicalHeight) {
 if(!renderer || commands.empty()) return;
 int width,height;SDL_GetRenderOutputSize(renderer,&width,&height);
 SDL_FRect viewport;SDL_GetRenderLogicalPresentationRect(renderer,&viewport);
 float density=std::max(1.0f,std::min(viewport.w/logicalWidth,viewport.h/logicalHeight));
 if(cacheRenderer!=renderer) { cache.clear();cacheRenderer=renderer; }
 if(cache.size()>512) { for(auto &entry:cache) SDL_DestroyTexture(entry.second.texture);cache.clear(); }
 SDL_Rect previous;bool clipped=SDL_RenderClipEnabled(renderer);SDL_GetRenderClipRect(renderer,&previous);
 for(const auto &command:commands) {
    int pixels=std::max(1,(int)std::ceil(command.size*density));
    std::string key=std::to_string(zod_text_percent)+":"+std::to_string(pixels)+":"+command.value;
    auto found=cache.find(key);
    if(found==cache.end()) {
        SDL_Surface *surface=text(command.value.c_str(),(float)pixels,{255,255,255,255});
        if(!surface) continue;
        Cached item{SDL_CreateTextureFromSurface(renderer,surface),(float)surface->w,(float)surface->h};
        SDL_DestroySurface(surface);if(!item.texture) continue;
        SDL_SetTextureBlendMode(item.texture,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(item.texture,SDL_SCALEMODE_LINEAR);
        found=cache.emplace(key,item).first;
    }
    auto &item=found->second;
    SDL_SetTextureColorMod(item.texture,command.colour.r,command.colour.g,command.colour.b);SDL_SetTextureAlphaMod(item.texture,command.colour.a);
    float fit=command.maxWidth>0?std::min(1.0f,command.maxWidth*density/item.width):1.0f;
    if(command.maxHeight>0)fit=std::min(fit,command.maxHeight*density/item.height);
    SDL_FRect destination{command.x,command.y,item.width/density*fit,item.height/density*fit};
    if(command.maxWidth>0) { SDL_Rect clip{(int)command.x,(int)command.y,(int)command.maxWidth,(int)std::ceil(destination.h)};SDL_SetRenderClipRect(renderer,&clip); }
    else SDL_SetRenderClipRect(renderer,clipped?&previous:nullptr);
    SDL_RenderTexture(renderer,item.texture,nullptr,&destination);
 }
 SDL_SetRenderClipRect(renderer,clipped?&previous:nullptr);commands.clear();
}
}
