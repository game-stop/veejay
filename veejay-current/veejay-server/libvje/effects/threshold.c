/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include "threshold.h"

#define THRESHOLD_PARAMS 5

#define P_THRESHOLD   0
#define P_REVERSE     1
#define P_SOFTNESS    2
#define P_EDGE_GLOW   3
#define P_MIX_DRIVE   4

typedef struct {
    uint8_t *mask;
    int n_threads;

    float threshold_state;
    float softness_state;
    float glow_state;
    float mix_drive_state;
    int initialized;
} threshold_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t threshold_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint8_t threshold_blend_black_y(uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)(((int)b * q8 + 128) >> 8);
}

static inline uint8_t threshold_blend_neutral_uv(uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)(128 + ((((int)b - 128) * q8 + 128) >> 8));
}



static inline int threshold_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}



vj_effect *threshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = THRESHOLD_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_THRESHOLD] = 0;  ve->limits[1][P_THRESHOLD] = 255;  ve->defaults[P_THRESHOLD] = 40;
    ve->limits[0][P_REVERSE] = 0;    ve->limits[1][P_REVERSE] = 1;      ve->defaults[P_REVERSE] = 0;
    ve->limits[0][P_SOFTNESS] = 0;   ve->limits[1][P_SOFTNESS] = 255;   ve->defaults[P_SOFTNESS] = 0;
    ve->limits[0][P_EDGE_GLOW] = 0;  ve->limits[1][P_EDGE_GLOW] = 255;  ve->defaults[P_EDGE_GLOW] = 0;
    ve->limits[0][P_MIX_DRIVE] = 0;  ve->limits[1][P_MIX_DRIVE] = 1000; ve->defaults[P_MIX_DRIVE] = 0;

    ve->description = "Map B from threshold mask";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Reverse",
        "Softness",
        "Edge Glow",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_REVERSE],
        P_REVERSE,
        "Normal",
        "Reverse"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS,                           12,  220,  12, 46, 1000, 3600, 0, 68,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_WINDOW_RADIUS,    VJ_BEAT_F_CONTINUOUS,                           0,   220,  12, 46, 1000, 3600, 0, 62,
        VJ_BEAT_GLOW,             VJ_BEAT_F_CONTINUOUS,                           0,   255,  12, 46, 800,  3000, 0, 74,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120, 1000, 16, 62, 700,  2800, 0, 92
    );

    return ve;
}

void *threshold_malloc(int w, int h)
{
    threshold_t *t = (threshold_t*) vj_calloc(sizeof(threshold_t));
    if(!t)
        return NULL;

    const int len = w * h;

    t->mask = (uint8_t*) vj_malloc((size_t)len);
    if(!t->mask) {
        free(t);
        return NULL;
    }

    t->threshold_state = 40.0f;
    t->softness_state = 0.0f;
    t->glow_state = 0.0f;
    t->mix_drive_state = 0.0f;
    t->initialized = 0;

    t->n_threads = vje_advise_num_threads(len);

    return (void*) t;
}

void threshold_free(void *ptr)
{
    threshold_t *t = (threshold_t*) ptr;

    free(t->mask);
    free(t);
}

static void threshold_build_soft_mask(threshold_t *t, const uint8_t *restrict Y, int w, int h)
{
    uint8_t *restrict mask = t->mask;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int y = 0; y < h; y++) {
        const int ym = (y > 0) ? y - 1 : y;
        const int yp = (y < h - 1) ? y + 1 : y;

        const uint8_t *restrict r0 = Y + ym * w;
        const uint8_t *restrict r1 = Y + y  * w;
        const uint8_t *restrict r2 = Y + yp * w;

        uint8_t *restrict out = mask + y * w;

        for(int x = 0; x < w; x++) {
            const int xm = (x > 0) ? x - 1 : x;
            const int xp = (x < w - 1) ? x + 1 : x;

            const int sum =
                (int)r0[xm] + (int)r0[x] + (int)r0[xp] +
                (int)r1[xm] + (int)r1[x] + (int)r1[xp] +
                (int)r2[xm] + (int)r2[x] + (int)r2[xp];

            out[x] = (uint8_t)((sum + 4) / 9);
        }
    }
}

static inline int threshold_matte_q8(int m, int threshold, int softness, int reverse)
{
    int q8;

    if(softness <= 0)
        q8 = (m > threshold) ? 256 : 0;
    else {
        const int lo = threshold - softness;
        const int hi = threshold + softness;
        const int span = hi - lo;

        if(m <= lo)
            q8 = 0;
        else if(m >= hi)
            q8 = 256;
        else
            q8 = ((m - lo) * 256 + (span >> 1)) / span;
    }

    if(reverse)
        q8 = 256 - q8;

    return clampi(q8, 0, 256);
}

void threshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    threshold_t *t = (threshold_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    int threshold = args[P_THRESHOLD];
    const int reverse = args[P_REVERSE] ? 1 : 0;
    int softness = args[P_SOFTNESS];
    int edge_glow = args[P_EDGE_GLOW];
    int mix_drive = args[P_MIX_DRIVE];

    const float fast = 0.245f;
    const float slow = 0.112f;

    if(!t->initialized) {
        t->threshold_state = (float)threshold;
        t->softness_state = (float)softness;
        t->glow_state = (float)edge_glow;
        t->mix_drive_state = (float)mix_drive;
        t->initialized = 1;
    } else {
        threshold = threshold_smooth_i(&t->threshold_state, threshold, fast, slow);
        softness = threshold_smooth_i(&t->softness_state, softness, fast * 0.90f, slow);
        edge_glow = threshold_smooth_i(&t->glow_state, edge_glow, fast * 1.08f, slow);
        mix_drive = threshold_smooth_i(&t->mix_drive_state, mix_drive, fast * 1.16f, slow);
    }

    mix_drive = clampi(mix_drive, 0, 1000);

    const int threshold_shift = (mix_drive * 92 + 500) / 1000;

    if(reverse)
        threshold = clampi(threshold + threshold_shift, 0, 255);
    else
        threshold = clampi(threshold - threshold_shift, 0, 255);

    softness = clampi(softness + ((mix_drive * 54 + 500) / 1000), 0, 255);
    edge_glow = clampi(edge_glow + ((mix_drive * 96 + 500) / 1000), 0, 255);

    const int mix_floor_q8 = clampi((mix_drive * 118 + 500) / 1000, 0, 192);
    const int glow_radius = clampi((softness >> 1) + 8 + ((mix_drive * 16 + 500) / 1000), 1, 255);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    threshold_build_soft_mask(t, Y, w, h);

    uint8_t *restrict mask = t->mask;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int i = 0; i < len; i++) {
        const int m = mask[i];
        int q8 = threshold_matte_q8(m, threshold, softness, reverse);

        if(q8 < mix_floor_q8)
            q8 = mix_floor_q8;

        int yv = threshold_blend_black_y(Y2[i], q8);
        int uv = threshold_blend_neutral_uv(Cb2[i], q8);
        int vv = threshold_blend_neutral_uv(Cr2[i], q8);

        if(edge_glow > 0) {
            int d = m - threshold;
            if(d < 0)
                d = -d;

            if(d < glow_radius) {
                const int q = glow_radius - d;
                const int glow = (edge_glow * q + (glow_radius >> 1)) / glow_radius;
                yv += glow;

                if(mix_drive > 0) {
                    const int chroma_kick = (glow * mix_drive + 500) / 1000;
                    uv += chroma_kick >> 3;
                    vv += chroma_kick >> 4;
                }
            }
        }

        Y[i]  = threshold_u8(yv);
        Cb[i] = threshold_u8(uv);
        Cr[i] = threshold_u8(vv);
    }
}
