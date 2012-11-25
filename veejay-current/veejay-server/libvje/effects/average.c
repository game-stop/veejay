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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "average.h"
#include "common.h"
vj_effect *average_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 1;
	ve->parallel = 1;
    ve->description = "Average";
    ve->sub_format = 0;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Value");
    return ve;
}

void average_apply(VJFrame *frame, int width, int height, int val)
{
    unsigned int i;
    const unsigned int len = frame->len;
    const unsigned int uv_len = frame->uv_len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    int a,b;
    
    for (i = 0; i < len; i++) {
	a = Y[i];
	b = ((val-1) * a + a)/val;
	Y[i] = CLAMP_Y(b);
    }

    for (i = 0; i < uv_len; i++) {
	a = Cb[i];
	b = ((val-1) * a + a)/val;
	Cb[i] = CLAMP_UV(b);
	a = Cr[i];
	b = ((val-1) * a + a )/val;
	Cr[i] = CLAMP_UV(b);
    }
}
void average_free(){}
