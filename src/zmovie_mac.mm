#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/QuartzCore.h>

#include <SDL3/SDL.h>

#include "zmovie.h"
#include "zvideo.h"

// Plays a video over the whole game window using AVFoundation: a black NSView
// with an AVPlayerLayer is laid over the window's content view for the length of
// the clip. Any key, click, tap or controller button skips it.
int ZMovie_PlayNative(const std::string &path)
{
	SDL_Window *win = ZVideo_GetWindow();
	if(!win) return ZMOVIE_MISSING;

	NSWindow *nswin = (__bridge NSWindow *)SDL_GetPointerProperty(
		SDL_GetWindowProperties(win), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	if(!nswin) return ZMOVIE_MISSING;

	int result = ZMOVIE_FINISHED;

	@autoreleasepool
	{
		NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
		if(![[NSFileManager defaultManager] fileExistsAtPath:url.path]) return ZMOVIE_MISSING;

		AVPlayerItem *item = [AVPlayerItem playerItemWithURL:url];
		AVPlayer *player = [AVPlayer playerWithPlayerItem:item];

		NSView *content = nswin.contentView;
		NSView *overlay = [[NSView alloc] initWithFrame:content.bounds];
		overlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		overlay.wantsLayer = YES;
		overlay.layer.backgroundColor = [NSColor blackColor].CGColor;

		AVPlayerLayer *layer = [AVPlayerLayer playerLayerWithPlayer:player];
		layer.frame = overlay.bounds;
		layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
		layer.videoGravity = AVLayerVideoGravityResizeAspect;
		[overlay.layer addSublayer:layer];
		[content addSubview:overlay positioned:NSWindowAbove relativeTo:nil];

		__block bool ended = false;
		id observer = [[NSNotificationCenter defaultCenter]
			addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
			object:item queue:nil usingBlock:^(NSNotification *n) { (void)n; ended = true; }];

		[player play];

		Uint64 start = SDL_GetTicks();
		bool done = false;
		while(!done)
		{
			SDL_Event e;
			while(SDL_PollEvent(&e))
			{
				switch(e.type)
				{
				case SDL_EVENT_KEY_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_FINGER_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
					result = ZMOVIE_SKIPPED;
					done = true;
					break;
				case SDL_EVENT_QUIT:
					//let the game see it once the clip is gone
					SDL_PushEvent(&e);
					result = ZMOVIE_SKIPPED;
					done = true;
					break;
				default:
					break;
				}
			}

			if(ended) done = true;

			//an unreadable / unsupported file: give up rather than sit on black
			if(item.status == AVPlayerItemStatusFailed)
			{
				result = ZMOVIE_MISSING;
				done = true;
			}
			//never hang: nothing started playing after 5 s
			if(!done && CMTimeGetSeconds(item.currentTime) <= 0.0 && SDL_GetTicks() - start > 5000)
			{
				result = ZMOVIE_MISSING;
				done = true;
			}

			SDL_Delay(15);
		}

		[player pause];
		[[NSNotificationCenter defaultCenter] removeObserver:observer];
		[overlay removeFromSuperview];
	}

	return result;
}
