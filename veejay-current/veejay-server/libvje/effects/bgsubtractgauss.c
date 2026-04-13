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

#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <float.h>
#include <veejaycore/vjmem.h>
#include <math.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vj-msg.h>
#include <libsubsample/subsample.h>
#include "bgsubtractgauss.h"
#include "common.h"

typedef struct {
    uint8_t *static_bg__;
    uint8_t *static_bg_frame__[4];
    double *pMu;   // Mean (Luma)
    double *pVar;  // Variance (Noise model)
    uint32_t bg_n;
    int auto_hist;
} bgsubtract_t;

vj_effect *bgsubtractgauss_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;   /* Learning Rate (Alpha) */
    ve->limits[1][0] = 1000;
    ve->limits[0][1] = 0;   /* Threshold (Mahalanobis Distance) */
    ve->limits[1][1] = 2000;
    ve->limits[0][2] = 1;   /* Min Noise Variance */
    ve->limits[1][2] = 5000;
    ve->limits[0][3] = 0;   /* Mode */
    ve->limits[1][3] = 3; 
    ve->limits[0][4] = 1;   /* Update Period */
    ve->limits[1][4] = 100;

    ve->defaults[0] = 10;   // Slow learning rate
    ve->defaults[1] = 600;  // Balanced threshold
    ve->defaults[2] = 200;  // Floor for variance
    ve->defaults[3] = 1;    // Default: Black Background
    ve->defaults[4] = 1;

    ve->description = "Gaussian Adaptive Background";
    ve->sub_format = -1;
    ve->has_user = 1;
    ve->static_bg = 1;
    ve->global = 1;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

    ve->param_description = vje_build_param_list( ve->num_params, "Learning Rate", "Threshold", "Min Noise", "Mode", "Update Period");
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3,
                    "Show Background Model",
                    "Black Background",
                    "Alpha Masking",
                    "Binary (B&W) Mask" );

    return ve;
}

void *bgsubtractgauss_malloc(int width, int height)
{
    bgsubtract_t *b = (bgsubtract_t*) vj_calloc(sizeof(bgsubtract_t));
    if(!b) return NULL;

    int plane_size = width * height;
    b->static_bg__ = (uint8_t*) vj_malloc( plane_size * 4 );
    b->pMu = (double*) vj_malloc( sizeof(double) * plane_size );
    b->pVar = (double*) vj_malloc( sizeof(double) * plane_size );

    if(!b->static_bg__ || !b->pMu || !b->pVar) {
        if(b->static_bg__) free(b->static_bg__);
        if(b->pMu) free(b->pMu);
        if(b->pVar) free(b->pVar);
        free(b);
        return NULL;
    }

    b->static_bg_frame__[0] = b->static_bg__;
    b->static_bg_frame__[1] = b->static_bg_frame__[0] + plane_size;
    b->static_bg_frame__[2] = b->static_bg_frame__[1] + plane_size;
    b->static_bg_frame__[3] = b->static_bg_frame__[2] + plane_size;

    veejay_memset( b->static_bg_frame__[1], 128, plane_size );
    veejay_memset( b->static_bg_frame__[2], 128, plane_size );
    b->bg_n = 0;
    return (void*) b;
}

void bgsubtractgauss_free(void *ptr) {
    bgsubtract_t *b = (bgsubtract_t*) ptr;
    free(b->static_bg__);
    free(b->pMu);
    free(b->pVar);
    free(b);
}

int bgsubtractgauss_prepare(void *ptr, VJFrame *frame)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    
    for(int i = 0; i < len; i++) {
        b->pMu[i] = (double)Y[i];
        b->pVar[i] = 100.0;
        b->static_bg_frame__[0][i] = Y[i];
    }

    if( frame->ssm ) {
        veejay_memcpy( b->static_bg_frame__[1], frame->data[1], len );
        veejay_memcpy( b->static_bg_frame__[2], frame->data[2], len );
    } else {
        veejay_memcpy( b->static_bg_frame__[1], frame->data[1], frame->uv_len );
        veejay_memcpy( b->static_bg_frame__[2], frame->data[2], frame->uv_len );
        chroma_supersample( SSM_422_444, frame, b->static_bg_frame__ );
    }
    return 1;
}

uint8_t* bgsubtractgauss_get_bg_frame(void *ptr, unsigned int plane)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;
       if( b->static_bg__ == NULL )
               return NULL;
       return b->static_bg_frame__[ plane ];
}

void bgsubtractgauss_apply(void *ptr, VJFrame *frame, int *args)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    const double alpha     = (double)args[0] / 10000.0;
    const double threshold = (double)args[1] / 10.0;
    const double min_noise = (double)args[2] / 10.0;
    const int mode         = args[3];
    const int period       = args[4];

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    uint8_t *restrict BG_Y = b->static_bg_frame__[0];
    double  *restrict mu   = b->pMu;
    double  *restrict var  = b->pVar;

    b->bg_n++;
    const int do_update = (b->bg_n % period == 0);

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++)
    {
        double m = mu[i];
        double v = var[i];

        double diff = (double)Y[i] - m;
        double dist_sq = (diff * diff) / v;

        const int is_fg = (dist_sq > threshold);

        if (!is_fg && do_update)
        {
            m += alpha * diff;
            v += alpha * (diff * diff - v);
            if (v < min_noise) v = min_noise;

            mu[i]  = m;
            var[i] = v;
            BG_Y[i] = (uint8_t)m;
        }

        if (mode == 0)
        {
            Y[i] = BG_Y[i];
        }
        else if (mode == 1)
        {
            if (!is_fg)
            {
                Y[i] = 16;
            }
        }
        else if (mode == 2)
        {
            A[i] = is_fg ? 255 : 0;
        }
        else
        {
            Y[i] = is_fg ? 255 : 0;
        }
    }

    if(mode == 1 || mode == 2)
    {
        const int uv_len = frame->uv_len;
		veejay_memset(U, 128, uv_len);
		veejay_memset(V, 128, uv_len);
	}
}
