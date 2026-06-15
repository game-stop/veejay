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
#include "bwselect.h"

typedef struct {
    int last_gamma;
    uint8_t table[256];
    int n_threads;
} bwselect_t;

vj_effect *bwselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255;  ve->defaults[0] = 16;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;  ve->defaults[1] = 235;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1000; ve->defaults[2] = 400;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;    ve->defaults[3] = 0;

    ve->description = "Black and White Mask by Threshold";
    ve->sub_format = -1;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(ve->num_params, "Min Threshold", "Max Threshold", "Gamma", "To Alpha");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 4,                  104,                10, 42, 1000, 3200, 0,    64,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     148,                255,                10, 42, 1000, 3200, 0,    64,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_LOG | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 80, 760, 8, 32, 1400, 4200, 0,    48,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

static void gamma_setup(bwselect_t *b, double gamma_value)
{
    for(int i = 0; i < 256; i++)
    {
        double val = (double)i / 255.0;

        val = pow(val, gamma_value) * 255.0;

        if(val < 0.0)
            val = 0.0;
        else if(val > 255.0)
            val = 255.0;

        b->table[i] = (uint8_t)(val + 0.5);
    }
}

void *bwselect_malloc(int w, int h)
{
    bwselect_t *b = (bwselect_t*) vj_calloc(sizeof(bwselect_t));

    if(!b)
        return NULL;

    gamma_setup(b, 4.0);
    b->last_gamma = 400;
    b->n_threads = vje_advise_num_threads(w * h);

    return b;
}

void bwselect_free(void *ptr)
{
    bwselect_t *b = (bwselect_t*) ptr;

    if(b)
        free(b);
}

void bwselect_apply(void *ptr, VJFrame *frame, int *args)
{
    bwselect_t *b = (bwselect_t*) ptr;

    int min_threshold = args[0];
    int max_threshold = args[1];
    const int gamma = args[2];
    int mode = args[3];
    const int len = frame->len;

    if(min_threshold > max_threshold)
    {
        const int tmp = min_threshold;
        min_threshold = max_threshold;
        max_threshold = tmp;
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];
    uint8_t *restrict table = b->table;

    if(mode == 1 && !A)
        mode = 0;

    if(gamma != 0 && gamma != b->last_gamma)
    {
        gamma_setup(b, (double)gamma / 100.0);
        b->last_gamma = gamma;
    }

    const int use_gamma = gamma != 0;

    if(mode == 0)
    {
        const int hi = pixel_Y_hi_;
        const int lo = pixel_Y_lo_;

        #pragma omp parallel for num_threads(b->n_threads) schedule(static)
        for(int i = 0; i < len; i++)
        {
            const uint8_t p = use_gamma ? table[Y[i]] : Y[i];
            const int cond = (p > min_threshold) & (p < max_threshold);

            Y[i] = (uint8_t)((-cond & hi) | (~(-cond) & lo));
        }

        veejay_memset(Cb, 128, frame->uv_len);
        veejay_memset(Cr, 128, frame->uv_len);

        return;
    }

    #pragma omp parallel for num_threads(b->n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        const uint8_t p = use_gamma ? table[Y[i]] : Y[i];
        const int cond = (p > min_threshold) & (p < max_threshold);

        A[i] = (uint8_t)(-cond);
    }
}
