/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2019 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

/*
 * Ported to SDL2
 */

#include <config.h>
#include <stdint.h>
#include <signal.h>
#include <veejaycore/defs.h>
#ifdef HAVE_SDL
#include <libveejay/vj-sdl.h>
#include <SDL2/SDL.h>
#include <veejaycore/defs.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-lib.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <veejaycore/vjmem.h>
#include <libel/vj-avcodec.h>
#include <veejaycore/yuvconv.h>
#include <libveejay/libveejay.h>
#include <veejaycore/avcommon.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <libveejay/vj-perf.h>

typedef struct vj_sdl_t {
    SDL_Window *screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Event event;
    uint32_t flags;
    char *caption;
    int show_cursor;
    int mouse_motion;
    int use_keyboard;
    int borderless;
    int width;
    int height;
    int sw_scale_width;
    int sw_scale_height;
    int fs;
    void *scaler;
    void *font;
	void *src_frame;
	void *dst_frame;
    int x;
    int y;
    uint8_t *pixels;
    uint8_t *buf[VIDEO_QUEUE_LEN];
    pthread_mutex_t command_mutex;
    pthread_t owner_thread;
    int owner_valid;
    int initialized;
    int pending_geometry;
    int pending_width;
    int pending_height;
    int pending_x;
    int pending_y;
    int pending_fullscreen;
    int pending_grab;
    int display_index;
    int target_display_x;
    int target_display_y;
    int identify_overlay;
    int frame_prepared;
    int direct_lock_disabled;
    char renderer_name[64];
    double refresh_interval_s;
    unsigned int timing_generation;
    vj_perf_context *perf;
} vj_sdl;


static void fill_yuyv_black(uint8_t *buf, int width, int height)
{
    uint8_t *p = buf;
    int pixels = width * height;

    for (int i = 0; i < pixels; i += 2) {
        p[0] = 0x00; // Y0
        p[1] = 0x80; // U
        p[2] = 0x00; // Y1
        p[3] = 0x80; // V
        p += 4;
    }
}


void *vj_sdl_allocate(VJFrame *frame, int use_key, int use_mouse, int show_cursor, int borderless)
{
    vj_sdl *vjsdl;
    VJFrame *src;
    VJFrame *dst;
    sws_template templ;
    size_t bufsize;

    if (!frame)
        return NULL;

    vjsdl = (vj_sdl *) vj_calloc(sizeof(vj_sdl));
    if (!vjsdl)
        return NULL;

    vjsdl->flags = 0;
    vjsdl->use_keyboard = use_key;
    vjsdl->mouse_motion = use_mouse;
    vjsdl->show_cursor = show_cursor;
    vjsdl->width = frame->width;
    vjsdl->height = frame->height;
    vjsdl->sw_scale_width = 0;
    vjsdl->sw_scale_height = 0;
    vjsdl->x = -1;
    vjsdl->y = -1;
    vjsdl->borderless = borderless;

    memset(&templ, 0, sizeof(sws_template));
    templ.flags = yuv_which_scaler();

    src = yuv_yuv_template(NULL, NULL, NULL,
                           frame->width,
                           frame->height,
                           alpha_fmt_to_yuv(frame->format));
    dst = yuv_yuv_template(NULL, NULL, NULL,
                           frame->width,
                           frame->height,
                           PIX_FMT_YUYV422);

    if (!src || !dst) {
        if (src)
            free(src);
        if (dst)
            free(dst);
        free(vjsdl);
        return NULL;
    }

    vjsdl->scaler = yuv_init_swscaler(src, dst, &templ, yuv_sws_get_cpu_flags());
    if (!vjsdl->scaler) {
        free(src);
        free(dst);
        free(vjsdl);
        return NULL;
    }

    vjsdl->src_frame = (void*) src;
    vjsdl->dst_frame = (void*) dst;

    bufsize = (size_t) frame->width * (size_t) frame->height * 2u;
    vjsdl->pixels = (uint8_t*) vj_calloc(bufsize);
    if (!vjsdl->pixels) {
        yuv_free_swscaler(vjsdl->scaler);
        free(src);
        free(dst);
        free(vjsdl);
        return NULL;
    }

    vjsdl->buf[0] = vjsdl->pixels;
    fill_yuyv_black(vjsdl->buf[0], vjsdl->width, vjsdl->height);

    if(pthread_mutex_init(&vjsdl->command_mutex, NULL) != 0) {
        free(vjsdl->pixels);
        yuv_free_swscaler(vjsdl->scaler);
        free(src);
        free(dst);
        free(vjsdl);
        return NULL;
    }

    vjsdl->pending_fullscreen = -1;
    vjsdl->pending_grab = -1;
    vjsdl->display_index = -1;
    vjsdl->target_display_x = -1;
    vjsdl->target_display_y = -1;
    vjsdl->identify_overlay = 0;
    vjsdl->refresh_interval_s = 1.0 / 60.0;
    return (void*) vjsdl;
}

void vj_sdl_resize(void *ptr, int x, int y,
                   int scaled_width, int scaled_height, int fs)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl)
        return;

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->pending_geometry = 1;
    vjsdl->pending_width = scaled_width;
    vjsdl->pending_height = scaled_height;
    vjsdl->pending_x = x;
    vjsdl->pending_y = y;
    vjsdl->pending_fullscreen = fs ? 1 : 0;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

void vj_sdl_get_position(void *ptr, int *x, int *y)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    int cached_x = 0;
    int cached_y = 0;

    if(vjsdl) {
        pthread_mutex_lock(&vjsdl->command_mutex);
        cached_x = vjsdl->x;
        cached_y = vjsdl->y;
        pthread_mutex_unlock(&vjsdl->command_mutex);
    }

    if(x)
        *x = cached_x;
    if(y)
        *y = cached_y;
}


static int vj_get_sdl_yuv_mode(int vjfmt, int w)
{
    const char *env = getenv("VJ_SDL_YUV_MODE");

    int mode = SDL_YUV_CONVERSION_AUTOMATIC;

    if (env && *env) {
        if (!strcasecmp(env, "jpeg")) {
            mode = SDL_YUV_CONVERSION_JPEG;
        } else if (!strcasecmp(env, "auto")) {
            mode = SDL_YUV_CONVERSION_AUTOMATIC;
        } else if (!strcasecmp(env, "bt601")) {
            mode = SDL_YUV_CONVERSION_BT601;
        } else if (!strcasecmp(env, "bt709")) {
            mode = SDL_YUV_CONVERSION_BT709;
        } else {
            veejay_msg(VEEJAY_MSG_WARNING,
                "[DISPLAY] Unknown VJ_SDL_YUV_MODE='%s', using default", env);
        }
    }

    switch (mode) {
        case SDL_YUV_CONVERSION_JPEG:
            veejay_msg(VEEJAY_MSG_DEBUG,
                "[DISPLAY] SDL YUV conversion mode: JPEG (full range)");
            break;
        case SDL_YUV_CONVERSION_AUTOMATIC:
            veejay_msg(VEEJAY_MSG_DEBUG,
                "[DISPLAY] BT.601 for SD content, BT.709 for HD content (limited range)");
            break;
        case SDL_YUV_CONVERSION_BT601:
            veejay_msg(VEEJAY_MSG_DEBUG,
                "[DISPLAY] SDL YUV conversion mode: BT.601");
            break;
        case SDL_YUV_CONVERSION_BT709:
            veejay_msg(VEEJAY_MSG_DEBUG,
                "[DISPLAY] SDL YUV conversion mode: BT.709");
            break;
    }

    return mode;
}

static int vj_sdl_display_for_point(int x, int y)
{
    if(x == -1 || y == -1)
        return -1;
    const int count = SDL_GetNumVideoDisplays();
    for(int i = 0; i < count; i++) {
        SDL_Rect bounds;
        if(SDL_GetDisplayBounds(i, &bounds) != 0)
            continue;
        if(x >= bounds.x && x < bounds.x + bounds.w &&
           y >= bounds.y && y < bounds.y + bounds.h)
            return i;
    }
    return -1;
}

static void vj_sdl_anchor_display(vj_sdl *vjsdl, int display)
{
    if(!vjsdl || !vjsdl->screen || display < 0 || display >= SDL_GetNumVideoDisplays())
        return;
    SDL_SetWindowPosition(vjsdl->screen,
                          SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                          SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

static void vj_sdl_anchor_to_display(vj_sdl *vjsdl, int x, int y)
{
    vj_sdl_anchor_display(vjsdl, vj_sdl_display_for_point(x, y));
}

static int vj_sdl_is_owner(vj_sdl *vjsdl)
{
    return vjsdl && vjsdl->owner_valid &&
           pthread_equal(vjsdl->owner_thread, pthread_self());
}

static void vj_sdl_update_display_timing(vj_sdl *vjsdl,
                                         int display_index,
                                         int log_interval)
{
    SDL_DisplayMode mode;
    double refresh_interval_s = 1.0 / 60.0;

    if(display_index >= 0 &&
       SDL_GetCurrentDisplayMode(display_index, &mode) == 0)
    {
        const int hz = mode.refresh_rate > 0 ? mode.refresh_rate : 60;
        refresh_interval_s = 1.0 / (double)hz;
    }

    pthread_mutex_lock(&vjsdl->command_mutex);
    if(vjsdl->display_index != display_index ||
       vjsdl->refresh_interval_s != refresh_interval_s)
    {
        vjsdl->timing_generation++;
    }
    vjsdl->display_index = display_index;
    vjsdl->refresh_interval_s = refresh_interval_s;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    if(log_interval)
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[DISPLAY] SDL display refresh interval is %f",
                   refresh_interval_s);
}

static void vj_sdl_bump_timing_generation(vj_sdl *vjsdl)
{
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->timing_generation++;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

static void vj_sdl_apply_grab(vj_sdl *vjsdl, int status)
{
    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return;

    SDL_SetRelativeMouseMode(status == 1 ? SDL_TRUE : SDL_FALSE);
    veejay_msg(VEEJAY_MSG_DEBUG, "%s",
               status == 1 ? "[DISPLAY] Released mouse focus" :
                             "[DISPLAY] Grabbed mouse focus");
}


static void vj_sdl_abort_init(vj_sdl *vjsdl)
{
    if(!vjsdl)
        return;
    if(vjsdl->texture) {
        SDL_DestroyTexture(vjsdl->texture);
        vjsdl->texture = NULL;
    }
    if(vjsdl->renderer) {
        SDL_DestroyRenderer(vjsdl->renderer);
        vjsdl->renderer = NULL;
    }
    if(vjsdl->screen) {
        SDL_DestroyWindow(vjsdl->screen);
        vjsdl->screen = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->initialized = 0;
    vjsdl->owner_valid = 0;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

int vj_sdl_init(void *ptr, int x, int y, int input_width, int input_height, int scaled_width, int scaled_height, char *caption, int show, int fs, int vjfmt, float fps, double *vsync)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;
	int i = 0;

    if(!vjsdl)
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->owner_thread = pthread_self();
    vjsdl->owner_valid = 1;
    pthread_mutex_unlock(&vjsdl->command_mutex);

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s", SDL_GetError());
        pthread_mutex_lock(&vjsdl->command_mutex);
        vjsdl->owner_valid = 0;
        pthread_mutex_unlock(&vjsdl->command_mutex);
		return 0;
	}

	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;
    if(x != -1) {
        vjsdl->x = x;
    }
    if(y != -1) {
        vjsdl->y = y;
    }

    vjsdl->width = input_width;
    vjsdl->height = input_height;


    if(vjsdl->caption) {
        free(vjsdl->caption);
        vjsdl->caption = NULL;
    }
    if(caption)
        vjsdl->caption = strdup(caption);

    // SDL2: key repeat behaviour has changed; measure interval or look at repeat value on keysym
	//int ms = ( 1.0 / fps ) * 1000;

    pthread_mutex_lock(&vjsdl->command_mutex);
    const int target_x = vjsdl->target_display_x;
    const int target_y = vjsdl->target_display_y;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    const int target_display = target_x != -1 && target_y != -1 ?
                               vj_sdl_display_for_point(target_x, target_y) :
                               vj_sdl_display_for_point(vjsdl->x, vjsdl->y);
    const int create_x = target_display >= 0 ? SDL_WINDOWPOS_CENTERED_DISPLAY(target_display) :
                         (vjsdl->x != -1 ? vjsdl->x : SDL_WINDOWPOS_UNDEFINED);
    const int create_y = target_display >= 0 ? SDL_WINDOWPOS_CENTERED_DISPLAY(target_display) :
                         (vjsdl->y != -1 ? vjsdl->y : SDL_WINDOWPOS_UNDEFINED);
    int flags = vjsdl->borderless ? SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS : SDL_WINDOW_OPENGL;

	vjsdl->screen = SDL_CreateWindow(vjsdl->caption, create_x, create_y,
            vjsdl->sw_scale_width, vjsdl->sw_scale_height, flags );

    if(!vjsdl->screen)
    {
		veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL window: %s", SDL_GetError());
        vj_sdl_abort_init(vjsdl);
		return 0;
    }

    if(target_display >= 0 && !fs && x != -1 && y != -1)
        SDL_SetWindowPosition(vjsdl->screen, x, y);
    if(fs) {
        if(target_display >= 0)
            vj_sdl_anchor_display(vjsdl, target_display);
        else
            vj_sdl_anchor_to_display(vjsdl, x, y);
        if(SDL_SetWindowFullscreen(vjsdl->screen, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to enter fullscreen on target display: %s", SDL_GetError());
            vj_sdl_abort_init(vjsdl);
            return 0;
        }
    }
    if(!fs)
        SDL_GetWindowPosition(vjsdl->screen, &vjsdl->x, &vjsdl->y);

    // Iterate through available driver and try in order of priority
    char *sdl_driver = getenv("VEEJAY_SDL_DRIVER");
    char *shrd = getenv("VEEJAY_SDL_HINT_RENDER_DRIVER");
    char *shfa = getenv("VEEJAY_SDL_HINT_FRAMEBUFFER_ACCELERATION");
   
    if( shrd != NULL ) {
      SDL_SetHint( SDL_HINT_RENDER_DRIVER, shrd );
    }
    if( shfa != NULL ) {
      SDL_SetHint( SDL_HINT_FRAMEBUFFER_ACCELERATION, shfa );
    }

    if(sdl_driver == NULL) {
        int num_renderers = SDL_GetNumRenderDrivers();
        int render_flags[3] = { SDL_RENDERER_PRESENTVSYNC, SDL_RENDERER_ACCELERATED, SDL_RENDERER_SOFTWARE };
        for( i = 0; i < num_renderers && i < 3; i ++ ) {
            vjsdl->flags = render_flags[i];
            vjsdl->renderer = SDL_CreateRenderer( vjsdl->screen, -1, vjsdl->flags );
            if(vjsdl->renderer)
                break;
        }
    }
    else {
        if(strcasecmp("software",sdl_driver) == 0 ) {
            vjsdl->flags = SDL_RENDERER_SOFTWARE;
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_SOFTWARE );
        } else if(strcasecmp("accelerated", sdl_driver) == 0 ) {
            vjsdl->flags = SDL_RENDERER_ACCELERATED;
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_ACCELERATED );
        } else if (strcasecmp("vsync", sdl_driver) == 0 ) {
            vjsdl->flags = SDL_RENDERER_PRESENTVSYNC;
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_PRESENTVSYNC );
        } else {
            veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Valid values for VEEJAY_SDL_DRIVER are: \"software\", \"accelerated\", \"vsync\"");
            vj_sdl_abort_init(vjsdl);
            return 0;
        }
    }

    if(!vjsdl->renderer) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s", SDL_GetError());
        vj_sdl_abort_init(vjsdl);
        return 0;
    }
    
    SDL_RendererInfo info;
    if(SDL_GetRendererInfo(vjsdl->renderer, &info) == 0 ) {
        pthread_mutex_lock(&vjsdl->command_mutex);
        vjsdl->flags = info.flags;
        pthread_mutex_unlock(&vjsdl->command_mutex);
        snprintf(vjsdl->renderer_name, sizeof(vjsdl->renderer_name), "%s",
                 info.name ? info.name : "unknown");
        veejay_msg(VEEJAY_MSG_INFO,
                   "[DISPLAY] SDL renderer=%s accelerated=%s vsync=%s",
                   info.name,
                   (info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no",
                   (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no");
    }
    else {
        snprintf(vjsdl->renderer_name, sizeof(vjsdl->renderer_name), "%s",
                 "unknown");
    }
    const char *direct_lock = getenv("VEEJAY_SDL_DIRECT_LOCK");
    const int renderer_is_vulkan =
        strcasecmp(vjsdl->renderer_name, "vulkan") == 0;
    const int direct_lock_forced =
        direct_lock &&
        (strcmp(direct_lock, "1") == 0 ||
         strcasecmp(direct_lock, "true") == 0 ||
         strcasecmp(direct_lock, "on") == 0);
    const int direct_lock_forbidden =
        direct_lock &&
        (strcmp(direct_lock, "0") == 0 ||
         strcasecmp(direct_lock, "false") == 0 ||
         strcasecmp(direct_lock, "off") == 0);

    /*
     * SDL's Vulkan streaming-texture lock can serialize staging-buffer work
     * at UnlockTexture and move the remaining cost into driver worker threads.
     * SAMPLE playback reaches this planar-frame fast path while the established
     * packed path uses UpdateTexture, making the regression mode-specific and
     * invisible to the producer-only OSD timing.  Keep the fast path on other
     * renderers, but use the stable update path on Vulkan unless explicitly
     * forced for comparison.
     */
    vjsdl->direct_lock_disabled =
        direct_lock_forbidden || (renderer_is_vulkan && !direct_lock_forced);
    veejay_msg(VEEJAY_MSG_INFO,
               "[DISPLAY] SDL texture=YUY2 access=streaming upload=%s reason=%s "
               "(VEEJAY_SDL_DIRECT_LOCK=0|1 overrides)",
               vjsdl->direct_lock_disabled ? "update-only" : "direct-lock",
               direct_lock_forbidden ? "environment" :
               (renderer_is_vulkan && !direct_lock_forced) ?
                   "vulkan-safe-default" :
               direct_lock_forced ? "environment" : "renderer-default");
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->timing_generation++;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "linear" );
 
    SDL_RenderSetLogicalSize( vjsdl->renderer, vjsdl->width, vjsdl->height );

    vjsdl->texture = SDL_CreateTexture( vjsdl->renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, vjsdl->width,vjsdl->height);
    if(!vjsdl->texture) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL texture: %s", SDL_GetError());
        vj_sdl_abort_init(vjsdl);
        return 0;
    }

	veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] SDL Output dimensions: %d x %d @ %d,%d", vjsdl->sw_scale_width, vjsdl->sw_scale_height,vjsdl->x,vjsdl->y );

	if (vjsdl->use_keyboard == 1) 
		SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
	else 
		SDL_EventState(SDL_KEYDOWN, SDL_DISABLE);
    
    if (vjsdl->mouse_motion == 1) 
		SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
	else
		SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    
	if (vjsdl->show_cursor == 1) 
		SDL_ShowCursor(SDL_ENABLE);
    else
		SDL_ShowCursor(SDL_DISABLE);

    vj_sdl_apply_grab(vjsdl, 0);

#if SDL_VERSION_ATLEAST(2,0,8)
    int sdlmode = vj_get_sdl_yuv_mode(vjfmt, vjsdl->width);
    SDL_SetYUVConversionMode( sdlmode );
#else
    veejay_msg(VEEJAY_MSG_WARNING, "[DISPLAY] Please update SDL2 to a more recent version.");
#endif
    SDL_DisableScreenSaver();

    SDL_SetRenderDrawColor( vjsdl->renderer, 0,0,0,255 );
    SDL_RenderClear(vjsdl->renderer);
    SDL_RenderPresent(vjsdl->renderer);

    vjsdl->fs = fs;

    int display_index = SDL_GetWindowDisplayIndex(vjsdl->screen);
    if(target_display >= 0) {
        if(display_index == target_display)
            veejay_msg(VEEJAY_MSG_INFO,
                       "[DISPLAY] SDL window locked to display %d", display_index);
        else
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[DISPLAY] Requested SDL display %d but compositor placed the window on display %d",
                       target_display, display_index);
    }
    vj_sdl_update_display_timing(vjsdl, display_index, 1);
    if(vsync)
        *vsync = vjsdl->refresh_interval_s;

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->initialized = 1;
    pthread_mutex_unlock(&vjsdl->command_mutex);

	return 1;
}

static int vj_sdl_apply_window_size(vj_sdl *vjsdl,
                                    int w, int h, int x, int y)
{
    int actual_w;
    int actual_h;
    int current_x;
    int current_y;
    int fullscreen;

    if(!vjsdl || !vj_sdl_is_owner(vjsdl) || !vjsdl->screen)
        return 0;

    if(w == 0 && h == 0) {
        SDL_HideWindow(vjsdl->screen);
        vj_sdl_bump_timing_generation(vjsdl);
        veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] SDL video window hidden");
        return 1;
    }

    if(w <= 0 || h <= 0)
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    current_x = vjsdl->x;
    current_y = vjsdl->y;
    fullscreen = vjsdl->fs;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    if(!fullscreen && (x == -1 || y == -1))
        SDL_GetWindowPosition(vjsdl->screen, &current_x, &current_y);

    if(x != -1)
        current_x = x;
    if(y != -1)
        current_y = y;

    if(!fullscreen) {
        int target_display;
        pthread_mutex_lock(&vjsdl->command_mutex);
        const int target_x = vjsdl->target_display_x;
        const int target_y = vjsdl->target_display_y;
        pthread_mutex_unlock(&vjsdl->command_mutex);
        target_display = target_x != -1 && target_y != -1 ?
                         vj_sdl_display_for_point(target_x, target_y) : -1;
        SDL_SetWindowSize(vjsdl->screen, w, h);
        if(target_display >= 0)
            vj_sdl_anchor_display(vjsdl, target_display);
        else
            vj_sdl_anchor_to_display(vjsdl, current_x, current_y);
        if(target_display < 0 || vj_sdl_display_for_point(current_x, current_y) == target_display)
            SDL_SetWindowPosition(vjsdl->screen, current_x, current_y);
    }

    if(vjsdl->renderer &&
       SDL_RenderSetLogicalSize(vjsdl->renderer,
                                vjsdl->width,
                                vjsdl->height) != 0)
    {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] Unable to retain logical video size: %s",
                   SDL_GetError());
        return 0;
    }

    if(!(SDL_GetWindowFlags(vjsdl->screen) & SDL_WINDOW_SHOWN))
        SDL_ShowWindow(vjsdl->screen);

    actual_w = w;
    actual_h = h;
    if(!fullscreen) {
        SDL_GetWindowSize(vjsdl->screen, &actual_w, &actual_h);
        SDL_GetWindowPosition(vjsdl->screen, &current_x, &current_y);
    }
    const int actual_display = SDL_GetWindowDisplayIndex(vjsdl->screen);
    vj_sdl_update_display_timing(vjsdl, actual_display, 0);

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->x = current_x;
    vjsdl->y = current_y;
    vjsdl->sw_scale_width = actual_w;
    vjsdl->sw_scale_height = actual_h;
    vjsdl->timing_generation++;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    veejay_msg(VEEJAY_MSG_INFO,
               "[DISPLAY] SDL video window %s at %dx%d, position x=%d, y=%d",
               fullscreen ? "geometry queued while fullscreen" : "resized",
               actual_w, actual_h, current_x, current_y);
    return 1;
}

static int vj_sdl_apply_fullscreen(vj_sdl *vjsdl, int enabled)
{
    uint32_t flags = enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    int width;
    int height;
    int x;
    int y;
    int requested_display = -1;

    if(!vjsdl || !vj_sdl_is_owner(vjsdl) || !vjsdl->screen)
        return 0;

    if(enabled) {
        pthread_mutex_lock(&vjsdl->command_mutex);
        const int target_x = vjsdl->target_display_x;
        const int target_y = vjsdl->target_display_y;
        pthread_mutex_unlock(&vjsdl->command_mutex);
        requested_display = target_x != -1 && target_y != -1 ?
                            vj_sdl_display_for_point(target_x, target_y) :
                            vj_sdl_display_for_point(vjsdl->x, vjsdl->y);
        if(SDL_GetWindowFlags(vjsdl->screen) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP))
            SDL_SetWindowFullscreen(vjsdl->screen, 0);
        if(requested_display >= 0)
            vj_sdl_anchor_display(vjsdl, requested_display);
        else
            vj_sdl_anchor_to_display(vjsdl, vjsdl->x, vjsdl->y);
    }

    if(SDL_SetWindowFullscreen(vjsdl->screen, flags) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] Fullscreen failed: %s", SDL_GetError());
        return 0;
    }

    const int actual_display = SDL_GetWindowDisplayIndex(vjsdl->screen);
    if(enabled && requested_display >= 0) {
        if(actual_display != requested_display)
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[DISPLAY] Fullscreen requested on display %d but active on display %d",
                       requested_display, actual_display);
        else
            veejay_msg(VEEJAY_MSG_INFO,
                       "[DISPLAY] Fullscreen locked to display %d", actual_display);
    }

    vj_sdl_update_display_timing(vjsdl, actual_display, 0);
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->fs = enabled ? 1 : 0;
    vjsdl->timing_generation++;
    width = vjsdl->sw_scale_width;
    height = vjsdl->sw_scale_height;
    x = vjsdl->x;
    y = vjsdl->y;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    if(enabled) {
        vj_sdl_apply_grab(vjsdl, 0);
    }
    else if(width > 0 && height > 0) {
        return vj_sdl_apply_window_size(vjsdl, width, height, x, y);
    }

    return 1;
}

int vj_sdl_set_fullscreen(void *ptr, int enabled)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl)
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    if(!vjsdl->initialized) {
        pthread_mutex_unlock(&vjsdl->command_mutex);
        return 0;
    }
    vjsdl->pending_fullscreen = enabled ? 1 : 0;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return 1;
}

int vj_sdl_set_window_size(void *ptr, int w, int h, int x, int y)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl || w < 0 || h < 0 || ((w == 0) != (h == 0)))
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    if(!vjsdl->initialized) {
        pthread_mutex_unlock(&vjsdl->command_mutex);
        return 0;
    }
    vjsdl->pending_geometry = 1;
    vjsdl->pending_width = w;
    vjsdl->pending_height = h;
    vjsdl->pending_x = x;
    vjsdl->pending_y = y;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return 1;
}

void vj_sdl_grab(void *ptr, int status)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl)
        return;

    pthread_mutex_lock(&vjsdl->command_mutex);
    if(vjsdl->initialized)
        vjsdl->pending_grab = status ? 1 : 0;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

void vj_sdl_process_pending(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    int geometry;
    int width;
    int height;
    int x;
    int y;
    int fullscreen;
    int grab;

    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return;

    pthread_mutex_lock(&vjsdl->command_mutex);
    geometry = vjsdl->pending_geometry;
    width = vjsdl->pending_width;
    height = vjsdl->pending_height;
    x = vjsdl->pending_x;
    y = vjsdl->pending_y;
    fullscreen = vjsdl->pending_fullscreen;
    grab = vjsdl->pending_grab;
    vjsdl->pending_geometry = 0;
    vjsdl->pending_fullscreen = -1;
    vjsdl->pending_grab = -1;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    if(fullscreen >= 0)
        vj_sdl_apply_fullscreen(vjsdl, fullscreen);

    if(geometry)
        vj_sdl_apply_window_size(vjsdl, width, height, x, y);

    if(grab >= 0)
        vj_sdl_apply_grab(vjsdl, grab);

    if(vjsdl->screen) {
        const int actual_display = SDL_GetWindowDisplayIndex(vjsdl->screen);
        pthread_mutex_lock(&vjsdl->command_mutex);
        const int cached_display = vjsdl->display_index;
        pthread_mutex_unlock(&vjsdl->command_mutex);
        if(actual_display != cached_display)
            vj_sdl_update_display_timing(vjsdl, actual_display, 1);
    }
}

void    vj_sdl_enable_screensaver(void)
{
    SDL_EnableScreenSaver();
}

static void vj_sdl_timing_snapshot(vj_sdl *vjsdl,
                                   vj_sdl_present_timing_t *timing)
{
    if(!timing)
        return;

    pthread_mutex_lock(&vjsdl->command_mutex);
    timing->vsync_enabled =
        (vjsdl->flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
    timing->refresh_interval_s = vjsdl->refresh_interval_s;
    timing->timing_generation = vjsdl->timing_generation;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

static int vj_sdl_prepare_pixels(vj_sdl *vjsdl,
                                 uint8_t *pixels_to_render,
                                 vj_sdl_present_timing_t *timing)
{
    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return 0;

    vjsdl->frame_prepared = 0;

    vj_sdl_process_pending(vjsdl);

    if(!vjsdl->renderer || !vjsdl->texture) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL renderer/texture is not initialized");
        return 0;
    }

    if(!pixels_to_render) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL texture update skipped: pixels buffer is NULL");
        return 0;
    }

    const uint64_t upload_started_ns = vj_perf_now_ns();
    const int update_result = SDL_UpdateTexture(vjsdl->texture, NULL,
                                                 pixels_to_render,
                                                 vjsdl->width * 2);
    const uint64_t update_completed_ns = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_TEXTURE_UPDATE,
                   upload_started_ns, update_completed_ns);
    if(update_result != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL texture update failed: %s",
                   SDL_GetError());
        return 0;
    }

    const uint64_t render_copy_started_ns = update_completed_ns;
    int render_result = SDL_RenderClear(vjsdl->renderer);
    if(render_result == 0)
        render_result = SDL_RenderCopy(vjsdl->renderer,
                                       vjsdl->texture, NULL, NULL);
    const uint64_t render_copy_completed_ns = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_RENDER_COPY,
                   render_copy_started_ns, render_copy_completed_ns);
    if(render_result != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL render failed: %s", SDL_GetError());
        return 0;
    }

    const uint64_t upload_completed_ns = render_copy_completed_ns;
    vjsdl->frame_prepared = 1;
    if(timing)
        timing->upload_ns = upload_completed_ns - upload_started_ns;
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_UPLOAD,
                   upload_started_ns, upload_completed_ns);
    vj_sdl_timing_snapshot(vjsdl, timing);
    return 1;
}

static int vj_sdl_present_prepared_internal(vj_sdl *vjsdl,
                                            vj_sdl_present_timing_t *timing)
{
    if(!vjsdl || !vj_sdl_is_owner(vjsdl) ||
       !vjsdl->renderer || !vjsdl->frame_prepared)
        return 0;

    const uint64_t present_started_ns = vj_perf_now_ns();
    SDL_RenderPresent(vjsdl->renderer);
    const uint64_t present_completed_ns = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_PRESENT_BLOCK,
                   present_started_ns, present_completed_ns);

    vjsdl->frame_prepared = 0;
    if(timing) {
        timing->present_block_ns = present_completed_ns - present_started_ns;
        timing->completed_ns = present_completed_ns;
    }
    vj_sdl_timing_snapshot(vjsdl, timing);
    return 1;
}

static int vj_sdl_present_pixels(vj_sdl *vjsdl, uint8_t *pixels_to_render)
{
    vj_sdl_present_timing_t timing;
    memset(&timing, 0, sizeof(timing));
    if(!vj_sdl_prepare_pixels(vjsdl, pixels_to_render, &timing))
        return 0;
    return vj_sdl_present_prepared_internal(vjsdl, &timing);
}

void vj_sdl_put_to_screen(void *ptr, uint8_t *pixels_to_render)
{
    vj_sdl_present_pixels((vj_sdl*)ptr, pixels_to_render);
}

void vj_sdl_preroll(void *ptr, int frame_count)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;

    if (!vjsdl || !vjsdl->renderer || !vjsdl->texture || !vjsdl->buf[0])
        return;

    veejay_msg(VEEJAY_MSG_INFO,
               "[DISPLAY] Initializing GPU pipeline (Preroll %d frames)",
               frame_count);

    fill_yuyv_black(vjsdl->buf[0], vjsdl->width, vjsdl->height);

    for (int i = 0; i < frame_count; i++) {
        if (SDL_UpdateTexture(vjsdl->texture, NULL,
                              vjsdl->buf[0],
                              vjsdl->width * 2) != 0)
        {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "[DISPLAY] SDL preroll texture update failed: %s",
                       SDL_GetError());
            return;
        }

        SDL_RenderClear(vjsdl->renderer);
        SDL_RenderCopy(vjsdl->renderer, vjsdl->texture, NULL, NULL);
        SDL_RenderPresent(vjsdl->renderer);
        SDL_Delay(10);
    }

    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] GPU Warm-up complete.");
}

void vj_sdl_convert_and_update_screen(void *ptr, uint8_t ** yuv420)
{

}

static int vj_sdl_convert_frame(vj_sdl *vjsdl, VJFrame *frame, uint8_t *pixels)
{
    if(!vjsdl || !frame || !pixels) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL conversion skipped: invalid frame or pixels buffer");
        return 0;
    }

    VJFrame *dst_frame = (VJFrame*)vjsdl->dst_frame;
    if(!dst_frame || !vjsdl->scaler) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL conversion skipped: scaler is not initialized");
        return 0;
    }

    dst_frame->data[0] = pixels;
    yuv_convert_and_scale_packed(vjsdl->scaler, frame, dst_frame);
    return 1;
}

void vj_sdl_convert_to_screen(void *ptr, VJFrame *frame_to_dsplay, uint8_t *pixels)
{
    vj_sdl_convert_frame((vj_sdl*)ptr, frame_to_dsplay, pixels);
}

static void vj_sdl_yuy2_rect(uint8_t *pixels, int pitch, int width, int height,
                             int x, int y, int w, int h, uint8_t luma)
{
    if(!pixels || w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > width ? width : x + w;
    int y1 = y + h > height ? height : y + h;
    if(x0 >= x1 || y0 >= y1)
        return;

    for(int py = y0; py < y1; py++) {
        uint8_t *row = pixels + (size_t)py * (size_t)pitch;
        for(int px = x0; px < x1; px++) {
            const size_t off = (size_t)px * 2u;
            row[off] = luma;
            row[off + 1u] = 0x80;
        }
    }
}

static void vj_sdl_identify_digit(uint8_t *pixels, int pitch, int width, int height,
                                  int x, int y, int digit, int scale)
{
    static const uint8_t masks[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };
    static const int segments[7][4] = {
        {1, 0, 3, 1}, {4, 1, 1, 3}, {4, 5, 1, 3},
        {1, 8, 3, 1}, {0, 5, 1, 3}, {0, 1, 1, 3},
        {1, 4, 3, 1}
    };
    if(digit < 0 || digit > 9)
        return;
    const uint8_t mask = masks[digit];
    for(int i = 0; i < 7; i++) {
        if(mask & (1u << i))
            vj_sdl_yuy2_rect(pixels, pitch, width, height,
                             x + segments[i][0] * scale,
                             y + segments[i][1] * scale,
                             segments[i][2] * scale,
                             segments[i][3] * scale, 235);
    }
}

static void vj_sdl_overlay_identify(vj_sdl *vjsdl, uint8_t *pixels, int pitch)
{
    int identify;
    int display;
    pthread_mutex_lock(&vjsdl->command_mutex);
    identify = vjsdl->identify_overlay;
    display = vjsdl->display_index;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    if(identify == 0 || !pixels)
        return;

    int number = identify > 0 ? identify : (display >= 0 ? display + 1 : 0);
    char text[16];
    snprintf(text, sizeof(text), "%d", number);
    const int digits = (int)strlen(text);
    int scale = vjsdl->height / 14;
    if(scale < 8)
        scale = 8;
    if(scale > vjsdl->width / (digits * 7 + 4))
        scale = vjsdl->width / (digits * 7 + 4);
    if(scale < 2)
        scale = 2;

    const int digit_w = 5 * scale;
    const int digit_h = 9 * scale;
    const int gap = scale;
    const int total_w = digits * digit_w + (digits - 1) * gap;
    const int panel_pad = 2 * scale;
    const int x0 = (vjsdl->width - total_w) / 2;
    const int y0 = (vjsdl->height - digit_h) / 2;

    vj_sdl_yuy2_rect(pixels, pitch, vjsdl->width, vjsdl->height,
                     x0 - panel_pad, y0 - panel_pad,
                     total_w + panel_pad * 2, digit_h + panel_pad * 2, 16);
    for(int i = 0; i < digits; i++)
        vj_sdl_identify_digit(pixels, pitch, vjsdl->width, vjsdl->height,
                              x0 + i * (digit_w + gap), y0,
                              text[i] - '0', scale);
}

static int vj_sdl_direct_pack_supported(vj_sdl *vjsdl, const VJFrame *frame)
{
    const int format = alpha_fmt_to_yuv(frame->format);
    return !vjsdl->direct_lock_disabled &&
           frame->data[0] && frame->data[1] && frame->data[2] &&
           frame->width == vjsdl->width && frame->height == vjsdl->height &&
           (frame->width & 1) == 0 &&
           frame->stride[0] >= frame->width &&
           frame->stride[1] >= (frame->width >> 1) &&
           frame->stride[2] >= (frame->width >> 1) &&
           (format == PIX_FMT_YUV422P || format == PIX_FMT_YUVJ422P);
}

static int vj_sdl_prepare_direct_frame(vj_sdl *vjsdl, VJFrame *frame,
                                       vj_sdl_present_timing_t *timing)
{
    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return 0;

    vj_sdl_process_pending(vjsdl);
    if(!vjsdl->renderer || !vjsdl->texture)
        return 0;

    const uint64_t prepare_start = vj_perf_now_ns();
    void *locked = NULL;
    int pitch = 0;
    const int lock_result =
        SDL_LockTexture(vjsdl->texture, NULL, &locked, &pitch);
    const uint64_t lock_end = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_TEXTURE_LOCK,
                   prepare_start, lock_end);
    if(lock_result != 0) {
        pthread_mutex_lock(&vjsdl->command_mutex);
        vjsdl->direct_lock_disabled = 1;
        pthread_mutex_unlock(&vjsdl->command_mutex);
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[DISPLAY] Direct texture lock unavailable, retaining swscale/update fallback: %s",
                   SDL_GetError());
        return -1;
    }

    const uint8_t *src[3] = { frame->data[0], frame->data[1], frame->data[2] };
    const int src_stride[3] = { frame->stride[0], frame->stride[1], frame->stride[2] };
    const uint64_t pack_start = vj_perf_now_ns();
    const int converted = vj_yuv422p_to_yuy2(src, src_stride,
                                              (uint8_t*)locked, pitch,
                                              frame->width, frame->height);
    if(converted)
        vj_sdl_overlay_identify(vjsdl, (uint8_t*)locked, pitch);
    const uint64_t pack_end = vj_perf_now_ns();
    const uint64_t unlock_start = pack_end;
    SDL_UnlockTexture(vjsdl->texture);
    const uint64_t unlock_end = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_TEXTURE_UNLOCK,
                   unlock_start, unlock_end);

    if(!converted)
        return -1;

    const uint64_t render_copy_start = unlock_end;
    int render_result = SDL_RenderClear(vjsdl->renderer);
    if(render_result == 0)
        render_result = SDL_RenderCopy(vjsdl->renderer,
                                       vjsdl->texture, NULL, NULL);
    const uint64_t render_copy_end = vj_perf_now_ns();
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_RENDER_COPY,
                   render_copy_start, render_copy_end);
    if(render_result != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL render failed: %s", SDL_GetError());
        return 0;
    }

    const uint64_t prepare_end = render_copy_end;
    vjsdl->frame_prepared = 1;
    if(timing) {
        timing->convert_ns = pack_end - pack_start;
        timing->upload_ns = (pack_start - prepare_start) +
                            (prepare_end - pack_end);
    }
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_CONVERT,
                   pack_start, pack_end);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_PACK,
                   pack_start, pack_end);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_UPLOAD,
                   pack_end, prepare_end);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_UPLOAD_PRESENT,
                   prepare_start, prepare_end);
    vj_sdl_timing_snapshot(vjsdl, timing);
    return 1;
}

int vj_sdl_prepare_frame(void *ptr, VJFrame *frame,
                         vj_sdl_present_timing_t *timing)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl || !frame || !vjsdl->buf[0])
        return 0;
    if(timing)
        memset(timing, 0, sizeof(*timing));

    if(vj_sdl_direct_pack_supported(vjsdl, frame)) {
        const int direct = vj_sdl_prepare_direct_frame(vjsdl, frame, timing);
        if(direct >= 0)
            return direct;
    }

    const uint64_t convert_start = vj_perf_now_ns();
    if(!vj_sdl_convert_frame(vjsdl, frame, vjsdl->buf[0]))
        return 0;
    vj_sdl_overlay_identify(vjsdl, vjsdl->buf[0], vjsdl->width * 2);
    const uint64_t convert_end = vj_perf_now_ns();
    if(timing)
        timing->convert_ns = convert_end - convert_start;
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_CONVERT,
                   convert_start, convert_end);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_SDL_PACK,
                   convert_start, convert_end);
    const uint64_t upload_start = vj_perf_now_ns();
    const int result =
        vj_sdl_prepare_pixels(vjsdl, vjsdl->buf[0], timing);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_UPLOAD_PRESENT,
                   upload_start, vj_perf_now_ns());
    return result;
}

int vj_sdl_present_prepared(void *ptr, vj_sdl_present_timing_t *timing)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    return vj_sdl_present_prepared_internal(vjsdl, timing);
}

void vj_sdl_discard_prepared(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(vjsdl && vj_sdl_is_owner(vjsdl))
        vjsdl->frame_prepared = 0;
}

int vj_sdl_get_present_mode(void *ptr, int *vsync_enabled,
                            double *refresh_interval_s,
                            unsigned int *timing_generation)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl)
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    const int initialized = vjsdl->initialized;
    if(vsync_enabled)
        *vsync_enabled =
            (vjsdl->flags & SDL_RENDERER_PRESENTVSYNC) ? 1 : 0;
    if(refresh_interval_s)
        *refresh_interval_s = vjsdl->refresh_interval_s;
    if(timing_generation)
        *timing_generation = vjsdl->timing_generation;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return initialized;
}

int vj_sdl_get_backend(void *ptr, char *name, size_t name_size,
                       int *direct_lock_disabled)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl)
        return 0;

    pthread_mutex_lock(&vjsdl->command_mutex);
    const int initialized = vjsdl->initialized;
    if(name && name_size > 0)
        snprintf(name, name_size, "%s",
                 vjsdl->renderer_name[0] ? vjsdl->renderer_name : "unknown");
    if(direct_lock_disabled)
        *direct_lock_disabled = vjsdl->direct_lock_disabled;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return initialized;
}

int vj_sdl_present_frame(void *ptr, VJFrame *frame)
{
    vj_sdl_present_timing_t timing;
    if(!vj_sdl_prepare_frame(ptr, frame, &timing))
        return 0;
    return vj_sdl_present_prepared(ptr, &timing);
}

int vj_sdl_set_identify(void *ptr, int display_number)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl || display_number < -1)
        return 0;
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->identify_overlay = display_number;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return 1;
}

int vj_sdl_set_display_target(void *ptr, int x, int y)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl || ((x == -1) != (y == -1)))
        return 0;
    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->target_display_x = x;
    vjsdl->target_display_y = y;
    if(vjsdl->initialized) {
        if(vjsdl->fs) {
            vjsdl->pending_fullscreen = 1;
        } else {
            vjsdl->pending_geometry = 1;
            vjsdl->pending_width = vjsdl->sw_scale_width > 0 ? vjsdl->sw_scale_width : vjsdl->width;
            vjsdl->pending_height = vjsdl->sw_scale_height > 0 ? vjsdl->sw_scale_height : vjsdl->height;
            vjsdl->pending_x = vjsdl->x;
            vjsdl->pending_y = vjsdl->y;
        }
    }
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return 1;
}

int vj_sdl_get_display_index(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl)
        return -1;
    pthread_mutex_lock(&vjsdl->command_mutex);
    const int display = vjsdl->display_index;
    pthread_mutex_unlock(&vjsdl->command_mutex);
    return display;
}

void vj_sdl_set_perf(void *ptr, void *perf)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(vjsdl)
        vjsdl->perf = (vj_perf_context*)perf;
}

uint8_t* vj_sdl_get_buffer(void *ptr, int index)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;

    if (!vjsdl)
        return NULL;

    if (index < 0 || index >= VIDEO_QUEUE_LEN)
        return NULL;

    return vjsdl->buf[index];
}

void vj_sdl_shutdown(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return;

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->initialized = 0;
    vjsdl->pending_geometry = 0;
    vjsdl->pending_fullscreen = -1;
    vjsdl->pending_grab = -1;
    vjsdl->display_index = -1;
    vjsdl->identify_overlay = 0;
    vjsdl->frame_prepared = 0;
    vjsdl->flags = 0;
    vjsdl->timing_generation++;
    pthread_mutex_unlock(&vjsdl->command_mutex);

    SDL_EnableScreenSaver();

    if(vjsdl->texture) {
        SDL_DestroyTexture(vjsdl->texture);
        vjsdl->texture = NULL;
    }
    if(vjsdl->renderer) {
        SDL_DestroyRenderer(vjsdl->renderer);
        vjsdl->renderer = NULL;
    }
    if(vjsdl->screen) {
        SDL_DestroyWindow(vjsdl->screen);
        vjsdl->screen = NULL;
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->owner_valid = 0;
    pthread_mutex_unlock(&vjsdl->command_mutex);
}

void vj_sdl_quit(void)
{
    SDL_Quit();
}

void vj_sdl_free(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;

    if(!vjsdl)
        return;

    if(vjsdl->texture || vjsdl->renderer || vjsdl->screen) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[DISPLAY] SDL resources survived renderer shutdown; releasing during final cleanup");
        if(vjsdl->texture)
            SDL_DestroyTexture(vjsdl->texture);
        if(vjsdl->renderer)
            SDL_DestroyRenderer(vjsdl->renderer);
        if(vjsdl->screen)
            SDL_DestroyWindow(vjsdl->screen);
    }

    if(vjsdl->scaler)
        yuv_free_swscaler(vjsdl->scaler);
    if(vjsdl->src_frame)
        free(vjsdl->src_frame);
    if(vjsdl->dst_frame)
        free(vjsdl->dst_frame);
    if(vjsdl->caption)
        free(vjsdl->caption);
    if(vjsdl->pixels)
        free(vjsdl->pixels);

    pthread_mutex_destroy(&vjsdl->command_mutex);
    free(vjsdl);
}

#endif
