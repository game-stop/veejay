/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "average.h"

typedef struct {
    float *running_sum[3];
} average_t;

vj_effect *average_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 1000;
    ve->defaults[0] = 1;
	ve->parallel = 0; 
    ve->description = "Exponential Moving Average";
    ve->sub_format = 1; 
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Smoothing factor" );

	return ve;
}

void *average_malloc(int width, int height)
{
    average_t *a = (average_t*) vj_calloc(sizeof(average_t));
    if(!a) {
        return NULL;
    }
	a->running_sum[0] = (float*) vj_calloc( sizeof(float) * (width * height * 3 ));
	if(!a->running_sum[0]) {
		free(a);
        return NULL;
    }
	a->running_sum[1] = a->running_sum[0] + (width*height);
	a->running_sum[2] = a->running_sum[1] + (width*height);

    return (void*) a;
}

void average_free(void *ptr) 
{
    average_t *a = (average_t*) ptr;
    if(!a)
        return;
    if(a->running_sum[0])
	    free(a->running_sum[0]);
    free(a);
}	

static inline uint8_t clamp_u8(double v)
{
    if (v <= 0.0)   return 0;
    if (v >= 255.0) return 255;
    return (uint8_t)v;
}


void average_apply(void *ptr, VJFrame *frame, int *args) {
    int max_sum = args[0];
    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    average_t *a = (average_t *)ptr;

    float *restrict running_sum[3];
    running_sum[0] = a->running_sum[0] + frame->offset;
    running_sum[1] = a->running_sum[1] + frame->offset;
    running_sum[2] = a->running_sum[2] + frame->offset;

    const float w  = 1.0 / max_sum;
    const float iw = 1.0 - w;

    for (int i = 0; i < len; i++) {
        running_sum[0][i] = iw * running_sum[0][i] + w * Y[i];
        running_sum[1][i] = iw * running_sum[1][i] + w * (Cb[i] - 128);
        running_sum[2][i] = iw * running_sum[2][i] + w * (Cr[i] - 128);
        Y[i]  = clamp_u8(running_sum[0][i]);
        Cb[i] = clamp_u8(128.0 + running_sum[1][i]);
        Cr[i] = clamp_u8(128.0 + running_sum[2][i]);
    }
}
