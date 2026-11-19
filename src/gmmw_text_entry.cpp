#include "zgui_main_menu_widgets.h"
#include "zmenu_theme.h"

//#246: single-line editable text field (used for the player name in Options).
//Mirrors GMMWLabel's text rendering; edit logic mirrors the old ZGuiTextBox.

GMMWTextEntry::GMMWTextEntry() : ZGMMWidget()
{
	widget_type = MMTEXTENTRY_WIDGET;

	rerender_text = false;
	selected = false;
	changed = false;
	max_text = 16;
	h = GMMWTEXTENTRY_HEIGHT;
}

bool GMMWTextEntry::Click(int x_, int y_)
{
	if(!active) return false;

	bool hit = WithinDimensions(x_, y_);

	//clicking the field focuses it; a click anywhere else in the menu drops focus
	//(every widget's Click() is called, so we deselect when the hit isn't ours).
	SetSelected(hit);

	return hit;
}

bool GMMWTextEntry::KeyPress(int c)
{
	if(!active) return false;
	if(!selected) return false;   //only the focused field consumes typing

	if(c == 8 || c == 127)   //backspace / delete
	{
		if(text.length())
		{
			text.erase(text.length() - 1, 1);
			rerender_text = true;
			changed = true;
		}
	}
	else if(c == 13 || c == 9)   //enter / tab: commit + drop focus
	{
		SetSelected(false);
	}
	else
	{
		if((int)text.length() >= max_text) return true;
		bool address_char=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':';
        if(digits_only ? !(c>='0' && c<='9') : address_mode ? !address_char : !good_user_char(c)) return true;

		text += (char)c;
		rerender_text = true;
		changed = true;
	}

	return true;
}

void GMMWTextEntry::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!active) return;

	tx += x;
	ty += y;

	ZMenuTheme::field(dest?dest:ZSDL_Surface::GetMainSoftwareSurface(),tx-1,ty-1,w+2,h+2,selected);
	MakeTextImage();

	if(text_img.GetBaseSurface()) {
        string value=label+text+(selected?"|":"");
        if(!ZMenuTheme::queueText(value.c_str(),tx,ty,12,{218,226,232,255},w))
            text_img.BlitSurface(0,0,w,h,NULL,tx,ty);
    }
}

void GMMWTextEntry::MakeTextImage()
{
	if(!rerender_text) return;

	//show a trailing caret while focused (mirrors the old text box's '{' marker)
	string disp = label + text + (selected ? "{" : "");

	if(disp.length())
		{
        SDL_Surface *surface=ZMenuTheme::text(disp.c_str());
        if(!surface) surface=ZFontEngine::GetFont(YELLOW_MENU_FONT).Render(disp.c_str());
        text_img.LoadBaseImage(surface);
    }
	else
		text_img.Unload();

	rerender_text = false;
}
