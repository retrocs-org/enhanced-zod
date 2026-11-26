#include "zgui_main_menu_base.h"
#include "zmenu_theme.h"
#include <atomic>

bool ZGuiMainMenuBase::finished_init = false;
ZSDL_Surface ZGuiMainMenuBase::menu_top_left_img;
ZSDL_Surface ZGuiMainMenuBase::menu_top_right_img;
ZSDL_Surface ZGuiMainMenuBase::menu_top_img;
ZSDL_Surface ZGuiMainMenuBase::menu_left_img;
ZSDL_Surface ZGuiMainMenuBase::menu_right_img;
ZSDL_Surface ZGuiMainMenuBase::menu_bottom_img;
ZSDL_Surface ZGuiMainMenuBase::menu_center_img;
ZSDL_Surface ZGuiMainMenuBase::menu_warning_img;

ZGuiMainMenuBase::ZGuiMainMenuBase()
{
    static std::atomic<std::uint64_t> next_id{1};
    instance_id=next_id.fetch_add(1);
	killme = false;
	menu_type = -1;
	x=y=0;
	w=h=0;
	render_scale = 1.0;   //#196
	click_grabbed = false;
	player_info = NULL;
	selectable_map_list = NULL;
	player_team = NULL;
	sound_setting = NULL;
	ztime = NULL;

	close_button.SetType(MMCLOSE_BUTTON);
}

void ZGuiMainMenuBase::Init()
{
	menu_top_left_img.LoadBaseImage("assets/other/main_menu_gui/menu_top_left.png");
	menu_top_right_img.LoadBaseImage("assets/other/main_menu_gui/menu_top_right.png");
	menu_top_img.LoadBaseImage("assets/other/main_menu_gui/menu_top.png");
	menu_left_img.LoadBaseImage("assets/other/main_menu_gui/menu_left.png");
	menu_right_img.LoadBaseImage("assets/other/main_menu_gui/menu_right.png");
	menu_bottom_img.LoadBaseImage("assets/other/main_menu_gui/menu_bottom.png");
	menu_center_img.LoadBaseImage("assets/other/main_menu_gui/menu_center.png");
	menu_warning_img.LoadBaseImage("assets/other/main_menu_gui/menu_warning.png");

	finished_init = true;
}

void ZGuiMainMenuBase::UpdateDimensions()
{
	close_button.SetCoords(w-16, 4);
}

void ZGuiMainMenuBase::SetCenterCoords(int cx_, int cy_)
{
	x = cx_ - (w >> 1);
	y = cy_ - (h >> 1);
}

void ZGuiMainMenuBase::Process()
{
	ProcessWidgets();
}

void ZGuiMainMenuBase::ProcessWidgets()
{
	close_button.Process();

	//widgets
	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		(*i)->Process();
}

bool ZGuiMainMenuBase::WithinDimensions(int x_, int y_)
{
	ScaleInput(x_, y_);
	if(x_<x) return false;
	if(y_<y) return false;
	if(x_>x+w) return false;
	if(y_>y+h) return false;

	return true;
}

bool ZGuiMainMenuBase::IsOverHUD(int hud_left, int hud_top)
{
	if(x + w >= hud_left) return true;
	if(y + h >= hud_top) return true;

	return false;
}

void ZGuiMainMenuBase::Move(double px, double py)
{
	int cx, cy;

	cx = x + (w>>1);
	cy = y + (h>>1);

	cx *= px;
	cy *= py;

	x = cx - (w>>1);
	y = cy - (h>>1);
}

bool ZGuiMainMenuBase::Motion(int x_, int y_)
{
	ScaleInput(x_, y_);
	bool action_taken = false;
	int tx, ty;

	gmm_flags.clear();

	//are we dragging the main window around?
	if(click_grabbed)
	{
		x = x_ - grab_x;
		y = y_ - grab_y;

		return true;
	}

	tx = x_ - x;
	ty = y_ - y;

	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->Motion(tx, ty))
		{
			HandleWidgetEvent(GMM_MOTION_EVENT, *i);
			action_taken = true;
		}

	return action_taken;
}

bool ZGuiMainMenuBase::KeyPress(int c)
{
	bool action_taken = false;

	gmm_flags.clear();
    auto focusable=[](ZGMMWidget *widget){return widget->IsActive()&&widget->GetWidgetType()!=MMLABEL_WIDGET&&widget->GetWidgetType()!=MMTEAM_COLOR_WIDGET;};
    if(c==9){
        int direction=(SDL_GetModState()&SDL_KMOD_SHIFT)?-1:1;
        if(keyboard_focus<0&&direction<0)keyboard_focus=0;
        for(auto *widget:widget_list)if(auto *entry=dynamic_cast<GMMWTextEntry*>(widget))entry->SetSelected(false);
        for(size_t step=0;step<widget_list.size();++step){keyboard_focus=(keyboard_focus+direction+(int)widget_list.size())%widget_list.size();if(focusable(widget_list[keyboard_focus])){if(auto *entry=dynamic_cast<GMMWTextEntry*>(widget_list[keyboard_focus]))entry->SetSelected(true);break;}}
        return true;
    }
    if(keyboard_focus>=0&&keyboard_focus<(int)widget_list.size()&&focusable(widget_list[keyboard_focus])){
        auto *widget=widget_list[keyboard_focus];
        if((c==13||c==32)&&widget->GetWidgetType()==MMBUTTON_WIDGET){widget->Click(widget->GetX()+1,widget->GetY()+1);HandleWidgetEvent(GMM_CLICK_EVENT,widget);widget->UnClick(widget->GetX()+1,widget->GetY()+1);HandleWidgetEvent(GMM_UNCLICK_EVENT,widget);return true;}
        if(auto *radio=dynamic_cast<GMMWRadio*>(widget))if(c==SDLK_LEFT||c==SDLK_RIGHT){int selected=(radio->GetSelected()+(c==SDLK_LEFT?-1:1)+radio->GetSelections())%radio->GetSelections();radio->GetGMMWFlags().mmradio_si_selected=selected;radio->SetSelected(selected);HandleWidgetEvent(GMM_CLICK_EVENT,radio);return true;}
        if(auto *list=dynamic_cast<GMMWList*>(widget))if(c==SDLK_UP||c==SDLK_DOWN){auto &rows=list->GetEntryList();int selected=list->GetFirstSelected();for(size_t step=0;step<rows.size();++step){selected=(selected+(c==SDLK_UP?-1:1)+(int)rows.size())%rows.size();if(!rows[selected].disabled){list->UnSelectAll();rows[selected].state=MMLIST_PRESSED;list->GetGMMWFlags().mmlist_entry_selected=selected;HandleWidgetEvent(GMM_CLICK_EVENT,list);list->EnsureVisible(selected);break;}}return true;}
        if(widget->GetWidgetType()!=MMTEXTENTRY_WIDGET&&widget->KeyPress(c)){HandleWidgetEvent(GMM_KEYPRESS_EVENT,widget);return true;}
    }

	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->KeyPress(c))
		{
			HandleWidgetEvent(GMM_KEYPRESS_EVENT, *i);
			action_taken = true;
		}

	return action_taken;
}

bool ZGuiMainMenuBase::WheelUpButton()
{
	bool action_taken = false;

	gmm_flags.clear();

	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->WheelUpButton())
		{
			HandleWidgetEvent(GMM_WHEELUP_EVENT, *i);
			action_taken = true;
		}

	return action_taken;
}

bool ZGuiMainMenuBase::WheelDownButton()
{
	bool action_taken = false;

	gmm_flags.clear();

	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->WheelDownButton())
		{
			HandleWidgetEvent(GMM_WHEELDOWN_EVENT, *i);
			action_taken = true;
		}

	return action_taken;
}

bool ZGuiMainMenuBase::Click(int x_, int y_)
{
    keyboard_focus=-1;
	ScaleInput(x_, y_);
	bool action_taken = false;
	int tx, ty;
	bool within_dimensions;

	gmm_flags.clear();

	tx = x_ - x;
	ty = y_ - y;

	//close button
	if(close_button.Click(tx, ty)) action_taken = true;

	//normal widgets
	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->Click(tx, ty))
		{
			HandleWidgetEvent(GMM_CLICK_EVENT, *i);
			action_taken = true;
		}

	within_dimensions = WithinDimensions(x_, y_);

	//grab the window for dragging?
	if(!action_taken && within_dimensions)
	{
		click_grabbed = true;
		grab_x = tx;
		grab_y = ty;
	}

	return action_taken || within_dimensions;
}

bool ZGuiMainMenuBase::UnClick(int x_, int y_)
{
	ScaleInput(x_, y_);
	bool action_taken = false;
	int tx, ty;

	gmm_flags.clear();

	//can't be true at this point
	click_grabbed = false;

	tx = x_ - x;
	ty = y_ - y;

	//close button
	if(close_button.UnClick(tx, ty))
	{
		killme = true;
		return true;
	}

	//normal widgets
	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
		if((*i)->UnClick(tx, ty))
		{
			HandleWidgetEvent(GMM_UNCLICK_EVENT, *i);
			action_taken = true;
		}

	return action_taken || WithinDimensions(x_, y_);
}

void ZGuiMainMenuBase::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
	if(!event_widget) return;
	if(event_type<0) return;
	if(event_type>=MAC_GMM_EVENTS) return;

	printf("ZGuiMainMenuBase::HandleWidgetEvent::%d:%s widget_type:%d ref_id:%d\n", event_type, gmm_event_type_string[event_type].c_str(), event_widget->GetWidgetType(), event_widget->GetRefID());
}

void ZGuiMainMenuBase::SetRenderScale(double s)
{
	//#196: request a scale, but never let a menu grow past the screen - cap it so
	//even the big grids (map-select etc.) fit, scaling as much as they can.
	if(s < 1.0) s = 1.0;

	int sw, sh;
	ZSDL_Surface::GetScreenDimensions(sw, sh);
	if(w > 0 && sw > 0) { double fx = (sw * 0.82) / w; if(s > fx) s = fx; }
	if(h > 0 && sh > 0) { double fy = (sh * 0.82) / h; if(s > fy) s = fy; }
	if(s < 1.0) s = 1.0;

	render_scale = s;
}

void ZGuiMainMenuBase::RenderContents(ZMap &the_map, SDL_Surface *dest)
{
	RenderBase(the_map, dest, x, y);

	//title
	{
		MakeTitle();

		//title_img.RenderSurface(x + 8, y + 6);
		if(!ZMenuTheme::queueText(title.c_str(),x+8,y+6,12,{244,164,65,255},w-28))
            title_img.BlitSurface(NULL,x+8,y+6);
	}

	//close button
	close_button.DoRender(the_map, dest, x, y);

	//widgets
	RenderWidgets(the_map, dest);
}

void ZGuiMainMenuBase::DoRender(ZMap &the_map, SDL_Surface *dest)
{
	if(!finished_init) return;
    ZMenuTheme::textTransform((float)render_scale,-x*(render_scale-1)-(int)(w*(render_scale-1)/2),-y*(render_scale-1)-(int)(h*(render_scale-1)/2));

	if(render_scale <= 1.0)
	{
		RenderContents(the_map, dest);
		return;
	}

	//#196 spike: render the menu to a scratch buffer at native size, then blit that
	//render_scale x. The widgets blit to the global software 'screen' (dst==NULL),
	//so swap that to the buffer while rendering, then scale-blit the menu's region.
	//Click()/etc divide the offset by render_scale so hit-testing still lines up.
	SDL_Surface *real = ZSDL_Surface::GetMainSoftwareSurface();
	if(!real) { RenderContents(the_map, dest); return; }

	static SDL_Surface *buf = NULL;
	if(!buf || buf->w != real->w || buf->h != real->h)
	{
		if(buf) SDL_DestroySurface(buf);
		buf = SDL_CreateSurface(real->w, real->h, SDL_PIXELFORMAT_ARGB8888);
		if(buf) SDL_SetSurfaceBlendMode(buf, SDL_BLENDMODE_BLEND);
	}
	if(!buf) { RenderContents(the_map, dest); return; }

	SDL_FillSurfaceRect(buf, NULL, 0);   //clear to transparent

	ZSDL_Surface::SetMainSoftwareSurface(buf);
	RenderContents(the_map, buf);
	ZSDL_Surface::SetMainSoftwareSurface(real);

	//pivot the scale around the menu's centre so it stays where the native menu
	//was (top-left anchoring pushed it down-right). ScaleInput uses the same dx/dy.
	int dx = (int)(w * (render_scale - 1) / 2);
	int dy = (int)(h * (render_scale - 1) / 2);
	SDL_Rect src = { x, y, w, h };
	SDL_Rect dst = { x - dx, y - dy, (int)(w * render_scale), (int)(h * render_scale) };
	SDL_BlitSurfaceScaled(buf, &src, real, &dst, SDL_SCALEMODE_NEAREST);
}

void ZGuiMainMenuBase::RenderWidgets(ZMap &the_map, SDL_Surface *dest)
{
	//widgets
	for(vector<ZGMMWidget*>::iterator i=widget_list.begin(); i!=widget_list.end(); ++i)
        {(*i)->DoRender(the_map, dest, x, y);
        if(keyboard_focus==(int)(i-widget_list.begin())&&(*i)->IsActive()){
            SDL_Surface *surface=dest?dest:ZSDL_Surface::GetMainSoftwareSurface();auto *widget=*i;
            if(surface){Uint32 color=SDL_MapSurfaceRGB(surface,255,207,105);int px=x+widget->GetX(),py=y+widget->GetY(),ww=widget->GetWidth(),hh=widget->GetHeight();SDL_Rect edges[]={{px,py,ww,1},{px,py+hh-1,ww,1},{px,py,1,hh},{px+ww-1,py,1,hh}};for(auto &edge:edges)SDL_FillSurfaceRect(surface,&edge,color);}
        }}
}

void ZGuiMainMenuBase::RenderBase(ZMap &the_map, SDL_Surface *dest, int tx, int ty)
{
    ZMenuTheme::panel(dest?dest:ZSDL_Surface::GetMainSoftwareSurface(),tx,ty,w,h);
}

void ZGuiMainMenuBase::MakeTitle()
{
	if(!title.length()) return;
	if(title_img.GetBaseSurface()) return;

	SDL_Surface *rendered=ZMenuTheme::text(title.c_str(),12,{244,164,65,255});
    if(!rendered) rendered=ZFontEngine::GetFont(YELLOW_MENU_FONT).Render(title.c_str());
    title_img.LoadBaseImage(rendered);
}
