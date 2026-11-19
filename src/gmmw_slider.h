#ifndef GMMW_SLIDER_H
#define GMMW_SLIDER_H
#include "zgui_main_menu_widgets.h"
class GMMWSlider : public ZGMMWidget {
public:
    GMMWSlider(const std::string &title,int *value):title(title),value(value) {w=250;h=32;}
    void DoRender(ZMap&,SDL_Surface *dest,int tx,int ty) override;
    bool Click(int px,int py) override;
    bool UnClick(int px,int py) override;
    bool Motion(int px,int py) override;
private:
    std::string title;int *value;bool dragging=false;void Change(int px);
};
#endif
