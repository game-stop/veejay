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
#include "scratcher.h"
#include "common.h"
#include "magicoverlays.h"


uint8_t *mtrace_buffer[3];
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
// FIXME private
void mtracer_free() {
  if(mtrace_buffer[0]) free(mtrace_buffer[0]);
  mtrace_buffer[0] = NULL;
  mtrace_buffer[1] = NULL;
}

int mtracer_malloc(int w, int h)
{
   mtrace_buffer[0] = (uint8_t *) vj_yuvalloc(w,h );
	if(!mtrace_buffer[0]) return 0;
	mtrace_buffer[1] = mtrace_buffer[0] + (w*h);
	mtrace_buffer[2] = mtrace_buffer[1] + (w*h);
	return 1;
}

void mtracer_apply( VJFrame *frame, VJFrame *frame2,
		   int width, int height, int mode, int n)
{

    unsigned int len = frame->len;
    unsigned int uv_len = frame->uv_len;
  	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];

    if (mtrace_counter == 0) {
	overlaymagic_apply(frame, frame2, width, height, mode,0);
	veejay_memcpy(mtrace_buffer[0], Y, len);
	veejay_memcpy(mtrace_buffer[1], Cb, uv_len);
	veejay_memcpy(mtrace_buffer[2], Cr, uv_len);
    } else {
	overlaymagic_apply(frame, frame2, width, height, mode,0);
	veejay_memcpy(mtrace_buffer[0], Y, len);
	veejay_memcpy(mtrace_buffer[1], Cb, uv_len);
	veejay_memcpy(mtrace_buffer[2], Cr, uv_len);

    }

    mtrace_counter++;
    if (mtrace_counter >= n)
	mtrace_counter = 0;


}
