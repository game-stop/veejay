/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <libvjmsg/vj-msg.h>

#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#define DECO (1L << 1)

static	int	screen_saver_[4];

#ifdef HAVE_XDPMS
static  CARD16  state_;
static  BOOL    onoff_;
#endif

void	x11_misc_init()
{
	memset(&screen_saver_,0, sizeof(screen_saver_));
#ifdef HAVE_XDPMS
	memset(&state_, 0,sizeof(state_));
	memset(&onoff_, 0,sizeof(onoff_));
#endif
}

void	x11_disable_screensaver( void *display )
{
//	XGetScreenSaver( d, &screen_saver_[0], &screen_saver_[1],
//			&screen_saver_[2], &screen_saver_[3] );

	if( screen_saver_[0] )
	{
		//XSetScreenSaver( d, 0, screen_saver_[1], screen_saver_[2],screen_saver_[3] );
	}

#ifdef HAVE_XDPMS
	int n = 0;
	if( DPMSQueryExtension( d, &n, &n ) )
	{
		DPMSInfo( d, &state_, &ononff_ );
		DPMSDisable( d );
	}
#endif
}

void	x11_enable_screensaver( void *display )
{
	if( screen_saver_[0] )
	{
	//	XSetScreenSaver( d, screen_saver_[0],screen_saver_[1],
	//			screen_saver_[2],screen_saver_[3] );
	}
#ifdef HAVE_XDPMS
	int n = 0;
	if( DPMSQueryExtension( d, &n, &n ) )
	{
		if( onoff_ )
			DPMSEnable( d );
	}
#endif
}

void	x11_misc_set_border( void *display, void *window, int status )
{
	
}

static	int	xinerama_x_ = 0;
static	int	xinerama_y_ = 0;
static  int	xinerama_user_selected_ = 0;
static	int	screen_w_ = 0;
static	int	screen_h_ = 0;

void	x11_move( void *display, void *window )
{
	Display *d = (Display*) display;
#ifdef HAVE_XINERAMA
	if( XineramaIsActive( d ) )
	{
		//XMoveWindow( d, w, xinerama_x_, xinerama_y_ );
	}
#endif
}

void	x11_user_select( int n )
{
	xinerama_user_selected_ = n;
}

void	x11_info(void *display)
{
	Display *d = (Display*) display;
#ifdef HAVE_XINERAMA

	int dis1,dis2;
	
	if( XineramaIsActive( d ) &&
	    XineramaQueryExtension( d, &dis1,&dis2) )
	{
		veejay_msg(VEEJAY_MSG_INFO, "\tUsing XFree Xinerama extension");
		
		int n = 0;
		XineramaScreenInfo *screens =
			XineramaQueryScreens( d, &n );
		
		if( xinerama_user_selected_ < 0 ||
		    xinerama_user_selected_ >= n )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "\tRequested screen number invalid");
			xinerama_user_selected_ = 0;
		}
				

		xinerama_x_ = screens[ xinerama_user_selected_ ].x_org;
		xinerama_y_ = screens[ xinerama_user_selected_ ].y_org;
		screen_w_   = screens[ xinerama_user_selected_ ].width;
		screen_h_   = screens[ xinerama_user_selected_ ].height;

		veejay_msg(VEEJAY_MSG_INFO, "\tUsing screen %d : %dx%d+%dx%d", 
				xinerama_user_selected_, screen_w_, screen_h_, xinerama_x_, xinerama_y_ );

	//	XFree( screens );
	}
#endif
}

#endif
