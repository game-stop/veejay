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

int	status_to_arr( char *status, int *array )
{
	return sscanf(status, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
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
		array + 16,
		array + 17,
	    array + 18,
	    array + 19,
		array + 20,
		array + 21,
		array + 22,
		array + 23,
		array + 24,
		array + 25,
		array + 26,
		array + 27,
		array + 28,
		array + 29,
		array + 30,
		array + 31,
		array + 32,
        array + 33,
        array + 34,
        array + 35,
        array + 36
    );
}

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
			*end++;
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

