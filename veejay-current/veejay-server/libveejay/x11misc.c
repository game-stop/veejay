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
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

static int   ss_timeout, ss_interval, ss_prefer_blank, ss_allow_exposures;
static BOOL  dpms_was_enabled = False;

void x11_disable_screensaver(void *display)
{
    Display *d = (Display *)display;
    if (!d) return;

    XGetScreenSaver(d, &ss_timeout, &ss_interval, &ss_prefer_blank, &ss_allow_exposures);
    
	XSetScreenSaver(d, 0, ss_interval, ss_prefer_blank, ss_allow_exposures);

#ifdef HAVE_XDPMS
    int dummy;
    CARD16 state;
    if (DPMSQueryExtension(d, &dummy, &dummy)) {
        DPMSInfo(d, &state, &dpms_was_enabled);
        if (dpms_was_enabled) {
            DPMSDisable(d);
        }
    }
#endif
    XFlush(d);
}

void x11_enable_screensaver(void *display)
{
    Display *d = (Display *)display;
    if (!d) return;

    XSetScreenSaver(d, ss_timeout, ss_interval, ss_prefer_blank, ss_allow_exposures);

#ifdef HAVE_XDPMS
    int dummy;
    if (DPMSQueryExtension(d, &dummy, &dummy)) {
        if (dpms_was_enabled) {
            DPMSEnable(d);
        }
    }
#endif
    XFlush(d);
}