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
#include <stdlib.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include "scratcher.h"
#include "common.h"
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
    ve->limits[1][0] = 30;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 25;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Magic Tracer";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_user = 0;	
    ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Length");
    return ve;
}
void mtracer_free() {
	if( mtrace_buffer[0] ) {
		   free(mtrace_buffer[0]);
	}
	mtrace_buffer[0] = NULL;
	mtrace_buffer[1] = NULL;
	mtrace_buffer[2] = NULL;
}

int mtracer_malloc(int w, int h)
{
	size_t buflen = RUP8( (w*h+w)*3) * sizeof(uint8_t);
	mtrace_buffer[0] = (uint8_t*) vj_malloc( buflen );
	if(!mtrace_buffer[0]) {
		return 0;
	}
	mtrace_buffer[1] = mtrace_buffer[0] + RUP8(w*h);
	mtrace_buffer[2] = mtrace_buffer[1] + RUP8(w*h);
	vj_frame_clear1( mtrace_buffer[0], pixel_Y_lo_, RUP8(w*h) );
	vj_frame_clear1( mtrace_buffer[1], 128, RUP8(w*h*2));
	return 1;
}

void mtracer_apply( VJFrame *frame, VJFrame *frame2, int width, int height, int mode, int n)
{
    VJFrame m;
    unsigned int len = frame->len;
    unsigned int uv_len = frame->uv_len;
    int strides[4] = { len, uv_len, uv_len, 0 };

    veejay_memcpy( &m, frame, sizeof(VJFrame ));

    if (mtrace_counter == 0) {
		overlaymagic_apply(frame, frame2, width, height, mode,0);
		vj_frame_copy( mtrace_buffer, frame->data, strides );
    } else {
		overlaymagic_apply(frame, frame2, width, height, mode,0);
		m.data[0] = mtrace_buffer[0];
		m.data[1] = mtrace_buffer[1];
		m.data[2] = mtrace_buffer[2];
		overlaymagic_apply( &m, frame2, width,height, mode, 0 );
		vj_frame_copy( mtrace_buffer,frame->data, strides );
    }

    mtrace_counter++;
    if (mtrace_counter >= n)
	mtrace_counter = 0;
}
