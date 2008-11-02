/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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

#ifndef VJDV1394
#define VJDV1394


typedef struct
{
	int handle; 
	int map_size;
	uint8_t *map;
	int width;
	int height;
	int channel;
	int norm;
	int avail;
	int done;
	int index;
	int quality;
	void *decoder;
} vj_dv1394;

vj_dv1394*	vj_dv1394_init(void *el, int channel_nr, int quality);

void		vj_dv1394_close( vj_dv1394 *v );

int		vj_dv1394_read_frame( vj_dv1394 *v, uint8_t *frame[3] , uint8_t *audio, int fmt );

#endif
