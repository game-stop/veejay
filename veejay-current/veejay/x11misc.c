/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

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
	Display *d = (Display*) display;

	XGetScreenSaver( d, &screen_saver_[0], &screen_saver_[1],
			&screen_saver_[2], &screen_saver_[3] );

	if( screen_saver_[0] )
	{
		XSetScreenSaver( d, 0, screen_saver_[1], screen_saver_[2],screen_saver_[3] );
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
	Display *d = (Display*) display;

	if( screen_saver_[0] )
	{
		XSetScreenSaver( d, screen_saver_[0],screen_saver_[1],
				screen_saver_[2],screen_saver_[3] );
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
