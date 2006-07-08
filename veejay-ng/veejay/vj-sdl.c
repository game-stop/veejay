/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
 * Output in YUY2 always
 * Seems that YUV 4:2:0 (YV12) overlays have problems with multiple SDL video window
 */
#include <config.h>
#ifdef HAVE_SDL
#include <veejay/vj-sdl.h>
#include <veejay/defs.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <libel/vj-avcodec.h>
#include <string.h>
#include <stdlib.h>
//extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;


vj_sdl *vj_sdl_allocate(int width, int height, int fmt)
{
    vj_sdl *vjsdl = (vj_sdl *) malloc(sizeof(vj_sdl));
    if (!vjsdl)
	return NULL;
    vjsdl->flags[0] = 0;
    vjsdl->flags[1] = 0;
    vjsdl->mouse_motion = 0;
    vjsdl->use_keyboard = 1;
    vjsdl->pix_format = SDL_YUY2_OVERLAY; // have best quality by default
    vjsdl->width = width;
    vjsdl->height = height;
    vjsdl->sw_scale_width = 0;
    vjsdl->sw_scale_height = 0;
    vjsdl->custom_geo[0] = -1;
    vjsdl->custom_geo[1] = -1;
    vjsdl->show_cursor = 0;
    vjsdl->fs = 0;
    return vjsdl;
}

void vj_sdl_set_geometry(vj_sdl* sdl, int w, int h)
{
	sdl->custom_geo[0] = w;
	sdl->custom_geo[1] = h;

}


int vj_sdl_init(vj_sdl * vjsdl, int scaled_width, int scaled_height, const char *caption, int show, int fs)
{
	uint8_t *sbuffer;
	char name[100];
	int i = 0;
	const int bpp = 24;
	const SDL_VideoInfo *info = NULL;
	if (!vjsdl)
		return 0;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", SDL_GetError());
		veejay_msg(VEEJAY_MSG_INFO, "\tHint: 'export SDL_VIDEODRIVER=x11'");
		return 0;
	}

	/* dont overwrite environment settings */
//	setenv( "SDL_VIDEO_YUV_DIRECT", "1", 0 );
//	setenv( "SDL_VIDEO_HWACCEL", "1", 0 );

	char *hw_env = getenv("SDL_VIDEO_HWACCEL");
	int hw_on = 0;
	if(hw_env)
	{
		char *val = strtok(hw_env, "=");
		hw_on = val ? atoi(val): 0;	
	}

	if( hw_on == 0 )
	{
		vjsdl->flags[0] = SDL_SWSURFACE | SDL_ASYNCBLIT | SDL_ANYFORMAT | SDL_RESIZABLE;
		vjsdl->flags[1] = SDL_SWSURFACE | SDL_FULLSCREEN | SDL_ASYNCBLIT |SDL_ANYFORMAT;
	}
	else
	{
		vjsdl->flags[0] = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_DOUBLEBUF | SDL_RESIZABLE;
		vjsdl->flags[1] = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_FULLSCREEN | SDL_DOUBLEBUF;
	}


	if (vjsdl->custom_geo[0] != -1 && vjsdl->custom_geo[1]!=-1)
       	{
        	char exp_str[100];
		sprintf(exp_str, "SDL_VIDEO_WINDOW_POS=%d,%d",vjsdl->custom_geo[0],
			vjsdl->custom_geo[1]);
	 	(void) putenv(exp_str);
	}

	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;


	SDL_EnableKeyRepeat( SDL_DEFAULT_REPEAT_DELAY, 100 );

	info = SDL_GetVideoInfo();

	veejay_msg(VEEJAY_MSG_INFO, "Video output driver: SDL");
	veejay_msg(VEEJAY_MSG_INFO, "\tHardware acceleration       : %s",
			(info->hw_available ? "Yes" : "No"));
	veejay_msg(VEEJAY_MSG_INFO, "\tWindow manager              : %s",
			(info->wm_available ? "Yes" : "No" ));
	veejay_msg(VEEJAY_MSG_DEBUG, "\tBLIT acceleration          : %s ",
			( info->blit_hw ? "Yes" : "No" ) );
	veejay_msg(VEEJAY_MSG_INFO, "\tSoftware surface            : %s",
			( info->blit_sw ? "Yes" : "No" ) );
	veejay_msg(VEEJAY_MSG_INFO, "\tVideo memory                : %dKB ", info->video_mem );
	veejay_msg(VEEJAY_MSG_INFO, "\tPreferred depth:            : %d bits/pixel", info->vfmt->BitsPerPixel);

	
	int my_bpp = SDL_VideoModeOK( vjsdl->sw_scale_width, vjsdl->sw_scale_height,bpp,	
				vjsdl->flags[fs] );
	if(!my_bpp)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Requested depth of 24 bits/pixel not supported");
		return 0;
	}

	vjsdl->screen = SDL_SetVideoMode( vjsdl->sw_scale_width, vjsdl->sw_scale_height,my_bpp,
				vjsdl->flags[fs]);


    	if (!vjsdl->screen)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", SDL_GetError());
		return 0;
    	}

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
    

 	 vjsdl->yuv_overlay = SDL_CreateYUVOverlay(vjsdl->width,
					      vjsdl->height,
					      vjsdl->pix_format,
					      vjsdl->screen);

	if (!vjsdl->yuv_overlay)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", SDL_GetError());
		return 0;
    	} 
	
	SDL_VideoDriverName( name, 100 );
	veejay_msg(VEEJAY_MSG_INFO, "\tUsing Video Driver          :%s", name );

	veejay_msg(VEEJAY_MSG_INFO, "\tSDL video dimensions (%dx%d+%dx%d)",
			scaled_width, scaled_height, 
			vjsdl->custom_geo[0] < 0 ? 0 : vjsdl->custom_geo[0],
			vjsdl->custom_geo[0] < 0 ? 0 : vjsdl->custom_geo[1] ); 
		
   	vjsdl->rectangle.x = 0;
	vjsdl->rectangle.y = 0;
	vjsdl->rectangle.w = scaled_width;
	vjsdl->rectangle.h = scaled_height;

	if (!vj_sdl_lock(vjsdl))
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant lock SDL Surface");
		return 0;
	}

	sbuffer = (uint8_t *) vjsdl->screen->pixels;
	for (i = 0; i < vjsdl->screen->h; ++i)
	{
		memset(sbuffer, (i * 255) / vjsdl->screen->h,
	        vjsdl->screen->w * vjsdl->screen->format->BytesPerPixel);
		sbuffer += vjsdl->screen->pitch;
    	}

    	//SDL_WM_SetCaption(caption, "0000000");
    	if (!vj_sdl_unlock(vjsdl))
		return 0;


    	/*
       	we can draw something on the raw surface.
     	*/

   	if(show)
	{
    		SDL_UpdateRect(vjsdl->screen, 0, 0, vjsdl->rectangle.w,
			vjsdl->rectangle.h);
	}

 	return 1;
}

void vj_sdl_show(vj_sdl *vjsdl) {
	SDL_UpdateRect(vjsdl->screen,0,0,vjsdl->rectangle.w, vjsdl->rectangle.h);
}

int vj_sdl_lock(vj_sdl * vjsdl)
{
   if (SDL_MUSTLOCK(vjsdl->screen)) {
	if (SDL_LockSurface(vjsdl->screen) < 0) {
	    sprintf(vjsdl->last_error, "%s", SDL_GetError());
	    return 0;
	}
    }
    if (SDL_LockYUVOverlay(vjsdl->yuv_overlay) < 0) {
	sprintf(vjsdl->last_error, "%s", SDL_GetError());
	return 0;
    }
    return 1;
}

int vj_sdl_unlock(vj_sdl * vjsdl)
{
    if (SDL_MUSTLOCK(vjsdl->screen)) {
	SDL_UnlockSurface(vjsdl->screen);
    }
    SDL_UnlockYUVOverlay(vjsdl->yuv_overlay);
    return 1;
}

int vj_sdl_update_yuv_overlay(vj_sdl * vjsdl, void *data)
{
	VJFrame *frame = (VJFrame*) data;
	if (!vj_sdl_lock(vjsdl))
		return 0;

	switch(frame->format)
	{
		case FMT_420:
			yuv420p_to_yuv422( frame->data, vjsdl->yuv_overlay->pixels[0],frame->width,frame->height);
		break;
		case FMT_422:
			yuv422_to_yuyv( frame->data, vjsdl->yuv_overlay->pixels[0], frame->width,frame->height);
		break;
		case FMT_444:
			yuv444_to_yuyv( frame->data, vjsdl->yuv_overlay->pixels[0], frame->width, frame->height);
		break;
		default:
			return 0;
			break;
	}

	if (!vj_sdl_unlock(vjsdl))
		return 0;

	SDL_DisplayYUVOverlay(vjsdl->yuv_overlay, &(vjsdl->rectangle));
	
	return 1;
}


void	vj_sdl_quit()
{
	SDL_Quit();
}

void vj_sdl_free(vj_sdl * vjsdl)
{
	SDL_FreeYUVOverlay(vjsdl->yuv_overlay);
	free( vjsdl );
}

void	vj_sdl_event_handle( vj_sdl *sdl, SDL_Event event )
{
	if( event.type == SDL_VIDEORESIZE )
	{
		sdl->sw_scale_width = event.resize.w;
		sdl->sw_scale_height = event.resize.h;
		sdl->screen = SDL_SetVideoMode( sdl->sw_scale_width, sdl->sw_scale_height,0,
				sdl->flags[0]);
  		sdl->rectangle.x = 0;
		sdl->rectangle.y = 0;
		sdl->rectangle.w = sdl->sw_scale_width;
		sdl->rectangle.h = sdl->sw_scale_height;
		SDL_UpdateRect(sdl->screen, 0, 0, sdl->rectangle.w,
			sdl->rectangle.h);
	}
}

int	vj_sdl_set_fullscreen( vj_sdl *sdl, int status )
{
	int w = sdl->screen->w;
	int h = sdl->screen->h;
	int bpp = 24;

	if( sdl->screen->flags & SDL_FULLSCREEN && status == 1 )
		return 0;
	if( !(sdl->screen->flags & SDL_FULLSCREEN) && status == 0 )
		return 0;
	
	int my_bpp = SDL_VideoModeOK( w, h,bpp,	sdl->flags[status] );
	if(!my_bpp)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Requested depth of 24 bits/pixel not supported");
		return 0;
	}

	sdl->screen = SDL_SetVideoMode( w, h,my_bpp,
				sdl->flags[status]);

	sdl->fs = status;
	
	return 1;	
}

int	vj_sdl_resize_window ( vj_sdl *sdl, int w, int h, int x , int y )
{
	int my_bpp = SDL_VideoModeOK( w, h,24,	sdl->flags[sdl->fs] );
	if(!my_bpp)
		return 0;

	sdl->sw_scale_width = w;
	sdl->sw_scale_height = h;
	sdl->screen = SDL_SetVideoMode( sdl->sw_scale_width, sdl->sw_scale_height,0,
				sdl->flags[sdl->fs]);
 	sdl->rectangle.x = 0;
	sdl->rectangle.y = 0;
	sdl->rectangle.w = sdl->sw_scale_width;
	sdl->rectangle.h = sdl->sw_scale_height;
	SDL_UpdateRect(sdl->screen, 0, 0, sdl->rectangle.w,
			sdl->rectangle.h);

	return 1;
}
#endif
