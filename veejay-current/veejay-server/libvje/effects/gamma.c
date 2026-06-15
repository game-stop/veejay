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
#include "gamma.h"

#include <math.h>

typedef struct {
    int gamma_key;
    uint8_t table[256];
    int n_threads;
} gamma_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *gamma_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 1;
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

    ve->defaults[0] = 124;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 500;

    ve->description = "Gamma Correction";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Gamma");
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 45, 260, 14, 54, 800, 3000, 0, 78
    );
    
    return ve;
}

void *gamma_malloc(int w, int h)
{
    gamma_t *g = (gamma_t*) vj_calloc(sizeof(gamma_t));

    if(!g)
        return NULL;

    g->gamma_key = -1;
    g->n_threads = vje_advise_num_threads(w * h);

    return (void*) g;
}

void gamma_free(void *ptr)
{
    free(ptr);
}

static void gamma_setup(gamma_t *g, int gamma_value)
{
    const double gamma_f = (double)gamma_value * 0.01;

    for(int i = 0; i < 256; i++) {
        double v = (double)i * (1.0 / 255.0);
        int y = (int)(pow(v, gamma_f) * 255.0 + 0.5);

        g->table[i] = (uint8_t)clampi(y, 0, 255);
    }

    g->gamma_key = gamma_value;
}

void gamma_apply(void *ptr, VJFrame *frame, int *args)
{
    gamma_t *g = (gamma_t*) ptr;

    const int gamma_value = args[0];
    const int len = frame->len;

    if(gamma_value != g->gamma_key)
        gamma_setup(g, gamma_value);

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict table = g->table;

#pragma omp parallel for schedule(static) num_threads(g->n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = table[Y[i]];
}
