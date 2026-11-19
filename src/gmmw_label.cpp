#include "zgui_main_menu_widgets.h"
#include "zmenu_theme.h"

GMMWLabel::GMMWLabel() : ZGMMWidget()
{
	widget_type = MMRADIO_WIDGET;

	rerender_text = false;
	justify = MMLABEL_NORMAL;
	h = MMLABEL_HEIGHT;
	font = YELLOW_MENU_FONT;
}

void GMMWLabel::SetText(string text_)
{
	if(text_ == rendered_text) return;

	text = text_; 
	rerender_text = true;
}

void GMMWLabel::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!active) return;

	//shift to our rendering point
	tx+=x;
	ty+=y;

	MakeTextImage();

	if(text_img.GetBaseSurface())
	{
		int added_shift=0;int textWidth=std::min(w,text_img.GetBaseSurface()->w);

		switch(justify)
		{
		case MMLABEL_CENTER: added_shift = (w >> 1) - (textWidth >> 1); break;
		case MMLABEL_RIGHT: added_shift = w - textWidth; break;
		}

		if(!ZMenuTheme::queueText(text.c_str(),tx+added_shift,ty,11,{218,226,232,255},w,h))
            text_img.BlitSurface(NULL,tx+added_shift,ty);
	}
}

void GMMWLabel::MakeTextImage()
{
if(rendered_text_percent!=zod_text_percent){rendered_text_percent=zod_text_percent;rerender_text=true;}
	if(!rerender_text) return;

	if(text.length()) {
        SDL_Surface *surface=ZMenuTheme::text(text.c_str(),11);
        if(!surface) surface=ZFontEngine::GetFont(font).Render(text.c_str());
        text_img.LoadBaseImage(surface);
    }
	else
		text_img.Unload();

	rendered_text = text;
	rerender_text = false;
}
