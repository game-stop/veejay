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
#include <libvjmem/vjmem.h>
#include "widthmirror.h"
#include <stdlib.h>
#include <stdio.h>
vj_effect *widthmirror_init(int max_width,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 2;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_width;

    ve->description = "Width Mirror";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Widths");
    return ve;
}

void widthmirror_apply(VJFrame *frame, int width, int height,
		       int width_div)
{
    unsigned int r, c;
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int uv_width = frame->uv_width;
    const int uv_width_div = width_div;
    int p1;
    uint8_t x1, x2, x3;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    if (width_div >= width || width_div < 2)
	width_div = 2;

    for (r = width; r < len; r += width) {
	unsigned int divisor = width / width_div;
	for (c = 0; c < width; c++) {
	    if (divisor - c < 0)
		p1 = c - divisor + r;
	    else
		p1 = divisor - c + r;
	    x1 = Y[c + r];
	    Y[p1] = x1;
	    Y[width - c + r] = x1;
	}
    }
    for (r = uv_width; r < uv_len; r += uv_width) {
	unsigned int divisor = uv_width / uv_width_div;
	for (c = 0; c < uv_width; c++) {
	    if (divisor - c < 0)
		p1 = c - divisor + r;
	    else
		p1 = divisor - c + r;
	
	    x2 = Cb[c + r];
	    Cb[p1] = x2;
	    Cb[uv_width - c + r] = x2;

	    x3 = Cr[c + r];
	    Cr[p1] = x3;
	    Cr[uv_width - c + r] = x3;
	
	}
    }





}
void widthmirror_free(){}
