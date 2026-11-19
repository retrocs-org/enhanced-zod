#include "zvideo.h"
#include "zmenu_theme.h"
#include <SDL3_image/SDL_image.h>   // IMG_SavePNG for screenshots
#include <stdlib.h>   // getenv/atoi, for the look-tuning env vars

// Modern scaled-framebuffer rendering: the game draws to an off-screen surface
// at its (low) logical resolution, and we GPU-scale that up to a large window
// via an SDL_Renderer. This makes the pixel art big and crisp on today's
// high-resolution / Retina displays instead of a tiny, OS-upscaled (blurry)
// region. The whole thing is contained here, so none of the game's blitting
// code has to know about it.
static SDL_Window   *g_window         = NULL;
static SDL_Renderer *g_renderer       = NULL;
static SDL_Texture  *g_frame_tex      = NULL;  // GPU texture we present
static SDL_Surface  *g_frame_surface  = NULL;  // CPU surface the game blits to
static int           g_logical_w      = 0;
static int           g_logical_h      = 0;
static int           g_scanlines      = 0;   // ZOD_SCANLINES: CRT-style overlay
static int           g_scanline_alpha = 64;  // ZOD_SCANLINES value = darkness 0-255

static int           g_gamma          = 0;   // -ZVIDEO_GAMMA_STEPS..+ZVIDEO_GAMMA_STEPS
static bool          g_canvas_mode    = false;

// GPU gamma: the frame is drawn once normally, then once more on top of itself
// with a custom blend that bends the tone curve while keeping black and white
// fixed:  brighten  out = x + k*x*(1-x)      darken  out = x - k*x*(1-x)
// (k = strength). Cheap, needs no shaders or read-backs, and works the same on
// Metal, OpenGL and Direct3D. Renderers without custom blend support skip it.
static void gamma_pass(SDL_Texture *tex, const SDL_FRect *dst)
{
    if (!g_gamma || !tex || !g_renderer) return;

    static SDL_BlendMode bm_brighten = SDL_BLENDMODE_INVALID;
    static SDL_BlendMode bm_darken   = SDL_BLENDMODE_INVALID;
    if (bm_brighten == SDL_BLENDMODE_INVALID)
    {
        // src*(1-dst) + dst, with src = k*x via the color mod
        bm_brighten = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        // src*dst + dst*(1-a), with src = k*x (color mod) and a = k (alpha mod)
        bm_darken = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    }

    float k = (g_gamma < 0 ? -g_gamma : g_gamma) * (0.75f / ZVIDEO_GAMMA_STEPS);
    Uint8 k8 = (Uint8)(k * 255.0f + 0.5f);

    SDL_BlendMode old_bm;
    Uint8 old_r, old_g, old_b, old_a;
    SDL_GetTextureBlendMode(tex, &old_bm);
    SDL_GetTextureColorMod(tex, &old_r, &old_g, &old_b);
    SDL_GetTextureAlphaMod(tex, &old_a);

    if (SDL_SetTextureBlendMode(tex, g_gamma > 0 ? bm_brighten : bm_darken))
    {
        SDL_SetTextureColorMod(tex, k8, k8, k8);
        SDL_SetTextureAlphaMod(tex, g_gamma > 0 ? 255 : k8);
        SDL_RenderTexture(g_renderer, tex, NULL, dst);
    }

    SDL_SetTextureBlendMode(tex, old_bm);
    SDL_SetTextureColorMod(tex, old_r, old_g, old_b);
    SDL_SetTextureAlphaMod(tex, old_a);
}

static int g_picture_mode = 0;   // zprefs picture_mode: 0 smooth, 1 sharp, 2 pixel-perfect

static SDL_RendererLogicalPresentation framebuffer_presentation()
{
    // ZOD_INTEGER stays as the old way to force whole-number scaling
    return (g_picture_mode == 2 || getenv("ZOD_INTEGER")) ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                                           : SDL_LOGICAL_PRESENTATION_LETTERBOX;
}

static SDL_ScaleMode framebuffer_scale_mode()
{
    const char *e_filter = getenv("ZOD_FILTER");
    if (e_filter && e_filter[0]) return e_filter[0] == '0' ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR;
    return g_picture_mode == 0 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
}

static void destroy_scaler()
{
    if (g_frame_tex)     { SDL_DestroyTexture(g_frame_tex); g_frame_tex = NULL; }
    if (g_frame_surface) { SDL_DestroySurface(g_frame_surface); g_frame_surface = NULL; }
}

bool ZVideo_GetDesktopSize(int &w, int &h)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;

    const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (!dm || dm->w <= 0 || dm->h <= 0) return false;

    w = dm->w;
    h = dm->h;
    return true;
}

bool ZVideo_GetOutputSize(int &w, int &h)
{
    if (!g_renderer) return false;
    return SDL_GetRenderOutputSize(g_renderer, &w, &h);
}

void ZVideo_SetFullscreen(bool fullscreen)
{
    if (!g_window) return;
    // No exclusive mode is ever set, so SDL3 gives borderless desktop
    // fullscreen - no frame, no mode switch.
    SDL_SetWindowFullscreen(g_window, fullscreen);
    SDL_SyncWindow(g_window);
}

bool ZVideo_GetFullscreen()
{
    if (!g_window) return false;
    return (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

SDL_Surface *ZVideo_SetMode(int w, int h, bool fullscreen)
{
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;

    // --- look-tuning knobs (env vars, so they can be compared without rebuilds) ---
    // ZOD_FILTER:    1 = linear/smooth (default), 0 = nearest/crisp
    // ZOD_INTEGER:   1 = force perfectly-even integer scaling (may add bars)
    // ZOD_SCANLINES: 0 = off (default), or 1-255 = CRT scanline darkness
    const char *e_scan = getenv("ZOD_SCANLINES");
    if (e_scan) { g_scanline_alpha = atoi(e_scan); g_scanlines = g_scanline_alpha > 0; }

    if (!g_window)
    {
        // Size the window to fill ~90% of the desktop while preserving the
        // game's aspect ratio, so the logical framebuffer scales up nicely.
        int win_w = w, win_h = h;
        const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (dm && dm->w > 0 && dm->h > 0)
        {
            double sx = (double)(dm->w * 9 / 10) / w;
            double sy = (double)(dm->h * 9 / 10) / h;
            double s  = sx < sy ? sx : sy;
            if (s < 1.0) s = 1.0;
            win_w = (int)(w * s);
            win_h = (int)(h * s);
        }

        Uint32 wflags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
        if (fullscreen) wflags |= SDL_WINDOW_FULLSCREEN;

        g_window = SDL_CreateWindow("zod-ee", win_w, win_h, wflags);
        if (!g_window) return NULL;
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        g_renderer = SDL_CreateRenderer(g_window, NULL);
        if (!g_renderer) return NULL;
        SDL_SetRenderVSync(g_renderer, 1);
    }
    else
    {
        SDL_SetWindowFullscreen(g_window, fullscreen);
    }

    // (Re)build the logical framebuffer + texture when the resolution changes.
    if (w != g_logical_w || h != g_logical_h || !g_frame_surface || !g_frame_tex)
    {
        destroy_scaler();
        g_logical_w = w;
        g_logical_h = h;

        // Aspect-correct (or integer) scaling; also maps mouse events into this space.
        // (The front-end keeps its own canvas presentation until it hands back.)
        if (!g_canvas_mode)
            SDL_SetRenderLogicalPresentation(g_renderer, w, h, framebuffer_presentation());
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);

        g_frame_tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, w, h);
        // SDL3 sets scale quality per-texture (SDL_HINT_RENDER_SCALE_QUALITY is gone).
        SDL_SetTextureScaleMode(g_frame_tex, framebuffer_scale_mode());

        g_frame_surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    }

    return g_frame_surface;
}

void ZVideo_Present()
{
    if (!g_renderer || !g_frame_tex || !g_frame_surface) return;

    SDL_UpdateTexture(g_frame_tex, NULL, g_frame_surface->pixels, g_frame_surface->pitch);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_frame_tex, NULL, NULL);
    gamma_pass(g_frame_tex, NULL);

    // Optional CRT-style scanlines: a translucent dark line over every other
    // logical row (drawn in logical space, so it scales with the image).
    if (g_scanlines)
    {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, (Uint8)g_scanline_alpha);
        for (int y = 0; y < g_logical_h; y += 2)
            SDL_RenderLine(g_renderer, 0.0f, (float)y, (float)(g_logical_w - 1), (float)y);
    }

    ZMenuTheme::presentText(g_renderer,g_logical_w,g_logical_h);
    SDL_RenderPresent(g_renderer);
}

// Save the current game frame to a PNG. g_frame_surface is the CPU surface the
// game blits into (the exact image being shown, at the logical resolution), so
// this captures the game view without the OS-level scaling/scanlines. Returns
// true on success.
bool ZVideo_SaveScreenshot(const char *path)
{
    if (!g_frame_surface || !path) return false;
    return IMG_SavePNG(g_frame_surface, path);
}

void ZVideo_ConvertEventCoords(SDL_Event *ev)
{
    if (g_renderer && ev) SDL_ConvertEventToRenderCoordinates(g_renderer, ev);
}

void ZVideo_WarpMouse(int x, int y)
{
    if (!g_window) return;
    if (g_renderer)
    {
        float wx = (float)x, wy = (float)y;
        SDL_RenderCoordinatesToWindow(g_renderer, (float)x, (float)y, &wx, &wy);
        SDL_WarpMouseInWindow(g_window, wx, wy);
    }
    else
        SDL_WarpMouseInWindow(g_window, (float)x, (float)y);
}

void ZVideo_SetCaption(const char *title)
{
    if (g_window && title) SDL_SetWindowTitle(g_window, title);
}

void ZVideo_SetIcon(SDL_Surface *icon)
{
    if (g_window && icon) SDL_SetWindowIcon(g_window, icon);
}

void ZVideo_SetGrab(bool grab)
{
    if (g_window) SDL_SetWindowMouseGrab(g_window, grab);
}

bool ZVideo_GetGrab()
{
    return g_window ? SDL_GetWindowMouseGrab(g_window) : false;
}

// ---- front-end support ------------------------------------------------------

SDL_Window *ZVideo_GetWindow()     { return g_window; }
SDL_Renderer *ZVideo_GetRenderer() { return g_renderer; }

void ZVideo_SetCanvasMode(int canvas_w, int canvas_h)
{
    if (!g_renderer) return;
    g_canvas_mode = true;
    SDL_SetRenderLogicalPresentation(g_renderer, canvas_w, canvas_h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

void ZVideo_SetFramebufferMode()
{
    if (!g_renderer) return;
    g_canvas_mode = false;
    if (g_logical_w > 0 && g_logical_h > 0)
        SDL_SetRenderLogicalPresentation(g_renderer, g_logical_w, g_logical_h, framebuffer_presentation());
}

void ZVideo_SetPictureMode(int mode)
{
    g_picture_mode = mode;
    if (g_frame_tex) SDL_SetTextureScaleMode(g_frame_tex, framebuffer_scale_mode());
    if (g_renderer && !g_canvas_mode && g_logical_w > 0 && g_logical_h > 0)
        SDL_SetRenderLogicalPresentation(g_renderer, g_logical_w, g_logical_h, framebuffer_presentation());
}

void ZVideo_PresentTexture(SDL_Texture *tex)
{
    if (!g_renderer || !tex) return;

    // A cinematic or a window resize can leave an explicit viewport behind.
    // Presentation owns the window target and must cover its current size.
    SDL_SetRenderTarget(g_renderer, nullptr);
    SDL_SetRenderViewport(g_renderer, nullptr);
    SDL_SetRenderClipRect(g_renderer, nullptr);
    SDL_SetRenderScale(g_renderer, 1.0f, 1.0f);
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_GetRenderLogicalPresentation(g_renderer, &lw, &lh, &mode);
    SDL_FRect dst = { 0.0f, 0.0f, (float)lw, (float)lh };

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, tex, NULL, &dst);
    gamma_pass(tex, &dst);
    SDL_RenderPresent(g_renderer);
}

void ZVideo_SetGamma(int level)
{
    if (level < -ZVIDEO_GAMMA_STEPS) level = -ZVIDEO_GAMMA_STEPS;
    if (level >  ZVIDEO_GAMMA_STEPS) level =  ZVIDEO_GAMMA_STEPS;
    g_gamma = level;
}

int ZVideo_GetGamma() { return g_gamma; }

ZVideoWindowScope::ZVideoWindowScope(SDL_Renderer *value):renderer(value),target(value?SDL_GetRenderTarget(value):nullptr)
{
    if(!renderer)return;
    SDL_SetRenderTarget(renderer,nullptr);
    SDL_GetRenderLogicalPresentation(renderer,&logicalW,&logicalH,&presentation);
    SDL_GetRenderScale(renderer,&scaleX,&scaleY);
    viewportSet=SDL_RenderViewportSet(renderer);
    SDL_GetRenderViewport(renderer,&viewport);
    clipped=SDL_RenderClipEnabled(renderer);
    SDL_GetRenderClipRect(renderer,&clip);
    // Video layout uses SDL_GetRenderOutputSize, hence physical pixels. Leaving
    // the menu's logical resolution enabled would scale those coordinates twice.
    SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(renderer,1,1);
    SDL_SetRenderViewport(renderer,nullptr);
    SDL_SetRenderClipRect(renderer,nullptr);
}
ZVideoWindowScope::~ZVideoWindowScope()
{
    if(!renderer)return;
    SDL_SetRenderTarget(renderer,nullptr);
    SDL_SetRenderLogicalPresentation(renderer,logicalW,logicalH,presentation);
    SDL_SetRenderScale(renderer,scaleX,scaleY);
    SDL_SetRenderViewport(renderer,viewportSet?&viewport:nullptr);
    SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);
    SDL_SetRenderTarget(renderer,target);
}
