/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include "yuv4mpeg_intern.h"
#include "yuv4mpeg.h"
#include "mpegconsts.h"
#include "mpegtimecode.h"

#include <veejaycore/vjmem.h>
#include <string.h>

void	generator_to_arr( char *line, int *array)
{
	char *p = line;
	int i = 0;
	while(*p) {
		char *end = NULL;
		if(p == NULL)
			break;
		array[i] = strtol(p, &end, 10 );
		if( end == p)
			break;

		while(*end == ' ' && end != NULL) {
			end++;
		}

		p = end;
		i++;
	}
}


char   *format_time(int pos, double fps)
{
	char temp[128];
    MPEG_timecode_t tc;
	y4m_ratio_t r = mpeg_conform_framerate(fps);
	mpeg_timecode(&tc,
		      pos,
                      mpeg_framerate_code(r),
		      fps );
    snprintf(temp,sizeof(temp),"%d:%2.2d:%2.2d:%2.2d",tc.h, tc.m, tc.s, tc.f );
    return strdup(temp);
}

char   *format_framenum(int pos) 
{
	char temp[64];
	snprintf(temp, sizeof(temp), "%8d", pos);
	return strdup(temp);
}
