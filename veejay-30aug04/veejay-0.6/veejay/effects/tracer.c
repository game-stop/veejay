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
#include "scratcher.h"
#include <stdlib.h>
#include "common.h"
#include "vj-common.h"

uint8_t *trace_buffer[3];
static int trace_counter = 0;
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

vj_effect *tracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_SCRATCH_FRAMES;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Tracer";
    ve->sub_format = 0;
    ve->extra_frame = 1;
    ve->has_internal_data = 1;
    return ve;
}

int 	tracer_malloc(int w, int h)
{
   trace_buffer[0] = (uint8_t *) vj_malloc(w * h * sizeof(uint8_t));
   if(!trace_buffer[0]) return 0;
    trace_buffer[1] = (uint8_t *) vj_malloc( ((w * h)/4) * sizeof(uint8_t));
   if(!trace_buffer[1]) return 0;
    trace_buffer[2] = (uint8_t *) vj_malloc( ((w * h)/4) * sizeof(uint8_t));
    if(!trace_buffer[2]) return 0;
	return 1;
}

void tracer_free() {
 if(trace_buffer[0]) free(trace_buffer[0]);
 if(trace_buffer[1]) free(trace_buffer[1]);
 if(trace_buffer[2]) free(trace_buffer[2]);
}

void tracer_apply(uint8_t * yuv1[3], uint8_t * yuv2[3],
		  int width, int height, int opacity, int n)
{

    unsigned int x, y, len = width * height;
    unsigned int op1 = (opacity > 255) ? 255 : opacity;
    unsigned int op0 = 255 - op1;
    unsigned int uv_len = len >> 2;

    if (trace_counter == 0) {
	for (x = 0; x < len; x++) {
	    yuv1[0][x] =
		limit_luma(func_opacity(yuv1[0][x], yuv2[0][x], op0, op1));
	}
	for (x = 0; x < uv_len; x++) {
	    yuv1[1][x] =
		limit_chroma(func_opacity
			     (yuv1[1][x], yuv2[1][x], op0, op1));
	    yuv1[2][x] =
		limit_chroma(func_opacity
			     (yuv1[2][x], yuv2[2][x], op0, op1));
	}
	veejay_memcpy(trace_buffer[0], yuv1[0], len);
	veejay_memcpy(trace_buffer[1], yuv1[1], uv_len);
	veejay_memcpy(trace_buffer[2], yuv1[2], uv_len);
    } else {
	for (x = 0; x < len; x++) {
	    yuv1[0][x] =
		((op0 * yuv1[0][x]) + (op1 * trace_buffer[0][x])) >> 8; // / 255;
	}
	for (x = 0; x < uv_len; x++) {
	    yuv1[1][x] =
		((op0 * yuv1[1][x]) + (op1 * trace_buffer[1][x])) >> 8 ; // 255;
	    yuv1[2][x] =
		((op0 * yuv1[2][x]) + (op1 * trace_buffer[2][x])) >> 8 ; // 255;
	}
	veejay_memcpy(trace_buffer[0], yuv1[0], len);
	veejay_memcpy(trace_buffer[1], yuv1[1], uv_len);
	veejay_memcpy(trace_buffer[2], yuv1[2], uv_len);

    }

    trace_counter++;
    if (trace_counter >= n)
	trace_counter = 0;


}
