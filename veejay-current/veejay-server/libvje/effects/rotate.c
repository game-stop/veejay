/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>
#include "rotate.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ROTATE_PARAMS 7

#define P_ROTATE       0
#define P_AUTOMATIC    1
#define P_DURATION     2
#define P_MIX          3
#define P_CHROMA       4
#define P_SPIN_DRIVE   5
#define P_WOBBLE_DRIVE 6

typedef struct {
    uint8_t *buf[3];
    float sin_lut[360];
    float cos_lut[360];

    double rotate;
    int frameCount;
    int direction;

    float sm_rotate;
    float sm_duration;
    float sm_mix;
    float sm_chroma;
    float sm_spin_drive;
    float sm_wobble_drive;
    float phase;
    int initialized;

    int n_threads;
} rotate_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float rotate_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int rotate_wrap360(double v)
{
    int a = (int)v % 360;

    if(a < 0)
        a += 360;

    return a;
}

static inline uint8_t rotate_blend_y(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t rotate_blend_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int rotate_to_q8_1000(float v)
{
    return clampi((int)(rotate_clampf(v, 0.0f, 1000.0f) * 0.256f + 0.5f), 0, 256);
}

static inline float rotate_smoothf(float oldv, float newv, float amount)
{
    return oldv + (newv - oldv) * amount;
}



static inline uint8_t rotate_bilinear_u8(const uint8_t *restrict img, int w, int x_fixed, int y_fixed)
{
    const int x = x_fixed >> 16;
    const int y = y_fixed >> 16;
    const int xf = (x_fixed >> 8) & 0xff;
    const int yf = (y_fixed >> 8) & 0xff;
    const int idx = y * w + x;
    const int w11 = (256 - xf) * (256 - yf);
    const int w21 = xf * (256 - yf);
    const int w12 = (256 - xf) * yf;
    const int w22 = xf * yf;
    const int res = img[idx] * w11 + img[idx + 1] * w21 + img[idx + w] * w12 + img[idx + w + 1] * w22;

    return (uint8_t)(res >> 16);
}

static inline float rotate_mirror_coord(float p, float max_safe)
{
    while(p < 0.0f || p > max_safe)
        p = p < 0.0f ? -p : (2.0f * max_safe - p);

    return p;
}

vj_effect *rotate_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = ROTATE_PARAMS;
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

    ve->defaults[P_ROTATE] = 0;
    ve->defaults[P_AUTOMATIC] = 1;
    ve->defaults[P_DURATION] = 100;
    ve->defaults[P_MIX] = 1000;
    ve->defaults[P_CHROMA] = 1000;
    ve->defaults[P_SPIN_DRIVE] = 0;
    ve->defaults[P_WOBBLE_DRIVE] = 0;

    ve->limits[0][P_ROTATE] = 0;       ve->limits[1][P_ROTATE] = 360;
    ve->limits[0][P_AUTOMATIC] = 0;    ve->limits[1][P_AUTOMATIC] = 1;
    ve->limits[0][P_DURATION] = 1;     ve->limits[1][P_DURATION] = 1500;
    ve->limits[0][P_MIX] = 0;          ve->limits[1][P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;       ve->limits[1][P_CHROMA] = 1000;
    ve->limits[0][P_SPIN_DRIVE] = 0;   ve->limits[1][P_SPIN_DRIVE] = 1000;
    ve->limits[0][P_WOBBLE_DRIVE] = 0; ve->limits[1][P_WOBBLE_DRIVE] = 1000;

    ve->description = "Rotate (Bilinear/Mirror)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Rotate",
        "Automatic",
        "Duration",
        "Mix",
        "Chroma Amount",
        "Spin Drive",
        "Wobble Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_AUTOMATIC], P_AUTOMATIC, "Manual", "Automatic");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 1, 360, 72, 100, 0, 220, 0, 1, 0, VJ_BEAT_COST_CHEAP, 60, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 24, 700, 82, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 420, 1000, 86, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 360, 1000, 70, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 88, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *rotate_malloc(int width, int height)
{
    rotate_t *r = (rotate_t*)vj_calloc(sizeof(rotate_t));

    if(!r)
        return NULL;

    const int len = width * height;

    r->buf[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!r->buf[0]) {
        free(r);
        return NULL;
    }

    r->buf[1] = r->buf[0] + len;
    r->buf[2] = r->buf[1] + len;

    r->rotate = 0.0;
    r->frameCount = 0;
    r->direction = 1;
    r->sm_rotate = 0.0f;
    r->sm_duration = 100.0f;
    r->sm_mix = 1000.0f;
    r->sm_chroma = 1000.0f;
    r->sm_spin_drive = 0.0f;
    r->sm_wobble_drive = 0.0f;
    r->phase = 0.0f;
    r->initialized = 0;

    for(int i = 0; i < 360; i++) {
        const double rad = (double)i * M_PI / 180.0;

        r->sin_lut[i] = a_sin(rad);
        r->cos_lut[i] = a_cos(rad);
    }

    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void rotate_free(void *ptr)
{
    rotate_t *r = (rotate_t*)ptr;

    free(r->buf[0]);
    free(r);
}

void rotate_apply(void *ptr, VJFrame *frame, int *args)
{
    rotate_t *r = (rotate_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int rotate_arg = args[P_ROTATE];
    const int automatic = args[P_AUTOMATIC] ? 1 : 0;
    const int duration_arg = args[P_DURATION];
    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA];
    const int spin_drive_arg = args[P_SPIN_DRIVE];
    const int wobble_drive_arg = args[P_WOBBLE_DRIVE];

    if(!r->initialized) {
        r->sm_rotate = (float)rotate_arg;
        r->sm_duration = (float)duration_arg;
        r->sm_mix = (float)mix_arg;
        r->sm_chroma = (float)chroma_arg;
        r->sm_spin_drive = (float)spin_drive_arg;
        r->sm_wobble_drive = (float)wobble_drive_arg;
        r->initialized = 1;
    }

    const float fast_t = 0.34f;
    const float slow_t = 0.18f;

    r->sm_rotate = rotate_smoothf(r->sm_rotate, (float)rotate_arg, fast_t);
    r->sm_duration = rotate_smoothf(r->sm_duration, (float)duration_arg, slow_t);
    r->sm_mix = rotate_smoothf(r->sm_mix, (float)mix_arg, fast_t);
    r->sm_chroma = rotate_smoothf(r->sm_chroma, (float)chroma_arg, fast_t);
    r->sm_spin_drive = rotate_smoothf(r->sm_spin_drive, (float)spin_drive_arg, fast_t);
    r->sm_wobble_drive = rotate_smoothf(r->sm_wobble_drive, (float)wobble_drive_arg, fast_t);

    const int duration = clampi((int)(r->sm_duration + 0.5f), 1, 1500);
    double rotate_value;

    const float spin_drive = rotate_clampf(r->sm_spin_drive * 0.001f, 0.0f, 1.0f);
    const float wobble_drive = rotate_clampf(r->sm_wobble_drive * 0.001f, 0.0f, 1.0f);

    if(automatic) {
        rotate_value = r->rotate;

        const float speed_boost = 1.0f + spin_drive * 1.65f + wobble_drive * 0.35f;

        r->rotate += (double)r->direction * (360.0 / (double)duration) * (double)speed_boost;
        r->frameCount++;

        if(r->frameCount >= duration || r->rotate <= 0.0 || r->rotate >= 360.0) {
            r->direction *= -1;
            r->frameCount = 0;

            if(r->rotate < 0.0)
                r->rotate = 0.0;
            else if(r->rotate > 360.0)
                r->rotate = 360.0;
        }
    }
    else {
        rotate_value = (double)r->sm_rotate;
        r->rotate = rotate_value;
        r->frameCount = 0;
        r->direction = 1;
    }

    r->phase += 0.015f + wobble_drive * 0.145f + spin_drive * 0.035f;

    if(r->phase >= 360.0f)
        r->phase -= 360.0f;

    const int phase_idx = rotate_wrap360((double)r->phase);
    const float wobble = r->sin_lut[phase_idx] * wobble_drive * 128.0f;
    const float direct_spin = spin_drive * 360.0f;

    rotate_value += (double)(direct_spin + wobble);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = r->buf[0];
    uint8_t *restrict srcU = r->buf[1];
    uint8_t *restrict srcV = r->buf[2];

    veejay_memcpy(srcY, dstY, len);
    veejay_memcpy(srcU, dstU, len);
    veejay_memcpy(srcV, dstV, len);

    const float center_x = ((float)width - 1.0f) * 0.5f;
    const float center_y = ((float)height - 1.0f) * 0.5f;
    const int angle = rotate_wrap360(rotate_value);
    const float c = r->cos_lut[angle];
    const float s = r->sin_lut[angle];
    const float max_x = (float)width - 1.001f;
    const float max_y = (float)height - 1.001f;

    int mix_q8 = rotate_to_q8_1000(r->sm_mix);
    int chroma_q8 = rotate_to_q8_1000((r->sm_mix * r->sm_chroma) * 0.001f);

    if(spin_drive > 0.0f || wobble_drive > 0.0f) {
        const float drive = rotate_clampf((spin_drive * 0.60f) + (wobble_drive * 0.40f), 0.0f, 1.0f);
        const int lift = (int)(drive * 38.0f + 0.5f);

        mix_q8 = clampi(mix_q8 + lift, 0, 256);
        chroma_q8 = clampi(chroma_q8 + (lift >> 1), 0, 256);
    }

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const float dy = (float)y - center_y;
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const float dx = (float)x - center_x;

            float rx = dx * c - dy * s + center_x;
            float ry = dx * s + dy * c + center_y;

            rx = rotate_mirror_coord(rx, max_x);
            ry = rotate_mirror_coord(ry, max_y);

            const int rx_f = (int)(rx * 65536.0f);
            const int ry_f = (int)(ry * 65536.0f);
            const int dst = row + x;
            const uint8_t ryv = rotate_bilinear_u8(srcY, width, rx_f, ry_f);
            const uint8_t ruv = rotate_bilinear_u8(srcU, width, rx_f, ry_f);
            const uint8_t rvv = rotate_bilinear_u8(srcV, width, rx_f, ry_f);

            dstY[dst] = rotate_blend_y(srcY[dst], ryv, mix_q8);
            dstU[dst] = rotate_blend_uv(srcU[dst], ruv, chroma_q8);
            dstV[dst] = rotate_blend_uv(srcV[dst], rvv, chroma_q8);
        }
    }
}
