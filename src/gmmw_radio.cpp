#include "zmenu_theme.h"
#include "zgui_main_menu_widgets.h"

bool GMMWRadio::finished_init = false;
ZSDL_Surface GMMWRadio::radio_left_img;
ZSDL_Surface GMMWRadio::radio_center_img;
ZSDL_Surface GMMWRadio::radio_right_img;
ZSDL_Surface GMMWRadio::radio_selector_img;

GMMWRadio::GMMWRadio() : ZGMMWidget()
{
	widget_type = MMRADIO_WIDGET;

	si = 0;
	SetMaxSelections(MMRADIO_MIN_SELECTIONS);
	h = MMRADIO_HEIGHT;
}

void GMMWRadio::Init()
{
	radio_left_img.LoadBaseImage("assets/other/main_menu_gui/radio/radio_left.png");
	radio_center_img.LoadBaseImage("assets/other/main_menu_gui/radio/radio_center.png");
	radio_right_img.LoadBaseImage("assets/other/main_menu_gui/radio/radio_right.png");
	radio_selector_img.LoadBaseImage("assets/other/main_menu_gui/radio/radio_selector.png");

	finished_init = true;
}

void GMMWRadio::SetMaxSelections(int selections_)
{
	selections = selections_;

	if(selections<MMRADIO_MIN_SELECTIONS) selections = MMRADIO_MIN_SELECTIONS;

	w = MMRADIO_LEFT_WIDTH + ((selections-2) * MMRADIO_CENTER_WIDTH) + MMRADIO_RIGHT_WIDTH;

	CheckSI();
}

void GMMWRadio::CheckSI()
{
	if(si<0 || si>=selections) 
	{
		printf("GMMWRadio::CheckSI:bad si:%d max_selections:%d\n", si, selections);
		si = 0;
	}
}

bool GMMWRadio::Click(int x_, int y_)
{
	the_flags.clear();

	if(x_ < x) return false;
	if(x_ > x + w) return false;
	if(y_ < y) return false;
	if(y_ > y + h) return false;

	x_ -= x;

	if(x_ < MMRADIO_LEFT_WIDTH)
		the_flags.mmradio_si_selected = 0;
	else if(x_ > w - MMRADIO_RIGHT_WIDTH)
		the_flags.mmradio_si_selected = selections - 1;
	else
		the_flags.mmradio_si_selected = 1 + ((x_ - MMRADIO_LEFT_WIDTH) / MMRADIO_CENTER_WIDTH);

	return true;
}

void GMMWRadio::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!finished_init) return;
	if(!active) return;

    tx+=x; ty+=y;
    SDL_Surface *surface=dest?dest:ZSDL_Surface::GetMainSoftwareSurface();
    ZMenuTheme::field(surface,tx,ty,w,h);
    for(int i=0;i<selections;++i) {
        int ix=i==0?7:MMRADIO_LEFT_WIDTH+(i-1)*MMRADIO_CENTER_WIDTH+4;
        ZMenuTheme::rect(surface,tx+ix+2,ty+2,1,5,83,98,109);
        if(i==si) ZMenuTheme::button(surface,tx+ix,ty+1,5,7,true,false,true);
    }
}
