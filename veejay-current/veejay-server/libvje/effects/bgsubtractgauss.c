/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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

#include <float.h>
#include "common.h"
#include "bgsubtractgauss.h"
#include <libsubsample/subsample.h>

typedef struct {
    uint8_t *static_bg__;
    uint8_t *static_bg_frame__[4];
    double *pMu;
    double *pVar;
    uint32_t bg_n;
    int auto_hist;
    int n_threads;
} bgsubtract_t;

vj_effect *bgsubtractgauss_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 1000; ve->defaults[0] = 10;
    ve->limits[0][1] = 0; ve->limits[1][1] = 2000; ve->defaults[1] = 600;
    ve->limits[0][2] = 1; ve->limits[1][2] = 5000; ve->defaults[2] = 200;
    ve->limits[0][3] = 0; ve->limits[1][3] = 3;    ve->defaults[3] = 1;
    ve->limits[0][4] = 1; ve->limits[1][4] = 100;  ve->defaults[4] = 1;

    ve->description = "Gaussian Adaptive Background";
    ve->sub_format = -1;
    ve->has_user = 1;
    ve->static_bg = 1;
    ve->global = 1;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(ve->num_params, "Learning Rate", "Threshold", "Min Noise", "Mode", "Update Period");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][3], 3, "Show Background Model", "Black Background", "Alpha Masking", "Binary (B&W) Mask");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 80,                 950,                12, 48,  900, 3000, 0,    72,
        VJ_BEAT_DETAIL,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MEMORY,       VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *bgsubtractgauss_malloc(int width, int height)
{
    bgsubtract_t *b = (bgsubtract_t*) vj_calloc(sizeof(bgsubtract_t));

    if(!b)
        return NULL;

    const int plane_size = width * height;

    b->static_bg__ = (uint8_t*) vj_malloc(plane_size * 4);
    b->pMu = (double*) vj_malloc(sizeof(double) * plane_size);
    b->pVar = (double*) vj_malloc(sizeof(double) * plane_size);

    if(!b->static_bg__ || !b->pMu || !b->pVar) {
        if(b->static_bg__)
            free(b->static_bg__);
        if(b->pMu)
            free(b->pMu);
        if(b->pVar)
            free(b->pVar);
        free(b);
        return NULL;
    }

    b->static_bg_frame__[0] = b->static_bg__;
    b->static_bg_frame__[1] = b->static_bg_frame__[0] + plane_size;
    b->static_bg_frame__[2] = b->static_bg_frame__[1] + plane_size;
    b->static_bg_frame__[3] = b->static_bg_frame__[2] + plane_size;

    veejay_memset(b->static_bg_frame__[0], 16, plane_size);
    veejay_memset(b->static_bg_frame__[1], 128, plane_size);
    veejay_memset(b->static_bg_frame__[2], 128, plane_size);
    veejay_memset(b->static_bg_frame__[3], 0, plane_size);

    b->bg_n = 0;
    b->n_threads = vje_advise_num_threads(plane_size);

    return b;
}

void bgsubtractgauss_free(void *ptr)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b)
        return;

    if(b->static_bg__)
        free(b->static_bg__);
    if(b->pMu)
        free(b->pMu);
    if(b->pVar)
        free(b->pVar);

    free(b);
}

int bgsubtractgauss_prepare(void *ptr, VJFrame *frame)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b || !frame || !frame->data[0])
        return 0;

    const int len = frame->len;
    uint8_t *Y = frame->data[0];

    #pragma omp parallel for num_threads(b->n_threads) schedule(static)
    for(int i = 0; i < len; i++) {
        b->pMu[i] = (double)Y[i];
        b->pVar[i] = 100.0;
        b->static_bg_frame__[0][i] = Y[i];
    }

    if(frame->ssm) {
        veejay_memcpy(b->static_bg_frame__[1], frame->data[1], len);
        veejay_memcpy(b->static_bg_frame__[2], frame->data[2], len);
    }
    else {
        veejay_memcpy(b->static_bg_frame__[1], frame->data[1], frame->uv_len);
        veejay_memcpy(b->static_bg_frame__[2], frame->data[2], frame->uv_len);
        chroma_supersample(SSM_422_444, frame, b->static_bg_frame__);
    }

    b->bg_n = 0;

    return 1;
}

uint8_t* bgsubtractgauss_get_bg_frame(void *ptr, unsigned int plane)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b || !b->static_bg__ || plane > 3)
        return NULL;

    return b->static_bg_frame__[plane];
}

void bgsubtractgauss_apply(void *ptr, VJFrame *frame, int *args)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    const int alpha_arg = args[0];
    const int threshold_arg = args[1];
    const int min_noise_arg = args[2];
    int mode = args[3];
    const int period = args[4];

    const double alpha = (double)alpha_arg / 10000.0;
    const double threshold = (double)threshold_arg / 10.0;
    const double min_noise = (double)min_noise_arg / 10.0;

    const int len = frame->len;
    const int n_threads = b->n_threads;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    uint8_t *restrict BG_Y = b->static_bg_frame__[0];
    double *restrict mu = b->pMu;
    double *restrict var = b->pVar;

    if(mode == 2 && !A)
        mode = 3;

    b->bg_n++;

    const int do_update = (b->bg_n % (uint32_t)period) == 0;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        double m = mu[i];
        double v = var[i];

        if(v < min_noise)
            v = min_noise;

        const double diff = (double)Y[i] - m;
        const double dist_sq = (diff * diff) / v;
        const int is_fg = dist_sq > threshold;

        if(!is_fg && do_update)
        {
            m += alpha * diff;
            v += alpha * (diff * diff - v);

            if(v < min_noise)
                v = min_noise;

            mu[i] = m;
            var[i] = v;
            BG_Y[i] = (uint8_t)(m < 0.0 ? 0.0 : (m > 255.0 ? 255.0 : m));
        }
        else if(v != var[i])
        {
            var[i] = v;
        }

        if(mode == 0)
        {
            Y[i] = BG_Y[i];
        }
        else if(mode == 1)
        {
            if(!is_fg)
                Y[i] = 16;
        }
        else if(mode == 2)
        {
            A[i] = is_fg ? 255 : 0;
        }
        else
        {
            Y[i] = is_fg ? 255 : 0;
        }
    }

    if(mode == 1 || mode == 3)
    {
        const int uv_len = frame->uv_len;
        veejay_memset(U, 128, uv_len);
        veejay_memset(V, 128, uv_len);
    }
}
