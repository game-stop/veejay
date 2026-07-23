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
#include "luminouswave.h"
#include <stdint.h>

#define LW_PARAMS 7

#define P_FREQ_X 0
#define P_FREQ_Y 1
#define P_AMPLITUDE 2
#define P_SPEED 3
#define P_ANGLE_X 4
#define P_ANGLE_Y 5
#define P_BREAK 6

#define LW_LUT_SIZE 1024
#define LW_LUT_MASK 1023
#define LW_Q14 16384
#define LW_PHASE_K_Q16 106807

typedef struct {
    int16_t sin_lut[LW_LUT_SIZE] __attribute__((aligned(64)));
    int16_t cos_lut[LW_LUT_SIZE] __attribute__((aligned(64)));
    int width;
    int height;
    int speed;
    int n_threads;
} luminouswave_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int luminouswave_angle_idx(int deg)
{
    return ((deg * LW_LUT_SIZE) / 360) & LW_LUT_MASK;
}

static inline int luminouswave_phase_step_q16(int freq, int trig_q14)
{
    return (int)(((int64_t)freq * (int64_t)trig_q14 * (int64_t)LW_PHASE_K_Q16) / LW_Q14);
}

static inline int luminouswave_phase_base_q16(int freq, int y, int trig_q14, int speed)
{
    return (int)((((int64_t)freq * (int64_t)y * (int64_t)trig_q14) + ((int64_t)speed * LW_Q14)) * (int64_t)LW_PHASE_K_Q16 / LW_Q14);
}

vj_effect *luminouswave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LW_PARAMS;
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

    ve->limits[0][P_FREQ_X] = 0;     ve->limits[1][P_FREQ_X] = 100;   ve->defaults[P_FREQ_X] = 4;
    ve->limits[0][P_FREQ_Y] = 1;     ve->limits[1][P_FREQ_Y] = 100;   ve->defaults[P_FREQ_Y] = 5;
    ve->limits[0][P_AMPLITUDE] = 0;  ve->limits[1][P_AMPLITUDE] = 45; ve->defaults[P_AMPLITUDE] = 30;
    ve->limits[0][P_SPEED] = 0;      ve->limits[1][P_SPEED] = 100;    ve->defaults[P_SPEED] = 10;
    ve->limits[0][P_ANGLE_X] = 0;    ve->limits[1][P_ANGLE_X] = 360;  ve->defaults[P_ANGLE_X] = 33;
    ve->limits[0][P_ANGLE_Y] = 0;    ve->limits[1][P_ANGLE_Y] = 360;  ve->defaults[P_ANGLE_Y] = 10;
    ve->limits[0][P_BREAK] = 1;      ve->limits[1][P_BREAK] = 500;    ve->defaults[P_BREAK] = 100;

    ve->description = "Luminous Wave";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Frequency X",
        "Frequency Y",
        "Amplitude",
        "Speed",
        "Angle X",
        "Angle Y",
        "Break"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 2, 72, 76, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 2, 72, 72, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 45, 92, 100, 0, 380, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 3, 100, 90, 100, 8, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 360, 80, 100, 0, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_LINEAR, 0, 360, 64, 92, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 10, 240, 74, 98, 20, 720, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *luminouswave_malloc(int w, int h)
{
    luminouswave_t *data = (luminouswave_t*) vj_malloc(sizeof(luminouswave_t));

    if(!data)
        return NULL;

    data->width = w;
    data->height = h;
    data->speed = 0;
    data->n_threads = vje_advise_num_threads(w * h);

    for(int i = 0; i < LW_LUT_SIZE; i++) {
        const float a = ((float)i / (float)LW_LUT_SIZE) * 6.28318530718f;
        data->sin_lut[i] = (int16_t)(a_sin(a) * (float)LW_Q14);
        data->cos_lut[i] = (int16_t)(a_cos(a) * (float)LW_Q14);
    }

    return data;
}

void luminouswave_free(void *ptr)
{
    free(ptr);
}

void luminouswave_apply(void *ptr, VJFrame *frame, int *args)
{
    luminouswave_t *data = (luminouswave_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int freq_x = args[P_FREQ_X];
    const int freq_y = args[P_FREQ_Y];
    const int amplitude = args[P_AMPLITUDE];
    const int min_speed = args[P_SPEED];
    const int angle_x = args[P_ANGLE_X];
    const int angle_y = args[P_ANGLE_Y];
    const int break_speed = args[P_BREAK];

    const int ax = luminouswave_angle_idx(angle_x);
    const int ay = luminouswave_angle_idx(angle_y);

    const int sx = data->sin_lut[ax];
    const int cx = data->cos_lut[ax];
    const int sy = data->sin_lut[ay];
    const int cy = data->cos_lut[ay];

    const int max_x = freq_x * width;
    const int max_y = freq_y * height;
    const int max_speed = max_x > max_y ? max_x : max_y;
    int step = max_speed / (break_speed * 10);

    if(step < 1)
        step = 1;

    data->speed += step;

    if(data->speed > max_speed)
        data->speed = min_speed;

    const int speed = min_speed + data->speed;
    const int inc_y_q16 = luminouswave_phase_step_q16(freq_y, sx);
    const int inc_x_q16 = luminouswave_phase_step_q16(freq_x, cx);
    const int offset = frame->jobnum * height;

    uint8_t *restrict Y = frame->data[0];

#pragma omp parallel for num_threads(data->n_threads) schedule(static)
    for(int y = 0; y < height; y++) {
        uint8_t *restrict row = Y + y * width;
        const int actual_y = y + offset;
        const int base_y_q16 = luminouswave_phase_base_q16(freq_y, actual_y, cy, speed);
        const int base_x_q16 = luminouswave_phase_base_q16(freq_x, actual_y, sy, speed);

        for(int x = 0; x < width; x++) {
            const int idx_y = (base_y_q16 + x * inc_y_q16) >> 16;
            const int idx_x = (base_x_q16 + x * inc_x_q16) >> 16;
            const int wave = (int)data->sin_lut[idx_y & LW_LUT_MASK] + (int)data->sin_lut[idx_x & LW_LUT_MASK];
            const int off = (amplitude * wave + (wave >= 0 ? (LW_Q14 >> 1) : -(LW_Q14 >> 1))) >> 14;
            int luma = (int)row[x] + off;

            if(luma < pixel_Y_lo_)
                luma = pixel_Y_lo_;
            else if(luma > pixel_Y_hi_)
                luma = pixel_Y_hi_;

            row[x] = (uint8_t)luma;
        }
    }
}
