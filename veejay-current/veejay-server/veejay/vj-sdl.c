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
#include <veejay/vj-sdl.h>
#include <SDL2/SDL.h>
#include <veejaycore/defs.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <veejaycore/vjmem.h>
#include <libel/vj-avcodec.h>
#include <veejaycore/yuvconv.h>
#include <veejay/libveejay.h>
#include <veejaycore/avcommon.h>
#include <string.h>
#include <stdlib.h>

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
    vj_sdl *vjsdl = (vj_sdl *) vj_calloc(sizeof(vj_sdl));
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
    vjsdl->borderless = borderless;

    sws_template templ;	
    memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();
    VJFrame *src = yuv_yuv_template( NULL,NULL,NULL,frame->width,frame->height, alpha_fmt_to_yuv(frame->format) );
    VJFrame *dst = yuv_yuv_template(  NULL,NULL,NULL,frame->width,frame->height,PIX_FMT_YUYV422);
    vjsdl->scaler = yuv_init_swscaler( src,dst, &templ, yuv_sws_get_cpu_flags() );

    vjsdl->src_frame = (void*) src;
    vjsdl->dst_frame = (void*) dst;

    size_t bufsize = frame->len * 2;

    vjsdl->pixels = (uint8_t*) vj_calloc(sizeof(uint8_t) * bufsize * 2); // continous dubblebuffer

    vjsdl->buf[0] = vjsdl->pixels;
    vjsdl->buf[1] = vjsdl->pixels + bufsize;

    fill_yuyv_black( vjsdl->buf[0], vjsdl->width, vjsdl->height);
    fill_yuyv_black( vjsdl->buf[1], vjsdl->width, vjsdl->height);

    return (void*) vjsdl;
}

void vj_sdl_resize( void *ptr ,int x, int y, int scaled_width, int scaled_height, int fs )
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;
	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;

    if( x >= 0 ) {
        vjsdl->x = x;
    }
    if( y >=0 ) {
        vjsdl->y = y;
    }

    if(vjsdl->screen) {
        SDL_DestroyWindow(vjsdl->screen);
    }

    int flags = (fs ? SDL_WINDOW_FULLSCREEN : (vjsdl->borderless ? SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS : SDL_WINDOW_OPENGL ) );

    vjsdl->screen = SDL_CreateWindow( vjsdl->caption, 
            (vjsdl->x >= 0 ? vjsdl->x : SDL_WINDOWPOS_UNDEFINED ),
            (vjsdl->y >= 0 ? vjsdl->y : SDL_WINDOWPOS_UNDEFINED ),
            vjsdl->sw_scale_width, vjsdl->sw_scale_height, flags );

	veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Changed video window to size %d x %d, position x=%d, y=%d",
			vjsdl->sw_scale_width,vjsdl->sw_scale_height, vjsdl->x, vjsdl->y);
}

void vj_sdl_get_position( void *ptr, int *x, int *y )
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;
    *x = vjsdl->x;
    *y = vjsdl->y;
}

int vj_sdl_init(void *ptr, int x, int y, int scaled_width, int scaled_height, char *caption, int show, int fs, int vjfmt, float fps, double *vsync)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;
	int i = 0;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s", SDL_GetError());
		return 0;
	}

	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;
    if( x >= 0 ) {
        vjsdl->x = x;
    }
    if( y >=0 ) {
        vjsdl->y = y;
    }

    if( caption )
        vjsdl->caption = strdup(caption);

    // SDL2: key repeat behaviour has changed; measure interval or look at repeat value on keysym
	//int ms = ( 1.0 / fps ) * 1000;

    int flags = (fs ? SDL_WINDOW_FULLSCREEN : (vjsdl->borderless ? SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS: SDL_WINDOW_OPENGL ));

	vjsdl->screen = SDL_CreateWindow(vjsdl->caption, 
            (vjsdl->x >= 0 ? vjsdl->x : SDL_WINDOWPOS_UNDEFINED),
            (vjsdl->y >= 0 ? vjsdl->y : SDL_WINDOWPOS_UNDEFINED),
            vjsdl->sw_scale_width, vjsdl->sw_scale_height, flags );

    if(!vjsdl->screen)
    {
		veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL window: %s", SDL_GetError());
		return 0;
    }

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
        int flags[3] = { SDL_RENDERER_PRESENTVSYNC, SDL_RENDERER_ACCELERATED, SDL_RENDERER_SOFTWARE };
        for( i = 0; i < num_renderers; i ++ ) {
            vjsdl->flags = flags[i];
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
            return 0;
        }
    }

    if(!vjsdl->renderer) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s", SDL_GetError());
        SDL_DestroyWindow( vjsdl->screen );
        return 0;
    }
    
    SDL_RendererInfo info;
    if(SDL_GetRenderDriverInfo(i, &info) == 0 ) {
        veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Using SDL driver %s", info.name);
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] The renderer uses hardware acceleration: %s", (info.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no");
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Present is synchronized with the refresh rate: %s", (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes": "no" );
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] Set VEEJAY_SDL_DRIVER to select another driver");
    }

    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "linear" );
 
    SDL_RenderSetLogicalSize( vjsdl->renderer, vjsdl->width, vjsdl->height );

    vjsdl->texture = SDL_CreateTexture( vjsdl->renderer, SDL_PIXELFORMAT_YUY2, SDL_TEXTUREACCESS_STREAMING, vjsdl->width,vjsdl->height);
    if(!vjsdl->texture) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Unable to create SDL texture: %s", SDL_GetError());
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

    vj_sdl_grab( vjsdl, 0 );

#if SDL_VERSION_ATLEAST(2,0,8)
    int sdlmode = (vj_is_full_range(vjfmt) ? SDL_YUV_CONVERSION_JPEG : SDL_YUV_CONVERSION_BT601 );

    if(sdlmode == SDL_YUV_CONVERSION_JPEG) {
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] SDL YUV conversion mode: JPEG (full range)");
    }
    if(sdlmode == SDL_YUV_CONVERSION_BT601) {
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY]  SDL YUV conversion mode: BT601 (limited range)");
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
    if(SDL_GetDisplayMode(display_index,0,&mode)) {
        int hz = (mode.refresh_rate > 0) ? mode.refresh_rate : 60;
        vsync_interval = 1.0 / (double) hz;
        *vsync = vsync_interval;
        veejay_msg(VEEJAY_MSG_DEBUG, "[DISPLAY] SDL V-Sync refresh rate is %f", vsync_interval);
    }

	return 1;
}

void vj_sdl_set_fullscreen(void *ptr, int enabled) {
    uint32_t flags = enabled ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    vj_sdl *vjsdl = (vj_sdl*) ptr;

    if (SDL_SetWindowFullscreen(vjsdl->screen, flags) == 0) {
        vjsdl->fs = enabled;
            
        if(enabled)
            vj_sdl_grab( vjsdl, 0 );
    } else {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] Fullscreen failed: %s", SDL_GetError());
    }
}

void vj_sdl_set_window_size(void *ptr, int w, int h, int x, int y) {
    vj_sdl *vjsdl = (vj_sdl*) ptr;
    if (!vjsdl || !vjsdl->screen) return;

    SDL_SetWindowSize(vjsdl->screen, w, h);
    
    SDL_SetWindowPosition(vjsdl->screen, x, y);

    SDL_RenderSetLogicalSize(vjsdl->renderer, vjsdl->width, vjsdl->height);

    vjsdl->sw_scale_width = w;
    vjsdl->sw_scale_height = h;
    
    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Window resized to %dx%d", w, h);
}

void    vj_sdl_enable_screensaver(void)
{
    SDL_EnableScreenSaver();
}

void	vj_sdl_grab(void *ptr, int status)
{
	SDL_SetRelativeMouseMode( (status==1? SDL_TRUE : SDL_FALSE) );
	veejay_msg(VEEJAY_MSG_DEBUG, "%s", status == 1 ? "[DISPLAY] Released mouse focus": "[DISPLAY] Grabbed mouse focus");
}

void vj_sdl_put_to_screen(void *ptr, uint8_t *pixels_to_render)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;

    if( SDL_UpdateTexture( vjsdl->texture, NULL, pixels_to_render, vjsdl->width * 2 ) != 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "[DISPLAY] %s" , SDL_GetError());
    }

    SDL_RenderClear( vjsdl->renderer );
    SDL_RenderCopy( vjsdl->renderer, vjsdl->texture, NULL,NULL );
    SDL_RenderPresent( vjsdl->renderer );   
}

void vj_sdl_preroll(void *ptr, int frame_count) {
        vj_sdl *vjsdl = (vj_sdl*) ptr;
    
    veejay_msg(VEEJAY_MSG_INFO, "[DISPLAY] Initializing GPU pipeline (Preroll %d frames)", frame_count);
    
    memset(vjsdl->pixels, 0x80, vjsdl->width * vjsdl->height * 2);

    for (int i = 0; i < frame_count; i++) {
        SDL_UpdateTexture(vjsdl->texture, NULL, vjsdl->pixels, vjsdl->width * 2);
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

void vj_sdl_convert_to_screen(void *ptr, VJFrame *frame_to_dsplay, uint8_t *pixels)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;

	VJFrame *dst_frame = (VJFrame*) vjsdl->dst_frame;
    dst_frame->data[0] = pixels;
	yuv_convert_and_scale_packed( vjsdl->scaler, frame_to_dsplay,dst_frame );


}

uint8_t* vj_sdl_get_buffer( void *ptr, int index ) {
    vj_sdl *vjsdl = (vj_sdl*) ptr;
    return vjsdl->buf[index];
}

void	vj_sdl_quit(void)
{
	SDL_Quit();
}

void vj_sdl_free(void *ptr)
{
    vj_sdl *vjsdl = (vj_sdl*) ptr;

    SDL_DestroyTexture( vjsdl->texture );
    SDL_DestroyRenderer( vjsdl->renderer );
    SDL_DestroyWindow( vjsdl->screen );

	if( vjsdl->scaler ) 
	   yuv_free_swscaler(vjsdl->scaler);

	if( vjsdl->src_frame )
	   free(vjsdl->src_frame );

	if( vjsdl->dst_frame )
	   free(vjsdl->dst_frame );

    if( vjsdl->caption)
        free(vjsdl->caption);

    if( vjsdl->pixels )
        free(vjsdl->pixels);
	
    free(vjsdl);
}
#endif
