/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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

#include <stdio.h>
#include <utils/mpegconsts.h>
#include <utils/mpegtimecode.h>

int	status_to_arr( char *status, int *array )
{
	int n = sscanf(status, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
		array + 0,
		array + 1,
		array + 2,
		array + 3,
		array + 4,
		array + 5,
		array + 6,
		array + 7,
		array + 8,
		array + 9,
		array + 10,
		array + 11,
		array + 12,
		array + 13,
		array + 14,
		array + 15,
		array + 16 );
	return n;
}

char   *format_time(int pos, float fps)
{
        MPEG_timecode_t tc;
        if(pos==0)
                memset(&tc, 0, sizeof(tc));
        else
                mpeg_timecode( &tc, pos,
                        mpeg_framerate_code(
                                mpeg_conform_framerate( fps ) ), fps );

        char *tmp = (char*) malloc( 20 );
        snprintf(tmp, 20, "%2d:%2.2d:%2.2d:%2.2d",
                tc.h, tc.m, tc.s, tc.f );

        return tmp;
}

