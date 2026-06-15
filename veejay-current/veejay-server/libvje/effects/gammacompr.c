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
#include "gammacompr.h"

#include <math.h>

/* 
 * Before applying Strong Luma Overlay, apply this effect ;)
 *
 */

typedef struct {
    int gamma_key;
    uint8_t table[256];
    int n_threads;
} gammacompr_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *gammacompr_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[0] = 3000;
    ve->defaults[1] = 240;
    ve->defaults[2] = 0;

    ve->limits[0][0] = 0; ve->limits[1][0] = 6000;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;
    ve->limits[0][2] = 0; ve->limits[1][2] = 255;

    ve->description = "Gamma Compression";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Gamma Compression", "White Threshold", "Black Threshold");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     1700, 4700, 14, 54,  800, 3000, 0,    82,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 128,  252,  12, 46, 1000, 3600, 0,    62,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS,                                                0,    96,   10, 38, 1200, 4200, 0,    48
    );

    return ve;
}

void *gammacompr_malloc(int w, int h)
{
    gammacompr_t *g = (gammacompr_t*) vj_calloc(sizeof(gammacompr_t));

    if(!g)
        return NULL;

    g->gamma_key = -1;
    g->n_threads = vje_advise_num_threads(w * h);

    return (void*) g;
}   

void gammacompr_free(void *ptr) 
{
    free(ptr);
}

static void gammacompr_setup(gammacompr_t *g, int value)
{
    const double gamma_value = ((double)value - 3000.0) * 0.001;

    for(int i = 0; i < 256; i++) {
        double v = (double)i * (1.0 / 256.0);
        int y = (int)(pow(v, gamma_value + ((double)i * 0.01)) * 256.0 + 0.5);

        g->table[i] = (uint8_t)clampi(y, 0, 255);
    }

    g->gamma_key = value;
}

void gammacompr_apply(void *ptr, VJFrame *frame, int *args)
{
    gammacompr_t *g = (gammacompr_t*) ptr;

    const int value = args[0];
    const int white_threshold = args[1];
    const int black_threshold = args[2];
    const int len = frame->len;

    if(value != g->gamma_key)
        gammacompr_setup(g, value);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict table = g->table;

#pragma omp parallel num_threads(g->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = table[Y[i]];

        if(white_threshold > 0 || black_threshold > 0) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int y = Y[i];
                const int white_mask = -(y > white_threshold);
                const int black_mask = -(y < black_threshold);
                const int mask = white_mask | black_mask;

                U[i] = (uint8_t)((U[i] & ~mask) | (128 & mask));
                V[i] = (uint8_t)((V[i] & ~mask) | (128 & mask));
            }
        }
    }
}
