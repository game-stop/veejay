/*
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>

#define KEYSELECT_PARAMS 8

#define P_HUE_ANGLE 0
#define P_RED       1
#define P_GREEN     2
#define P_BLUE      3
#define P_THRESHOLD 4
#define P_SOLIDITY  5
#define P_BLENDMODE 6
#define P_SWAP      7

#define KEYSELECT_SCALE 4096
#define KEYSELECT_PI    3.14159265358979323846f

typedef struct {
    int n_threads;
    int last[KEYSELECT_PARAMS];
    int mag_fp;
    int cos_q_fp;
    int sin_q_fp;
    int inv_wedge_slope_fp;
    int inv_range_fp;
    int black_clip_fp;
    int blend_mode;
    int swap;
} keyselect_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int keyselect_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t keyselect_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t keyselect_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * opacity + (int)b * inv;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

static inline uint8_t keyselect_blend_mode(int mode, uint8_t a, uint8_t b)
{
    switch(mode) {
        case 1:
            return (a == 0) ? 0 : keyselect_u8(255 - (((255 - (int)b) * (255 - (int)b)) / (int)a));
        case 2:
            return (uint8_t)(((uint16_t)a * (uint16_t)b) >> 8);
        case 3: {
            const int c = 255 - (int)b;
            return (c == 0) ? 255 : keyselect_u8(((int)a * (int)a) / c);
        }
        case 4: {
            const int c = 255 - (int)b;
            return (c == 0) ? b : keyselect_u8(((int)b * 255) / c);
        }
        case 5:
            return keyselect_u8((int)a + (int)b - 255);
        case 6:
            return keyselect_u8((int)a + ((int)b << 1) - 255);
        case 7: {
            const int c = (b < 128) ? (((int)a * (int)b) >> 7)
                                    : 255 - ((((255 - (int)b) * (255 - (int)a))) >> 7);
            return keyselect_u8(c);
        }
        case 0:
        default:
            return (uint8_t)(255 - keyselect_absi(255 - (int)a - (int)b));
    }
}

vj_effect *keyselect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = KEYSELECT_PARAMS;
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

    ve->defaults[P_HUE_ANGLE] = 4500;
    ve->defaults[P_RED] = 0;
    ve->defaults[P_GREEN] = 0;
    ve->defaults[P_BLUE] = 255;
    ve->defaults[P_THRESHOLD] = 40;
    ve->defaults[P_SOLIDITY] = 160;
    ve->defaults[P_BLENDMODE] = 3;
    ve->defaults[P_SWAP] = 0;

    ve->limits[0][P_HUE_ANGLE] = 500; ve->limits[1][P_HUE_ANGLE] = 8500;
    ve->limits[0][P_RED] = 0;         ve->limits[1][P_RED] = 255;
    ve->limits[0][P_GREEN] = 0;       ve->limits[1][P_GREEN] = 255;
    ve->limits[0][P_BLUE] = 0;        ve->limits[1][P_BLUE] = 255;
    ve->limits[0][P_THRESHOLD] = 0;   ve->limits[1][P_THRESHOLD] = 255;
    ve->limits[0][P_SOLIDITY] = 1;    ve->limits[1][P_SOLIDITY] = 255;
    ve->limits[0][P_BLENDMODE] = 0;   ve->limits[1][P_BLENDMODE] = 7;
    ve->limits[0][P_SWAP] = 0;        ve->limits[1][P_SWAP] = 1;

    ve->description = "Blend by Color Key (Advanced)";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Hue Angle",
        "Red",
        "Green",
        "Blue",
        "Threshold",
        "Solidity",
        "Blend mode",
        "Swap Selection"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_BLENDMODE],
        P_BLENDMODE,
        "Softburn",
        "Color Dodge",
        "Multiply",
        "Color Burn",
        "Lighten Burn",
        "Subtract",
        "Linear Burn",
        "Overlay"
    );
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_SWAP], P_SWAP, "Normal", "Swap");

    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 800, 7600, 78, 100, 15, 520, 0, 20, 60, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 220, 84, 100, 12, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 40, 255, 80, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *keyselect_malloc(int w, int h)
{
    keyselect_t *s = (keyselect_t*) vj_malloc(sizeof(keyselect_t));

    if(!s)
        return NULL;

    for(int i = 0; i < KEYSELECT_PARAMS; i++)
        s->last[i] = -1000000;

    s->mag_fp = KEYSELECT_SCALE;
    s->cos_q_fp = KEYSELECT_SCALE;
    s->sin_q_fp = 0;
    s->inv_wedge_slope_fp = KEYSELECT_SCALE;
    s->inv_range_fp = 255 << 8;
    s->black_clip_fp = 0;
    s->blend_mode = 3;
    s->swap = 0;
    s->n_threads = vje_advise_num_threads(w * h);

    return (void*) s;
}

void keyselect_free(void *ptr)
{
    free(ptr);
}

static void keyselect_update_cache(keyselect_t *s, const int *args)
{
    int changed = 0;

    for(int i = 0; i < KEYSELECT_PARAMS; i++) {
        if(args[i] != s->last[i]) {
            changed = 1;
            break;
        }
    }

    if(!changed)
        return;

    const int angle = clampi(args[P_HUE_ANGLE], 500, 8500);
    const int red = clampi(args[P_RED], 0, 255);
    const int green = clampi(args[P_GREEN], 0, 255);
    const int blue = clampi(args[P_BLUE], 0, 255);
    const int threshold = clampi(args[P_THRESHOLD], 0, 255);
    const int solidity = clampi(args[P_SOLIDITY], 1, 255);
    const int range = solidity > threshold ? solidity - threshold : 1;

    int iy = 0;
    int iu = 128;
    int iv = 128;

    _rgb2yuv(red, green, blue, iy, iu, iv);

    const float ut_f = (float)iu - 128.0f;
    const float vt_f = (float)iv - 128.0f;
    float mag_f = sqrtf(ut_f * ut_f + vt_f * vt_f);

    if(mag_f < 1.0f)
        mag_f = 1.0f;

    const float angle_rad = ((float)angle * 0.01f) * (KEYSELECT_PI / 180.0f);
    const float t = tanf(angle_rad);

    s->mag_fp = (int)(mag_f * (float)KEYSELECT_SCALE + 0.5f);
    s->cos_q_fp = (int)((ut_f / mag_f) * (float)KEYSELECT_SCALE + (ut_f >= 0.0f ? 0.5f : -0.5f));
    s->sin_q_fp = (int)((vt_f / mag_f) * (float)KEYSELECT_SCALE + (vt_f >= 0.0f ? 0.5f : -0.5f));
    s->inv_wedge_slope_fp = (int)((1.0f / t) * (float)KEYSELECT_SCALE + 0.5f);
    s->inv_range_fp = (int)((255.0f / (float)range) * 256.0f + 0.5f);
    s->black_clip_fp = threshold * KEYSELECT_SCALE;
    s->blend_mode = clampi(args[P_BLENDMODE], 0, 7);
    s->swap = args[P_SWAP] ? 1 : 0;

    for(int i = 0; i < KEYSELECT_PARAMS; i++)
        s->last[i] = args[i];
}

void keyselect_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    keyselect_t *s = (keyselect_t*) ptr;

    keyselect_update_cache(s, args);

    const int mag_fp = s->mag_fp;
    const int cos_q_fp = s->cos_q_fp;
    const int sin_q_fp = s->sin_q_fp;
    const int inv_wedge_slope_fp = s->inv_wedge_slope_fp;
    const int inv_range_fp = s->inv_range_fp;
    const int black_clip_fp = s->black_clip_fp;
    const int blend_mode = s->blend_mode;
    const int swap = s->swap;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict src_Y = swap ? frame2->data[0] : frame->data[0];
    const uint8_t *restrict src_Cb = swap ? frame2->data[1] : frame->data[1];
    const uint8_t *restrict src_Cr = swap ? frame2->data[2] : frame->data[2];

    const uint8_t *restrict bg_Y = swap ? frame->data[0] : frame2->data[0];
    const uint8_t *restrict bg_U = swap ? frame->data[1] : frame2->data[1];
    const uint8_t *restrict bg_V = swap ? frame->data[2] : frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int pos = 0; pos < len; pos++) {
        const int uc = (int)Cb[pos] - 128;
        const int vc = (int)Cr[pos] - 128;

        const int xx = (uc * cos_q_fp + vc * sin_q_fp) >> 12;
        const int yy = (vc * cos_q_fp - uc * sin_q_fp) >> 12;
        const int abs_yy = keyselect_absi(yy);

        const int dist_fp = (mag_fp - (xx << 12)) + (abs_yy * inv_wedge_slope_fp);
        int alpha = ((dist_fp - black_clip_fp) * inv_range_fp) >> 20;

        alpha = clampi(alpha, 0, 255);

        const int alpha_inv = 255 - alpha;

        if(alpha_inv > 0) {
            const uint8_t blended_y = keyselect_blend_mode(blend_mode, src_Y[pos], bg_Y[pos]);
            const uint8_t b_cb = keyselect_u8(((int)bg_Y[pos] * ((int)src_Cb[pos] - (int)bg_U[pos]) >> 8) + (int)src_Cb[pos]);
            const uint8_t b_cr = keyselect_u8(((int)bg_Y[pos] * ((int)src_Cr[pos] - (int)bg_V[pos]) >> 8) + (int)src_Cr[pos]);

            if(alpha_inv == 255) {
                Y[pos] = blended_y;
                Cb[pos] = b_cb;
                Cr[pos] = b_cr;
            }
            else {
                Y[pos] = keyselect_blend255(blended_y, Y[pos], alpha_inv);
                Cb[pos] = keyselect_blend255(b_cb, Cb[pos], alpha_inv);
                Cr[pos] = keyselect_blend255(b_cr, Cr[pos], alpha_inv);
            }
        }
    }
}
