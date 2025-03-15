/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

/* This effect was inspired by an article by Sqrt(-1) */
/* 08-22-02 Optimized by WP */
/* note that the code has not been fully optimized */

/* orignal code for RGB, define INTENSITY( r + b + g / 3 ),
   this effect works in YCbCr space now. */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "reflection.h"

typedef struct {
    short *reflect_aSin;
    int *reflection_map;
    int sin_index;
    int sin_index2; //  20
    uint8_t *reflection_buffer;
} reflection_t;

vj_effect *reflection_init(int width,int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 2;
    ve->defaults[1] = 5;
    ve->defaults[2] = 1;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->description = "Bump 2D";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Value 1", "Value 2", "Mode");
	return ve;
}

void *reflection_malloc(int width, int height)
{ 
    reflection_t *r = (reflection_t*) vj_calloc(sizeof(reflection_t));
    if(!r) {
        return NULL;
    }

    r->reflection_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * width);
    if(!r->reflection_buffer) {
        free(r);
        return NULL;
    }

	r->reflect_aSin = (short*) vj_malloc(sizeof(short) * width);
    if (!r->reflect_aSin) {
        free(r->reflection_buffer);
        free(r);
        return NULL;
    }

    r->reflection_map = (int*) vj_malloc(sizeof(int) * width * height);
    if (!r->reflection_map) {
        free(r->reflect_aSin);
        free(r->reflection_buffer);
        free(r);
        return NULL;
    }

    r->sin_index2 = 20;

	int i, x, y;
    float rad;
 
    for (i = 0; i < width; i++) {
		rad = (float) i * 0.0174532 * 0.703125;
		r->reflect_aSin[i] = (short) ((a_sin(rad) * 100.0) + 256.0);
    }
    
	for (x = 0; x < width; ++x) {
		for (y = 0; y < height; ++y) {
		    float xx = (x - 128) / 128.0;
		    float yy = (y - 128) / 128.0;
		    float zz = 1.0 - sqrt(xx * xx + yy * yy);
		    zz *= 255.0;
		    if (zz < 0.0)
			zz = 0.0;
		    r->reflection_map[ y * width + x] = (int) zz;
		}
    }

    return (void*) r;
}


void reflection_free(void *ptr) {
    reflection_t *r = (reflection_t*) ptr;
	if(r) {
		free(r->reflection_map);
    	free(r->reflection_buffer);
   	 	free(r);
	}
}


void reflection_apply(void *ptr, VJFrame *frame, int *args)
{
    reflection_t *r = (reflection_t*) ptr;
    int index1 = args[0];
    int index2 = args[1];
    int move = args[2];

    unsigned int normalx, normaly, x, y;
    unsigned int lightx, lighty, temp;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    uint8_t *row = frame->data[0] + width + 1;
    uint8_t *cbrow = frame->data[1] + width + 1;
	uint8_t *crrow = frame->data[2] + width + 1;
    uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *reflection_buffer = r->reflection_buffer;

    lightx = r->reflect_aSin[r->sin_index];
    lighty = r->reflect_aSin[r->sin_index2];


    if (!move) {
		r->sin_index = index1;
		r->sin_index2 = index2;
    } else {
		r->sin_index += index1;
		r->sin_index2 += index2;
    }
    r->sin_index &= 511;
    r->sin_index2 &= 511;

#pragma omp simd
    for (x = 0; x < width; x++) {
		reflection_buffer[x]= Y[x];
    }

    for (y = 1; y < height - 1; y++) {
		uint8_t p;
		temp = lighty - y;
		p = Y[x + (y * width)];

		for (x = 0; x < width; x++) {
		    int i1 = (int) p;
		    int i2 = Y[x + 1 + (y * width)];	/* deviate */
		    int i3 = (int) reflection_buffer[x];
		    normalx = i2 - i1 + lightx - x;
		    normaly = i1 - i3 + temp;
			normalx = (normalx < 0) ? 0 : (normalx > 255 ? 255 : normalx);
			normaly = (normaly < 0) ? 0 : (normaly > 255 ? 255 : normaly);
		    
			int pos = normaly * width + x;

			*row++ = r->reflection_map[pos];
            *cbrow++ = ((r->reflection_map[pos] * (Cb[x + 1 + (y * width)]-128)) >> 8) +128;
			*crrow++ = ((r->reflection_map[pos] * (Cr[x + 1 + (y * width)]-128)) >> 8) +128;

		    p = i2;
		    reflection_buffer[x] = i2;
		}
		*row += 2;
    }
}
