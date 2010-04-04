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
#include <config.h>
#include "opacitythreshold.h"

vj_effect *opacitythreshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[0] = 180;
    ve->defaults[1] = 50;
    ve->defaults[2] = 255;
    ve->description = "Threshold blur with overlay";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Min Threshold", "Max Threshold");
    return ve;
}



void opacitythreshold_apply( VJFrame *frame, VJFrame *frame2,
			    int width, int height, int opacity,
			    int threshold, int t2)
{

    unsigned int x, y, len = width * height-width;
    uint8_t a1, a2;
    unsigned int op0, op1;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    op1 = (opacity > 255) ? 255 : opacity;
    op0 = 255 - op1;

    for (y = width; y < len; y += width) {
	for (x = 1; x < width-1; x++) {
	    a1 = Y[x + y];
	    a2 = Y2[x + y];
	    if (a1 < threshold || a1 > t2) {
		    a1 = (Y[y - width + x - 1] +
			  Y[y - width + x + 1] +
			  Y[y - width + x] +
			  Y[y + x] +
			  Y[y + x - 1] +
			  Y[y + x + 1] +
			  Y[y + width + x] +
			  Y[y + width + x + 1] +
			  Y[y + width + x - 1]
			) / 9;

		    a2 = (Y2[y - width + x - 1] +
			  Y2[y - width + x + 1] +
			  Y2[y - width + x] +
			  Y2[y + x] +
			  Y2[y + x - 1] +
			  Y2[y + x + 1] +
			  Y2[y + width + x] +
			  Y2[y + width + x + 1] +
			  Y2[y + width + x - 1]
			) / 9;

		    Y[x + y] = (op0 * a1 + op1 * a2) >> 8;
		    Cb[x + y] =
			(op0 * Cb[x + y] +
			 op1 * Cb2[x + y]) >> 8;
		    Cr[x + y] =
			(op0 * Cr[x + y] +
			 op1 * Cr2[x + y]) >> 8;
	    }
	}
    }
}
void opacitythreshold_free(){}
