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

#include "average.h"
#include <stdlib.h>

vj_effect *average_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 1;
    ve->description = "Average";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;
    return ve;
}

void average_apply(uint8_t *yuv[3], int width, int height, int val)
{
    unsigned int i;
    const unsigned int len = (width * height);
    const unsigned int uv_len = len /4;
    int a,b;
    for (i = 0; i < len; i++) {
	a = yuv[0][i];
	b = ((val-1) * a + a)/val;
	if(b < 16) b = 16; else if (b > 240) b = 240;
	yuv[0][i] = b;
    }
    for (i = 0; i < uv_len; i++) {
	a = yuv[1][i];
	b = ((val-1) * a + a)/val;
	if(b < 16) b = 16; else if (b > 235) b = 235;
	yuv[1][i] = b;
	a = yuv[2][i];
	b = ((val-1) * a + a )/val;
	if(b < 16) b = 16; else if (b > 235) b = 235;
	yuv[2][i] = b;
    }
}
void average_free(){}
