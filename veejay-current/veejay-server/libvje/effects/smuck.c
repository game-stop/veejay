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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "smuck.h"

#define SMUCK_PARAMS 7

#define P_SHIMMER       0
#define P_FULL_COLOR    1
#define P_STATIC_SEED   2
#define P_DIRECTION     3
#define P_MIX           4
#define P_SHIMMER_DRIVE 5
#define P_JITTER_DRIVE  6

typedef struct {
    uint8_t *tmp[3];
    uint32_t seed;
    uint32_t beat_seed;
    int n_threads;

    float eff_shimmer;
    float eff_mix;
    float eff_shimmer_drive;
    float eff_jitter_drive;
    int initialized;
} smuck_t;

static inline int clampi(int v, int lo, int hi)
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


static inline int smuck_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline int smuck_scale_signed(int v, int q8)
{
    if(v >= 0)
        return (v * q8 + 128) >> 8;

    return -(((-v) * q8 + 128) >> 8);
}

static inline uint8_t smuck_mix_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

vj_effect *smuck_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SMUCK_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_SHIMMER] = 12;
    ve->limits[0][P_SHIMMER] = 0;
    ve->limits[1][P_SHIMMER] = 17;

    ve->defaults[P_FULL_COLOR] = 0;
    ve->limits[0][P_FULL_COLOR] = 0;
    ve->limits[1][P_FULL_COLOR] = 1;

    ve->defaults[P_STATIC_SEED] = 1;
    ve->limits[0][P_STATIC_SEED] = 0;
    ve->limits[1][P_STATIC_SEED] = 1;

    ve->defaults[P_DIRECTION] = 2;
    ve->limits[0][P_DIRECTION] = 0;
    ve->limits[1][P_DIRECTION] = 2;

    ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_MIX] = 0;
    ve->limits[1][P_MIX] = 1000;

    ve->defaults[P_SHIMMER_DRIVE] = 0;
    ve->limits[0][P_SHIMMER_DRIVE] = 0;
    ve->limits[1][P_SHIMMER_DRIVE] = 1000;

    ve->defaults[P_JITTER_DRIVE] = 0;
    ve->limits[0][P_JITTER_DRIVE] = 0;
    ve->limits[1][P_JITTER_DRIVE] = 1000;

    ve->description = "SmuckTV";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shimmer",
        "Full color",
        "Static seed",
        "Direction",
        "Mix",
        "Shimmer Drive",
        "Jitter Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_FULL_COLOR],
        P_FULL_COLOR,
        "Luma only",
        "Full color"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_STATIC_SEED],
        P_STATIC_SEED,
        "Moving seed",
        "Static seed"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_DIRECTION],
        P_DIRECTION,
        "Horizontal",
        "Vertical",
        "Both"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_TURBULENCE,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  17,                 12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 240,                1000,               12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_TURBULENCE,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 140,                1000,               16, 62,  700, 2800, 0,    92,
        VJ_BEAT_DRIFT,            VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               16, 62,  700, 2800, 0,    88
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
    s->beat_seed = smuck_hash_u32(s->seed ^ 0x9e3779b9U);

    s->n_threads = vje_advise_num_threads(len);

    s->eff_shimmer = 0.0f;
    s->eff_mix = 0.0f;
    s->eff_shimmer_drive = 0.0f;
    s->eff_jitter_drive = 0.0f;
    s->initialized = 0;

    return (void*) s;
}

void smuck_free(void *ptr)
{
    smuck_t *s = (smuck_t*) ptr;

    free(s->tmp[0]);
    free(s);
}

static void smuck_apply_plane(uint8_t *restrict dst,
                              const uint8_t *restrict src,
                              int w,
                              int h,
                              int shimmer,
                              int amount_q8,
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

            const int raw_dx = ((int)((r >> shift) & mask) - bias) * mx;
            const int raw_dy = ((int)((r >> yshift) & mask) - bias) * my;

            int dx = smuck_scale_signed(raw_dx, amount_q8);
            int dy = smuck_scale_signed(raw_dy, amount_q8);

            int sx = x + dx;
            int sy = y + dy;

            sx = clampi(sx, 0, w - 1);
            sy = clampi(sy, 0, h - 1);

            dst[idx] = src[sy * w + sx];
        }
    }
}

void smuck_apply(void *ptr, VJFrame *frame, int *args)
{
    smuck_t *s = (smuck_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    const int shimmer_arg = args[P_SHIMMER];
    const int full_color = args[P_FULL_COLOR] ? 1 : 0;
    const int static_seed = args[P_STATIC_SEED] ? 1 : 0;
    const int direction = args[P_DIRECTION];
    const int mix_arg = args[P_MIX];
    const int shimmer_drive_arg = args[P_SHIMMER_DRIVE];
    const int jitter_drive_arg = args[P_JITTER_DRIVE];

    int shimmer = shimmer_arg;
    int mix = mix_arg;
    int shimmer_drive = shimmer_drive_arg;
    int jitter_drive = jitter_drive_arg;

    if(!s->initialized) {
        s->eff_shimmer = (float)shimmer;
        s->eff_mix = (float)mix;
        s->eff_shimmer_drive = (float)shimmer_drive;
        s->eff_jitter_drive = (float)jitter_drive;
        s->initialized = 1;
    } else {
        shimmer = smuck_smooth_i(&s->eff_shimmer, shimmer, 0.30f, 0.075f);
        mix = smuck_smooth_i(&s->eff_mix, mix, 0.24f, 0.075f);
        shimmer_drive = smuck_smooth_i(&s->eff_shimmer_drive, shimmer_drive, 0.34f, 0.090f);
        jitter_drive = smuck_smooth_i(&s->eff_jitter_drive, jitter_drive, 0.38f, 0.100f);
    }

    shimmer = clampi(shimmer, 0, 17);
    mix = clampi(mix, 0, 1000);
    shimmer_drive = clampi(shimmer_drive, 0, 1000);
    jitter_drive = clampi(jitter_drive, 0, 1000);

    const int direct_q = clampi(((shimmer_drive * 720) + (jitter_drive * 520) + 500) / 1000, 0, 1000);

    int eff_shimmer = shimmer + ((direct_q * 5 + 500) / 1000);
    eff_shimmer = clampi(eff_shimmer, 0, 17);

    int amount_q8 = 256 + ((shimmer_drive * 520 + jitter_drive * 720 + 500) / 1000);
    amount_q8 = clampi(amount_q8, 0, 1024);

    int mix_q8 = clampi((mix * 256 + 500) / 1000, 0, 256);
    if(direct_q > 0 && mix_q8 < 256)
        mix_q8 = clampi(mix_q8 + (((256 - mix_q8) * direct_q + 500) / 1000), 0, 256);

    const int mx = (direction == 0 || direction == 2) ? 1 : 0;
    const int my = (direction == 1 || direction == 2) ? 1 : 0;

    if(!static_seed || jitter_drive > 0)
        s->beat_seed = smuck_hash_u32(s->beat_seed + 0x6d2b79f5U + (uint32_t)(jitter_drive * 23 + shimmer_drive * 11));

    const uint32_t base_seed = static_seed ? 0x1337BEEFU : s->seed;
    const uint32_t drive_seed = smuck_hash_u32(((uint32_t)shimmer_drive * 0x45d9f3bu) ^
                                               ((uint32_t)jitter_drive * 0x27d4eb2du));
    const uint32_t drive_mask = (uint32_t)-(shimmer_drive > 0 || jitter_drive > 0);
    const uint32_t seed = base_seed ^
                          (s->beat_seed & (uint32_t)-(jitter_drive > 0)) ^
                          (drive_seed & drive_mask);

    uint8_t *restrict Y = frame->data[0];

    veejay_memcpy(s->tmp[0], Y, len);

    smuck_apply_plane(
        Y,
        s->tmp[0],
        w,
        h,
        eff_shimmer,
        amount_q8,
        mx,
        my,
        seed,
        0,
        s->n_threads
    );

    if(mix_q8 < 256) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++)
            Y[i] = smuck_mix_u8(s->tmp[0][i], Y[i], mix_q8);
    }

    if(full_color) {
        const int uv_len = frame->ssm ? len : frame->uv_len;
        const int uv_w = frame->ssm ? w : frame->uv_width;
        const int uv_h = frame->ssm ? h : frame->uv_height;

        veejay_memcpy(s->tmp[1], frame->data[1], uv_len);
        veejay_memcpy(s->tmp[2], frame->data[2], uv_len);

        smuck_apply_plane(
            frame->data[1],
            s->tmp[1],
            uv_w,
            uv_h,
            eff_shimmer,
            amount_q8,
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
            eff_shimmer,
            amount_q8,
            mx,
            my,
            seed ^ 0x85ebca6bU,
            1,
            s->n_threads
        );

        if(mix_q8 < 256) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
            for(int i = 0; i < uv_len; i++) {
                frame->data[1][i] = smuck_mix_u8(s->tmp[1][i], frame->data[1][i], mix_q8);
                frame->data[2][i] = smuck_mix_u8(s->tmp[2][i], frame->data[2][i], mix_q8);
            }
        }
    }

    if(!static_seed)
        s->seed = smuck_hash_u32(s->seed + 0x6d2b79f5U);
}
