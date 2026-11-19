#include "gmm_warning.h"

GMMWarning::GMMWarning(gmm_warning_flag warning_flags_) : ZGuiMainMenuBase()
{
	warning_flags = warning_flags_;

	menu_type = GMM_WARNING;
	title = warning_flags.quit_game ? "Confirm quit" : "Confirm action";
	w = 200;
	h = 108;

	close_button.SetActive(false);

	SetupLayout1();
}

void GMMWarning::SetupLayout1()
{
	int label_y;

	cancel_button.SetType(MMGENERIC_BUTTON);
	cancel_button.SetText("Cancel");
	cancel_button.SetCoords(12, 76);
	cancel_button.SetDimensions(76, 22);
	AddWidget(&cancel_button);

	ok_button.SetType(MMGENERIC_BUTTON);
	ok_button.SetText("OK");
	ok_button.SetCoords(112, 76);
	ok_button.SetDimensions(76, 22);
	AddWidget(&ok_button);

	label_y = 30;
	text1_label.SetFont(SMALL_WHITE_FONT);
	text1_label.SetText(warning_flags.text1);
	text1_label.SetCoords(12, label_y);
	text1_label.SetDimensions(w-24, 16);
	text1_label.SetJustification(MMLABEL_CENTER);
	AddWidget(&text1_label);
	label_y += 18;

	text2_label.SetFont(SMALL_WHITE_FONT);
	text2_label.SetText(warning_flags.text2);
	text2_label.SetCoords(12, label_y);
	text2_label.SetDimensions(w-24, 16);
	text2_label.SetJustification(MMLABEL_CENTER);
	AddWidget(&text2_label);

	//needed if w is ever changed / set
	UpdateDimensions();
}

void GMMWarning::RenderContents(ZMap &the_map, SDL_Surface *dest)
{
cancel_button.SetGreen(!confirmFocus);ok_button.SetGreen(confirmFocus);
	// Use the shared scalable panel and typography instead of the fixed-size bitmap.
	ZGuiMainMenuBase::RenderContents(the_map, dest);
}

void GMMWarning::HandleWidgetEvent(int event_type, ZGMMWidget *event_widget)
{
	int w_ref_id;

	if(!event_widget) return;

	w_ref_id = event_widget->GetRefID();

	switch(event_type)
	{
	case GMM_UNCLICK_EVENT:
		if(w_ref_id == ok_button.GetRefID())
		{
			killme = true;
			gmm_flags.quit_game = warning_flags.quit_game;
			gmm_flags.reset_map = warning_flags.reset_map;
		}
		else if(w_ref_id == cancel_button.GetRefID())
		{
			killme = true;
		}
		break;
	}
}


bool GMMWarning::KeyPress(int key){if(key==SDLK_ESCAPE){killme=true;return true;}if(key==SDLK_TAB||key==SDLK_LEFT||key==SDLK_RIGHT){confirmFocus=!confirmFocus;return true;}if(key==SDLK_RETURN){HandleWidgetEvent(GMM_UNCLICK_EVENT,confirmFocus?&ok_button:&cancel_button);return true;}return false;}
