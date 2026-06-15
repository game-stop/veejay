/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "scratcher.h"

#define SCRATCHER_PARAMS 5

#define P_OPACITY       0
#define P_BUFFER_LEN    1
#define P_PINGPONG      2
#define P_SCRATCH_MIX   3
#define P_CHROMA_AMOUNT 4

typedef struct {
    uint8_t *frame[3];

    int phase_q8;
    int direction;
    int last_pingpong;
    int last_n;
    int n_threads;

    float sm_opacity;
    float sm_buffer;
    float sm_mix;
    float sm_chroma;
} scratcher_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t scratcher_mix_y(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t scratcher_mix_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline void scratcher_smooth_to(float *state, float target, float a)
{
    *state += (target - *state) * a;
}

vj_effect *scratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SCRATCHER_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_OPACITY] = 0;       ve->limits[1][P_OPACITY] = 255;                      ve->defaults[P_OPACITY] = 150;
    ve->limits[0][P_BUFFER_LEN] = 1;    ve->limits[1][P_BUFFER_LEN] = MAX_SCRATCH_FRAMES - 1; ve->defaults[P_BUFFER_LEN] = 8;
    ve->limits[0][P_PINGPONG] = 0;      ve->limits[1][P_PINGPONG] = 1;                        ve->defaults[P_PINGPONG] = 1;
    ve->limits[0][P_SCRATCH_MIX] = 0;   ve->limits[1][P_SCRATCH_MIX] = 1000;                  ve->defaults[P_SCRATCH_MIX] = 1000;
    ve->limits[0][P_CHROMA_AMOUNT] = 0; ve->limits[1][P_CHROMA_AMOUNT] = 1000;                ve->defaults[P_CHROMA_AMOUNT] = 1000;

    ve->description = "Overlay Scratcher";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Scratch buffer",
        "PingPong",
        "Scratch Mix",
        "Chroma Amount"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_PINGPONG], P_PINGPONG, "Loop", "PingPong");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      96,                 245,                   14, 54,  800, 3000, 0,    76,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 3,                  MAX_SCRATCH_FRAMES - 1, 4,  14, 3200, 8600, 2400, 24,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,    0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SOURCE_MIX,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      420,                1000,                  12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      420,                1000,                  12, 46, 1000, 3600, 0,    68
    );

    (void)w;
    (void)h;

    return ve;
}

void *scratcher_malloc(int w, int h)
{
    scratcher_t *s = (scratcher_t*)vj_calloc(sizeof(scratcher_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->frame[0] = (uint8_t*)vj_malloc((size_t)len * 3u * (size_t)MAX_SCRATCH_FRAMES);

    if(!s->frame[0]) {
        free(s);
        return NULL;
    }

    s->frame[1] = s->frame[0] + ((size_t)len * (size_t)MAX_SCRATCH_FRAMES);
    s->frame[2] = s->frame[1] + ((size_t)len * (size_t)MAX_SCRATCH_FRAMES);

    veejay_memset(s->frame[0], pixel_Y_lo_, len * MAX_SCRATCH_FRAMES);
    veejay_memset(s->frame[1], 128, len * MAX_SCRATCH_FRAMES);
    veejay_memset(s->frame[2], 128, len * MAX_SCRATCH_FRAMES);

    s->phase_q8 = 0;
    s->direction = 1;
    s->last_pingpong = 1;
    s->last_n = 8;
    s->n_threads = vje_advise_num_threads(len);
    s->sm_opacity = 150.0f;
    s->sm_buffer = 8.0f;
    s->sm_mix = 1000.0f;
    s->sm_chroma = 1000.0f;

    return (void*)s;
}

void scratcher_free(void *ptr)
{
    scratcher_t *s = (scratcher_t*)ptr;

    free(s->frame[0]);
    free(s);
}

static void scratcher_store_current(scratcher_t *s, VJFrame *src, int slot)
{
    const int len = src->len;
    const int uv_len = src->ssm ? len : src->uv_len;

    uint8_t *restrict dy = s->frame[0] + ((size_t)len * (size_t)slot);
    uint8_t *restrict du = s->frame[1] + ((size_t)uv_len * (size_t)slot);
    uint8_t *restrict dv = s->frame[2] + ((size_t)uv_len * (size_t)slot);

    veejay_memcpy(dy, src->data[0], len);
    veejay_memcpy(du, src->data[1], uv_len);
    veejay_memcpy(dv, src->data[2], uv_len);
}

static void scratcher_advance(scratcher_t *s, int n, int pingpong)
{
    if(n <= 1) {
        s->phase_q8 = 0;
        s->direction = 1;
        return;
    }

    const int step_q8 = 256;
    const int max_q8 = (n - 1) << 8;

    if(pingpong) {
        s->phase_q8 += step_q8 * s->direction;

        while(s->phase_q8 >= max_q8 || s->phase_q8 < 0) {
            if(s->phase_q8 >= max_q8) {
                s->phase_q8 = max_q8 - (s->phase_q8 - max_q8);
                s->direction = -1;
            }
            else {
                s->phase_q8 = -s->phase_q8;
                s->direction = 1;
            }
        }
    }
    else {
        s->phase_q8 += step_q8;

        const int span_q8 = n << 8;

        while(s->phase_q8 >= span_q8)
            s->phase_q8 -= span_q8;

        while(s->phase_q8 < 0)
            s->phase_q8 += span_q8;

        s->direction = 1;
    }
}

static void scratcher_blend_from_slot(scratcher_t *s, VJFrame *src, int slot, int wet_q8, int chroma_q8)
{
    if(slot <= 0 || wet_q8 <= 0)
        return;

    const int len = src->len;
    const int uv_len = src->ssm ? len : src->uv_len;

    uint8_t *restrict Y = src->data[0];
    uint8_t *restrict U = src->data[1];
    uint8_t *restrict V = src->data[2];

    const uint8_t *restrict hY = s->frame[0] + ((size_t)len * (size_t)slot);
    const uint8_t *restrict hU = s->frame[1] + ((size_t)uv_len * (size_t)slot);
    const uint8_t *restrict hV = s->frame[2] + ((size_t)uv_len * (size_t)slot);

#pragma omp parallel num_threads(s->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = scratcher_mix_y(Y[i], hY[i], wet_q8);

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            U[i] = scratcher_mix_uv(U[i], hU[i], chroma_q8);
            V[i] = scratcher_mix_uv(V[i], hV[i], chroma_q8);
        }
    }
}

void scratcher_apply(void *ptr, VJFrame *src, int *args)
{
    scratcher_t *s = (scratcher_t*)ptr;
    const int raw_opacity = args[P_OPACITY];
    const int raw_n = args[P_BUFFER_LEN];
    const int pingpong = args[P_PINGPONG] ? 1 : 0;
    const int raw_mix = args[P_SCRATCH_MIX];
    const int raw_chroma = args[P_CHROMA_AMOUNT];
    const float follow = 0.34f;
    const float slow_follow = 0.16f;

    scratcher_smooth_to(&s->sm_opacity, (float)raw_opacity, follow);
    scratcher_smooth_to(&s->sm_buffer,  (float)raw_n, slow_follow);
    scratcher_smooth_to(&s->sm_mix,     (float)raw_mix, follow);
    scratcher_smooth_to(&s->sm_chroma,  (float)raw_chroma, follow);

    int n = clampi((int)(s->sm_buffer + 0.5f), 1, MAX_SCRATCH_FRAMES - 1);

    if(n != s->last_n || pingpong != s->last_pingpong) {
        s->last_n = n;
        s->last_pingpong = pingpong;

        const int max_q8 = (n - 1) << 8;

        if(s->phase_q8 > max_q8)
            s->phase_q8 = max_q8;
        if(s->phase_q8 < 0)
            s->phase_q8 = 0;

        s->direction = 1;
    }

    const int slot = clampi((s->phase_q8 + 128) >> 8, 0, n - 1);
    int wet_q8 = ((int)(s->sm_opacity + 0.5f) * 256 + 127) / 255;

    wet_q8 = (wet_q8 * clampi((int)(s->sm_mix + 0.5f), 0, 1000) + 500) / 1000;
    wet_q8 = clampi(wet_q8, 0, 256);

    int chroma_q8 = (wet_q8 * clampi((int)(s->sm_chroma + 0.5f), 0, 1000) + 500) / 1000;

    chroma_q8 = clampi(chroma_q8, 0, 256);

    scratcher_blend_from_slot(s, src, slot, wet_q8, chroma_q8);

    if(!pingpong || s->direction > 0)
        scratcher_store_current(s, src, slot);

    scratcher_advance(s, n, pingpong);
}
