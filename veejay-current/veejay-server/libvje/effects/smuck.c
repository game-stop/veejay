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

/* 
 * Linux VeeJay - Effectv's SmuckTV
 */

#include "common.h"
#include "smuck.h"

typedef struct {
    uint8_t *tmp[3];
    uint32_t seed;
    int n_threads;
} smuck_t;

static inline int smuck_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint32_t smuck_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

vj_effect *smuck_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 12;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 17;

    ve->defaults[1] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->defaults[2] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->defaults[3] = 2;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;

    ve->description = "SmuckTV (Pro)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shimmer",
        "Full color",
        "Static seed",
        "Direction"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Luma only",
        "Full color"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Moving seed",
        "Static seed"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Horizontal",
        "Vertical",
        "Both"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_TURBULENCE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 0,                  17,                 6, 22, 1600, 3400, 700, 30,    /* Shimmer */
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Full color */
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Static seed */
        VJ_BEAT_SELECTOR,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Direction */
    );

    (void) w;
    (void) h;

    return ve;
}

void *smuck_malloc(int w, int h)
{
    smuck_t *s = (smuck_t*) vj_calloc(sizeof(smuck_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->tmp[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->tmp[0]) {
        free(s);
        return NULL;
    }

    s->tmp[1] = s->tmp[0] + len;
    s->tmp[2] = s->tmp[1] + len;

    s->seed = 0x1337BEEFU ^ (uint32_t)(w * 73856093u) ^ (uint32_t)(h * 19349663u);

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void smuck_free(void *ptr)
{
    smuck_t *s = (smuck_t*) ptr;
    if(!s)
        return;

    if(s->tmp[0])
        free(s->tmp[0]);

    free(s);
}

static void smuck_apply_plane(uint8_t *restrict dst,
                              const uint8_t *restrict src,
                              int w,
                              int h,
                              int shimmer,
                              int mx,
                              int my,
                              uint32_t seed,
                              int chroma,
                              int n_threads)
{
    const unsigned int shift = (unsigned int)(10 + shimmer);
    const unsigned int mask = chroma ? 0x3u : 0x7u;
    const int bias = chroma ? 1 : 3;
    const unsigned int yshift = shift + (chroma ? 2u : 3u);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int idx = row + x;
            const uint32_t r = smuck_hash_u32((uint32_t)idx ^ seed);

            const int dx = ((int)((r >> shift) & mask) - bias) * mx;
            const int dy = ((int)((r >> yshift) & mask) - bias) * my;

            int sx = x + dx;
            int sy = y + dy;

            sx = smuck_clampi(sx, 0, w - 1);
            sy = smuck_clampi(sy, 0, h - 1);

            dst[idx] = src[sy * w + sx];
        }
    }
}

void smuck_apply(void *ptr, VJFrame *frame, int *args)
{
    smuck_t *s = (smuck_t*) ptr;

    if(!s || !frame || !args || !frame->data[0])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    const int shimmer = smuck_clampi(args[0], 0, 17);
    const int full_color = args[1] ? 1 : 0;
    const int static_seed = args[2] ? 1 : 0;
    const int direction = smuck_clampi(args[3], 0, 2);

    const int mx = (direction == 0 || direction == 2) ? 1 : 0;
    const int my = (direction == 1 || direction == 2) ? 1 : 0;

    const uint32_t seed = static_seed ? 0x1337BEEFU : s->seed;

    uint8_t *restrict Y = frame->data[0];

    veejay_memcpy(s->tmp[0], Y, len);

    smuck_apply_plane(
        Y,
        s->tmp[0],
        w,
        h,
        shimmer,
        mx,
        my,
        seed,
        0,
        s->n_threads
    );

    if(full_color && frame->data[1] && frame->data[2]) {
        const int uv_len = frame->ssm ? len : frame->uv_len;
        const int uv_w = frame->ssm ? w : frame->uv_width;
        const int uv_h = frame->ssm ? h : frame->uv_height;

        if(uv_len > 0 && uv_w > 0 && uv_h > 0) {
            veejay_memcpy(s->tmp[1], frame->data[1], uv_len);
            veejay_memcpy(s->tmp[2], frame->data[2], uv_len);

            smuck_apply_plane(
                frame->data[1],
                s->tmp[1],
                uv_w,
                uv_h,
                shimmer,
                mx,
                my,
                seed ^ 0x9e3779b9U,
                1,
                s->n_threads
            );

            smuck_apply_plane(
                frame->data[2],
                s->tmp[2],
                uv_w,
                uv_h,
                shimmer,
                mx,
                my,
                seed ^ 0x85ebca6bU,
                1,
                s->n_threads
            );
        }
    }

    if(!static_seed)
        s->seed = smuck_hash_u32(s->seed + 0x6d2b79f5U);
}