#include "zmenu_theme.h"
#include "zgui_main_menu_widgets.h"

bool GMMWList::finished_init = false;
ZSDL_Surface GMMWList::list_top_left_img;
ZSDL_Surface GMMWList::list_top_img;
ZSDL_Surface GMMWList::list_top_right_img;
ZSDL_Surface GMMWList::list_left_img;
ZSDL_Surface GMMWList::list_right_img;
ZSDL_Surface GMMWList::list_bottom_left_img;
ZSDL_Surface GMMWList::list_bottom_img;
ZSDL_Surface GMMWList::list_bottom_right_img;
ZSDL_Surface GMMWList::list_entry_top_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_entry_left_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_entry_center_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_entry_right_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_entry_bottom_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_button_up_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_button_down_img[MAX_MMLIST_STATES];
ZSDL_Surface GMMWList::list_scroller_img;

GMMWList::GMMWList() : ZGMMWidget()
{
	widget_type = MMLIST_WIDGET;

	view_i = 0;
	SetVisibleEntries(MMLIST_MIN_ENTRIES);
	up_button_state = MMLIST_NORMAL;
	down_button_state = MMLIST_NORMAL;
	dragging_scroller = false;
	button_down_time = 0;
}

void GMMWList::Init()
{
	list_top_left_img.LoadBaseImage("assets/other/main_menu_gui/list/list_top_left.png");
	list_top_img.LoadBaseImage("assets/other/main_menu_gui/list/list_top.png");
	list_top_right_img.LoadBaseImage("assets/other/main_menu_gui/list/list_top_right.png");
	list_left_img.LoadBaseImage("assets/other/main_menu_gui/list/list_left.png");
	list_right_img.LoadBaseImage("assets/other/main_menu_gui/list/list_right.png");
	list_bottom_left_img.LoadBaseImage("assets/other/main_menu_gui/list/list_bottom_left.png");
	list_bottom_img.LoadBaseImage("assets/other/main_menu_gui/list/list_bottom.png");
	list_bottom_right_img.LoadBaseImage("assets/other/main_menu_gui/list/list_bottom_right.png");

	list_scroller_img.LoadBaseImage("assets/other/main_menu_gui/list/list_scroller.png");

	for(int i=0;i<MAX_MMLIST_STATES;i++)
	{
		string token;

		switch(i)
		{
		case MMLIST_NORMAL: token = "normal"; break;
		case MMLIST_PRESSED: token = "pressed"; break;
		}

		list_entry_top_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_entry_top_" + token + ".png");
		list_entry_left_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_entry_left_" + token + ".png");
		list_entry_center_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_entry_center_" + token + ".png");
		list_entry_right_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_entry_right_" + token + ".png");
		list_entry_bottom_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_entry_bottom_" + token + ".png");

		list_button_up_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_button_up_" + token + ".png");
		list_button_down_img[i].LoadBaseImage("assets/other/main_menu_gui/list/list_button_down_" + token + ".png");
	}

	finished_init = true;
}

void GMMWList::Process()
{
	const int clicks_per_second = 30;
	int clicks_to_go;
	double time_dif;
	double the_time;

	if(up_button_state==MMLIST_PRESSED)
	{
		the_time = current_time();
		time_dif = the_time - button_down_time;
		if(time_dif < 0) return;
		clicks_to_go = time_dif * clicks_per_second;

		if(clicks_to_go)
		{
			view_i -= clicks_to_go;
			if(view_i < 0) view_i = 0;
			button_down_time = the_time;
		}
	}
	
	if(down_button_state==MMLIST_PRESSED)
	{
		the_time = current_time();
		time_dif = the_time - button_down_time;
		if(time_dif < 0) return;
		clicks_to_go = time_dif * clicks_per_second;

		if(clicks_to_go)
		{
			view_i += clicks_to_go;
			CheckViewI();
			button_down_time = the_time;
		}
	}
}

void GMMWList::SetVisibleEntries(int visible_entries_)
{
	visible_entries = visible_entries_;

	if(visible_entries < MMLIST_MIN_ENTRIES) visible_entries = MMLIST_MIN_ENTRIES;

	SetHeight();
}

void GMMWList::SetHeight()
{
	h = 3 + (visible_entries * MMLIST_ENTRY_HEIGHT) + 2;
}

int GMMWList::GetFirstSelected()
{
	for(int i=0;i<entry_list.size();i++)
		if(entry_list[i].state == MMLIST_PRESSED)
			return i;

	return -1;
}

void GMMWList::UnSelectAll(int except_entry)
{
	for(int i=0;i<entry_list.size();i++)
		if(i != except_entry)
			entry_list[i].state = MMLIST_NORMAL;
}

void GMMWList::CheckViewI()
{
	int available_slots;

	available_slots = entry_list.size() - visible_entries;

	if(view_i > available_slots) view_i = available_slots;
	if(view_i < 0) view_i = 0;
}

bool GMMWList::Click(int x_, int y_)
{
	int entry_clicked;

	the_flags.clear();

	x_-=x;
	y_-=y;

	//buttons?
	if(WithinUpButton(x_, y_)) 
	{ 
		up_button_state=MMLIST_PRESSED; 
		button_down_time=current_time()+0.2;
		return true; 
	}
	if(WithinDownButton(x_, y_))
	{
		down_button_state=MMLIST_PRESSED;
		button_down_time=current_time()+0.2;
		return true;
	}

	//scrollbar track (the strip between the up/down buttons): jump the view to
	//where you click and keep dragging to scroll - so you don't have to click
	//the arrow once per row to reach a distant entry (#78)
	if(WithinScrollTrack(x_, y_))
	{
		dragging_scroller = true;
		SetViewFromTrackY(y_);
		return true;
	}

	//entry?
	entry_clicked = WithinEntry(x_, y_);
	if(entry_clicked != -1 && entry_clicked < entry_list.size())
	{
		if(entry_list[entry_clicked].state == MMLIST_NORMAL)
			entry_list[entry_clicked].state = MMLIST_PRESSED;
		else
			entry_list[entry_clicked].state = MMLIST_NORMAL;

		//flag stuff
		the_flags.mmlist_entry_selected = entry_clicked;

		return true;
	}

	return false;
}

bool GMMWList::UnClick(int x_, int y_)
{
	int prev_up_button_state;
	int prev_down_button_state;

	x_-=x;
	y_-=y;

	prev_up_button_state = up_button_state;
	prev_down_button_state = down_button_state;

	up_button_state = MMLIST_NORMAL;
	down_button_state = MMLIST_NORMAL;
	dragging_scroller = false;

	if(prev_up_button_state==MMLIST_PRESSED && WithinUpButton(x_, y_)) { return MoveUp(); }
	if(prev_down_button_state==MMLIST_PRESSED && WithinDownButton(x_, y_)) { return MoveDown(); }

	return false;
}

//while the left button is held after grabbing the scrollbar track, follow the
//cursor (#78). Returns true so the menu treats the motion as consumed.
bool GMMWList::Motion(int x_, int y_)
{
	if(!dragging_scroller) return false;

	y_-=y;
	SetViewFromTrackY(y_);

	return true;
}

//the scrollbar track is the right-edge column between the up and down buttons.
bool GMMWList::WithinScrollTrack(int x_, int y_)
{
	const int button_h = 8;   //matches WithinUp/DownButton
	int track_left, track_top, track_bottom;

	track_left = w - MMLIST_UP_BUTTON_FROM_RIGHT;
	track_top = MMLIST_UP_BUTTON_FROM_TOP + button_h;
	track_bottom = h - MMLIST_DOWN_BUTTON_FROM_BOTTOM;

	if(x_ < track_left) return false;
	if(x_ > w) return false;
	if(y_ < track_top) return false;
	if(y_ > track_bottom) return false;

	return true;
}

//map a y within the scrollbar track to a view position. Clamps at both ends.
void GMMWList::SetViewFromTrackY(int y_)
{
	const int button_h = 8;
	int track_top, track_bottom, available;
	double pct;

	available = (int)entry_list.size() - visible_entries;
	if(available <= 0) { view_i = 0; return; }

	track_top = MMLIST_UP_BUTTON_FROM_TOP + button_h;
	track_bottom = h - MMLIST_DOWN_BUTTON_FROM_BOTTOM;
	if(track_bottom <= track_top) { view_i = 0; return; }

	pct = (double)(y_ - track_top) / (track_bottom - track_top);
	if(pct < 0) pct = 0;
	if(pct > 1) pct = 1;

	view_i = (int)(pct * available + 0.5);
	if(view_i < 0) view_i = 0;
	if(view_i > available) view_i = available;
}

int GMMWList::WithinEntry(int x_, int y_)
{
	int entry_found;

    const int left=list_left_img.GetBaseSurface()?list_left_img.GetBaseSurface()->w:2;
    const int top=list_top_img.GetBaseSurface()?list_top_img.GetBaseSurface()->h:2;
    const int right=list_right_img.GetBaseSurface()?list_right_img.GetBaseSurface()->w:16;
    if(x_<left||x_>w-left-right||y_<top||y_>top+visible_entries*MMLIST_ENTRY_HEIGHT)return -1;
    entry_found=(y_-top)/MMLIST_ENTRY_HEIGHT;

	if(entry_found < 0) return -1;
	if(entry_found >= visible_entries) return -1;

	entry_found += view_i;

	if(entry_found < 0) return -1;
	if(entry_found >= entry_list.size()) return -1;

	//#77: a locked entry can't be picked (the click just does nothing)
	if(entry_list[entry_found].disabled) return -1;

	return entry_found;
}

bool GMMWList::WithinUpButton(int x_, int y_)
{
	const int iw = 11;
	const int ih = 8;
	int ix, iy;

	ix = (w - MMLIST_UP_BUTTON_FROM_RIGHT);
	iy = MMLIST_UP_BUTTON_FROM_TOP;
	
	if(x_ < ix) return false;
	if(y_ < iy) return false;
	if(x_ > ix + iw) return false;
	if(y_ > iy + ih) return false;

	return true;
}

bool GMMWList::WithinDownButton(int x_, int y_)
{
	const int iw = 11;
	const int ih = 8;
	int ix, iy;

	ix = (w - MMLIST_DOWN_BUTTON_FROM_RIGHT);
	iy = h - MMLIST_DOWN_BUTTON_FROM_BOTTOM;
	
	if(x_ < ix) return false;
	if(y_ < iy) return false;
	if(x_ > ix + iw) return false;
	if(y_ > iy + ih) return false;

	return true;
}

bool GMMWList::MoveUp()
{
	view_i--;

	if(view_i<0)
	{
		view_i = 0;
		return false;
	}

	return true;
}

bool GMMWList::MoveDown()
{
	int available_slots;

	view_i++;

	available_slots =  entry_list.size() - visible_entries;

	if(view_i > available_slots)
	{
		view_i = available_slots;

		if(view_i<0) view_i = 0;

		return false;
	}

	return true;
}

void GMMWList::DoRender(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	if(!finished_init) return;
	if(!active) return;

	tx += x;
	ty += y;

	RenderBackround(the_map, dest, tx, ty);
	RenderEntries(the_map, dest, tx, ty);
	RenderControls(the_map, dest, tx, ty);
}

void GMMWList::RenderBackround(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    ZMenuTheme::field(dest?dest:ZSDL_Surface::GetMainSoftwareSurface(),tx,ty,w,h);
}

void GMMWList::RenderEntries(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
	int ix, iy, iw;
	int i, j;

    const int left=list_left_img.GetBaseSurface()?list_left_img.GetBaseSurface()->w:2;
    const int top=list_top_img.GetBaseSurface()?list_top_img.GetBaseSurface()->h:2;
    const int right=list_right_img.GetBaseSurface()?list_right_img.GetBaseSurface()->w:16;
    ix=tx+left;iy=ty+top;iw=w-left-right;

	for(i=0,j=view_i;i<visible_entries;i++,j++)
	{
		if(j < entry_list.size())
		{
            if(ZMenuTheme::queueText(entry_list[j].text.c_str(),ix+4,iy+i*MMLIST_ENTRY_HEIGHT-1,11,{218,226,232,(Uint8)(entry_list[j].disabled?100:255)},iw-8,MMLIST_ENTRY_HEIGHT)) {
                RenderEntry(the_map,dest,ix,iy+i*MMLIST_ENTRY_HEIGHT,iw,nullptr,entry_list[j].state);
                continue;
            }
			ZSDL_Surface text_img;

			SDL_Surface *surface=ZMenuTheme::text(entry_list[j].text.c_str(),11);
            if(!surface) surface=ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(entry_list[j].text.c_str());
            text_img.LoadBaseImage(surface);

			//#156: a disabled entry (e.g. a locked campaign map you haven't reached)
			//renders dimmed/greyed, so you can tell at a glance which maps you've
			//unlocked vs which are still locked. The font is white-only, so we fade
			//it via alpha - over the dark menu it reads as grey.
			text_img.SetAlpha(entry_list[j].disabled ? 100 : 255);

			bool queued=ZMenuTheme::queueText(entry_list[j].text.c_str(),ix+4,iy+i*MMLIST_ENTRY_HEIGHT+(MMLIST_ENTRY_HEIGHT-(text_img.GetBaseSurface()?text_img.GetBaseSurface()->h:0))/2,11,{218,226,232,(Uint8)(entry_list[j].disabled?100:255)},iw-8,MMLIST_ENTRY_HEIGHT);
            RenderEntry(the_map,dest,ix,iy+i*MMLIST_ENTRY_HEIGHT,iw,queued?nullptr:&text_img,entry_list[j].state);
		}
		else
			RenderEntry(the_map, dest, ix, iy + (i*MMLIST_ENTRY_HEIGHT), iw, NULL, MMLIST_NORMAL);
	}
}

void GMMWList::RenderEntry(ZMap &the_map, SDL_Surface *dest, int tx, int ty, int tw, ZSDL_Surface *text, int state)
{
    ZMenuTheme::field(dest?dest:ZSDL_Surface::GetMainSoftwareSurface(),tx,ty,tw,MMLIST_ENTRY_HEIGHT,state!=MMLIST_NORMAL);
    if(text && text->GetBaseSurface())
        text->BlitSurface(0,0,tw-8,MMLIST_ENTRY_HEIGHT,NULL,tx+4,ty+(MMLIST_ENTRY_HEIGHT-text->GetBaseSurface()->h)/2);
}

void GMMWList::RenderControls(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    SDL_Surface *surface=dest?dest:ZSDL_Surface::GetMainSoftwareSurface();
    int left=tx+w-MMLIST_UP_BUTTON_FROM_RIGHT;
    auto arrow=[&](int y,bool down,bool pressed) {
        ZMenuTheme::button(surface,left,y,11,8,false,pressed,false);
        for(int i=0;i<3;++i) ZMenuTheme::rect(surface,left+3+i,y+(down?2+i:4-i),5-2*i,1,218,226,232);
    };
    arrow(ty+MMLIST_UP_BUTTON_FROM_TOP,false,up_button_state==MMLIST_PRESSED);
    arrow(ty+h-MMLIST_DOWN_BUTTON_FROM_BOTTOM,true,down_button_state==MMLIST_PRESSED);
    int top=ty+MMLIST_UP_BUTTON_FROM_TOP+8;
    int track=h-MMLIST_DOWN_BUTTON_FROM_BOTTOM-MMLIST_UP_BUTTON_FROM_TOP-8;
    ZMenuTheme::rect(surface,left+4,top,3,track,43,53,62);
    int available=std::max(0,(int)entry_list.size()-visible_entries);
    if(available>0) {
        int thumb=std::max(5,track*visible_entries/std::max(1,(int)entry_list.size()));
        int y=top+(track-thumb)*view_i/available;
        ZMenuTheme::rect(surface,left+2,y,7,thumb,244,164,65);
    }
}

bool sort_mmlist_entry_func(const mmlist_entry &a, const mmlist_entry &b)
{
    return a.sort_number < b.sort_number;
}
