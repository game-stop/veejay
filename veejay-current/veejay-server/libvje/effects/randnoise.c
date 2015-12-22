/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <elburg@hio.hen.nl>
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

/*
 * Add pseudo random noise to image
 */

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "common.h"
#include "randnoise.h"

vj_effect *randnoise_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = -255;
    ve->limits[1][0] = 255;
	ve->limits[0][1] = -255;
	ve->limits[1][1] = 255;

    ve->defaults[0] = -16;
    ve->defaults[1] = 16;

	ve->description = "Randnoise";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->parallel = 1;
	ve->has_user = 0;

    ve->param_description = vje_build_param_list( ve->num_params, "Min", "Max" );
    return ve;
}

static __thread unsigned long x = 123456789, y = 362436069, z = 521288629;

void randnoise_apply( VJFrame *frame, int min, int max)
{
    int i;
    const unsigned int len = frame->len;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

	unsigned long t;

	for( i = 0; i < len; i ++ ) {

		//xor shift
		x ^= x << 16;
		x ^= x >> 5;
		x ^= x << 1;

		t = x;
		x = y;
		y = z;
		z = t ^ x ^ y;
		//z is the new pseudo random number

		int rv = (z % max) + min;
		int y0 = Y[i] + rv;
		if( y0 > pixel_Y_hi_ )
			y0 = pixel_Y_hi_;
		else if( y0 < pixel_Y_lo_ ) 
			y0 = pixel_Y_lo_;

		Y[i] = y0;
	}
}
