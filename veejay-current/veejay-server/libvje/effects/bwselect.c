
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
#include "bwselect.h"

vj_effect *bwselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 16;
    ve->defaults[1] = 235;
    ve->defaults[2] = 400;
    ve->defaults[3] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1000;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->description = "Black and White Mask by Threshold";
    
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user =0;
    ve->parallel = 0;
    
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

    ve->param_description = vje_build_param_list( ve->num_params, "Min Threshold", "Max Threshold", "Gamma", "To Alpha" );
    return ve;
}

typedef struct {    
    int last_gamma;
    uint8_t table[256];
} bwselect_t;

static void gamma_setup(bwselect_t *b, double gamma_value)
{
    int i;
    double val;

    for (i = 0; i < 256; i++) {
        val = i / 256.0;
        val = pow(val, gamma_value);
        val = 256.0 * val;
        b->table[i] = val;
    }
}

void *bwselect_malloc(int w, int h)
{
    bwselect_t *b = (bwselect_t*) vj_calloc(sizeof(bwselect_t));
    if(!b) {
        return NULL;
    }
    gamma_setup(b, 4.0 ); 
    return (void*) b;
}

void bwselect_free(void *ptr)
{
    bwselect_t *b = (bwselect_t*) ptr;
    free(b);
}


void bwselect_apply(void *ptr, VJFrame *frame, int *args) {
    int min_threshold = args[0];
    int max_threshold = args[1];
    int gamma = args[2];
    int mode  = args[3];

    bwselect_t *b = (bwselect_t*) ptr;
    uint8_t *table = b->table;

    const int len = frame->len;
    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A  = frame->data[3];

    if (gamma != 0 && gamma != b->last_gamma) {
        gamma_setup(b, (double)gamma / 100.0);
        b->last_gamma = gamma;
    }

    if (mode == 0) {
#pragma omp simd
        for (int i = 0; i < len; i++) {
            uint8_t p = (gamma == 0) ? Y[i] : table[Y[i]];
            uint8_t cond = (p > min_threshold) & (p < max_threshold);
            Y[i] = (cond * pixel_Y_hi_) | ((1 - cond) * pixel_Y_lo_);
        }
        veejay_memset(Cb, 128, (frame->ssm ? len : frame->uv_len));
        veejay_memset(Cr, 128, (frame->ssm ? len : frame->uv_len));
    } else {
#pragma omp simd
        for (int i = 0; i < len; i++) {
            uint8_t p = (gamma == 0) ? Y[i] : table[Y[i]];
            uint8_t cond = (p > min_threshold) & (p < max_threshold);
            A[i] = cond * 0xff;
        }
    }
}
