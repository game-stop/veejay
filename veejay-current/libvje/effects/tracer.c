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
#include "common.h"

static uint8_t *trace_buffer[3];
static int trace_counter = 0;

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
    ve->description = "Tracer";
    ve->sub_format = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
    return ve;
}

int 	tracer_malloc(int w, int h)
{
	trace_buffer[0] = (uint8_t *) vj_malloc(w * h * 3* sizeof(uint8_t));
	if(!trace_buffer[0]) return 0;
	trace_buffer[1] = trace_buffer[0] + ( w * h );
	trace_buffer[2] = trace_buffer[1] + ( w * h );

   return 1;
}

void tracer_free() {
	if(trace_buffer[0])
	 free(trace_buffer[0]);
	trace_buffer[0] = NULL;
	trace_buffer[1] = NULL;
	trace_buffer[2] = NULL;
}

void tracer_apply(VJFrame *frame, VJFrame *frame2,
		  int width, int height, int opacity, int n)
{

    unsigned int x, len = frame->len;
    unsigned int op1 = (opacity > 255) ? 255 : opacity;
    unsigned int op0 = 255 - op1;
    unsigned int uv_len = frame->uv_len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb= frame->data[1];
	uint8_t *Cr= frame->data[2];
    uint8_t *Y2 = frame2->data[0];
 	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];

    if (trace_counter == 0)
	{
		for (x = 0; x < len; x++)
		{
		    Y[x] =
			limit_luma(func_opacity(Y[x], Y2[x], op0, op1));
		}
		for (x = 0; x < uv_len; x++)
		{
	   	 	Cb[x] =
				limit_chroma(func_opacity
			     (Cb[x], Cb2[x], op0, op1));
	   	 	Cr[x] =
				limit_chroma(func_opacity
			     (Cr[x], Cr2[x], op0, op1));
		}
		veejay_memcpy(trace_buffer[0], Y, len);
		veejay_memcpy(trace_buffer[1], Cb, uv_len);
		veejay_memcpy(trace_buffer[2], Cr, uv_len);
    }
	else
	{
		for (x = 0; x < len; x++)
		{
	  	  Y[x] =
			((op0 * Y[x]) + (op1 * trace_buffer[0][x])) >> 8; // / 255;
		}
		for (x = 0; x < uv_len; x++)
		{
	    	Cb[x] =
			((op0 * Cb[x]) + (op1 * trace_buffer[1][x])) >> 8 ; // 255;
	    	Cr[x] =
			((op0 * Cr[x]) + (op1 * trace_buffer[2][x])) >> 8 ; // 255;
		}
		veejay_memcpy(trace_buffer[0], Y, len);
		veejay_memcpy(trace_buffer[1], Cb, uv_len);
		veejay_memcpy(trace_buffer[2], Cr, uv_len);

    }

    trace_counter++;
    if (trace_counter >= n)
		trace_counter = 0;


}
