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

#include "gamma.h"
#include <stdlib.h>
#include <math.h>
static int gamma_flag = 0;

static uint8_t table[256];

vj_effect *gamma_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 171;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 5000;
    ve->has_internal_data = 0;
    ve->description = "Gamma Correction";
    ve->extra_frame = 0;
    ve->sub_format = 0;
    return ve;
}


inline void gamma_setup(int width, int height,
			double gamma_value)
{
    int i;
    double val;

    for (i = 0; i < 256; i++) {
	val = i / 256.0;
	val = pow(val, gamma_value);
	val = 256.0 * val;
	table[i] = val;
    }
}

void gamma_apply(uint8_t * yuv[3], int width,
		 int height, int gamma_value)
{
    unsigned int i, len;
    /* gamma correction YCbCr, only on luminance not on chroma */
    if (gamma_value != gamma_flag)
	gamma_setup(width, height, (double) (gamma_value / 1000.0));

    len = (width * height);
    for (i = 0; i < len; i++) {
	yuv[0][i] = (uint8_t) table[(yuv[0][i])];
    }
}
void gamma_free(){}
