/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "dither.h"
#include "common.h"

vj_effect *dither_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 2;
    ve->defaults[1] = 0;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Matrix Dithering";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;

	ve->param_description = vje_build_param_list( ve->num_params, "Value", "Mode" );
    
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1, "Static", "Random" );
	
	return ve;
}

static int last_size = 0;
void dither_apply(VJFrame *frame, int width, int height, int size, int random_on)
{
    long int w_, h_;
    long int dith[size][size];
    long int i, j, d, v, l, m;
    uint8_t *Y = frame->data[0];

	if( last_size != size || random_on ) {
	  for (l = 0; l < size; l++) {
		for (m = 0; m < size; m++) {
			dith[l][m] =
				(int) ((double) (size) * rand() / (RAND_MAX + 1.0));
		}
	  }
	  last_size = size;
	}

    for (h_ = 0; h_ < height; h_++) {
		j = h_ % size;
		for (w_ = 0; w_ < width; w_++) {
			i = w_ % size;
			d = dith[i][j] << 4;
			v = ((long) Y[((h_ * width) + w_)] + d);
			Y[(h_ * width) + w_] = (uint8_t) ((v >> 7) << 7);
		}
    }
}
