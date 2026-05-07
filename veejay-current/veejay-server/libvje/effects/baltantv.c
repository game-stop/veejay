/* 
 * Linux VeeJay
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * BaltanTV - like StreakTV, but following for a long time
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 * Ported to veejay by Niels Elburg 
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
#include "baltantv.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define PLANES 64
#define MAX_TAPS 8

typedef struct
{
    uint8_t *historyY;
    int16_t *historyU;
    int16_t *historyV;

    int plane;
    int frame_size;
    int uv_size;

    int n_threads;
} baltantv_t;

vj_effect *baltantv_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;

    ve->defaults = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->defaults[0] = 8;

    ve->limits[0][1] = 2;
    ve->limits[1][1] = MAX_TAPS;
    ve->defaults[1] = 4;

    ve->limits[0][2] = 32;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 180;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3] = 128;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->defaults[4] = 96;

    ve->description = "BaltanTV";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description =
        vje_build_param_list(
            ve->num_params,
            "Stride",
            "Temporal Taps",
            "Decay",
            "Feedback",
            "Chroma Persistence"
        );

    return ve;
}


void *baltantv_malloc(int w, int h)
{
    baltantv_t *b = (baltantv_t*) vj_calloc(sizeof(baltantv_t));

    if(!b)
        return NULL;

    const int len = w * h;

    b->frame_size = len;
    b->uv_size = len;

    b->historyY = (uint8_t*) vj_calloc(sizeof(uint8_t) * len * PLANES);
    b->historyU = (int16_t*) vj_calloc(sizeof(int16_t) * len * PLANES);

    b->historyV = (int16_t*) vj_calloc(sizeof(int16_t) * len * PLANES);

    if(!b->historyY || !b->historyU || !b->historyV)
    {
        baltantv_free(b);
        return NULL;
    }

    b->n_threads = vje_advise_num_threads(len);

    return b;
}

void baltantv_free(void *ptr)
{
    baltantv_t *b = (baltantv_t*) ptr;

    if(!b)
        return;

    free(b->historyY);
    free(b->historyU);
    free(b->historyV);

    free(b);
}

void baltantv_apply(void *ptr, VJFrame *frame, int *args)
{
    baltantv_t *b = (baltantv_t*) ptr;

    const int stride        = args[0];
    const int taps          = args[1];
    const int decay         = args[2];
    const int feedback      = args[3];
    const int chromaPersist = args[4];

    const int len    = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    uint8_t *inY = Y;
    uint8_t *inU = U;
    uint8_t *inV = V;

    uint8_t *restrict dstY = b->historyY + (b->plane * len);
    int16_t *restrict dstU = b->historyU + (b->plane * uv_len);
    int16_t *restrict dstV = b->historyV + (b->plane * uv_len);

    const int plane = b->plane;

    #pragma omp parallel num_threads(b->n_threads)
    {
    
        #pragma omp for simd schedule(static)
        for(int i = 0; i < len; i++)
            dstY[i] = inY[i];

        #pragma omp for simd schedule(static)
        for(int i = 0; i < uv_len; i++)
        {
            dstU[i] = inU[i] - 128;
            dstV[i] = inV[i] - 128;
        }

        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            int accumY = 0;

            for(int t = 0; t < taps; t++)
            {
                int idx = (plane - (t * stride) + PLANES) & (PLANES - 1);
                accumY += b->historyY[idx * len + i];
            }

            accumY = (accumY * decay) >> 8;

            int finalY =
                ((inY[i] * (255 - feedback)) +
                 (accumY * feedback / taps)) >> 8;

            Y[i] = CLAMP_Y(finalY);
        }

        #pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++)
        {
            int accumU = 0;
            int accumV = 0;

            for(int t = 0; t < taps; t++)
            {
                int idx = (plane - (t * stride) + PLANES) & (PLANES - 1);

                accumU += b->historyU[idx * uv_len + i];
                accumV += b->historyV[idx * uv_len + i];
            }

            accumU /= taps;
            accumV /= taps;

            accumU = (accumU * chromaPersist) >> 8;
            accumV = (accumV * chromaPersist) >> 8;

            U[i] = CLAMP_UV(accumU + 128);
            V[i] = CLAMP_UV(accumV + 128);
        }
    }

    b->plane = (plane + 1) & (PLANES - 1);
}