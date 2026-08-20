/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include "rainbowshift.h"

#define RAINBOWSHIFT_PARAMS 2

#define P_AMPLITUDE 0
#define P_FREQUENCY 1

#define RAINBOW_LUT_BITS 10
#define RAINBOW_LUT_SIZE (1 << RAINBOW_LUT_BITS)
#define RAINBOW_LUT_MASK (RAINBOW_LUT_SIZE - 1)
#define RAINBOW_FP_SHIFT 16
#define RAINBOW_PI_X2 6.28318530717958647692

static int16_t rainbow_sin_lut[RAINBOW_LUT_SIZE];
static int rainbow_lut_ready = 0;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void rainbowshift_init_lut(void)
{
    if(rainbow_lut_ready)
        return;

    for(int i = 0; i < RAINBOW_LUT_SIZE; i++) {
        const double phase = ((double)i * RAINBOW_PI_X2) / (double)RAINBOW_LUT_SIZE;
        rainbow_sin_lut[i] = (int16_t)(a_sin(phase) * 16384.0);
    }

    rainbow_lut_ready = 1;
}

vj_effect *rainbowshift_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RAINBOWSHIFT_PARAMS;
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

    ve->limits[0][P_AMPLITUDE] = 0; ve->limits[1][P_AMPLITUDE] = 255; ve->defaults[P_AMPLITUDE] = 1;
    ve->limits[0][P_FREQUENCY] = 0; ve->limits[1][P_FREQUENCY] = 10;  ve->defaults[P_FREQUENCY] = 1;

    ve->description = "Rainbow Shift";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Amplitude",
        "Frequency"
    );
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 8, 220, 92, 100, 6, 480, 24, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 10, 86, 100, 12, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    rainbowshift_init_lut();

    return ve;
}

void rainbowshift_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int amplitude = args[P_AMPLITUDE];
    const int frequency = args[P_FREQUENCY];

    if(amplitude == 0 || frequency == 0)
        return;

    const int len = frame->len;
    const uint32_t phase_step = (uint32_t)((((uint64_t)frequency * RAINBOW_LUT_SIZE) << RAINBOW_FP_SHIFT) / (uint64_t)len);
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const uint32_t phase = (uint32_t)i * phase_step;
        const int wave = (amplitude * rainbow_sin_lut[(phase >> RAINBOW_FP_SHIFT) & RAINBOW_LUT_MASK]) >> 14;

        Y[i] = (uint8_t)((Y[i] + wave) & 255);
        Cb[i] = (uint8_t)((Cb[i] + wave) & 255);
        Cr[i] = (uint8_t)((Cr[i] - wave) & 255);
    }
}
