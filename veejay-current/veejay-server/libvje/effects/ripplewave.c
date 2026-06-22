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
#include <stdint.h>
#include "ripplewave.h"

#define RIPPLEWAVE_PARAMS 7

#define P_FREQ_X 0
#define P_FREQ_Y 1
#define P_AMP    2
#define P_SPEED  3
#define P_MIX    4
#define P_CHROMA 5
#define P_PHASE  6

#define RIPPLE_PI2 6.28318530718f

typedef struct {
    uint8_t *block;
    uint8_t *buf[3];
    float *lut_x;
    float *lut_y;
    int width;
    int height;
    float phase;
    float sm_freq_x;
    float sm_freq_y;
    float sm_amp;
    float sm_speed;
    float sm_mix;
    float sm_chroma;
    float sm_phase;
    int have_smooth;
    int n_threads;
} ripplewave_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ripplewave_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int ripplewave_q8_from_1000(int v)
{
    return (clampi(v, 0, 1000) * 256 + 500) / 1000;
}

static inline float ripplewave_smooth_to(float cur, float target, float k)
{
    return cur + (target - cur) * k;
}

vj_effect *ripplewave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RIPPLEWAVE_PARAMS;
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

    ve->limits[0][P_FREQ_X] = 0;   ve->limits[1][P_FREQ_X] = 100;  ve->defaults[P_FREQ_X] = 10;
    ve->limits[0][P_FREQ_Y] = 1;   ve->limits[1][P_FREQ_Y] = 100;  ve->defaults[P_FREQ_Y] = 15;
    ve->limits[0][P_AMP] = 0;      ve->limits[1][P_AMP] = 45;      ve->defaults[P_AMP] = 30;
    ve->limits[0][P_SPEED] = 0;    ve->limits[1][P_SPEED] = 100;   ve->defaults[P_SPEED] = 10;
    ve->limits[0][P_MIX] = 0;      ve->limits[1][P_MIX] = 1000;    ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;   ve->limits[1][P_CHROMA] = 1000; ve->defaults[P_CHROMA] = 1000;
    ve->limits[0][P_PHASE] = 0;    ve->limits[1][P_PHASE] = 1000;  ve->defaults[P_PHASE] = 0;

    ve->description = "Wave Patterns (H/V)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Frequency X",
        "Frequency Y",
        "Amplitude",
        "Speed",
        "Opacity",
        "Chroma Amount",
        "Phase"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WARP,             VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,             VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SQUARED | VJ_BEAT_F_NO_ZERO_CROSS,  8,                  45,                 18, 72,  450, 1800, 0,    96,
        VJ_BEAT_SPEED,            VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                      4,                  96,                 12, 54,  650, 2600, 0,    74,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_GEOMETRY_PHASE,   VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *ripplewave_malloc(int w, int h)
{
    ripplewave_t *data = (ripplewave_t*)vj_calloc(sizeof(ripplewave_t));

    if(!data)
        return NULL;

    const int len = w * h;
    const size_t data_bytes = (size_t)len * 3u;
    const size_t lut_x_bytes = sizeof(float) * (size_t)w;
    const size_t lut_y_bytes = sizeof(float) * (size_t)h;
    const size_t total = data_bytes + lut_x_bytes + lut_y_bytes + 64u;

    data->block = (uint8_t*)vj_malloc(total);

    if(!data->block) {
        free(data);
        return NULL;
    }

    uint8_t *p = data->block;

    data->buf[0] = p;
    p += (size_t)len;
    data->buf[1] = p;
    p += (size_t)len;
    data->buf[2] = p;
    p += (size_t)len;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    data->lut_x = (float*)p;
    p += lut_x_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    data->lut_y = (float*)p;

    data->width = w;
    data->height = h;
    data->phase = 0.0f;
    data->have_smooth = 0;
    data->n_threads = vje_advise_num_threads(len);

    return (void*)data;
}

void ripplewave_free(void *ptr)
{
    ripplewave_t *data = (ripplewave_t*)ptr;

    free(data->block);
    free(data);
}

void ripplewave_apply(void *ptr, VJFrame *frame, int *args)
{
    ripplewave_t *data = (ripplewave_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int freq_x_arg = args[P_FREQ_X];
    const int freq_y_arg = args[P_FREQ_Y];
    const int amp_arg = args[P_AMP];
    const int speed_arg = args[P_SPEED];
    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA];
    const int phase_arg = args[P_PHASE];

    const float param_k = 0.34f;

    if(!data->have_smooth) {
        data->sm_freq_x = (float)freq_x_arg;
        data->sm_freq_y = (float)freq_y_arg;
        data->sm_amp = (float)amp_arg;
        data->sm_speed = (float)speed_arg;
        data->sm_mix = (float)mix_arg;
        data->sm_chroma = (float)chroma_arg;
        data->sm_phase = (float)phase_arg;
        data->have_smooth = 1;
    }
    else {
        data->sm_freq_x = ripplewave_smooth_to(data->sm_freq_x, (float)freq_x_arg, param_k);
        data->sm_freq_y = ripplewave_smooth_to(data->sm_freq_y, (float)freq_y_arg, param_k);
        data->sm_amp = ripplewave_smooth_to(data->sm_amp, (float)amp_arg, param_k);
        data->sm_speed = ripplewave_smooth_to(data->sm_speed, (float)speed_arg, param_k);
        data->sm_mix = ripplewave_smooth_to(data->sm_mix, (float)mix_arg, param_k);
        data->sm_chroma = ripplewave_smooth_to(data->sm_chroma, (float)chroma_arg, param_k);
        data->sm_phase = ripplewave_smooth_to(data->sm_phase, (float)phase_arg, param_k);
    }

    const float frequency_x = data->sm_freq_x * 0.01f;
    const float frequency_y = data->sm_freq_y * 0.01f;
    const float amplitude = clampf(data->sm_amp, 0.0f, 45.0f);
    const float speed = clampf(data->sm_speed, 0.0f, 100.0f);

    if(speed > 0.0001f) {
        data->phase += speed * 0.01f;

        if(data->phase > 628.3185f)
            data->phase -= 628.3185f;
    }

    const float phase_offset = data->sm_phase * 0.001f * RIPPLE_PI2;
    const float render_phase = data->phase + phase_offset;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict dstY = data->buf[0];
    uint8_t *restrict dstU = data->buf[1];
    uint8_t *restrict dstV = data->buf[2];

    float *restrict lut_x = data->lut_x;
    float *restrict lut_y = data->lut_y;

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++)
        lut_y[y] = a_sin(frequency_y * (float)y + render_phase);

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int x = 0; x < width; x++)
        lut_x[x] = a_cos(frequency_x * (float)x + render_phase * 0.93f);

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int offset_y = (int)(amplitude * lut_y[y]);

        for(int x = 0; x < width; x++) {
            const int offset_x = (int)(amplitude * lut_x[x]);

            int sx = x + offset_x;
            int sy = y + offset_y;

            sx = clampi(sx, 0, width - 1);
            sy = clampi(sy, 0, height - 1);

            const int src = sy * width + sx;
            const int dst = row + x;

            dstY[dst] = Y[src];
            dstU[dst] = U[src];
            dstV[dst] = V[src];
        }
    }

    const int mix_q8 = ripplewave_q8_from_1000((int)(data->sm_mix + 0.5f));
    const int chroma_amount = clampi((int)(data->sm_chroma + 0.5f), 0, 1000);
    const int chroma_q8 = (mix_q8 * chroma_amount + 500) / 1000;

    if(mix_q8 >= 256 && chroma_q8 >= 256) {
        veejay_memcpy(Y, dstY, len);
        veejay_memcpy(U, dstU, len);
        veejay_memcpy(V, dstV, len);
        return;
    }

    if(mix_q8 <= 0 && chroma_q8 <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int i = 0; i < len; i++) {
        Y[i] = ripplewave_mix_u8(Y[i], dstY[i], mix_q8);
        U[i] = ripplewave_mix_u8(U[i], dstU[i], chroma_q8);
        V[i] = ripplewave_mix_u8(V[i], dstV[i], chroma_q8);
    }
}
