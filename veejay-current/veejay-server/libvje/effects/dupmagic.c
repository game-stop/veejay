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
#include "dupmagic.h"
#include "magicoverlays.h"

vj_effect *dupmagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->description = "Strong Luma Overlay";
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 13;
    ve->extra_frame = 1;
	ve->parallel = 1;    
	ve->sub_format = 0;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode" );
    return ve;
}

void dupmagic_apply(VJFrame *frame, VJFrame *frame2, int width,
		    int height, int n)
{
    switch (n) {
    case 1:
	_overlaymagic_additive(frame, frame, width, height);
	_overlaymagic_additive(frame2, frame2, width, height);
	_overlaymagic_additive(frame, frame2, width, height);
	break;
    case 2:
	_overlaymagic_multiply(frame, frame, width, height);
	_overlaymagic_multiply(frame2, frame2, width, height);
	_overlaymagic_multiply(frame, frame2, width, height);
	break;
    case 3:
	_overlaymagic_divide(frame, frame, width, height);
	_overlaymagic_divide(frame2, frame2, width, height);
	_overlaymagic_divide(frame, frame2, width, height);
	break;
    case 4:
	_overlaymagic_lighten(frame, frame, width, height);
	_overlaymagic_lighten(frame2, frame2, width, height);
	_overlaymagic_lighten(frame, frame2, width, height);
	break;
    case 5:
	_overlaymagic_diffnegate(frame, frame, width, height);
	_overlaymagic_diffnegate(frame2, frame2, width, height);
	_overlaymagic_diffnegate(frame, frame2, width, height);
	break;
    case 6:
	_overlaymagic_freeze(frame, frame, width, height);
	_overlaymagic_freeze(frame2, frame2, width, height);
	_overlaymagic_freeze(frame, frame2, width, height);
	break;
    case 7:
	_overlaymagic_unfreeze(frame, frame, width, height);
	_overlaymagic_unfreeze(frame2, frame2, width, height);
	_overlaymagic_unfreeze(frame, frame2, width, height);
	break;
    case 8:
	_overlaymagic_relativeadd(frame, frame2, width, height);
	_overlaymagic_relativeadd(frame2, frame2, width, height);
	_overlaymagic_relativeadd(frame, frame2, width, height);
	break;
    case 9:
	_overlaymagic_relativeaddlum(frame, frame, width, height);
	_overlaymagic_relativeaddlum(frame2, frame2, width, height);
	_overlaymagic_relativeaddlum(frame, frame2, width, height);
	break;
    case 10:
	_overlaymagic_maxselect(frame, frame, width, height);
	_overlaymagic_maxselect(frame2, frame2, width, height);
	_overlaymagic_maxselect(frame, frame2, width, height);
	break;
    case 11:
	_overlaymagic_minselect(frame, frame, width, height);
	_overlaymagic_minselect(frame2, frame2, width, height);
	_overlaymagic_minselect(frame, frame2, width, height);
	break;
    case 12:
	_overlaymagic_addtest2(frame, frame, width, height);
	_overlaymagic_addtest2(frame2, frame2, width, height);
	_overlaymagic_addtest2(frame, frame2, width, height);
	break;
    case 13:
	_overlaymagic_softburn(frame, frame, width, height);
	_overlaymagic_softburn(frame2, frame2, width, height);
	_overlaymagic_softburn(frame, frame2, width, height);
	break;
    }
}
void dupmagic_free(){}
