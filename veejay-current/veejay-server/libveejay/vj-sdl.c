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
#include <stdlib.h>
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
    size_t total_size;

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

    /* SDL texture is YUYV/YUY2: 2 bytes per source pixel.  Allocate one
     * packed display buffer per queue slot; the renderer rotates through
     * VIDEO_QUEUE_LEN indices, not just two.
     */
    bufsize = (size_t) frame->width * (size_t) frame->height * 2u;
    total_size = bufsize * (size_t) VIDEO_QUEUE_LEN;

    vjsdl->pixels = (uint8_t*) vj_calloc(total_size);
    if (!vjsdl->pixels) {
        yuv_free_swscaler(vjsdl->scaler);
        free(src);
        free(dst);
        free(vjsdl);
        return NULL;
    }

    for (int i = 0; i < VIDEO_QUEUE_LEN; i++) {
        vjsdl->buf[i] = vjsdl->pixels + ((size_t)i * bufsize);
        fill_yuyv_black(vjsdl->buf[i], vjsdl->width, vjsdl->height);
    }

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

static int vj_sdl_is_owner(vj_sdl *vjsdl)
{
    return vjsdl && vjsdl->owner_valid &&
           pthread_equal(vjsdl->owner_thread, pthread_self());
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


    if( caption )
        vjsdl->caption = strdup(caption);

    // SDL2: key repeat behaviour has changed; measure interval or look at repeat value on keysym
	//int ms = ( 1.0 / fps ) * 1000;

    int flags = (fs ? SDL_WINDOW_FULLSCREEN : (vjsdl->borderless ? SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS: SDL_WINDOW_OPENGL ));

	vjsdl->screen = SDL_CreateWindow(vjsdl->caption, 
            (vjsdl->x != -1 ? vjsdl->x : SDL_WINDOWPOS_UNDEFINED),
            (vjsdl->y != -1 ? vjsdl->y : SDL_WINDOWPOS_UNDEFINED),
            vjsdl->sw_scale_width, vjsdl->sw_scale_height, flags );

    if(!vjsdl->screen)
    {
		veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL window: %s", SDL_GetError());
		return 0;
    }

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
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_SOFTWARE );
        } else if(strcasecmp("accelerated", sdl_driver) == 0 ) {
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_ACCELERATED );
        } else if (strcasecmp("vsync", sdl_driver) == 0 ) {
            vjsdl->renderer = SDL_CreateRenderer(vjsdl->screen, -1, SDL_RENDERER_PRESENTVSYNC );
        } else {
            veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Valid values for VEEJAY_SDL_DRIVER are: \"software\", \"accelerated\", \"vsync\"");
            SDL_DestroyWindow(vjsdl->screen);
            vjsdl->screen = NULL;
            return 0;
        }
    }

    if(!vjsdl->renderer) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s", SDL_GetError());
        SDL_DestroyWindow(vjsdl->screen);
        vjsdl->screen = NULL;
        return 0;
    }
    
    SDL_RendererInfo info;
    if(SDL_GetRendererInfo(vjsdl->renderer, &info) == 0 ) {
        veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Using SDL renderer %s", info.name);
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] The renderer uses hardware acceleration: %s", (info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no");
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Present is synchronized with the refresh rate: %s", (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes": "no" );
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Set VEEJAY_SDL_DRIVER to select another driver");
    }

    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "linear" );
 
    SDL_RenderSetLogicalSize( vjsdl->renderer, vjsdl->width, vjsdl->height );

    vjsdl->texture = SDL_CreateTexture( vjsdl->renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, vjsdl->width,vjsdl->height);
    if(!vjsdl->texture) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL texture: %s", SDL_GetError());
        SDL_DestroyRenderer(vjsdl->renderer);
        SDL_DestroyWindow(vjsdl->screen);
        vjsdl->renderer = NULL;
        vjsdl->screen = NULL;
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
    if(sdlmode == SDL_YUV_CONVERSION_JPEG) {
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] SDL YUV conversion mode: JPEG (full range)");
    }
    if(sdlmode == SDL_YUV_CONVERSION_AUTOMATIC) {
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] BT.601 for SD content, BT.709 for HD content (limited range)");
    }

    SDL_SetYUVConversionMode( sdlmode );
#else
    veejay_msg(VEEJAY_MSG_WARNING, "[DISPLAY] Please update SDL2 to a more recent version.");
#endif
    SDL_DisableScreenSaver();

    SDL_SetRenderDrawColor( vjsdl->renderer, 0,0,0,255 );
    SDL_RenderClear(vjsdl->renderer);
    SDL_RenderPresent(vjsdl->renderer);

    vjsdl->fs = fs;

    SDL_DisplayMode mode;
    int display_index = SDL_GetWindowDisplayIndex(vjsdl->screen);
    double vsync_interval = 1.0 / 60.0;

    if(vsync)
        *vsync = vsync_interval;

    if(display_index >= 0 && SDL_GetDisplayMode(display_index, 0, &mode) == 0) {
        int hz = (mode.refresh_rate > 0) ? mode.refresh_rate : 60;
        vsync_interval = 1.0 / (double) hz;
        if(vsync)
            *vsync = vsync_interval;
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] SDL V-Sync refresh interval is %f", vsync_interval);
    }

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
        SDL_SetWindowSize(vjsdl->screen, w, h);
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

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->x = current_x;
    vjsdl->y = current_y;
    vjsdl->sw_scale_width = actual_w;
    vjsdl->sw_scale_height = actual_h;
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

    if(!vjsdl || !vj_sdl_is_owner(vjsdl) || !vjsdl->screen)
        return 0;

    if(SDL_SetWindowFullscreen(vjsdl->screen, flags) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] Fullscreen failed: %s", SDL_GetError());
        return 0;
    }

    pthread_mutex_lock(&vjsdl->command_mutex);
    vjsdl->fs = enabled ? 1 : 0;
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
}

void    vj_sdl_enable_screensaver(void)
{
    SDL_EnableScreenSaver();
}

static int vj_sdl_present_pixels(vj_sdl *vjsdl, uint8_t *pixels_to_render)
{
    if(!vjsdl || !vj_sdl_is_owner(vjsdl))
        return 0;

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

    if(SDL_UpdateTexture(vjsdl->texture, NULL,
                         pixels_to_render,
                         vjsdl->width * 2) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL texture update failed: %s",
                   SDL_GetError());
        return 0;
    }

    if(SDL_RenderClear(vjsdl->renderer) != 0 ||
       SDL_RenderCopy(vjsdl->renderer, vjsdl->texture, NULL, NULL) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR,
                   "[DISPLAY] SDL render failed: %s", SDL_GetError());
        return 0;
    }

    SDL_RenderPresent(vjsdl->renderer);
    return 1;
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

        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Pushed warm-up frame %d", i);
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

int vj_sdl_present_frame(void *ptr, VJFrame *frame)
{
    vj_sdl *vjsdl = (vj_sdl*)ptr;
    if(!vjsdl || !vjsdl->buf[0])
        return 0;
    const uint64_t convert_start = vj_perf_now_ns();
    if(!vj_sdl_convert_frame(vjsdl, frame, vjsdl->buf[0]))
        return 0;
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_CONVERT,
                   convert_start, vj_perf_now_ns());
    const uint64_t present_start = vj_perf_now_ns();
    const int result = vj_sdl_present_pixels(vjsdl, vjsdl->buf[0]);
    vj_perf_record(vjsdl->perf, VJ_PERF_STAGE_UPLOAD_PRESENT,
                   present_start, vj_perf_now_ns());
    return result;
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
