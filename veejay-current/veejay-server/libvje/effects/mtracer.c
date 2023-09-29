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
#include "mtracer.h"
#include "magicoverlays.h"

typedef struct {
    uint8_t *mtrace_buffer[4];
    int mtrace_counter;
	int started;
	int prev_n;
} m_tracer_t;

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
void mtracer_free(void *ptr) {
    m_tracer_t *m = (m_tracer_t*) ptr;
    free(m->mtrace_buffer[0]);
    free(m);
}

void *mtracer_malloc(int w, int h)
{
    m_tracer_t *m = (m_tracer_t*) vj_calloc(sizeof(m_tracer_t));
    if(!m) {
        return NULL;
    }

	size_t buflen = ( (w*h+w)) * sizeof(uint8_t);
	m->mtrace_buffer[0] = (uint8_t*) vj_malloc( buflen );
	if(!m->mtrace_buffer[0]) {
        free(m);
		return NULL;
	}
	return (void*) m;
}

void mtracer_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int mode = args[0];
    int n = args[1];

    m_tracer_t *m = (m_tracer_t*) ptr;

	const int len = frame->len;
    VJFrame mt;
    veejay_memcpy( &mt, frame, sizeof(VJFrame ));

    int om_args[2] = { mode, 0 };
    m->mtrace_counter = (m->mtrace_counter+1) % n;

	if(m->started == 0 || n != m->prev_n || m->mtrace_counter == 0) {
		m->started = 1;
		m->prev_n = n;
		overlaymagic_apply(NULL, frame, frame2, om_args);
		veejay_memcpy( m->mtrace_buffer[0], frame->data[0], len );
		return;
	}
	
	mt.data[0] = m->mtrace_buffer[0];
	mt.data[1] = frame->data[1];
	mt.data[2] = frame->data[2];
	mt.data[3] = frame->data[3];
	overlaymagic_apply(NULL, &mt, frame2, om_args );
	overlaymagic_apply(NULL, &mt, frame, om_args );
	overlaymagic_apply(NULL, frame, &mt, om_args );

}
