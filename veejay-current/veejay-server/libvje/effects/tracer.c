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
} tracer_t;

#define MAX_OLD_FRAMES 128

vj_effect *tracer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = MAX_OLD_FRAMES;
    ve->defaults[0] = 150;
    ve->defaults[1] = 8;
    ve->description = "Tracer (Frame Echo)";
    ve->sub_format = -1;
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

    veejay_memset(t->trace_buffer[0], pixel_Y_lo_, len );
    veejay_memset(t->trace_buffer[1], 128, len );
    veejay_memset(t->trace_buffer[2], 128, len );

    return (void*) t;
}

void tracer_free(void *ptr) {
    tracer_t *t = (tracer_t*) ptr;
    if(t->trace_buffer[0])
        free(t->trace_buffer[0]);
    free(t);
}

void tracer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    tracer_t *t = (tracer_t*) ptr;
    
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int opacity = args[0];
    const int buffer_len = args[1];

    const int decay = 256 - (256 / buffer_len);
    const int blend = 256 - decay;
    const int op_scale = (opacity > 255) ? 255 : opacity;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    uint8_t *restrict t0 = t->trace_buffer[0];
    uint8_t *restrict t1 = t->trace_buffer[1];
    uint8_t *restrict t2 = t->trace_buffer[2];
  
#pragma omp simd
    for (int x = 0; x < len; x++)
    {
        int mixed = (Y[x] + Y2[x]) * op_scale >> 9;
        int accum = (t0[x] * decay + mixed * blend) >> 8;

        t0[x] = (uint8_t)accum;
        Y[x]  = (uint8_t)accum;
    }

#pragma omp simd
    for (int x = 0; x < uv_len; x++)
    {
        int cb_acc = t1[x] - 128;
        int cr_acc = t2[x] - 128;

        int mixed_cb = (Cb[x] + Cb2[x] - 256) * op_scale >> 9;
        int mixed_cr = (Cr[x] + Cr2[x] - 256) * op_scale >> 9;

        cb_acc = (cb_acc * decay + mixed_cb * blend) >> 8;
        cr_acc = (cr_acc * decay + mixed_cr * blend) >> 8;

        t1[x] = CLAMP_UV(cb_acc + 128);
        t2[x] = CLAMP_UV(cr_acc + 128);

        Cb[x] = t1[x];
        Cr[x] = t2[x];
    }
}