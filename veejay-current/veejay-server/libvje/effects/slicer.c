/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "slicer.h"

typedef struct {
    int *slice_xshift;
    int *slice_yshift;
    int *prev_slice_xshift;
    int *prev_slice_yshift;

    uint8_t *tmp[3];

    int last_period;
    int current_period;
    int have_shift;

    uint32_t seed;
    int n_threads;
} slicer_t;

static inline int slicer_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint32_t slicer_rand(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 0x6d2b79f5u;
    return *state;
}

static inline int slicer_rand_range(uint32_t *state, int lo, int hi)
{
    if(hi <= lo)
        return lo;

    return lo + (int)(slicer_rand(state) % (uint32_t)(hi - lo + 1));
}

vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = w;
    ve->limits[0][1] = 1; ve->limits[1][1] = h;
    ve->limits[0][2] = 0; ve->limits[1][2] = 128;
    ve->limits[0][3] = 0; ve->limits[1][3] = 500;
    ve->limits[0][4] = 0; ve->limits[1][4] = 1;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100;
    ve->limits[0][6] = 0; ve->limits[1][6] = 100;

    int min_dim = (w < h) ? w : h;
    int max_block_size = min_dim / 2;

    if(max_block_size < 4)
        max_block_size = 4;

    if(max_block_size > 512)
        max_block_size = 512;

    int max_shift = 0;
    int bs = 1;

    while((bs << 1) <= max_block_size && max_shift < 9) {
        bs <<= 1;
        max_shift++;
    }

    if(max_shift < 2)
        max_shift = 2;
    if(max_shift > 9)
        max_shift = 9;

    ve->limits[0][7] = 2;
    ve->limits[1][7] = max_shift;

    ve->defaults[0] = 16;
    ve->defaults[1] = 16;
    ve->defaults[2] = 8;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;
    ve->defaults[6] = 50;
    ve->defaults[7] = 5;

    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Width",
        "Height",
        "Shatter",
        "Period",
        "Mode",
        "Smoothness",
        "Dominance",
        "Block Size"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][4],
        4,
        "Clip",
        "Wrap"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 4,                  96,                 6, 22, 1800, 4200, 900, 30,    /* Width */
        VJ_BEAT_WARP,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 4,                  96,                 6, 22, 1800, 4200, 900, 30,    /* Height */
        VJ_BEAT_TURBULENCE,VJ_BEAT_F_CONTINUOUS,                                                    0,                  96,                 10,38, 1000, 2600, 0,   62,    /* Shatter */
        VJ_BEAT_SPEED,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                               0,                  240,                6, 22, 1800, 4200, 900, 30,    /* Period */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_MEMORY,   VJ_BEAT_F_CONTINUOUS,                                                    0,                  82,                 8, 32, 1200, 3200, 0,   50,    /* Smoothness */
        VJ_BEAT_SOURCE_MIX,VJ_BEAT_F_CONTINUOUS,                                                   0,                  100,                8, 30, 1200, 3000, 0,   45,    /* Dominance */
        VJ_BEAT_GRID_SIZE,VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                               2,                  max_shift,          6, 22, 2200, 5200, 1800, 25     /* Block Size */
    );

    return ve;
}

static void slicer_free_fields(slicer_t *s)
{
    if(!s)
        return;

    if(s->slice_xshift)      free(s->slice_xshift);
    if(s->slice_yshift)      free(s->slice_yshift);
    if(s->prev_slice_xshift) free(s->prev_slice_xshift);
    if(s->prev_slice_yshift) free(s->prev_slice_yshift);
    if(s->tmp[0])            free(s->tmp[0]);
}

static void recalc(
    slicer_t *s,
    int w,
    int h,
    const uint8_t *restrict Yinp,
    int v1,
    int v2,
    int shatter,
    uint32_t seed,
    int smoothness
) {
    uint32_t state = seed ? seed : 0x1234abcdU;

    v1 = slicer_clampi(v1, 1, w);
    v2 = slicer_clampi(v2, 1, h);
    shatter = slicer_clampi(shatter, 0, 128);
    smoothness = slicer_clampi(smoothness, 0, 100);

    if(s->have_shift) {
        veejay_memcpy(s->prev_slice_xshift, s->slice_xshift, sizeof(int) * h);
        veejay_memcpy(s->prev_slice_yshift, s->slice_yshift, sizeof(int) * w);
    } else {
        veejay_memset(s->prev_slice_xshift, 0, sizeof(int) * h);
        veejay_memset(s->prev_slice_yshift, 0, sizeof(int) * w);
    }

    const int half_x = v1 >> 1;
    const int half_y = v2 >> 1;
    const int scale_num = 100 + (shatter * 2);

    int run = 0;
    int shift = 0;

    for(int x = 0; x < w; x++) {
        if(run <= 0) {
            const int base = slicer_rand_range(&state, -half_x, half_x);
            shift = (base * scale_num) / 100;

            int sample = Yinp ? Yinp[x] : 0;
            int span = half_x > 0 ? half_x : 1;
            run = 1 + (sample % span);
        } else {
            run--;
        }

        s->slice_yshift[x] = s->have_shift
            ? ((shift * (100 - smoothness)) + (s->prev_slice_yshift[x] * smoothness)) / 100
            : shift;
    }

    run = 0;
    shift = 0;

    for(int y = 0; y < h; y++) {
        if(run <= 0) {
            const int base = slicer_rand_range(&state, -half_y, half_y);
            shift = (base * scale_num) / 100;

            int sample = Yinp ? Yinp[y * w] : 0;
            int span = half_y > 0 ? half_y : 1;
            run = 1 + (sample % span);
        } else {
            run--;
        }

        s->slice_xshift[y] = s->have_shift
            ? ((shift * (100 - smoothness)) + (s->prev_slice_xshift[y] * smoothness)) / 100
            : shift;
    }

    s->have_shift = 1;
}

void *slicer_malloc(int width, int height)
{
    slicer_t *s = (slicer_t*) vj_calloc(sizeof(slicer_t));
    if(!s)
        return NULL;

    const size_t frame_sz = (size_t)width * (size_t)height;

    s->slice_xshift      = (int*) vj_malloc(sizeof(int) * height);
    s->slice_yshift      = (int*) vj_malloc(sizeof(int) * width);
    s->prev_slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    s->prev_slice_yshift = (int*) vj_malloc(sizeof(int) * width);

    s->tmp[0] = (uint8_t*) vj_malloc(frame_sz * 3u);
    if(s->tmp[0]) {
        s->tmp[1] = s->tmp[0] + frame_sz;
        s->tmp[2] = s->tmp[1] + frame_sz;
    }

    if(!s->slice_xshift || !s->slice_yshift ||
       !s->prev_slice_xshift || !s->prev_slice_yshift ||
       !s->tmp[0])
    {
        slicer_free_fields(s);
        free(s);
        return NULL;
    }

    veejay_memset(s->slice_xshift, 0, sizeof(int) * height);
    veejay_memset(s->slice_yshift, 0, sizeof(int) * width);
    veejay_memset(s->prev_slice_xshift, 0, sizeof(int) * height);
    veejay_memset(s->prev_slice_yshift, 0, sizeof(int) * width);

    s->last_period = -1;
    s->current_period = 0;
    s->have_shift = 0;
    s->seed = 0x6d2b79f5u ^ (uint32_t)(width * 73856093u) ^ (uint32_t)(height * 19349663u);

    s->n_threads = vje_advise_num_threads((int)frame_sz);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return s;
}

void slicer_free(void *ptr)
{
    slicer_t *s = (slicer_t*) ptr;

    if(s) {
        slicer_free_fields(s);
        free(s);
    }
}

static inline int slicer_wrapi(int v, int max)
{
    if(max <= 1)
        return 0;

    v %= max;

    if(v < 0)
        v += max;

    return v;
}

void slicer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    slicer_t *s = (slicer_t*) ptr;

    if(!s || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int val1        = slicer_clampi(args[0], 1, width);
    int val2        = slicer_clampi(args[1], 1, height);
    int shatter     = slicer_clampi(args[2], 0, 128);
    int period      = slicer_clampi(args[3], 0, 500);
    int mode        = args[4] ? 1 : 0;
    int smoothness  = slicer_clampi(args[5], 0, 100);
    int dominance   = slicer_clampi(args[6], 0, 100);
    int block_shift = slicer_clampi(args[7], 2, 9);

    if(s->last_period != period) {
        s->last_period = period;
        s->current_period = 0;
    }

    if(s->current_period <= 0 || !s->have_shift) {
        s->seed ^= (uint32_t)(frame->timecode * 1000003.0);
        s->seed ^= (uint32_t)(val1 * 0x45d9f3bu);
        s->seed ^= (uint32_t)(val2 * 0x119de1f3u);
        s->seed ^= (uint32_t)(shatter * 0x27d4eb2du);

        recalc(s, width, height, frame->data[0], val1, val2, shatter, s->seed, smoothness);

        s->current_period = period > 0 ? period : 1;
    }

    s->current_period--;

    uint8_t *restrict dY  = frame->data[0];
    uint8_t *restrict dCb = frame->data[1];
    uint8_t *restrict dCr = frame->data[2];

    veejay_memcpy(s->tmp[0], dY,  len);
    veejay_memcpy(s->tmp[1], dCb, len);
    veejay_memcpy(s->tmp[2], dCr, len);

    const uint8_t *restrict s1Y  = s->tmp[0];
    const uint8_t *restrict s1Cb = s->tmp[1];
    const uint8_t *restrict s1Cr = s->tmp[2];

    const uint8_t *restrict s2Y  = frame2->data[0];
    const uint8_t *restrict s2Cb = frame2->data[1];
    const uint8_t *restrict s2Cr = frame2->data[2];

    int *restrict sx_row = s->slice_xshift;
    int *restrict sy_col = s->slice_yshift;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int shift_x = sx_row[y];

        for(int x = 0; x < width; x++) {
            int ix = x + shift_x;
            int iy = y + sy_col[x];

            const int dst = row + x;

            if(mode == 0) {
                if((unsigned)ix >= (unsigned)width ||
                   (unsigned)iy >= (unsigned)height)
                {
                    dY[dst]  = s1Y[dst];
                    dCb[dst] = s1Cb[dst];
                    dCr[dst] = s1Cr[dst];
                    continue;
                }
            } else {
                ix = slicer_wrapi(ix, width);
                iy = slicer_wrapi(iy, height);
            }

            const int src = iy * width + ix;

            const int chunk_x = ix >> block_shift;
            const int chunk_y = iy >> block_shift;
            const uint32_t hash = ((uint32_t)chunk_x * 104729u) ^ ((uint32_t)chunk_y * 131071u);
            const int use_s2 = (int)(hash % 100u) < dominance;

            const uint8_t *restrict srcY  = use_s2 ? s2Y  : s1Y;
            const uint8_t *restrict srcCb = use_s2 ? s2Cb : s1Cb;
            const uint8_t *restrict srcCr = use_s2 ? s2Cr : s1Cr;

            dY[dst]  = srcY[src];
            dCb[dst] = srcCb[src];
            dCr[dst] = srcCr[src];
        }
    }
}