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
#include <config.h>
#ifdef HAVE_SDL
#include <veejay/vj-sdl.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-global.h>
#include <libvjmem/vjmem.h>
#include <veejay/vj-avcodec.h>
#include <string.h>
#include <stdlib.h>
//extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;


vj_sdl *vj_sdl_allocate(int width, int height, int fmt)
{
    vj_sdl *vjsdl = (vj_sdl *) malloc(sizeof(vj_sdl));
    if (!vjsdl)
	return NULL;
    vjsdl->use_yuv_direct = 0;
    vjsdl->use_yuv_hwaccel = 0;
    vjsdl->show_cursor = 0;
    vjsdl->mouse_motion = 0;
    vjsdl->use_keyboard = 1;
    if(fmt == FMT_420) 
    	vjsdl->pix_format = SDL_YV12_OVERLAY;
    else
	vjsdl->pix_format = SDL_YUY2_OVERLAY;
    vjsdl->pix_fmt = fmt;
    vjsdl->width = width;
    vjsdl->height = height;
    vjsdl->frame_size = width * height;
    vjsdl->sw_scale_width = 0;
    vjsdl->sw_scale_height = 0;
    vjsdl->custom_geo[0] = -1;
    vjsdl->custom_geo[1] = -1;
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
    int i = 0;
    if (!vjsdl)
	return 0;

//    if (vjsdl->use_yuv_direct == 1)
//	setenv("SDL_VIDEO_YUV_DIRECT", "1", 1);
  //  if (vjsdl->use_yuv_hwaccel == 1)
//	setenv("SDL_VIDEO_HWACCEL", "1", 1);

    if (vjsdl->custom_geo[0] != -1 && vjsdl->custom_geo[1]!=-1)
       {
         char exp_str[100];
	 sprintf(exp_str, "SDL_VIDEO_WINDOW_POS=%d,%d",vjsdl->custom_geo[0],
			vjsdl->custom_geo[1]);
	 if(putenv(exp_str)==0)
		veejay_msg(VEEJAY_MSG_DEBUG,"SDL geometry %d , %d",
			vjsdl->custom_geo[0],vjsdl->custom_geo[1]);
	}

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
	return 0;

    if (scaled_width)
	vjsdl->sw_scale_width = scaled_width;
    if (scaled_height)
	vjsdl->sw_scale_height = scaled_height;


	if(fs == 0 )
    vjsdl->screen =
	SDL_SetVideoMode(vjsdl->sw_scale_width, vjsdl->sw_scale_height, 0,
			 SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_DOUBLEBUF |
			 SDL_HWACCEL);
	else
   vjsdl->screen =
	SDL_SetVideoMode(vjsdl->sw_scale_width, vjsdl->sw_scale_height, 0,
			 SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_DOUBLEBUF |
			 SDL_HWACCEL | SDL_FULLSCREEN);
	

    if (!vjsdl->screen) {
	sprintf(vjsdl->last_error, "%s", SDL_GetError());
	return 0;
    }

    if (vjsdl->use_keyboard == 1) {
	SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
    } else {
	SDL_EventState(SDL_KEYDOWN, SDL_DISABLE);
    }

    if (vjsdl->mouse_motion == 1) {
	SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    } else {
	SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
    }

    if (vjsdl->show_cursor == 1) {
	SDL_ShowCursor(SDL_ENABLE);
    } else {
	SDL_ShowCursor(SDL_DISABLE);
    }

    vjsdl->yuv_overlay = SDL_CreateYUVOverlay(vjsdl->width,
					      vjsdl->height,
					      vjsdl->pix_format,
					      vjsdl->screen);

    if (!vjsdl->yuv_overlay) {
	sprintf(vjsdl->last_error, "%s", SDL_GetError());
	return 0;
    }

	veejay_msg(VEEJAY_MSG_INFO, "Initialized %s SDL video overlay (%dx%d), %s",
		( vjsdl->pix_format == SDL_YV12_OVERLAY ? "YV12" : "YUYV"),
		  scaled_width, scaled_height,
	    ( vjsdl->yuv_overlay->hw_overlay ? "using Hardware Acceleration" : "not using Hardware Acceleration"));

/*
    if (vjsdl->yuv_overlay->pitches[0] !=
	vjsdl->yuv_overlay->pitches[1] * 2
	|| vjsdl->yuv_overlay->pitches[0] !=
	vjsdl->yuv_overlay->pitches[2] * 2) {
	veejay_msg(VEEJAY_MSG_ERROR, "SDL return non YUV 420 overlay %d %d %d",
		vjsdl->yuv_overlay->pitches[0],
		vjsdl->yuv_overlay->pitches[1],
		vjsdl->yuv_overlay->pitches[2]);
//	return 0;
    }
*/

    vjsdl->rectangle.x = 0;
    vjsdl->rectangle.y = 0;
    vjsdl->rectangle.w = scaled_width;
    vjsdl->rectangle.h = scaled_height;

    if (!vj_sdl_lock(vjsdl))
	return 0;

    sbuffer = (uint8_t *) vjsdl->screen->pixels;
    for (i = 0; i < vjsdl->screen->h; ++i) {
	memset(sbuffer, (i * 255) / vjsdl->screen->h,
	       vjsdl->screen->w * vjsdl->screen->format->BytesPerPixel);
	sbuffer += vjsdl->screen->pitch;
    }

    SDL_WM_SetCaption(caption, "0000000");
    if (!vj_sdl_unlock(vjsdl))
	return 0;


    /*
       we can draw something on the raw surface.
     */

    if(show) {
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

int vj_sdl_update_yuv_overlay(vj_sdl * vjsdl, uint8_t ** yuv420)
{
    if (!vj_sdl_lock(vjsdl))
	return 0;
    if (!yuv420[0] || !yuv420[1] || !yuv420[2])
	return 0;

    if( vjsdl->pix_fmt == FMT_420)
    {
    // SDL needs IV12, which is equal to YUV420 but with Cb/Cr swapped 
    veejay_memcpy(vjsdl->yuv_overlay->pixels[0],yuv420[0],
	       vjsdl->frame_size);
    veejay_memcpy(vjsdl->yuv_overlay->pixels[1],yuv420[2],
	       vjsdl->frame_size / 4);
    veejay_memcpy(vjsdl->yuv_overlay->pixels[2],yuv420[1],
	       vjsdl->frame_size / 4);

    }
    else
    {
		yuv422_to_yuyv( yuv420, vjsdl->yuv_overlay->pixels[0], vjsdl->width,vjsdl->height);
/*
    int w = vjsdl->width;
    int h = vjsdl->height;
    int x,y;
    uint8_t *pixels = vjsdl->yuv_overlay->pixels[0];
    uint8_t *Y = yuv420[0];
    uint8_t *U = yuv420[1];
    uint8_t *V = yuv420[2]; // U Y V Y
 /// YUY2: Y0, U0, Y1 , V0  

	for(y = 0; y < h; y ++ )
	{
		Y = yuv420[0] + y * w;
		U = yuv420[1] + (y>>1) * w;
		V = yuv420[2] + (y>>1) * w;
		for(x=0; x < w; x+= 2)
		{
			*(pixels + 0) = Y[0];
			*(pixels + 1) = U[0];
			*(pixels + 2) = Y[1];
			*(pixels + 3) = V[0];

			pixels += 4;
			Y+=2;
			++U;
			++V;
		}
	}
*/
    }
    if (!vj_sdl_unlock(vjsdl))
	return 0;

    SDL_DisplayYUVOverlay(vjsdl->yuv_overlay, &(vjsdl->rectangle));

    return 1;
}


void vj_sdl_free(vj_sdl * vjsdl)
{
    SDL_FreeYUVOverlay(vjsdl->yuv_overlay);
    SDL_Quit();
}
#endif
