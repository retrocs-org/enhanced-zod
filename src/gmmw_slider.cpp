#include "gmmw_slider.h"
#include "zmenu_theme.h"
#include "zprefs.h"
#include "zaudio.h"
void GMMWSlider::Change(int px) {
    int next=std::max(0,std::min(100,(px-x)*100/std::max(1,w-1)));if(next==*value)return;*value=next;ZAudio_ApplyVolumes();ZPrefs_Save();
}
bool GMMWSlider::Click(int px,int py) {if(!active||(!WithinDimensions(px,py)||py<y+16))return false;dragging=true;Change(px);return true;}
bool GMMWSlider::Motion(int px,int py) {if(!dragging||!active)return false;Change(px);return true;}
bool GMMWSlider::UnClick(int px,int py) {if(!dragging)return false;Change(px);dragging=false;return true;}
void GMMWSlider::DoRender(ZMap&,SDL_Surface *dest,int tx,int ty) {
    if(!active)return;tx+=x;ty+=y;auto *surface=dest?dest:ZSDL_Surface::GetMainSoftwareSurface();
    std::string label=title+": "+std::to_string(*value)+"%";
    if(!ZMenuTheme::queueText(label.c_str(),tx,ty,11,{218,226,232,255},w)) {
        SDL_Surface *text=ZMenuTheme::text(label.c_str(),11);if(text){SDL_Rect dst{tx,ty,text->w,text->h};SDL_BlitSurface(text,nullptr,surface,&dst);SDL_DestroySurface(text);}
    }
    ZMenuTheme::field(surface,tx,ty+20,w,6);ZMenuTheme::rect(surface,tx,ty+22,w*(*value)/100,2,244,164,65);
    ZMenuTheme::button(surface,tx+(w-6)*(*value)/100,ty+18,6,10,true,dragging,true);
}
