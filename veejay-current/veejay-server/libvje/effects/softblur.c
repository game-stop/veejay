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
#include <veejaycore/vjmem.h>
#include "softblur.h"

extern int vje_get_quality(void);

#define SOFTBLUR_PARAMS 5

#define P_KERNEL       0
#define P_MIX          1
#define P_CHROMA       2
#define P_BLUR_DRIVE   3
#define P_MIX_DRIVE    4

typedef struct {
    uint8_t *src[3];
    uint8_t *tmp[3];
    int max_len;
    int n_threads;

    float eff_kernel;
    float eff_mix;
    float eff_chroma;
    float eff_blur_drive;
    float eff_mix_drive;
    int eff_initialized;
} softblur_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}



static inline int softblur_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline uint8_t softblur_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

vj_effect *softblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SOFTBLUR_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->defaults[P_KERNEL]     = 0;
    ve->defaults[P_MIX]        = 1000;
    ve->defaults[P_CHROMA]     = 1000;
    ve->defaults[P_BLUR_DRIVE] = 0;
    ve->defaults[P_MIX_DRIVE]  = 0;

    ve->limits[0][P_KERNEL]     = 0;    ve->limits[1][P_KERNEL]     = 2;
    ve->limits[0][P_MIX]        = 0;    ve->limits[1][P_MIX]        = 1000;
    ve->limits[0][P_CHROMA]     = 0;    ve->limits[1][P_CHROMA]     = 1000;
    ve->limits[0][P_BLUR_DRIVE] = 0;    ve->limits[1][P_BLUR_DRIVE] = 1000;
    ve->limits[0][P_MIX_DRIVE]  = 0;    ve->limits[1][P_MIX_DRIVE]  = 1000;

    ve->description = "Soft Blur";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Kernel Size",
        "Mix",
        "Chroma Amount",
        "Blur Drive",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_KERNEL],
        P_KERNEL,
        "1x3",
        "3x3",
        "5x5"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 2, 88, 100, 6, 420, 20, 1, 120, VJ_BEAT_COST_MODERATE, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 280, 1000, 88, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 320, 1000, 70, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 850, 68, 94, 220, 1400, 0, 5, 0, VJ_BEAT_COST_CHEAP, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *softblur_malloc(int w, int h)
{
    softblur_t *sb = (softblur_t*) vj_calloc(sizeof(softblur_t));
    if(!sb)
        return NULL;

    const int len = w * h;

    sb->src[0] = (uint8_t*) vj_malloc((size_t)len * 6u);
    if(!sb->src[0]) {
        free(sb);
        return NULL;
    }

    sb->tmp[0] = sb->src[0] + len;
    sb->src[1] = sb->tmp[0] + len;
    sb->tmp[1] = sb->src[1] + len;
    sb->src[2] = sb->tmp[1] + len;
    sb->tmp[2] = sb->src[2] + len;
    sb->max_len = len;

    sb->eff_kernel = 0.0f;
    sb->eff_mix = 1000.0f;
    sb->eff_chroma = 1000.0f;
    sb->eff_blur_drive = 0.0f;
    sb->eff_mix_drive = 0.0f;
    sb->eff_initialized = 0;

    sb->n_threads = vje_advise_num_threads(len);

    return (void*) sb;
}

void softblur_free(void *ptr)
{
    softblur_t *sb = (softblur_t*) ptr;

    free(sb->src[0]);
    free(sb);
}

static inline void softblur_copy_plane(const uint8_t *restrict src,
                                          uint8_t *restrict dst,
                                          int len)
{
#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        dst[i] = src[i];
}

static inline void softblur1_core(const uint8_t *restrict src,
                                      uint8_t *restrict dst,
                                      int w,
                                      int h)
{
#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict row = src + y * w;
        uint8_t *restrict out = dst + y * w;

        out[0] = (uint8_t)(((int)row[0] * 2 + (int)row[1] + 1) / 3);

        for(int x = 1; x < w - 1; x++)
            out[x] = (uint8_t)(((int)row[x - 1] + (int)row[x] + (int)row[x + 1] + 1) / 3);

        out[w - 1] = (uint8_t)(((int)row[w - 2] + (int)row[w - 1] * 2 + 1) / 3);
    }
}

static inline void softblur3_h(const uint8_t *restrict src,
                                   uint8_t *restrict tmp,
                                   int w,
                                   int h)
{
#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict row = src + y * w;
        uint8_t *restrict trow = tmp + y * w;

        trow[0] = (uint8_t)(((int)row[0] * 2 + (int)row[1] + 1) / 3);

        for(int x = 1; x < w - 1; x++)
            trow[x] = (uint8_t)(((int)row[x - 1] + (int)row[x] + (int)row[x + 1] + 1) / 3);

        trow[w - 1] = (uint8_t)(((int)row[w - 2] + (int)row[w - 1] * 2 + 1) / 3);
    }
}

static inline void softblur3_v(const uint8_t *restrict tmp,
                                   uint8_t *restrict dst,
                                   int w,
                                   int h)
{
#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = tmp + ym * w;
        const uint8_t *restrict r1 = tmp + y * w;
        const uint8_t *restrict r2 = tmp + yp * w;
        uint8_t *restrict out = dst + y * w;

        for(int x = 0; x < w; x++)
            out[x] = (uint8_t)(((int)r0[x] + (int)r1[x] + (int)r2[x] + 1) / 3);
    }
}

static inline void softblur3_core(const uint8_t *restrict src,
                                      uint8_t *restrict tmp,
                                      uint8_t *restrict dst,
                                      int w,
                                      int h)
{
    softblur3_h(src, tmp, w, h);
    softblur3_v(tmp, dst, w, h);
}

static inline void softblur_blend_plane(const uint8_t *restrict src,
                                            uint8_t *restrict dst,
                                            int len,
                                            int mix_q8)
{
    mix_q8 = clampi(mix_q8, 0, 256);

    if(mix_q8 >= 256)
        return;

    if(mix_q8 <= 0) {
        softblur_copy_plane(src, dst, len);
        return;
    }

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        dst[i] = softblur_mix_u8(src[i], dst[i], mix_q8);
}

static inline void softblur_plane(uint8_t *restrict src,
                                      uint8_t *restrict tmp,
                                      uint8_t *restrict plane,
                                      int w,
                                      int h,
                                      int type,
                                      int mix_q8)
{
    const int len = w * h;

    softblur_copy_plane(plane, src, len);

    switch(type) {
        case 0:
            softblur1_core(src, plane, w, h);
            break;

        case 1:
            softblur3_core(src, tmp, plane, w, h);
            break;

        case 2:
            softblur3_core(src, tmp, plane, w, h);
            softblur3_core(plane, tmp, plane, w, h);
            break;

        default:
            break;
    }

    softblur_blend_plane(src, plane, len, mix_q8);
}

void softblur_apply_internal(VJFrame *frame)
{
    const int type = clampi(vje_get_quality(), 0, 2);
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *src = (uint8_t*) vj_malloc((size_t)len * 2u);
    if(!src)
        return;

    uint8_t *tmp = src + len;

#pragma omp parallel num_threads(n_threads)
    {
        softblur_plane(src, tmp, frame->data[0], frame->width, frame->height, type, 256);
    }

    free(src);
}

void softblur_apply(void *ptr, VJFrame *frame, int *args)
{
    softblur_t *blur = (softblur_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int kernel_arg = args[P_KERNEL];
    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA];
    const int blur_drive_arg = args[P_BLUR_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];

    if(!blur->eff_initialized) {
        blur->eff_kernel = (float)kernel_arg;
        blur->eff_mix = (float)mix_arg;
        blur->eff_chroma = (float)chroma_arg;
        blur->eff_blur_drive = (float)blur_drive_arg;
        blur->eff_mix_drive = (float)mix_drive_arg;
        blur->eff_initialized = 1;
    } else {
        const float param_fast = 0.26f;
        const float param_slow = 0.090f;

        softblur_smooth_i(&blur->eff_kernel,     kernel_arg,     param_fast, param_slow);
        softblur_smooth_i(&blur->eff_mix,        mix_arg,        param_fast * 0.82f, param_slow);
        softblur_smooth_i(&blur->eff_chroma,     chroma_arg,     param_fast * 0.82f, param_slow);
        softblur_smooth_i(&blur->eff_blur_drive, blur_drive_arg, param_fast, param_slow);
        softblur_smooth_i(&blur->eff_mix_drive,  mix_drive_arg,  param_fast, param_slow);
    }

    const int eff_kernel = clampi((int)(blur->eff_kernel + 0.5f), 0, 2);
    const int eff_mix = clampi((int)(blur->eff_mix + 0.5f), 0, 1000);
    const int eff_chroma = clampi((int)(blur->eff_chroma + 0.5f), 0, 1000);
    const int blur_drive = clampi((int)(blur->eff_blur_drive + 0.5f), 0, 1000);
    const int mix_drive = clampi((int)(blur->eff_mix_drive + 0.5f), 0, 1000);

    int type = eff_kernel;
    if(type < 1 && blur_drive >= 280)
        type = 1;
    if(type < 2 && blur_drive >= 660)
        type = 2;

    const int base_mix_q8 = (eff_mix * 256 + 500) / 1000;
    const int clarity_q8 = (mix_drive * 112 + 500) / 1000;
    const int y_mix_q8 = clampi(base_mix_q8 - clarity_q8, 0, 256);
    const int c_mix_q8 = clampi((y_mix_q8 * eff_chroma + 500) / 1000, 0, 256);

    const int uv_width  = frame->ssm ? width  : frame->uv_width;
    const int uv_height = frame->ssm ? height : frame->uv_height;

#pragma omp parallel num_threads(blur->n_threads)
    {
        softblur_plane(blur->src[0], blur->tmp[0], frame->data[0], width, height, type, y_mix_q8);
        softblur_plane(blur->src[1], blur->tmp[1], frame->data[1], uv_width, uv_height, type, c_mix_q8);
        softblur_plane(blur->src[2], blur->tmp[2], frame->data[2], uv_width, uv_height, type, c_mix_q8);
    }
}
