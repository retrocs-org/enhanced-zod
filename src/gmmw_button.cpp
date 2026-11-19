#include "zgui_main_menu_widgets.h"
#include "zmenu_theme.h"

bool GMMWButton::finished_init = false;
ZSDL_Surface GMMWButton::generic_top_left_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_top_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_top_right_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_left_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_center_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_right_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_bottom_left_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_bottom_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::generic_bottom_right_img[MAX_MMBUTTON_STATES];
ZSDL_Surface GMMWButton::non_generic_img[MAX_MMBUTTON_TYPES][MAX_MMBUTTON_STATES];

GMMWButton::GMMWButton() : ZGMMWidget()
{
	widget_type = MMBUTTON_WIDGET;

	rerender_text = false;
	state = MMBUTTON_NORMAL;
	type = MMGENERIC_BUTTON;
	is_green = false;
	hovered = false;
}

void GMMWButton::Init()
{
	string filename;

	//normal button images
	for(int t=MMGENERIC_BUTTON+1;t<MAX_MMBUTTON_TYPES;t++)
		for(int i=0;i<MAX_MMBUTTON_STATES;i++)
		{
			string token;

			switch(i)
			{
			case MMBUTTON_NORMAL: token = "normal"; break;
			case MMBUTTON_PRESSED: token = "pressed"; break;
			case MMBUTTON_GREEN: token = "normal"; break;
			}

			non_generic_img[t][i].LoadBaseImage("assets/other/main_menu_gui/" + mmbutton_type_string[t] + "_button_" + token + ".png");
		}


	//generic button images
	for(int i=0;i<MAX_MMBUTTON_STATES;i++)
	{
		string token;

		switch(i)
		{
		case MMBUTTON_NORMAL: token = "normal"; break;
		case MMBUTTON_PRESSED: token = "pressed"; break;
		case MMBUTTON_GREEN: token = "green"; break;
		}

		generic_top_left_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_top_left.png");
		generic_top_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_top.png");
		generic_top_right_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_top_right.png");

		generic_left_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_left.png");
		generic_center_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_center.png");
		generic_right_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_right.png");

		generic_bottom_left_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_bottom_left.png");
		generic_bottom_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_bottom.png");
		generic_bottom_right_img[i].LoadBaseImage("assets/other/main_menu_gui/generic_button_" + token + "_bottom_right.png");
	}

	finished_init = true;
}

void GMMWButton::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!finished_init) return;
	if(!active) return;

	//shift to our rendering point
	tx+=x;
	ty+=y;

	if(type==MMGENERIC_BUTTON)
		RenderGeneric(the_map, dest, tx, ty);
	else
	{
        SDL_Surface *surface=dest?dest:ZSDL_Surface::GetMainSoftwareSurface();
        ZMenuTheme::button(surface,tx,ty,w,h,hovered,state==MMBUTTON_PRESSED,false);
        for(int i=3;i<w-3;++i) {
            ZMenuTheme::rect(surface,tx+i,ty+3+(i-3)*(h-7)/std::max(1,w-7),1,1,220,228,234);
            ZMenuTheme::rect(surface,tx+i,ty+h-4-(i-3)*(h-7)/std::max(1,w-7),1,1,220,228,234);
        }
	}

	//text
	{
		MakeTextImage();

		if(text_img.GetBaseSurface())
		{
			int added_shift=0;

			if(state==MMBUTTON_PRESSED) added_shift=1;

			//text_img.RenderSurface((tx + (w >> 1)) - (text_img.GetBaseSurface()->w >> 1), ty + (h-text_img.GetBaseSurface()->h)/2 + added_shift);
			if(!ZMenuTheme::queueText(text.c_str(),(tx+(w>>1))-(std::min(w-4,text_img.GetBaseSurface()->w)>>1),ty+(h-text_img.GetBaseSurface()->h)/2+added_shift,12,{218,226,232,255},w-4,h-2))
                text_img.BlitSurface(NULL,(tx+(w>>1))-(std::min(w-4,text_img.GetBaseSurface()->w)>>1),ty+(h-text_img.GetBaseSurface()->h)/2+added_shift);
		}
	}
}

void GMMWButton::RenderGeneric(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    ZMenuTheme::button(dest?dest:ZSDL_Surface::GetMainSoftwareSurface(),tx,ty,w,h,hovered,state==MMBUTTON_PRESSED,is_green);
}

void GMMWButton::MakeTextImage()
{
if(rendered_text_percent!=zod_text_percent){rendered_text_percent=zod_text_percent;rerender_text=true;}
	if(!rerender_text) return;

	if(text.length())
	{
		SDL_Surface *rendered=ZMenuTheme::text(text.c_str());
        if(!rendered) rendered=ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(text.c_str());
        text_img.LoadBaseImage(rendered);
	}
	else
		text_img.Unload();

	rerender_text = false;
}

void GMMWButton::DetermineDimensions()
{
	switch(type)
	{
	case MMCLOSE_BUTTON:
		w=12;
		h=12;
		break;
	}
}

bool GMMWButton::Click(int x_, int y_)
{
	if(!active) return false;
	if(!WithinDimensions(x_, y_)) return false;

	state = MMBUTTON_PRESSED;

	return true;
}

bool GMMWButton::UnClick(int x_, int y_)
{
	int previous_state = state;

	state = MMBUTTON_NORMAL;

	if(!active) return false;
	if(!WithinDimensions(x_, y_)) return false;

	//call an unclick event if we were previously pressed
	//and the unclick is over the button
	return (previous_state == MMBUTTON_PRESSED);
}

//remember whether the cursor is over us, so a mouse-wheel turn knows which
//button to adjust (#73 - wheel events arrive without coordinates). We return
//false so this doesn't count as a handled motion / change any other behaviour.
bool GMMWButton::Motion(int x_, int y_)
{
	hovered = (active && WithinDimensions(x_, y_));
	return false;
}
