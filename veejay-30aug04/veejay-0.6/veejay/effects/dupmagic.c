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

#include "dupmagic.h"
#include "magicoverlays.h"
#include "sampleadm.h"
#include "../subsample.h"

#include <stdlib.h>


vj_effect *dupmagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->description = "Strong Luma Overlay";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 13;
    ve->extra_frame = 1;
    ve->has_internal_data = 0;
    ve->sub_format = 0;
    return ve;
}

void dupmagic_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
		    int height, int n)
{
    switch (n) {
    case 1:
	_overlaymagic_additive(yuv1, yuv1, width, height);
	_overlaymagic_additive(yuv2, yuv2, width, height);
	_overlaymagic_additive(yuv1, yuv2, width, height);
	break;
    case 2:
	_overlaymagic_multiply(yuv1, yuv1, width, height);
	_overlaymagic_multiply(yuv2, yuv2, width, height);
	_overlaymagic_multiply(yuv1, yuv2, width, height);
	break;
    case 3:
	_overlaymagic_divide(yuv1, yuv1, width, height);
	_overlaymagic_divide(yuv2, yuv2, width, height);
	_overlaymagic_divide(yuv1, yuv2, width, height);
	break;
    case 4:
	_overlaymagic_lighten(yuv1, yuv1, width, height);
	_overlaymagic_lighten(yuv2, yuv2, width, height);
	_overlaymagic_lighten(yuv1, yuv2, width, height);
	break;
    case 5:
	_overlaymagic_diffnegate(yuv1, yuv1, width, height);
	_overlaymagic_diffnegate(yuv2, yuv2, width, height);
	_overlaymagic_diffnegate(yuv1, yuv2, width, height);
	break;
    case 6:
	_overlaymagic_freeze(yuv1, yuv1, width, height);
	_overlaymagic_freeze(yuv2, yuv2, width, height);
	_overlaymagic_freeze(yuv1, yuv2, width, height);
	break;
    case 7:
	_overlaymagic_unfreeze(yuv1, yuv1, width, height);
	_overlaymagic_unfreeze(yuv2, yuv2, width, height);
	_overlaymagic_unfreeze(yuv1, yuv2, width, height);
	break;
    case 8:
	_overlaymagic_relativeadd(yuv1, yuv2, width, height);
	_overlaymagic_relativeadd(yuv2, yuv2, width, height);
	_overlaymagic_relativeadd(yuv1, yuv2, width, height);
	break;
    case 9:
	_overlaymagic_relativeaddlum(yuv1, yuv1, width, height);
	_overlaymagic_relativeaddlum(yuv2, yuv2, width, height);
	_overlaymagic_relativeaddlum(yuv1, yuv2, width, height);
	break;
    case 10:
	_overlaymagic_maxselect(yuv1, yuv1, width, height);
	_overlaymagic_maxselect(yuv2, yuv2, width, height);
	_overlaymagic_maxselect(yuv1, yuv2, width, height);
	break;
    case 11:
	_overlaymagic_minselect(yuv1, yuv1, width, height);
	_overlaymagic_minselect(yuv2, yuv2, width, height);
	_overlaymagic_minselect(yuv1, yuv2, width, height);
	break;
    case 12:
	_overlaymagic_addtest2(yuv1, yuv1, width, height);
	_overlaymagic_addtest2(yuv2, yuv2, width, height);
	_overlaymagic_addtest2(yuv1, yuv2, width, height);
	break;
    case 13:
	_overlaymagic_softburn(yuv1, yuv1, width, height);
	_overlaymagic_softburn(yuv2, yuv2, width, height);
	_overlaymagic_softburn(yuv1, yuv2, width, height);
	break;
    }
}
void dupmagic_free(){}
