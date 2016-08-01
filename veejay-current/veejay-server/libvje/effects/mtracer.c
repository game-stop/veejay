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
#include <libvjmem/vjmem.h>
#include "mtracer.h"
#include "magicoverlays.h"

static uint8_t *mtrace_buffer[4] = { NULL,NULL,NULL,NULL };
static int mtrace_counter = 0;

vj_effect *mtracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 32;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 25;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Magic Tracer";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;	
    ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Length");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0,
		"Additive", "Subtractive","Multiply","Divide","Lighten","Hardlight",
		"Difference","Difference Negate","Exclusive","Base","Freeze",
		"Unfreeze","Relative Add","Relative Subtract","Max select", "Min select",
		"Relative Luma Add", "Relative Luma Subtract", "Min Subselect", "Max Subselect",
		"Add Subselect", "Add Average", "Experimental 1","Experimental 2", "Experimental 3",
		"Multisub", "Softburn", "Inverse Burn", "Dodge", "Distorted Add", "Distorted Subtract", "Experimental 4", "Negation Divide");


    return ve;
}
void mtracer_free() {
	if( mtrace_buffer[0] ) {
		   free(mtrace_buffer[0]);
	}
	mtrace_buffer[0] = NULL;
}

int mtracer_malloc(int w, int h)
{
	size_t buflen = RUP8( (w*h+w)) * sizeof(uint8_t);
	mtrace_buffer[0] = (uint8_t*) vj_malloc( buflen );
	if(!mtrace_buffer[0]) {
		return 0;
	}
	return 1;
}

void mtracer_apply( VJFrame *frame, VJFrame *frame2, int mode, int n)
{
	const int len = frame->len;
    VJFrame m;
    veejay_memcpy( &m, frame, sizeof(VJFrame ));

    if (mtrace_counter == 0) {
		overlaymagic_apply(frame, frame2, mode,0);
		vj_frame_copy1( mtrace_buffer[0], frame->data[0], len );
    } else {
		overlaymagic_apply(frame, frame2, mode,0);
		m.data[0] = mtrace_buffer[0];
		m.data[1] = frame->data[1];
		m.data[2] = frame->data[2];
		m.data[3] = frame->data[3];
		overlaymagic_apply( &m, frame2, mode, 0 );
		vj_frame_copy1( mtrace_buffer[0],frame->data[0], len );
    }

    mtrace_counter++;
    if (mtrace_counter >= n)
	mtrace_counter = 0;
}
