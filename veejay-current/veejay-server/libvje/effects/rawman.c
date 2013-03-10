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
#include "rawman.h"
#include <stdlib.h>
vj_effect *rawman_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 15;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 4;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;
    ve->sub_format = 0;
    ve->description = "Raw Data Manipulation";
	ve->parallel = 1;
	ve->has_user = 0;
    ve->extra_frame = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Value");
    return ve;
}

void rawman_apply(VJFrame *frame, unsigned int width,
		  unsigned int height, unsigned int mode, unsigned int YY)
{
    unsigned int len = width * height;
    unsigned int i;
    uint8_t *Y = frame->data[0];

    /* playing with data. experimentation gives the greatest results.
       maybe these routine don't seem usefull, but combine them with
       other effects.
     */
    switch (mode) {

    case 1:			/* 1 in reverse, if Y == 0 this darkens the image */
	for (i = 0; i < len; i++) {
	    if ((Y[i] < YY)) {
		Y[i] *= 2;
	    } else {
		Y[i] /= 2;
	    }
	}
	break;
    case 2:

	for (i = 0; i < len; i++) {
	    Y[i] -= YY;
	}
	break;
    case 3:			/* divide action */
	for (i = 0; i < len; i++) {
	    if ((Y[i] < YY)) {
		Y[i] /= 2;
	    } else {
		/* divide by 2 */
		Y[i] *= 2;
	    }
	}
	break;
    case 4:			/* addition */
	for (i = 0; i < len; i++) {
	    if ((Y[i] < YY)) {
		Y[i] += YY;
	    } else {
		Y[i] -= YY;
	    }
	}
	break;
    default:
	/* brightness */
	for (i = 0; i < len; i++) {
	    Y[i] += YY;
	}
	break;

    }
}
void rawman_free(){}
