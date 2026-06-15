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
#include "dither.h"

typedef struct {
    int *dith;
    int w;
    int last_size;
    int last_mode;
    uint32_t seed;
    int n_threads;
} dither_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t dither_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

vj_effect *dither_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 2;
    ve->defaults[1] = 0;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = (w > 2) ? (w - 1) : 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Matrix Dithering";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Value", "Mode");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Static", "Random");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  18,                 4,  14, 3200, 8600, 2400, 22,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *dither_malloc(int w, int h)
{
    dither_t *d = (dither_t*) vj_calloc(sizeof(dither_t));

    if(!d)
        return NULL;

    if(w < 2)
        w = 2;

    d->dith = (int*) vj_calloc(sizeof(int) * w * w);

    if(!d->dith) {
        free(d);
        return NULL;
    }

    d->w = w;
    d->last_size = -1;
    d->last_mode = -1;
    d->seed = 0x1234abcdU;
    d->n_threads = vje_advise_num_threads(w * h);
    return d;
}

void dither_free(void *ptr)
{
    dither_t *d = (dither_t*) ptr;

    if(!d)
        return;

    if(d->dith)
        free(d->dith);

    free(d);
}

static void dither_build_matrix(dither_t *d, int size, int random_on)
{
    int *restrict dith = d->dith;
    const uint32_t seed = random_on ? (++d->seed * 747796405U + 2891336453U) : 0x6d2b79f5U;

    for(int y = 0; y < size; y++)
    {
        for(int x = 0; x < size; x++)
        {
            uint32_t h = dither_hash(seed ^ (uint32_t)(x * 374761393U) ^ (uint32_t)(y * 668265263U) ^ (uint32_t)(size * 1442695041U));

            dith[y * size + x] = (int)(h % (uint32_t)size);
        }
    }

    d->last_size = size;
    d->last_mode = random_on;
}

void dither_apply(void *ptr, VJFrame *frame, int *args)
{
    dither_t *dh = (dither_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;


    const int size = clampi(args[0], 2, dh->w);
    const int random_on = args[1];
    
    uint8_t *restrict Y = frame->data[0];

    if(dh->last_size != size || dh->last_mode != random_on || random_on)
        dither_build_matrix(dh, size, random_on);

    const int *restrict dith = dh->dith;

    #pragma omp parallel for schedule(static) num_threads(dh->n_threads)
    for(int y = 0; y < height; y++)
    {
        const int j = y % size;
        const int row_base = y * width;
        const int row_dither = j * size;

        for(int x = 0; x < width; x++)
        {
            const int i = x % size;
            const int d = dith[row_dither + i] << 4;
            const int v = (int)Y[row_base + x] + d;

            Y[row_base + x] = (uint8_t)((v >> 7) << 7);
        }
    }
}
