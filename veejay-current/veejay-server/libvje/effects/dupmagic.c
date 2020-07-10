/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "magicoverlays.h"
#include "dupmagic.h"

vj_effect *dupmagic_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 5;
	ve->description = "Strong Luma Overlay";
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 12;
	ve->extra_frame = 1;
	ve->parallel = 1;    
	ve->sub_format = -1;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "Softburn", "Additive",
	   "Multiply", "Divide", "Lighten", "Difference Negate", "Freeze", "Unfreeze", "Relative Add",
	   "Relative Add Luma",	"Max Select", "Min Select", "Experimental"   );

	return ve;
}

void dupmagic_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int n = args[0];

    switch (n) {
    case 1:
		overlaymagic_additive(frame, frame );
		overlaymagic_additive(frame2, frame2 );
		overlaymagic_additive(frame, frame2 );
		break;
    case 2:
		overlaymagic_multiply(frame, frame );
		overlaymagic_multiply(frame2, frame2 );
		overlaymagic_multiply(frame, frame2 );
		break;
    case 3:
		overlaymagic_divide(frame, frame );
		overlaymagic_divide(frame2, frame2 );
		overlaymagic_divide(frame, frame2 );
		break;
    case 4:
		overlaymagic_lighten(frame, frame );
		overlaymagic_lighten(frame2, frame2 );
		overlaymagic_lighten(frame, frame2 );
		break;
    case 5:
		overlaymagic_diffnegate(frame, frame );
		overlaymagic_diffnegate(frame2, frame2 );
		overlaymagic_diffnegate(frame, frame2 );
		break;
    case 6:
		overlaymagic_freeze(frame, frame );
		overlaymagic_freeze(frame2, frame2 );
		overlaymagic_freeze(frame, frame2 );
		break;
    case 7:
		overlaymagic_unfreeze(frame, frame );
		overlaymagic_unfreeze(frame2, frame2 );
		overlaymagic_unfreeze(frame, frame2 );
		break;
    case 8:
		overlaymagic_relativeadd(frame, frame2 );
		overlaymagic_relativeadd(frame2, frame2 );
		overlaymagic_relativeadd(frame, frame2 );
		break;
    case 9:
		overlaymagic_relativeaddlum(frame, frame );
		overlaymagic_relativeaddlum(frame2, frame2 );
		overlaymagic_relativeaddlum(frame, frame2 );
	break;
    case 10:
		overlaymagic_maxselect(frame, frame );
		overlaymagic_maxselect(frame2, frame2 );
		overlaymagic_maxselect(frame, frame2 );
		break;
    case 11:
		overlaymagic_minselect(frame, frame );
		overlaymagic_minselect(frame2, frame2 );
		overlaymagic_minselect(frame, frame2 );
		break;
    case 12:
		overlaymagic_addtest2(frame, frame );
		overlaymagic_addtest2(frame2, frame2 );
		overlaymagic_addtest2(frame, frame2 );
		break;
    default:
		overlaymagic_softburn(frame, frame );
		overlaymagic_softburn(frame2, frame2 );
		overlaymagic_softburn(frame, frame2 );
		break;
    }
}
