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
    int n_threads;
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

    a->n_threads = vje_advise_num_threads(width * height);

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

void average_apply(void *ptr, VJFrame *frame, int *args)
{
    average_t *a = (average_t *)ptr;

    const int max_sum = args[0];
    const int len = frame->len;
    const int n_threads = a->n_threads;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    float *restrict rsY  = a->running_sum[0] + frame->offset;
    float *restrict rsCb = a->running_sum[1] + frame->offset;
    float *restrict rsCr = a->running_sum[2] + frame->offset;

    const float w  = 1.0f / (float)max_sum;
    const float iw = 1.0f - w;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++)
    {
        float y  = rsY[i];
        float cb = rsCb[i];
        float cr = rsCr[i];

        float inY  = (float)Y[i];
        float inCb = (float)Cb[i] - 128.0f;
        float inCr = (float)Cr[i] - 128.0f;

        y  = iw * y  + w * inY;
        cb = iw * cb + w * inCb;
        cr = iw * cr + w * inCr;

        rsY[i]  = y;
        rsCb[i] = cb;
        rsCr[i] = cr;

        Y[i]  = clamp_u8(y);
        Cb[i] = clamp_u8(128.0f + cb);
        Cr[i] = clamp_u8(128.0f + cr);
    }
}
