/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
#ifdef HAVE_SDL_TTF
#include <veejay/vj-sdl-font.h>
#endif
#include <SDL/SDL_syswm.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libvjmem/vjmem.h>
#include <libel/vj-avcodec.h>
#include <libyuv/yuvconv.h>
#include <veejay/libveejay.h>
#include <string.h>
#include <stdlib.h>
//extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;
/*
static	int	wm_fullscreen(int action, Display *disp, Window *win) {
#endif
	XEvent xev;
	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.send_event = True;
	xev.xclient.message_type = XInternAtom( disp, 
					"_NET_WM_STATE", False );
	xev.xclient.window = win;
	xev.xclient.format = 32;
	xev.xclient.data.1[0] = action;
	xev.xclient.data.1[1] = XInternAtom( disp,
					"_NET_WM_STATE_FULLSCREEN",False );
	xev.xclient.data.1[2] = 0;
	xev.xclient.data.1[3] = 0;
	xev.xclient.data.1[4] = 0;
		
	if( !XSendEvent( disp, DefaultRootWindow( disp ), False,
				SubstructureRedirectMask |
				SubstructureNotifyMask, &xev ) )
	{
		veejay_msg(0, "WM Fullscreen state failed");
		return 0;
	}
	return 1;
}
*/
static void vj_sdl_move( vj_sdl *vjsdl , int scaled_width, int scaled_height, int x, int y )
{
	//@ sw_scale_width is misleading ; it lets SDL use the BES 
	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;

	int my_bpp = SDL_VideoModeOK( vjsdl->sw_scale_width, vjsdl->sw_scale_height,24,	
				vjsdl->flags[1] );
	if(!my_bpp)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Requested depth not supported");
		return;
	}

	vjsdl->screen = SDL_SetVideoMode( vjsdl->sw_scale_width, vjsdl->sw_scale_height,my_bpp,
				vjsdl->flags[1]);

	vjsdl->rectangle.x = 0;
	vjsdl->rectangle.y = 0;
	vjsdl->rectangle.w = scaled_width;
	vjsdl->rectangle.h = scaled_height;


	veejay_msg(VEEJAY_MSG_INFO, "Changed video window to size %d x %d",
			vjsdl->sw_scale_width,vjsdl->sw_scale_height);
}

vj_sdl *vj_sdl_allocate(int width, int height, int fmt, int use_key, int use_mouse, int show_cursor)
{
    vj_sdl *vjsdl = (vj_sdl *) vj_malloc(sizeof(vj_sdl));
    if (!vjsdl)
	return NULL;

    veejay_memset( vjsdl,0,sizeof(vj_sdl));

    vjsdl->flags[0] = 0;
    vjsdl->flags[1] = 0;
//   vjsdl->mouse_motion = 1;
  //  vjsdl->use_keyboard = 1;
    vjsdl->use_keyboard = use_key;
    vjsdl->mouse_motion = use_mouse;
    vjsdl->show_cursor = show_cursor;
    vjsdl->pix_format = SDL_YUY2_OVERLAY; 
    vjsdl->pix_fmt = fmt;
    vjsdl->width = width;
    vjsdl->height = height;
    vjsdl->frame_size = width * height;
    vjsdl->sw_scale_width = 0;
    vjsdl->sw_scale_height = 0;
    vjsdl->custom_geo[0] = -1;
    vjsdl->custom_geo[1] = -1;
    vjsdl->display = NULL;
	switch(fmt) {
	 //@ dont use YUVJ here - on blitting it to SDL it will be converted to YUV clamped for YUYJ422
	case FMT_422F:vjsdl->ffmpeg_pixfmt = PIX_FMT_YUV422P;break;
	case FMT_422:vjsdl->ffmpeg_pixfmt = PIX_FMT_YUV422P;break;
	}
    sws_template templ;	
    memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();
    VJFrame *src = yuv_yuv_template( NULL,NULL,NULL,vjsdl->width,vjsdl->height, vjsdl->ffmpeg_pixfmt );
    VJFrame *dst = yuv_yuv_template(  NULL,NULL,NULL,vjsdl->width,vjsdl->height,PIX_FMT_YUYV422);
    vjsdl->scaler = yuv_init_swscaler( src,dst, &templ, yuv_sws_get_cpu_flags() );

    vjsdl->src_frame = (void*) src;
    vjsdl->dst_frame = (void*) dst;

    return vjsdl;
}

void vj_sdl_set_geometry(vj_sdl* sdl, int w, int h)
{
	sdl->custom_geo[0] = w;
	sdl->custom_geo[1] = h;
	if (sdl->custom_geo[0] != -1 && sdl->custom_geo[1]!=-1)
       	{
        	char exp_str[100];
		sprintf(exp_str, "SDL_VIDEO_WINDOW_POS=%d,%d",sdl->custom_geo[0],
			sdl->custom_geo[1]);
	 	if(putenv(exp_str)==0)
			veejay_msg(VEEJAY_MSG_DEBUG,"SDL geometry %d , %d",
				sdl->custom_geo[0],sdl->custom_geo[1]);
	}

}

void vj_sdl_resize( vj_sdl *vjsdl , int scaled_width, int scaled_height, int fs )
{
	//@ sw_scale_width is misleading ; it lets SDL use the BES 
	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;

	int my_bpp = SDL_VideoModeOK( vjsdl->sw_scale_width, vjsdl->sw_scale_height,24,	
				vjsdl->flags[fs] );
	if(!my_bpp)
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Requested depth not supported");
		return;
	}

	vjsdl->screen = SDL_SetVideoMode( vjsdl->sw_scale_width, vjsdl->sw_scale_height,my_bpp,
				vjsdl->flags[fs]);

	vjsdl->rectangle.x = 0;
	vjsdl->rectangle.y = 0;
	vjsdl->rectangle.w = scaled_width;
	vjsdl->rectangle.h = scaled_height;


	veejay_msg(VEEJAY_MSG_INFO, "Changed video window to size %d x %d",
			vjsdl->sw_scale_width,vjsdl->sw_scale_height);
}


int vj_sdl_init(int ncpu, vj_sdl * vjsdl, int scaled_width, int scaled_height, const char *caption, int show, int fs, float fps)
{
	uint8_t *sbuffer;
	char name[100];
	int i = 0;
	const int bpp = 24;
	const SDL_VideoInfo *info = NULL;
	SDL_SysWMinfo  wminfo;

	if (!vjsdl)
		return 0;

	veejay_memset( &wminfo, 0, sizeof(SDL_SysWMinfo));


	/* dont overwrite environment settings, but export if they are not set already */

	setenv( "SDL_VIDEO_YUV_DIRECT", "1", 0 );
	setenv( "SDL_VIDEO_HWACCEL", "1", 0 );

	char *hw_env = getenv("SDL_VIDEO_HWACCEL");
	int hw_on = 1;
	if(hw_env)
	{
		hw_on = atoi(hw_env);	
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env SDL_VIDEO_HWACCEL=[0|1] not set");
	}

	int extra_flags = (ncpu > 1  ? SDL_ASYNCBLIT : 0 );
	int extra_fs_flags = 0;
	int manual_positioning = 0;
	/* setup with no window, scale to some wxh+0x0 on fs
	 */
	char *veejay_screen_geom = getenv( "VEEJAY_SCREEN_GEOMETRY" );
	char *veejay_screen_size  = getenv( "VEEJAY_SCREEN_SIZE" );
	if( veejay_screen_geom && veejay_screen_size ) {
		extra_flags = extra_flags | SDL_NOFRAME;
		manual_positioning = 1;
	} else {
		extra_fs_flags = SDL_FULLSCREEN;
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_SCREEN_GEOMETRY and VEEJAY_SCREEN_SIZE not set");
	}
#ifdef HAVE_SDL_TTF
	vjsdl->font = vj_sdl_font_init();
	if(vjsdl->font == NULL ) 
		return 0;
#endif

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "%s", SDL_GetError());
		veejay_msg(VEEJAY_MSG_INFO, "\tHint: 'export SDL_VIDEODRIVER=x11'");
		return 0;
	}

//FIXME
	if( hw_on == 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Setting up for software emulation");
		vjsdl->flags[0] = SDL_SWSURFACE | SDL_ANYFORMAT | extra_flags;
		vjsdl->flags[1] = SDL_SWSURFACE | SDL_ANYFORMAT | extra_flags | extra_fs_flags;
	//	vjsdl->flags[1] = SDL_SWSURFACE | SDL_FULLSCREEN | SDL_ANYFORMAT | extra_flags;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Setting up SDL with Hardware Acceleration");
		vjsdl->flags[0] = SDL_HWSURFACE | SDL_DOUBLEBUF | extra_flags;
		vjsdl->flags[1] = SDL_HWSURFACE | SDL_DOUBLEBUF | extra_flags | extra_fs_flags;
	//	vjsdl->flags[1] = SDL_HWSURFACE | SDL_FULLSCREEN | SDL_DOUBLEBUF | extra_flags;
	}

	if (vjsdl->custom_geo[0] != -1 && vjsdl->custom_geo[1]!=-1)
       	{
        	char exp_str[100];
		sprintf(exp_str, "SDL_VIDEO_WINDOW_POS=%d,%d",vjsdl->custom_geo[0],
			vjsdl->custom_geo[1]);
	 	if(putenv(exp_str)==0)
			veejay_msg(VEEJAY_MSG_DEBUG,"SDL geometry %d , %d",
				vjsdl->custom_geo[0],vjsdl->custom_geo[1]);
	}

	if (scaled_width)
		vjsdl->sw_scale_width = scaled_width;
	if (scaled_height)
		vjsdl->sw_scale_height = scaled_height;


	int ms = ( 1.0 / fps ) * 1000;

	SDL_EnableKeyRepeat( SDL_DEFAULT_REPEAT_DELAY, ms  ); //@ default 100

	info = SDL_GetVideoInfo();


	veejay_msg(VEEJAY_MSG_DEBUG, "Video output driver: SDL");
	if( info->hw_available == 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "(SDL) Hardware surface not available");
	}
	veejay_msg(VEEJAY_MSG_DEBUG, " window manager = %s",
			(info->wm_available ? "Yes" : "No" ));	
	if( info->blit_hw == 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "(SDL) No BLIT acceleration");
	}
	veejay_msg(VEEJAY_MSG_DEBUG, " Software surface: %s",
			( info->blit_sw ? "Yes" : "No" ) );
	veejay_msg(VEEJAY_MSG_DEBUG, " Preferred depth: %d bits/pixel", info->vfmt->BitsPerPixel);
 
	int my_bpp = SDL_VideoModeOK( vjsdl->sw_scale_width, vjsdl->sw_scale_height,bpp,	
				vjsdl->flags[fs] );
	if(!my_bpp)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Requested depth of %d bits/pixel not supported for %dx%d",bpp,vjsdl->sw_scale_width,vjsdl->sw_scale_height);
		return 0;
	}

	vjsdl->screen = SDL_SetVideoMode( vjsdl->sw_scale_width, vjsdl->sw_scale_height,my_bpp,
				vjsdl->flags[fs]);

    	if (!vjsdl->screen)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "%s", SDL_GetError());
		return 0;
    	}

	veejay_msg(VEEJAY_MSG_DEBUG, " Output dimensions: %d x %d",
			vjsdl->sw_scale_width, vjsdl->sw_scale_height );

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
  
	SDL_WM_GrabInput( SDL_GRAB_OFF );
  
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

#ifndef X_DISPLAY_MISSING
	if( vjsdl->display )
		x11_disable_screensaver( vjsdl->display );
#endif
	veejay_msg(VEEJAY_MSG_DEBUG, "Using Video Driver %s", name );

	veejay_msg(VEEJAY_MSG_INFO, "Initialized %s SDL video overlay (%dx%d), %s",
		( vjsdl->pix_format == SDL_YV12_OVERLAY ? "YV12" : "YUYV"),
		  scaled_width, scaled_height,
	    ( vjsdl->yuv_overlay->hw_overlay ? "using Hardware Acceleration" : "not using Hardware Acceleration"));

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
	vjsdl->display = NULL;
	if(SDL_GetWMInfo(&wminfo))
	{
		if( wminfo.subsystem == SDL_SYSWM_X11 )
		{
			vjsdl->display = wminfo.info.x11.display;
		
		}
	/*	XWindowAttributes attr;
		Display *disp;
		Window win,dum;
		int x=0,y=0,w=0,h=0,gravity=0;
		disp = wminfo.info.x11.display;
		win  = wminfo.info.x11.window;
		XSync(disp,0);
		XGetWindowAttributes(disp,win, &attr);
		XTranslateCoordinates(disp,win,attr.root,&w,&h,&x,&y, &dum);
		gravity = attr.win_gravity;*/

		int screen = DefaultScreen( vjsdl->display );

		if( manual_positioning ) {
			int new_w,new_h,new_x=0,new_y=0;
			int vid_w=0,vid_h=0;
			int use_geom = 0;
			int use_size = 0;
			int offset_x=0,offset_y=0;
			if( veejay_screen_geom )
			if( sscanf( veejay_screen_geom, "%dx%d+%dx%d",&new_w,&new_h,&new_x,&new_y) == 4 ) 
				use_geom = 1;

			if( veejay_screen_size )
			if( sscanf( veejay_screen_size,"%dx%d",&vid_w,&vid_h) == 2 ) 
				use_size = 1;

			if( veejay_screen_size && !use_size ) {
				veejay_msg(VEEJAY_MSG_ERROR,
						"Invalid syntax for VEEJAY_SCREEN_SIZE, use \"<N>x<N>\"");
				veejay_msg(VEEJAY_MSG_ERROR,
						" for example: VEEJAY_SCREEN_SIZE=1024x768");
			}

			if(!use_size) {
				veejay_msg(VEEJAY_MSG_WARNING, 
						"Warning, will use %dx%d for Fullscreen mode", DisplayWidth( vjsdl->display,screen),
							DisplayHeight( vjsdl->display,screen) );

			}
			if( veejay_screen_geom && !use_geom) {
				veejay_msg(VEEJAY_MSG_ERROR,
						"Invalid syntax for VEEJAY_SCREEN_GEOMETRY, use\"<Width>x<Height>+<Offset X>x<Offset Y>\"");
				veejay_msg(VEEJAY_MSG_ERROR,
						" for example: VEEJAY_SCREEN_GEOMETRY=2624x1024+1600+0 for TwinView/One Big Desktop");
				veejay_msg(VEEJAY_MSG_ERROR,
						"              VEEJAY_SCREEN_GEOMETRY=0x0+0+0 for Single Screen Desktops");
			}


			if( fs == 1 ) {
				scaled_width = DisplayWidth( vjsdl->display,screen);
				scaled_height= DisplayHeight( vjsdl->display,screen);
			/*	if( use_geom ) {
					scaled_width = new_w;
					scaled_height = new_h;
				}*/
			}
			if( use_geom ) {
				offset_x = new_x;
				offset_y = new_y;
				veejay_msg(VEEJAY_MSG_DEBUG, "VEEJAY_SCREEN_GEOMETRY: %dx%d+%dx%d", new_w,new_h,new_x,new_y);
			}

			if( use_size ) {
				scaled_width = vid_w;
				scaled_height = vid_h;
				veejay_msg(VEEJAY_MSG_DEBUG, "VEEJAY_SCREEN_SIZE: %dx%d", vid_w,vid_h);
			}

			veejay_msg(VEEJAY_MSG_INFO, "Size of video window: %dx%d", scaled_width,scaled_height);
			veejay_msg(VEEJAY_MSG_INFO, "Position            : %dx%d", new_x, new_y );
			veejay_msg(VEEJAY_MSG_INFO, "Display size        : %dx%d", DisplayWidth(vjsdl->display,screen),
										   DisplayHeight(vjsdl->display,screen));
			
			if( wminfo.subsystem == SDL_SYSWM_X11 )
			{
				Window rootwin;
				Window parentwin;
				Window *children;
				unsigned int children_count;
				XQueryTree( wminfo.info.x11.display,
					    wminfo.info.x11.window,
					    &rootwin,
					    &parentwin,
					    &children,
					    &children_count );
				wminfo.info.x11.lock_func();
				XMoveWindow( wminfo.info.x11.display, parentwin, offset_x,offset_y );
				XResizeWindow( wminfo.info.x11.display, parentwin, scaled_width,scaled_height);
				wminfo.info.x11.unlock_func();
				if(children) free(children);
			}
			vj_sdl_move( vjsdl, scaled_width,scaled_height,new_x,new_y );
		}
	}

    	SDL_WM_SetCaption(caption, NULL);

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


	vjsdl->fs = fs;

	SDL_EnableKeyRepeat( SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL );

 	return 1;
}

int	vj_sdl_screen_w( vj_sdl *vjsdl )
{
	return vjsdl->screen->w;
}
int	vj_sdl_screen_h( vj_sdl *vjsdl )
{
	return vjsdl->screen->h;
}

void	vj_sdl_set_title( const char *caption )
{
	SDL_WM_SetCaption(caption, NULL);
}

void	vj_sdl_grab(vj_sdl *vjsdl, int status)
{
	SDL_WM_GrabInput( (status==1? SDL_GRAB_ON : SDL_GRAB_OFF) );
	veejay_msg(VEEJAY_MSG_DEBUG, "%s", status == 1 ? "Released mouse focus":
						"Grabbed mouse focus");
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

uint8_t	*vj_sdl_get_yuv_overlay(vj_sdl *vjsdl )
{
	return vjsdl->yuv_overlay->pixels[0];
}

void	vj_sdl_flip( vj_sdl *vjsdl )
{
	SDL_DisplayYUVOverlay( vjsdl->yuv_overlay, &(vjsdl->rectangle));
}

int vj_sdl_update_yuv_overlay(vj_sdl * vjsdl, uint8_t ** yuv420)
{
	if (!vj_sdl_lock(vjsdl))
		return 0;
#ifdef HAVE_SDL_TTF
	if( veejay_log_to_ringbuffer() ) {
		vj_sdl_draw_to_buffer( vjsdl->font, vjsdl->width, vjsdl->height );
		vj_sdl_font_logging( vjsdl->font, yuv420, vjsdl->width, vjsdl->height );
	}
#endif
	VJFrame *src_frame = (VJFrame*) vjsdl->src_frame;
	VJFrame *dst_frame = (VJFrame*) vjsdl->dst_frame;

	src_frame->data[0] = yuv420[0];
	src_frame->data[1] = yuv420[1];
	src_frame->data[2] = yuv420[2];
	dst_frame->data[0] = vjsdl->yuv_overlay->pixels[0];

	yuv_convert_and_scale_packed( vjsdl->scaler, vjsdl->src_frame,dst_frame );

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
#ifdef HAVE_SDL_TTF
	if( vjsdl->font ) 
		vj_sdl_font_free(vjsdl->font);
#endif
	if( vjsdl->yuv_overlay)
 	   SDL_FreeYUVOverlay(vjsdl->yuv_overlay);
	if( vjsdl->scaler )
	   yuv_free_swscaler(vjsdl->scaler);
	if( vjsdl->src_frame )
	   free(vjsdl->src_frame );
	if( vjsdl->dst_frame )
	   free(vjsdl->dst_frame );
	free(vjsdl);
}
#endif
