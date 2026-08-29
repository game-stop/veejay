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

    int center_x;
    int center_y;
    int effective_radius;
    float strength;
    float angle;
    float ratio_x;
    float ratio_y;
    int rebuild_center;
    int rebuild_angle;
    int rebuild_radius;
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

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 12, 100, 92, 100, 8, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 360, 76, 100, 0, 240, 0, 1, 180, VJ_BEAT_COST_EXPENSIVE, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 8, radius_hi, 82, 100, 12, 520, 0, 1, 220, VJ_BEAT_COST_EXPENSIVE, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 55, 160, 72, 94, 80, 720, 0, 1, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 55, 160, 68, 92, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 94, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void spherize_rebuild_center_luts(spherize_t *s, int w, int h, int cx, int cy)
{
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
    for(int i = 0; i < len; i++)
        s->sin_lut[i] = sinf(s->atan2_lut[i] - angle);

    s->last_angle = angle;
}

static void spherize_rebuild_exp_lut(spherize_t *s, int len, int radius)
{
    const float r = (float)(radius > 0 ? radius : 1);
    const float inv_sigma = 1.0f / (2.0f * r * r);

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

    const int strength_arg = args[P_STRENGTH];
    const int angle_arg = args[P_ANGLE];
    const int radius_arg = args[P_RADIUS];
    const int ratio_x_arg = args[P_RATIO_X];
    const int ratio_y_arg = args[P_RATIO_Y];
    const int center_x_arg = args[P_CENTER_X];
    const int center_y_arg = args[P_CENTER_Y];
    const int mode = args[P_MODE];

    const int warp_drive_arg = clampi(args[P_WARP_DRIVE], 0, 1000);
    const int radius_drive_arg = clampi(args[P_RADIUS_DRIVE], 0, 1000);

    const int max_radius = (int)sqrtf(((float)width * (float)width * 0.25f) + ((float)height * (float)height * 0.25f));
    const int radius_max = (max_radius > 1) ? max_radius : 1;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    #pragma omp single
    {
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

        float geo_attack = 0.18f;
        float geo_release = 0.095f;
        float slow_attack = 0.115f;
        float slow_release = 0.070f;

        int sm_strength = clampi(spherize_smooth_i(&s->eff_strength, strength_arg, geo_attack, geo_release), 0, 100);
        int sm_angle = (int)(spherize_smooth_angle_deg(&s->eff_angle, (float)angle_arg, slow_attack, slow_release) + 0.5f);
        int sm_radius = clampi(spherize_smooth_i(&s->eff_radius, radius_arg, slow_attack, slow_release), 1, radius_max);
        int sm_ratio_x = clampi(spherize_smooth_i(&s->eff_ratio_x, ratio_x_arg, slow_attack, slow_release), 10, 200);
        int sm_ratio_y = clampi(spherize_smooth_i(&s->eff_ratio_y, ratio_y_arg, slow_attack, slow_release), 10, 200);
        int sm_center_x = clampi(spherize_smooth_i(&s->eff_center_x, center_x_arg, slow_attack, slow_release), 0, width);
        int sm_center_y = clampi(spherize_smooth_i(&s->eff_center_y, center_y_arg, slow_attack, slow_release), 0, height);

        int sm_warp = clampi(spherize_smooth_i(&s->eff_warp_drive, warp_drive_arg, geo_attack, geo_release), 0, 1000);
        int sm_radius_drv = clampi(spherize_smooth_i(&s->eff_radius_drive, radius_drive_arg, geo_attack, geo_release), 0, 1000);

        int effective_strength = sm_strength;
        effective_strength += (sm_warp * 44 + 500) / 1000;
        effective_strength = clampi(effective_strength, 0, 100);

        s->effective_radius = sm_radius;
        s->effective_radius += (sm_radius_drv * radius_max * 36 + 50000) / 100000;
        s->effective_radius = clampi(s->effective_radius, 1, radius_max);

        float effective_angle_deg = (float)sm_angle;
        effective_angle_deg += (float)sm_warp * 0.055f;
        while(effective_angle_deg >= 360.0f)
            effective_angle_deg -= 360.0f;
        while(effective_angle_deg < 0.0f)
            effective_angle_deg += 360.0f;

        float ratio_spread = (float)sm_radius_drv * 0.00018f;
        s->ratio_x = (float)sm_ratio_x * 0.01f;
        s->ratio_y = (float)sm_ratio_y * 0.01f;
        s->ratio_x *= (1.0f + ratio_spread);
        s->ratio_y *= (1.0f - (ratio_spread * 0.55f));
        s->ratio_x = clampf(s->ratio_x, 0.10f, 2.40f);
        s->ratio_y = clampf(s->ratio_y, 0.10f, 2.40f);

        int center_dx = (int)((float)sm_radius_drv * 0.000035f * (float)width);
        int center_dy = (int)((float)sm_warp * -0.000025f * (float)height);
        s->center_x = clampi(sm_center_x + center_dx, 0, width);
        s->center_y = clampi(sm_center_y + center_dy, 0, height);

        s->strength = (float)effective_strength * 0.01f;
        s->angle = effective_angle_deg * ((float)M_PI / 180.0f);

        s->rebuild_center = (s->last_cx != s->center_x || s->last_cy != s->center_y);
        s->rebuild_angle = s->rebuild_center || (s->last_angle != s->angle);
        s->rebuild_radius = s->rebuild_center || (s->last_radius != s->effective_radius);
    }
    
    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        bufY[i] = srcY[i];
        bufU[i] = srcU[i];
        bufV[i] = srcV[i];
    }

    if(s->rebuild_center)
        spherize_rebuild_center_luts(s, width, height, s->center_x, s->center_y);

    if(s->rebuild_angle)
        spherize_rebuild_sin_lut(s, len, s->angle);

    if(s->rebuild_radius)
        spherize_rebuild_exp_lut(s, len, s->effective_radius);

    const float *restrict sin_lut = s->sin_lut;
    const float *restrict exp_lut = s->exp_lut;
    const float strength = s->strength;
    const float ratio_x = s->ratio_x;
    const float ratio_y = s->ratio_y;
    const int center_x = s->center_x;
    const int center_y = s->center_y;

    if(mode == 0) {
        #pragma omp for schedule(static)
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
        #pragma omp for schedule(static)
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
        #pragma omp for schedule(static)
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