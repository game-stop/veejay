/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */


/* the distortion effect comes originally from the demo effects collection,
   http://demo-effects.sourceforge.net
   by W.P. van Paassen - PLASMA

   the veejay version of this FX does not use a palette but instead pulls the color from the source image
   also, everything is parameterized
 */

/* Copyright (C) 2002 W.P. van Paassen - peter@paassen.tmfweb.nl

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "widthmirror.h"
#include "distort.h"

static int plasma_table[512];
static int plasma_pos1 = 0;
static int plasma_pos2 = 0;
static int *plasma_map = NULL;
static uint8_t *plasma_buf[4] = { NULL,NULL,NULL, NULL };

vj_effect *distortion_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    int i;
    float rad;
    for (i = 0; i < 512; i++) {
		rad = ((float) i * 0.703125f) * 0.0174532f;
		plasma_table[i] = myround( sinf(rad) * 1024.0f );
    }

    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->defaults[1] = 3;
	ve->defaults[2] = 3;
	ve->defaults[3] = 1;
	ve->defaults[4] = 9;
	ve->defaults[5] = 8;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 0xff;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 0xff;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 0xff;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 0xff;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 0xff;
	ve->limits[0][5] = 0;
	ve->limits[1][5] = 0xff;

    ve->description = "Distortion (Plasma)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Increment 1", "Increment 2", "Increment 3", "Increment 4", "Increment 5", "Increment 6"	);

    return ve;
}

int distortion_malloc(int w, int h)
{
	if( plasma_map == NULL ) {
		plasma_map = (int*) vj_calloc( sizeof(int) * RUP8( w * h ) );
		if(!plasma_map)
			return 0;
	}

	if( plasma_buf[0] == NULL ) {
		plasma_buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8( w * h * 3 ));
		if(plasma_buf[0] == NULL ) {
			free(plasma_map);
			return 0;
		}
		plasma_buf[1] = plasma_buf[0] + RUP8(w*h);
		plasma_buf[2] = plasma_buf[1] + RUP8(w*h);

		veejay_memset( plasma_buf[0], 0, RUP8(w*h));
		veejay_memset( plasma_buf[1], 128,RUP8(w*h));
		veejay_memset( plasma_buf[2], 128,RUP8(w*h));
	}

	return 1; 
}

void distortion_free()
{
	if( plasma_map ) {
		free(plasma_map);
	}
	plasma_map = NULL;

	if( plasma_buf[0] ) {
		free(plasma_buf[0]);
	}
	plasma_buf[0] = NULL;
	plasma_buf[1] = NULL;
	plasma_buf[2] = NULL;
	plasma_buf[3] = NULL;
}

void distortion_apply(VJFrame *frame, int inc_val1, int inc_val2, int inc_val3, int inc_val4, int inc_val5, int inc_val6 )
{

    int x, i, j;
    int tpos1 = 0, tpos2 = 0, tpos3 = 0, tpos4 = 0;
    const int z = 511;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	const unsigned int height = frame->height;
	const unsigned int width = frame->width;
	const int len = frame->len;

	int strides[4] = { len,len,len, 0 };

	vj_frame_copy( frame->data, plasma_buf, strides );

	for( i = 0; i < height; ++i ) {
		tpos1 = plasma_pos1 + inc_val1;
		tpos2 = plasma_pos2 + inc_val2;

		tpos3 &= z;
		tpos4 &= z;
	
		for (j = 0; j < width; ++j) {
		    tpos1 &= z;
		    tpos2 &= z;
		    x = (plasma_table[tpos1] + plasma_table[tpos2] +
					plasma_table[tpos3] + plasma_table[tpos4] );

			if( x < 0 ) { x = x * -1; }

		    plasma_map[i * width +j] = x;
			tpos1 += inc_val1;
		    tpos2 += inc_val2;
		}
		tpos4 += inc_val4;
		tpos3 += inc_val3;
 	}

	plasma_pos1 += inc_val5;
	plasma_pos2 += inc_val6;
	
	for ( i = 0; i < len; i ++ )
	{
		Y[i] = plasma_buf[0][ plasma_map[i] ];
		Cb[i]= plasma_buf[1][ plasma_map[i] ];
		Cr[i]= plasma_buf[2][ plasma_map[i] ];
	}

}
