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
#include "tracer.h"

#define func_opacity(a,b,p,q) (  ((a * p) + (b * q)) >> 8 )

typedef struct {
    uint8_t *trace_buffer[4];
    int trace_counter;
} tracer_t;

vj_effect *tracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 25;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Tracer (Frame Echo)";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Opacity", "Buffer length");
    return ve;
}

void *tracer_malloc(int w, int h)
{
    tracer_t *t = (tracer_t*) vj_calloc(sizeof(tracer_t));
    if(!t) {
        return NULL;
    }
	const int len = (w * h);
    const int total_len = (len * 3);
    
    t->trace_buffer[0] = (uint8_t *) vj_malloc(sizeof(uint8_t) * total_len );
    if(!t->trace_buffer[0]) {
        free(t);
        return NULL;
    }
	t->trace_buffer[1] = t->trace_buffer[0] + len;
	t->trace_buffer[2] = t->trace_buffer[1] + len;
   	return (void*) t;
}

void tracer_free(void *ptr) {
    tracer_t *t = (tracer_t*) ptr;
	if(t->trace_buffer[0])
		free(t->trace_buffer[0]);
    free(t);
}

void tracer_apply(void *ptr,VJFrame *frame, VJFrame *frame2, int *args )
{
    unsigned int x;
	const int len = frame->len;
    unsigned int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
    int opacity = args[0];
    int n = args[1];
    tracer_t *t = (tracer_t*) ptr;
    unsigned int op1 = (opacity > 255) ? 255 : opacity;
    unsigned int op0 = 255 - op1;

    uint8_t **trace_buffer = t->trace_buffer;
        
    if (t->trace_counter == 0)
	{
#pragma omp simd
		for (x = 0; x < len; x++)
		{
		    Y[x] = func_opacity(Y[x], Y2[x], op0, op1);
		}
#pragma omp simd
		for (x = 0; x < uv_len; x++)
		{
	   	 	Cb[x] = func_opacity(Cb[x], Cb2[x], op0, op1);
	   	 	Cr[x] = func_opacity(Cr[x], Cr2[x], op0, op1);
		}
		int strides[4] = { len, uv_len, uv_len, 0 };
		vj_frame_copy( frame->data, trace_buffer, strides );
    }
	else
	{
#pragma omp simd
		for (x = 0; x < len; x++)
		{
	  	    Y[x] = ((op0 * Y[x]) + (op1 * trace_buffer[0][x])) >> 8;
	  	}
#pragma omp simd
		for (x = 0; x < uv_len; x++)
		{
	   		Cb[x] = ((op0 * Cb[x]) + (op1 * trace_buffer[1][x])) >> 8;
	    		Cr[x] = ((op0 * Cr[x]) + (op1 * trace_buffer[2][x])) >> 8;
		}
		int strides[4] = { len, uv_len, uv_len, 0 };
		vj_frame_copy( frame->data, trace_buffer, strides );
    }

    t->trace_counter++;
    if (t->trace_counter >= n)
		t->trace_counter = 0;


}
