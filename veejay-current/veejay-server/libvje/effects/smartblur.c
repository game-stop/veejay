/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include <veejaycore/vjmem.h>
#include "smartblur.h"

#define SMARTBLUR_PARAMS 7

#define P_RADIUS         0
#define P_SHARPNESS      1
#define P_CHROMA         2
#define P_MIX            3
#define P_RADIUS_DRIVE    4
#define P_SHARPNESS_DRIVE 5
#define P_MIX_DRIVE       6

typedef struct {
    uint8_t *region;
    uint8_t *small_src;
    uint8_t *orig_plane;

    float *a_buf;
    float *b_buf;
    float *tmp_mu;
    float *tmp_var;
    float *mI;
    float *mII;
    float *inv_counts;

    float sm_radius;
    float sm_sharpness;
    float sm_chroma;
    float sm_mix;
    float sm_radius_drive;
    float sm_sharpness_drive;
    float sm_mix_drive;

    int w;
    int h;
    int len;
    int sw;
    int sh;
    int small_len;
    int max_sdim;
    int n_threads;
} smartblur_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t smartblur_u8(float v)
{
    int iv = (int)(v + 0.5f);
    return (uint8_t)clampi(iv, 0, 255);
}

static inline uintptr_t smartblur_align_up(uintptr_t p, uintptr_t a)
{
    return (p + (a - 1u)) & ~(a - 1u);
}

static inline uint8_t smartblur_blend_y(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t smartblur_blend_uv(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);

    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline float smartblur_smooth_lane(float oldv, float target, float smooth, float fast)
{
    if(oldv < -900000.0f)
        return target;

    float a = fast + (1.0f - smooth) * (0.46f - fast);
    if(a < 0.035f)
        a = 0.035f;
    else if(a > 0.62f)
        a = 0.62f;

    return oldv + (target - oldv) * a;
}

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SMARTBLUR_PARAMS;

    ve->defaults  = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_RADIUS] = 1;
    ve->limits[1][P_RADIUS] = 100;
    ve->defaults[P_RADIUS] = 8;

    ve->limits[0][P_SHARPNESS] = 1;
    ve->limits[1][P_SHARPNESS] = 1000;
    ve->defaults[P_SHARPNESS] = 200;

    ve->limits[0][P_CHROMA] = 0;
    ve->limits[1][P_CHROMA] = 100;
    ve->defaults[P_CHROMA] = 50;

    ve->limits[0][P_MIX] = 0;
    ve->limits[1][P_MIX] = 1000;
    ve->defaults[P_MIX] = 1000;

    ve->limits[0][P_RADIUS_DRIVE] = 0;
    ve->limits[1][P_RADIUS_DRIVE] = 1000;
    ve->defaults[P_RADIUS_DRIVE] = 0;

    ve->limits[0][P_SHARPNESS_DRIVE] = 0;
    ve->limits[1][P_SHARPNESS_DRIVE] = 1000;
    ve->defaults[P_SHARPNESS_DRIVE] = 0;

    ve->limits[0][P_MIX_DRIVE] = 0;
    ve->limits[1][P_MIX_DRIVE] = 1000;
    ve->defaults[P_MIX_DRIVE] = 0;

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->description = "Smart Blur";

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Sharpness",
        "Chroma",
        "Mix",
        "Radius Drive",
        "Sharpness Drive",
        "Mix Drive"
    );

    const int sw = (w > 0) ? ((w + 1) >> 1) : 320;
    const int sh = (h > 0) ? ((h + 1) >> 1) : 240;
    int radius_hi = ((sw < sh) ? sw : sh) - 1;

    if(radius_hi > 100)
        radius_hi = 100;
    if(radius_hi < 2)
        radius_hi = 2;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 24, 78, 100, 16, 620, 0, 1, 100, VJ_BEAT_COST_MODERATE, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 40, 900, 76, 100, 4, 480, 20, 5, 0, VJ_BEAT_COST_MODERATE, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 12, 100, 70, 96, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 58, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 300, 1000, 86, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 6, 420, 20, 5, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 760, 62, 94, 220, 1400, 0, 5, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *smartblur_malloc(int w, int h)
{
    smartblur_t *s = (smartblur_t*) vj_calloc(sizeof(*s));
    if(!s)
        return NULL;

    s->w = w;
    s->h = h;
    s->len = w * h;
    s->sw = (w + 1) >> 1;
    s->sh = (h + 1) >> 1;
    s->small_len = s->sw * s->sh;
    s->max_sdim = (s->sw > s->sh) ? s->sw : s->sh;

    s->n_threads = vje_advise_num_threads(s->len);

    const size_t small_bytes = (size_t)s->small_len;
    const size_t plane_bytes = (size_t)s->len;
    const size_t plane_floats = (size_t)s->small_len;
    const size_t prefix_floats = (size_t)s->n_threads * (size_t)(s->max_sdim + 1);
    const size_t inv_floats = (size_t)(s->max_sdim + 2);

    uintptr_t off = 0;

    const uintptr_t small_off = off;
    off += small_bytes;

    const uintptr_t orig_off = off;
    off += plane_bytes;

    off = smartblur_align_up(off, (uintptr_t)sizeof(float));
    const uintptr_t a_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t b_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t tmp_mu_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t tmp_var_off = off;
    off += plane_floats * sizeof(float);

    const uintptr_t mi_off = off;
    off += prefix_floats * sizeof(float);

    const uintptr_t mii_off = off;
    off += prefix_floats * sizeof(float);

    const uintptr_t inv_off = off;
    off += inv_floats * sizeof(float);

    s->region = (uint8_t*) vj_malloc((size_t)off);
    if(!s->region) {
        free(s);
        return NULL;
    }

    s->small_src  = s->region + small_off;
    s->orig_plane = s->region + orig_off;
    s->a_buf      = (float*)(void*)(s->region + a_off);
    s->b_buf      = (float*)(void*)(s->region + b_off);
    s->tmp_mu     = (float*)(void*)(s->region + tmp_mu_off);
    s->tmp_var    = (float*)(void*)(s->region + tmp_var_off);
    s->mI         = (float*)(void*)(s->region + mi_off);
    s->mII        = (float*)(void*)(s->region + mii_off);
    s->inv_counts = (float*)(void*)(s->region + inv_off);

    for(int i = 0; i < s->max_sdim + 2; i++)
        s->inv_counts[i] = (i > 0) ? (1.0f / (float)i) : 1.0f;

    s->sm_radius = -1000000.0f;
    s->sm_sharpness = -1000000.0f;
    s->sm_chroma = -1000000.0f;
    s->sm_mix = -1000000.0f;
    s->sm_radius_drive = -1000000.0f;
    s->sm_sharpness_drive = -1000000.0f;
    s->sm_mix_drive = -1000000.0f;

    return (void*) s;
}

void smartblur_free(void *ptr)
{
    smartblur_t *s = (smartblur_t*) ptr;

    free(s->region);
    free(s);
}

static void smartblur_downsample_2x(smartblur_t *s, const uint8_t *restrict data)
{
    const int w = s->w;
    const int h = s->h;
    const int sw = s->sw;
    const int sh = s->sh;

#pragma omp for schedule(static)
    for(int y = 0; y < sh; y++) {
        const int y0 = y << 1;
        const int y1 = (y0 + 1 < h) ? y0 + 1 : y0;

        const uint8_t *restrict r0 = data + y0 * w;
        const uint8_t *restrict r1 = data + y1 * w;
        uint8_t *restrict out = s->small_src + y * sw;

        for(int x = 0; x < sw; x++) {
            const int x0 = x << 1;
            const int x1 = (x0 + 1 < w) ? x0 + 1 : x0;

            out[x] = (uint8_t)(((int)r0[x0] + (int)r0[x1] + (int)r1[x0] + (int)r1[x1] + 2) >> 2);
        }
    }
}

static void smartblur_build_coefficients(smartblur_t *s, int r, float eps, float offset)
{
    const int sw = s->sw;
    const int sh = s->sh;
    const int max_sdim = s->max_sdim;
    const float *restrict inv = s->inv_counts;

#pragma omp for schedule(static)
    for(int y = 0; y < sh; y++) {
        const int tid = omp_get_thread_num();

        float *restrict rowI  = s->mI  + (size_t)tid * (size_t)(max_sdim + 1);
        float *restrict rowII = s->mII + (size_t)tid * (size_t)(max_sdim + 1);

        const uint8_t *restrict line = s->small_src + y * sw;
        float *restrict tmu  = s->tmp_mu  + y * sw;
        float *restrict tvar = s->tmp_var + y * sw;

        rowI[0] = 0.0f;
        rowII[0] = 0.0f;

        for(int x = 0; x < sw; x++) {
            const float v = (float)line[x] - offset;
            rowI[x + 1] = rowI[x] + v;
            rowII[x + 1] = rowII[x] + v * v;
        }

        for(int x = 0; x < sw; x++) {
            const int r1 = (x - r < 0) ? 0 : x - r;
            const int r2 = (x + r >= sw) ? sw - 1 : x + r;
            const int count = r2 - r1 + 1;
            const float ic = inv[count];

            tmu[x]  = (rowI[r2 + 1]  - rowI[r1])  * ic;
            tvar[x] = (rowII[r2 + 1] - rowII[r1]) * ic;
        }
    }

    {
        const int tid = omp_get_thread_num();

        float *restrict colI  = s->mI  + (size_t)tid * (size_t)(max_sdim + 1);
        float *restrict colII = s->mII + (size_t)tid * (size_t)(max_sdim + 1);

#pragma omp for schedule(static)
        for(int x = 0; x < sw; x++) {
            colI[0] = 0.0f;
            colII[0] = 0.0f;

            for(int y = 0; y < sh; y++) {
                const int idx = y * sw + x;

                colI[y + 1] = colI[y] + s->tmp_mu[idx];
                colII[y + 1] = colII[y] + s->tmp_var[idx];
            }

            for(int y = 0; y < sh; y++) {
                const int r1 = (y - r < 0) ? 0 : y - r;
                const int r2 = (y + r >= sh) ? sh - 1 : y + r;
                const int count = r2 - r1 + 1;
                const float ic = inv[count];

                const float mu = (colI[r2 + 1] - colI[r1]) * ic;
                float var = (colII[r2 + 1] - colII[r1]) * ic - mu * mu;

                if(var < 0.0f)
                    var = 0.0f;

                const float a = var / (var + eps);
                const int idx = y * sw + x;

                s->a_buf[idx] = a;
                s->b_buf[idx] = (1.0f - a) * mu;
            }
        }
    }
}

static void smartblur_apply_coefficients(smartblur_t *s,
                                         uint8_t *restrict data,
                                         float offset,
                                         float strength,
                                         int luma_only)
{
    if(strength <= 0.001f)
        return;

    const int w = s->w;
    const int h = s->h;
    const int sw = s->sw;

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int ys = y >> 1;
        uint8_t *restrict out = data + y * w;

        if(luma_only) {
            for(int x = 0; x < w; x++) {
                const int xs = x >> 1;
                const int si = ys * sw + xs;
                const float a = s->a_buf[si];
                const float b = s->b_buf[si];

                out[x] = smartblur_u8((float)out[x] * a + b);
            }
        } else {
            for(int x = 0; x < w; x++) {
                const int xs = x >> 1;
                const int si = ys * sw + xs;
                const float a = s->a_buf[si];
                const float b = s->b_buf[si];
                const float v_coeff = 1.0f + strength * (a - 1.0f);
                const float sb_off  = strength * (b + offset * (1.0f - a));

                out[x] = smartblur_u8((float)out[x] * v_coeff + sb_off);
            }
        }
    }
}

static void smartblur_plane(smartblur_t *s,
                            uint8_t *restrict data,
                            int radius,
                            float eps,
                            float offset,
                            float strength,
                            int luma_only)
{
    if(strength <= 0.001f)
        return;

    const int max_r = ((s->sw < s->sh) ? s->sw : s->sh) - 1;
    const int r = clampi(radius, 1, (max_r > 1) ? max_r : 1);

    smartblur_downsample_2x(s, data);
    smartblur_build_coefficients(s, r, eps, offset);
    smartblur_apply_coefficients(s, data, offset, strength, luma_only);
}

static void smartblur_mix_plane(smartblur_t *s,
                                uint8_t *restrict data,
                                int len,
                                int mix_q8,
                                int chroma)
{
    if(mix_q8 >= 256)
        return;

    const uint8_t *restrict src = s->orig_plane;

    if(mix_q8 <= 0) {
#pragma omp single
        veejay_memcpy(data, src, len);
        return;
    }

    if(chroma) {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            data[i] = smartblur_blend_uv(src[i], data[i], mix_q8);
    } else {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            data[i] = smartblur_blend_y(src[i], data[i], mix_q8);
    }
}

static void smartblur_process_plane(smartblur_t *s,
                                    uint8_t *restrict data,
                                    int len,
                                    int radius,
                                    float eps,
                                    float offset,
                                    float strength,
                                    int luma_only,
                                    int mix_q8,
                                    int chroma)
{
    if(mix_q8 < 256) {
#pragma omp single
        veejay_memcpy(s->orig_plane, data, len);
    }

    smartblur_plane(s, data, radius, eps, offset, strength, luma_only);

    if(mix_q8 < 256)
        smartblur_mix_plane(s, data, len, mix_q8, chroma);
}

void smartblur_apply(void *ptr, VJFrame *frame, int *args)
{
    smartblur_t *s = (smartblur_t*) ptr;

    const int radius_arg = args[P_RADIUS];
    const int sharpness_arg = args[P_SHARPNESS];
    const int chroma_arg = args[P_CHROMA];
    const int mix_arg = args[P_MIX];
    const int radius_drive_arg = args[P_RADIUS_DRIVE];
    const int sharpness_drive_arg = args[P_SHARPNESS_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];
    const float lane_smooth = 0.52f;

    s->sm_radius = smartblur_smooth_lane(s->sm_radius, (float)radius_arg, lane_smooth, 0.11f);
    s->sm_sharpness = smartblur_smooth_lane(s->sm_sharpness, (float)sharpness_arg, lane_smooth, 0.13f);
    s->sm_chroma = smartblur_smooth_lane(s->sm_chroma, (float)chroma_arg, lane_smooth, 0.13f);
    s->sm_mix = smartblur_smooth_lane(s->sm_mix, (float)mix_arg, lane_smooth, 0.13f);
    s->sm_radius_drive = smartblur_smooth_lane(s->sm_radius_drive, (float)radius_drive_arg, lane_smooth, 0.16f);
    s->sm_sharpness_drive = smartblur_smooth_lane(s->sm_sharpness_drive, (float)sharpness_drive_arg, lane_smooth, 0.16f);
    s->sm_mix_drive = smartblur_smooth_lane(s->sm_mix_drive, (float)mix_drive_arg, lane_smooth, 0.16f);

    const int radius_boost = (int)((s->sm_radius_drive * 38.0f) * 0.001f + 0.5f);
    int radius = (int)(s->sm_radius + 0.5f) + radius_boost;
    radius = clampi(radius, 1, 100);

    const int sharpness_boost = (int)((s->sm_sharpness_drive * 820.0f) * 0.001f + 0.5f);
    int sharpness = (int)(s->sm_sharpness + 0.5f) + sharpness_boost;
    sharpness = clampi(sharpness, 1, 1000);

    int chroma = (int)(s->sm_chroma + 0.5f);
    chroma = clampi(chroma, 0, 100);

    int mix = (int)(s->sm_mix + 0.5f);
    mix -= (int)((s->sm_mix_drive * 420.0f) * 0.001f + 0.5f);
    mix = clampi(mix, 0, 1000);

    float eps = (float)sharpness * 0.1f;
    eps *= eps;

    if(eps < 0.0001f)
        eps = 0.0001f;

    const float chroma_strength = (float)chroma * 0.01f;
    const int mix_q8 = (mix * 256 + 500) / 1000;

#pragma omp parallel num_threads(s->n_threads)
    {
        smartblur_process_plane(s, frame->data[0], s->len, radius, eps, 0.0f,   1.0f,            1, mix_q8, 0);
        smartblur_process_plane(s, frame->data[1], s->len, radius, eps, 128.0f, chroma_strength, 0, mix_q8, 1);
        smartblur_process_plane(s, frame->data[2], s->len, radius, eps, 128.0f, chroma_strength, 0, mix_q8, 1);
    }
}
