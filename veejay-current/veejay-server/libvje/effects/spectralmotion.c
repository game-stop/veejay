/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "spectralmotion.h"

#define SPECTRALMOTION_PARAMS 8

#define P_TRIGGER            0
#define P_CYCLE_SPEED        1
#define P_OPACITY            2
#define P_MODE               3
#define P_STROBE_RATE        4
#define P_TRAIL_PERSISTENCE  5
#define P_MOTION_PERSISTENCE 6
#define P_MOTION_GAIN        7

typedef struct {
    uint8_t *buf[5];
    uint8_t rainbow[256][3];

    int timestamp;
    int n_threads;

    float smooth_threshold;
    float phase;

    float eff_trigger;
    float eff_cycle_speed;
    float eff_opacity;
    float eff_strobe_rate;
    float eff_trail_persistence;
    float eff_motion_persistence;
    float eff_motion_gain;
    int eff_initialized;

    int sensitivity;
    int opacity;
    int persistence;
    int energy_persist;
    int motion_gain;
    int cutoff;
    int flash_q8;
    uint8_t strobe_Y;
    int strobe_U;
    int strobe_V;
    int adaptation;
} spectralmotion_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int spectralmotion_abs_i(int v)
{
    const int m = v >> 31;
    return (v ^ m) - m;
}

static inline uint8_t spectralmotion_blend_y(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t spectralmotion_blend_uv(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);

    int ac = (int)a - 128;
    int bc = (int)b - 128;
    int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int spectralmotion_smooth_i(float *restrict state,
                                          int target,
                                          float attack,
                                          float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float k = (diff >= 0.0f) ? attack : release;
    const float out = cur + diff * k;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline int spectralmotion_smooth_discrete_i(float *restrict state,
                                                   int target,
                                                   float attack,
                                                   float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float k = (diff >= 0.0f) ? attack : release;
    const float out = cur + diff * k;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static void spectralmotion_build_rainbow(uint8_t lut[256][3])
{
    for(int i = 0; i < 256; i++) {
        const float h = (float)i * (6.2831853f / 256.0f);

        const int Y = 140 + (int)(40.0f * sinf(h * 0.5f));
        const int U = 128 + (int)(90.0f * sinf(h));
        const int V = 128 + (int)(90.0f * cosf(h));

        lut[i][0] = CLAMP_Y(Y);
        lut[i][1] = CLAMP_UV(U);
        lut[i][2] = CLAMP_UV(V);
    }
}

vj_effect *spectralmotion_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SPECTRALMOTION_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);



    ve->defaults[P_TRIGGER]            = 150;
    ve->defaults[P_CYCLE_SPEED]        = 10;
    ve->defaults[P_OPACITY]            = 150;
    ve->defaults[P_MODE]               = 0;
    ve->defaults[P_STROBE_RATE]        = 8;
    ve->defaults[P_TRAIL_PERSISTENCE]  = 200;
    ve->defaults[P_MOTION_PERSISTENCE] = 180;
    ve->defaults[P_MOTION_GAIN]        = 256;

    ve->limits[0][P_TRIGGER]            = 0; ve->limits[1][P_TRIGGER]            = 255;
    ve->limits[0][P_CYCLE_SPEED]        = 0; ve->limits[1][P_CYCLE_SPEED]        = 255;
    ve->limits[0][P_OPACITY]            = 0; ve->limits[1][P_OPACITY]            = 255;
    ve->limits[0][P_MODE]               = 0; ve->limits[1][P_MODE]               = 2;
    ve->limits[0][P_STROBE_RATE]        = 1; ve->limits[1][P_STROBE_RATE]        = 120;
    ve->limits[0][P_TRAIL_PERSISTENCE]  = 0; ve->limits[1][P_TRAIL_PERSISTENCE]  = 255;
    ve->limits[0][P_MOTION_PERSISTENCE] = 0; ve->limits[1][P_MOTION_PERSISTENCE] = 255;
    ve->limits[0][P_MOTION_GAIN]        = 0; ve->limits[1][P_MOTION_GAIN]        = 1024;

    ve->description = "Spectral Motion Trail";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trigger",
        "Cycle Speed",
        "Opacity",
        "Mode",
        "Strobe Rate",
        "Trail Persistence",
        "Motion Persistence",
        "Motion Gain"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Full Trail",
        "Overlay",
        "Motion Debug"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 16, 180, 88, 100, 8, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 4, 180, 86, 100, 8, 440, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 72, 255, 90, 100, 6, 440, 24, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 48, 88, 100, 8, 440, 0, 1, 80, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LOG, 72, 248, 74, 96, 240, 1700, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 64, 240, 72, 94, 180, 1500, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 180, 1024, 94, 100, 8, 420, 0, 4, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *spectralmotion_malloc(int w, int h)
{
    spectralmotion_t *s = (spectralmotion_t*) vj_calloc(sizeof(spectralmotion_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 5u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;
    s->buf[3] = s->buf[2] + len;
    s->buf[4] = s->buf[3] + len;

    veejay_memset(s->buf[0], 0,   len);
    veejay_memset(s->buf[1], 0,   len);
    veejay_memset(s->buf[2], 128, len);
    veejay_memset(s->buf[3], 128, len);
    veejay_memset(s->buf[4], 0,   len);

    s->timestamp = 0;
    s->smooth_threshold = 0.0f;
    s->phase = 0.0f;

    s->eff_trigger = 150.0f;
    s->eff_cycle_speed = 10.0f;
    s->eff_opacity = 150.0f;
    s->eff_strobe_rate = 8.0f;
    s->eff_trail_persistence = 200.0f;
    s->eff_motion_persistence = 180.0f;
    s->eff_motion_gain = 256.0f;
    s->eff_initialized = 0;

    spectralmotion_build_rainbow(s->rainbow);

    return (void*) s;
}

void spectralmotion_free(void *ptr)
{
    spectralmotion_t *s = (spectralmotion_t*) ptr;

    free(s->buf[0]);
    free(s);
}

static void spectralmotion_seed(spectralmotion_t *s, VJFrame *frame)
{
    const int len = frame->len;

    veejay_memset(s->buf[0], 0,   len);
    veejay_memcpy(s->buf[1], frame->data[0], len);
    veejay_memset(s->buf[2], 128, len);
    veejay_memset(s->buf[3], 128, len);
    veejay_memset(s->buf[4], 64,  len);

    s->smooth_threshold = 0.0f;
    s->phase = 0.0f;
    s->timestamp = 1;
}

static void spectralmotion_output_overlay(uint8_t *restrict Y,
                                          uint8_t *restrict U,
                                          uint8_t *restrict V,
                                          const uint8_t *restrict vY,
                                          const uint8_t *restrict vU,
                                          const uint8_t *restrict vV,
                                          int opacity,
                                          int len,
                                          int n_threads)
{
    (void)n_threads;

    const int q8 = (opacity * 256 + 127) / 255;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        Y[i] = spectralmotion_blend_y(Y[i], vY[i], q8);
        U[i] = spectralmotion_blend_uv(U[i], vU[i], q8);
        V[i] = spectralmotion_blend_uv(V[i], vV[i], q8);
    }
}

static void spectralmotion_output_debug(uint8_t *restrict Y,
                                        uint8_t *restrict U,
                                        uint8_t *restrict V,
                                        const uint8_t *restrict exc,
                                        int len,
                                        int n_threads)
{
    (void)n_threads;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        Y[i] = CLAMP_Y((int)exc[i] * 2);
        U[i] = 128;
        V[i] = 128;
    }
}

static void spectralmotion_output_full(uint8_t *restrict Y,
                                       uint8_t *restrict U,
                                       uint8_t *restrict V,
                                       const uint8_t *restrict vY,
                                       const uint8_t *restrict vU,
                                       const uint8_t *restrict vV,
                                       int len,
                                       int n_threads)
{
    (void)n_threads;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        Y[i] = vY[i];
        U[i] = vU[i];
        V[i] = vV[i];
    }
}

void spectralmotion_apply(void *ptr, VJFrame *frame, int *args)
{
    spectralmotion_t *s = (spectralmotion_t*) ptr;

    const int len = frame->len;

    const int raw_trigger     = args[P_TRIGGER];
    const int raw_cycle       = args[P_CYCLE_SPEED];
    const int raw_opacity     = args[P_OPACITY];
    const int mode            = args[P_MODE];
    const int raw_strobe      = args[P_STROBE_RATE];
    const int raw_trail       = args[P_TRAIL_PERSISTENCE];
    const int raw_motion_pers = args[P_MOTION_PERSISTENCE];
    const int raw_motion_gain = args[P_MOTION_GAIN];

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict vY  = s->buf[0];
    uint8_t *restrict mY  = s->buf[1];
    uint8_t *restrict vU  = s->buf[2];
    uint8_t *restrict vV  = s->buf[3];
    uint8_t *restrict exc = s->buf[4];

    #pragma omp single
    {
        float param_attack = 0.46f;
        float param_release = 0.14f;

        if(!s->eff_initialized) {
            s->eff_trigger = (float)raw_trigger;
            s->eff_cycle_speed = (float)raw_cycle;
            s->eff_opacity = (float)raw_opacity;
            s->eff_strobe_rate = (float)raw_strobe;
            s->eff_trail_persistence = (float)raw_trail;
            s->eff_motion_persistence = (float)raw_motion_pers;
            s->eff_motion_gain = (float)raw_motion_gain;
            s->eff_initialized = 1;
        }

        int sensitivity_smooth = spectralmotion_smooth_i(&s->eff_trigger, raw_trigger, param_attack, param_release);
        int cycle_speed_smooth = spectralmotion_smooth_i(&s->eff_cycle_speed, raw_cycle, param_attack, param_release);
        int opacity_smooth = spectralmotion_smooth_i(&s->eff_opacity, raw_opacity, param_attack, param_release);
        int strobe_rate_smooth = spectralmotion_smooth_discrete_i(&s->eff_strobe_rate, raw_strobe, 0.34f, 0.10f);
        int persistence_smooth = spectralmotion_smooth_i(&s->eff_trail_persistence, raw_trail, param_attack, param_release);
        int energy_persist_smooth = spectralmotion_smooth_i(&s->eff_motion_persistence, raw_motion_pers, param_attack, param_release);
        int motion_gain_smooth = spectralmotion_smooth_i(&s->eff_motion_gain, raw_motion_gain, param_attack, param_release);

        s->sensitivity = clampi(sensitivity_smooth, 0, 255);
        s->opacity = clampi(opacity_smooth, 0, 255);
        s->persistence = clampi(persistence_smooth, 0, 255);
        s->energy_persist = clampi(energy_persist_smooth, 0, 255);
        s->motion_gain = clampi(motion_gain_smooth, 0, 1024);
        int strobe_rate = clampi(strobe_rate_smooth, 1, 120);
        s->adaptation = clampi(256 - s->sensitivity, 1, 256);

        if(s->timestamp == 0)
            spectralmotion_seed(s, frame);

        uint32_t histogram[256];
        for(int i = 0; i < 256; i++) histogram[i] = 0;

        for(int i = 0; i < len; i += 16) {
            const int diff = spectralmotion_abs_i((int)Y[i] - (int)mY[i]);
            histogram[diff]++;
        }

        uint32_t raw_threshold = otsu_method(histogram);

        s->smooth_threshold = (s->smooth_threshold * 0.85f) + ((float)raw_threshold * 0.15f);

        int cutoff_val = (int)s->smooth_threshold + (128 - s->sensitivity);
        s->cutoff = clampi(cutoff_val, 0, 255);

        int is_flash_frame = ((s->timestamp % strobe_rate) == 0);

        float cycle = powf(2.0f, ((float)cycle_speed_smooth - 128.0f) * (1.0f / 64.0f));
        int color_idx = ((int)s->phase) & 255;

        s->phase += cycle;
        if(s->phase >= 256.0f)
            s->phase = fmodf(s->phase, 256.0f);

        s->strobe_Y = s->rainbow[color_idx][0];
        s->strobe_U = (int)s->rainbow[color_idx][1];
        s->strobe_V = (int)s->rainbow[color_idx][2];

        s->flash_q8 = is_flash_frame ? 255 : 192;

        s->timestamp++;
    }
    
    const int persistence = s->persistence;
    const int energy_persist = s->energy_persist;
    const int motion_gain = s->motion_gain;
    const int cutoff = s->cutoff;
    const int flash_q8 = s->flash_q8;
    const uint8_t strobe_Y = s->strobe_Y;
    const int strobe_U = s->strobe_U;
    const int strobe_V = s->strobe_V;
    const int adaptation = s->adaptation;
    const int opacity = s->opacity;

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        const int input_y = Y[i];
        const int diff = spectralmotion_abs_i(input_y - (int)mY[i]);

        const int vy = ((int)vY[i] * persistence) >> 8;
        const int vu = ((((int)vU[i] - 128) * persistence) >> 8) + 128;
        const int vv = ((((int)vV[i] - 128) * persistence) >> 8) + 128;

        int over = diff - cutoff;
        over = (over > 0) ? over : 0;

        int excitation_raw = (over * motion_gain) >> 8;
        if(excitation_raw > 255)
            excitation_raw = 255;

        int excitation = (((int)exc[i] * energy_persist) + (excitation_raw * (256 - energy_persist))) >> 8;
        excitation = (excitation * flash_q8) >> 8;
        if(excitation > 255)
            excitation = 255;

        exc[i] = (uint8_t)excitation;

        const int inv_exc = 255 - excitation;

        const int newY = (vy * inv_exc + (int)strobe_Y * excitation) >> 8;

        const int vu_c = vu - 128;
        const int vv_c = vv - 128;

        const int newU = ((vu_c * inv_exc + (strobe_U - 128) * excitation) >> 8) + 128;
        const int newV = ((vv_c * inv_exc + (strobe_V - 128) * excitation) >> 8) + 128;

        vY[i] = CLAMP_Y(newY);
        vU[i] = CLAMP_UV(newU);
        vV[i] = CLAMP_UV(newV);

        mY[i] = (uint8_t)(((int)mY[i] * (256 - adaptation) + input_y * adaptation) >> 8);
    }

    switch(mode) {
        case 2:
            spectralmotion_output_debug(Y, U, V, exc, len, s->n_threads);
            break;
        case 1:
            spectralmotion_output_overlay(Y, U, V, vY, vU, vV, opacity, len, s->n_threads);
            break;
        case 0:
        default:
            spectralmotion_output_full(Y, U, V, vY, vU, vV, len, s->n_threads);
            break;
    }
}

void spectralmotion_apply3(void *ptr, VJFrame *frame, int *args)
{
    spectralmotion_apply(ptr, frame, args);
}