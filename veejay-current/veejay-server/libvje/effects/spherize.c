/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "spherize.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPHERIZE_PARAMS 10

#define P_STRENGTH     0
#define P_ANGLE        1
#define P_RADIUS       2
#define P_RATIO_X      3
#define P_RATIO_Y      4
#define P_CENTER_X     5
#define P_CENTER_Y     6
#define P_MODE         7
#define P_WARP_DRIVE   8
#define P_RADIUS_DRIVE 9

typedef struct {
    uint8_t *buf[3];
    float *lut;
    float *atan2_lut;
    float *sin_lut;
    float *dist_lut;
    float *exp_lut;

    int last_cx;
    int last_cy;
    int last_radius;
    float last_angle;

    float eff_strength;
    float eff_angle;
    float eff_radius;
    float eff_ratio_x;
    float eff_ratio_y;
    float eff_center_x;
    float eff_center_y;
    float eff_warp_drive;
    float eff_radius_drive;
    int eff_initialized;

    int n_threads;
} spherize_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int wrapi(int v, int max)
{
    if(max <= 1)
        return 0;

    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline int reflecti(int v, int max)
{
    if(max <= 1)
        return 0;

    const int hi = max - 1;
    const int period = hi << 1;

    if(period <= 0)
        return 0;

    v %= period;

    if(v < 0)
        v += period;

    return (v <= hi) ? v : period - v;
}


static inline int spherize_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline float spherize_smooth_angle_deg(float *state, float target, float attack, float release)
{
    float cur = *state;
    float diff = target - cur;

    while(diff > 180.0f)
        diff -= 360.0f;
    while(diff < -180.0f)
        diff += 360.0f;

    cur += diff * ((diff > 0.0f) ? attack : release);

    while(cur < 0.0f)
        cur += 360.0f;
    while(cur >= 360.0f)
        cur -= 360.0f;

    *state = cur;
    return cur;
}

static inline uint8_t spherize_blend_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

vj_effect *spherize_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SPHERIZE_PARAMS;

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

    const int max_radius = (int)sqrtf(((float)w * (float)w * 0.25f) + ((float)h * (float)h * 0.25f));
    const int radius_hi = max_radius > 1 ? max_radius : 1;

    ve->limits[0][P_STRENGTH]    = 0;              ve->limits[1][P_STRENGTH]    = 100;
    ve->limits[0][P_ANGLE]       = 0;              ve->limits[1][P_ANGLE]       = 360;
    ve->limits[0][P_RADIUS]      = 1;              ve->limits[1][P_RADIUS]      = radius_hi;
    ve->limits[0][P_RATIO_X]     = 10;             ve->limits[1][P_RATIO_X]     = 200;
    ve->limits[0][P_RATIO_Y]     = 10;             ve->limits[1][P_RATIO_Y]     = 200;
    ve->limits[0][P_CENTER_X]    = 0;              ve->limits[1][P_CENTER_X]    = w;
    ve->limits[0][P_CENTER_Y]    = 0;              ve->limits[1][P_CENTER_Y]    = h;
    ve->limits[0][P_MODE]        = 0;              ve->limits[1][P_MODE]        = 2;
    ve->limits[0][P_WARP_DRIVE]   = 0;             ve->limits[1][P_WARP_DRIVE]   = 1000;
    ve->limits[0][P_RADIUS_DRIVE] = 0;             ve->limits[1][P_RADIUS_DRIVE] = 1000;

    ve->defaults[P_STRENGTH]    = 33;
    ve->defaults[P_ANGLE]       = 340;
    ve->defaults[P_RADIUS]      = radius_hi / 2;
    ve->defaults[P_RATIO_X]     = 100;
    ve->defaults[P_RATIO_Y]     = 100;
    ve->defaults[P_CENTER_X]    = w / 2;
    ve->defaults[P_CENTER_Y]    = h / 2;
    ve->defaults[P_MODE]        = 2;
    ve->defaults[P_WARP_DRIVE]   = 0;
    ve->defaults[P_RADIUS_DRIVE] = 0;

    ve->description = "Spherize";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Strength",
        "Angle",
        "Radius",
        "Ratio X",
        "Ratio Y",
        "Center X",
        "Center Y",
        "Mode",
        "Warp Drive",
        "Radius Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Clamp",
        "Wrap",
        "Reflect"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WARP,               VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       0,                  100,                30, 100, 120, 1100, 0,    96,
        VJ_BEAT_GEOMETRY_PHASE,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                                0,                  360,                18, 78,  260, 1800, 0,    78,
        VJ_BEAT_WINDOW_RADIUS,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       1,                  radius_hi,          20, 86,  260, 1900, 0,    84,
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      30,                 200,                10, 48,  900, 3600, 400,  42,
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      30,                 200,                10, 48,  900, 3600, 400,  42,
        VJ_BEAT_DRIFT,              VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_CONTINUOUS,                         0,                  w,                   6, 24, 2600, 8200, 1800, 26,
        VJ_BEAT_DRIFT,              VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_CONTINUOUS,                         0,                  h,                   6, 24, 2600, 8200, 1800, 26,
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,               VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       0,                  1000,               34, 100,  80,  900, 0,    100,
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       0,                  1000,               28, 96,  120, 1100, 0,    94
    );

    return ve;
}

static void spherize_rebuild_center_luts(spherize_t *s, int w, int h, int cx, int cy)
{
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < h; y++) {
        const float dy = (float)(y - cy);
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const float dx = (float)(x - cx);
            const int idx = row + x;

            s->atan2_lut[idx] = atan2f(dy, dx);
            s->dist_lut[idx] = sqrtf(dx * dx + dy * dy);
        }
    }

    s->last_cx = cx;
    s->last_cy = cy;
    s->last_angle = -999999.0f;
    s->last_radius = -1;
}

static void spherize_rebuild_sin_lut(spherize_t *s, int len, float angle)
{
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++)
        s->sin_lut[i] = sinf(s->atan2_lut[i] - angle);

    s->last_angle = angle;
}

static void spherize_rebuild_exp_lut(spherize_t *s, int len, int radius)
{
    const float r = (float)(radius > 0 ? radius : 1);
    const float inv_sigma = 1.0f / (2.0f * r * r);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        const float d = s->dist_lut[i];
        s->exp_lut[i] = expf(-(d * d) * inv_sigma);
    }

    s->last_radius = radius;
}

void *spherize_malloc(int w, int h)
{
    spherize_t *s = (spherize_t*) vj_calloc(sizeof(spherize_t));
    if(!s)
        return NULL;

    const int pixels = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)pixels * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + pixels;
    s->buf[2] = s->buf[1] + pixels;

    s->lut = (float*) vj_malloc(sizeof(float) * (size_t)pixels * 4u);
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->atan2_lut = s->lut;
    s->sin_lut   = s->atan2_lut + pixels;
    s->dist_lut  = s->sin_lut + pixels;
    s->exp_lut   = s->dist_lut + pixels;

    s->last_cx = INT_MIN;
    s->last_cy = INT_MIN;
    s->last_radius = -1;
    s->last_angle = -999999.0f;

    s->eff_strength = 0.0f;
    s->eff_angle = 0.0f;
    s->eff_radius = 0.0f;
    s->eff_ratio_x = 0.0f;
    s->eff_ratio_y = 0.0f;
    s->eff_center_x = 0.0f;
    s->eff_center_y = 0.0f;
    s->eff_warp_drive = 0.0f;
    s->eff_radius_drive = 0.0f;
    s->eff_initialized = 0;

    s->n_threads = vje_advise_num_threads(pixels);

    return (void*) s;
}

void spherize_free(void *ptr)
{
    spherize_t *s = (spherize_t*) ptr;

    free(s->buf[0]);
    free(s->lut);
    free(s);
}

void spherize_apply(void *ptr, VJFrame *frame, int *args)
{
    spherize_t *s = (spherize_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int max_radius = (int)sqrtf(((float)width * (float)width * 0.25f) + ((float)height * (float)height * 0.25f));
    const int radius_max = (max_radius > 1) ? max_radius : 1;

    int strength_arg = args[P_STRENGTH];
    int angle_arg = args[P_ANGLE];
    int radius_arg = args[P_RADIUS];
    int ratio_x_arg = args[P_RATIO_X];
    int ratio_y_arg = args[P_RATIO_Y];
    int center_x_arg = args[P_CENTER_X];
    int center_y_arg = args[P_CENTER_Y];
    const int mode = args[P_MODE];

    const int warp_drive_arg = clampi(args[P_WARP_DRIVE], 0, 1000);
    const int radius_drive_arg = clampi(args[P_RADIUS_DRIVE], 0, 1000);

    if(!s->eff_initialized) {
        s->eff_strength = (float)strength_arg;
        s->eff_angle = (float)angle_arg;
        s->eff_radius = (float)radius_arg;
        s->eff_ratio_x = (float)ratio_x_arg;
        s->eff_ratio_y = (float)ratio_y_arg;
        s->eff_center_x = (float)center_x_arg;
        s->eff_center_y = (float)center_y_arg;
        s->eff_warp_drive = (float)warp_drive_arg;
        s->eff_radius_drive = (float)radius_drive_arg;
        s->eff_initialized = 1;
    }

    const float geo_attack = 0.18f;
    const float geo_release = 0.095f;
    const float slow_attack = 0.115f;
    const float slow_release = 0.070f;

    strength_arg = clampi(spherize_smooth_i(&s->eff_strength, strength_arg, geo_attack, geo_release), 0, 100);
    angle_arg = (int)(spherize_smooth_angle_deg(&s->eff_angle, (float)angle_arg, slow_attack, slow_release) + 0.5f);
    radius_arg = clampi(spherize_smooth_i(&s->eff_radius, radius_arg, slow_attack, slow_release), 1, radius_max);
    ratio_x_arg = clampi(spherize_smooth_i(&s->eff_ratio_x, ratio_x_arg, slow_attack, slow_release), 10, 200);
    ratio_y_arg = clampi(spherize_smooth_i(&s->eff_ratio_y, ratio_y_arg, slow_attack, slow_release), 10, 200);
    center_x_arg = clampi(spherize_smooth_i(&s->eff_center_x, center_x_arg, slow_attack, slow_release), 0, width);
    center_y_arg = clampi(spherize_smooth_i(&s->eff_center_y, center_y_arg, slow_attack, slow_release), 0, height);

    const int warp_drive = clampi(spherize_smooth_i(&s->eff_warp_drive, warp_drive_arg, geo_attack, geo_release), 0, 1000);
    const int radius_drive = clampi(spherize_smooth_i(&s->eff_radius_drive, radius_drive_arg, geo_attack, geo_release), 0, 1000);

    int effective_strength = strength_arg;
    effective_strength += (warp_drive * 44 + 500) / 1000;
    effective_strength = clampi(effective_strength, 0, 100);

    int effective_radius = radius_arg;
    effective_radius += (radius_drive * radius_max * 36 + 50000) / 100000;
    effective_radius = clampi(effective_radius, 1, radius_max);

    float effective_angle_deg = (float)angle_arg;
    effective_angle_deg += (float)warp_drive * 0.055f;
    while(effective_angle_deg >= 360.0f)
        effective_angle_deg -= 360.0f;
    while(effective_angle_deg < 0.0f)
        effective_angle_deg += 360.0f;

    const float ratio_spread = (float)radius_drive * 0.00018f;
    float ratio_x = (float)ratio_x_arg * 0.01f;
    float ratio_y = (float)ratio_y_arg * 0.01f;
    ratio_x *= (1.0f + ratio_spread);
    ratio_y *= (1.0f - (ratio_spread * 0.55f));
    ratio_x = clampf(ratio_x, 0.10f, 2.40f);
    ratio_y = clampf(ratio_y, 0.10f, 2.40f);

    const int center_dx = (int)((float)radius_drive * 0.000035f * (float)width);
    const int center_dy = (int)((float)warp_drive * -0.000025f * (float)height);
    const int center_x = clampi(center_x_arg + center_dx, 0, width);
    const int center_y = clampi(center_y_arg + center_dy, 0, height);

    const float strength = (float)effective_strength * 0.01f;
    const float angle = effective_angle_deg * ((float)M_PI / 180.0f);

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if(s->last_cx != center_x || s->last_cy != center_y)
        spherize_rebuild_center_luts(s, width, height, center_x, center_y);

    if(s->last_angle != angle)
        spherize_rebuild_sin_lut(s, len, angle);

    if(s->last_radius != effective_radius)
        spherize_rebuild_exp_lut(s, len, effective_radius);

    const float *restrict sin_lut = s->sin_lut;
    const float *restrict exp_lut = s->exp_lut;

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = clampi(sx, 0, width - 1);
                sy = clampi(sy, 0, height - 1);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    } else if(mode == 1) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = wrapi(sx, width);
                sy = wrapi(sy, height);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const float dy_scaled = (float)(y - center_y) * ratio_y;

            for(int x = 0; x < width; x++) {
                const int idx = row + x;
                const float dx_scaled = (float)(x - center_x) * ratio_x;
                const float warp = 1.0f + strength * sin_lut[idx] * exp_lut[idx];

                int sx = (int)((float)center_x + dx_scaled * warp);
                int sy = (int)((float)center_y + dy_scaled * warp);

                sx = reflecti(sx, width);
                sy = reflecti(sy, height);

                const int src = sy * width + sx;

                srcY[idx] = bufY[src];
                srcU[idx] = bufU[src];
                srcV[idx] = bufV[src];
            }
        }
    }
}
