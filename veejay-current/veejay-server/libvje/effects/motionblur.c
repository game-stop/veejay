/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "motionblur.h"
#include <stdint.h>

#define MOTIONBLUR_PARAMS 5

#define P_SHUTTER   0
#define P_DECAY     1
#define P_DIRECTION 2
#define P_VELOCITY  3
#define P_RESET     4

#define MB_Q8_SHIFT 8
#define MB_Q8_ONE   (1 << MB_Q8_SHIFT)
#define MB_Q16_ONE  65536

typedef struct {
    int32_t *accY;
    int32_t *accU;
    int32_t *accV;
    uint8_t *prevY;
    int initialized;
    int dir_key;
    int dx_q16;
    int dy_q16;
    int n_threads;
} motionblur_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int motionblur_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline int32_t motionblur_clamp_q8(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t motionblur_y_from_q8(int32_t v)
{
    v = motionblur_clamp_q8(v, 0, 255 << MB_Q8_SHIFT);
    return (uint8_t)((v + 128) >> MB_Q8_SHIFT);
}

static inline int motionblur_round_q8_signed(int32_t v)
{
    if(v >= 0)
        return (v + 128) >> MB_Q8_SHIFT;

    return -((-v + 128) >> MB_Q8_SHIFT);
}

static inline uint8_t motionblur_uv_from_q8(int32_t v)
{
    int out;

    v = motionblur_clamp_q8(v, -128 << MB_Q8_SHIFT, 127 << MB_Q8_SHIFT);
    out = motionblur_round_q8_signed(v) + 128;

    return (uint8_t)clampi(out, 0, 255);
}

static void motionblur_update_direction(motionblur_t *m, int dir)
{
    const float angle = (float)dir * 0.03141592653589793f;

    m->dx_q16 = (int)(a_cos(angle) * 32768.0f);
    m->dy_q16 = (int)(a_sin(angle) * 32768.0f);
    m->dir_key = dir;
}

vj_effect *motionblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MOTIONBLUR_PARAMS;
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

    ve->defaults[P_SHUTTER] = 8;
    ve->defaults[P_DECAY] = 90;
    ve->defaults[P_DIRECTION] = 0;
    ve->defaults[P_VELOCITY] = 50;
    ve->defaults[P_RESET] = 30;

    ve->limits[0][P_SHUTTER] = 1;     ve->limits[1][P_SHUTTER] = 64;
    ve->limits[0][P_DECAY] = 50;      ve->limits[1][P_DECAY] = 100;
    ve->limits[0][P_DIRECTION] = -100; ve->limits[1][P_DIRECTION] = 100;
    ve->limits[0][P_VELOCITY] = 0;    ve->limits[1][P_VELOCITY] = 100;
    ve->limits[0][P_RESET] = 0;       ve->limits[1][P_RESET] = 255;

    ve->description = "Motion Blur";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Shutter",
        "Decay",
        "Direction",
        "Velocity",
        "Reset Threshold"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 40, 82, 100, 12, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LOG, 68, 99, 74, 96, 60, 1400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -100, 100, 92, 100, 6, 380, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 5, 100, 90, 100, 6, 400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 28, 180, 68, 94, 20, 700, 0, 1, 120, VJ_BEAT_COST_MODERATE, 60, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void motionblur_free(void *ptr)
{
    motionblur_t *m = (motionblur_t*) ptr;

    free(m->accY);
    free(m->prevY);
    free(m);
}

void *motionblur_malloc(int w, int h)
{
    motionblur_t *m = (motionblur_t*) vj_calloc(sizeof(motionblur_t));

    if(!m)
        return NULL;

    const int size = w * h;

    m->accY = (int32_t*) vj_malloc(sizeof(int32_t) * (size_t)size * 3u);
    m->prevY = (uint8_t*) vj_malloc((size_t)size);

    if(!m->accY || !m->prevY) {
        motionblur_free(m);
        return NULL;
    }

    m->accU = m->accY + size;
    m->accV = m->accU + size;
    m->initialized = 0;
    m->dir_key = 0x7fffffff;
    m->dx_q16 = 32768;
    m->dy_q16 = 0;
    m->n_threads = vje_advise_num_threads(size);

    return m;
}

static void motionblur_init_accumulators(motionblur_t *m,
                                         uint8_t *restrict Y,
                                         uint8_t *restrict U,
                                         uint8_t *restrict V,
                                         int size)
{
    for(int i = 0; i < size; i++) {
        m->accY[i] = (int32_t)Y[i] << MB_Q8_SHIFT;
        m->accU[i] = ((int32_t)U[i] - 128) << MB_Q8_SHIFT;
        m->accV[i] = ((int32_t)V[i] - 128) << MB_Q8_SHIFT;
        m->prevY[i] = Y[i];
    }

    m->initialized = 1;
}

static int motionblur_should_reset(motionblur_t *m,
                                   const uint8_t *restrict Y,
                                   int w,
                                   int h,
                                   int reset)
{
    const int step = 16;
    int64_t sum = 0;
    int count = 0;

    for(int y = 0; y < h; y += step) {
        const int row = y * w;

        for(int x = 0; x < w; x += step) {
            const int idx = row + x;

            sum += motionblur_absi((int)Y[idx] - (int)m->prevY[idx]);
            count++;
        }
    }

    return sum > (int64_t)reset * (int64_t)count;
}

void motionblur_apply(void *ptr, VJFrame *f, int *a)
{
    motionblur_t *m = (motionblur_t*) ptr;

    const int w = f->width;
    const int h = f->height;
    const int size = f->len;

    uint8_t *restrict Y = f->data[0];
    uint8_t *restrict U = f->data[1];
    uint8_t *restrict V = f->data[2];

    const int shutter = a[P_SHUTTER];
    const int decay = a[P_DECAY];
    const int direction = a[P_DIRECTION];
    const int velocity = a[P_VELOCITY];
    const int reset = a[P_RESET];

    if(direction != m->dir_key)
        motionblur_update_direction(m, direction);

    if(!m->initialized) {
        motionblur_init_accumulators(m, Y, U, V, size);
        return;
    }

    if(motionblur_should_reset(m, Y, w, h, reset)) {
        motionblur_init_accumulators(m, Y, U, V, size);
        return;
    }

    const int decay_q8 = (decay * MB_Q8_ONE + 50) / 100;
    const int alpha_base_q16 = MB_Q16_ONE / shutter;
    const int dx_q16 = m->dx_q16;
    const int dy_q16 = m->dy_q16;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const int row_bias_q16 = (dy_q16 * ((y << 1) - h)) / (h << 1);
        const int col_step_q16 = dx_q16 / w;
        int col_bias_q16 = -(dx_q16 >> 1);

        uint8_t *restrict Y_row = Y + row;
        uint8_t *restrict U_row = U + row;
        uint8_t *restrict V_row = V + row;

        int32_t *restrict accY_row = m->accY + row;
        int32_t *restrict accU_row = m->accU + row;
        int32_t *restrict accV_row = m->accV + row;
        uint8_t *restrict prevY_row = m->prevY + row;

        for(int x = 0; x < w; x++) {
            const int diff = motionblur_absi((int)Y_row[x] - (int)prevY_row[x]);
            const int vel_q8 = MB_Q8_ONE + ((velocity * diff + 50) / 100);
            int bias_q16 = MB_Q16_ONE + row_bias_q16 + col_bias_q16;
            int alpha_q16;
            int alpha_q8;
            int32_t yv;
            int32_t uv;
            int32_t vv;
            uint8_t yo;

            bias_q16 = clampi(bias_q16, MB_Q16_ONE >> 1, (MB_Q16_ONE * 3) >> 1);
            alpha_q16 = (alpha_base_q16 * vel_q8) >> MB_Q8_SHIFT;
            alpha_q16 = (alpha_q16 * bias_q16) >> 16;

            if(alpha_q16 > MB_Q16_ONE)
                alpha_q16 = MB_Q16_ONE;

            alpha_q8 = (alpha_q16 + 128) >> 8;

            yv = ((accY_row[x] * decay_q8 + 128) >> MB_Q8_SHIFT) + (int32_t)Y_row[x] * alpha_q8;
            uv = ((accU_row[x] * decay_q8 + 128) >> MB_Q8_SHIFT) + ((int32_t)U_row[x] - 128) * alpha_q8;
            vv = ((accV_row[x] * decay_q8 + 128) >> MB_Q8_SHIFT) + ((int32_t)V_row[x] - 128) * alpha_q8;

            yv = motionblur_clamp_q8(yv, 0, 255 << MB_Q8_SHIFT);
            uv = motionblur_clamp_q8(uv, -128 << MB_Q8_SHIFT, 127 << MB_Q8_SHIFT);
            vv = motionblur_clamp_q8(vv, -128 << MB_Q8_SHIFT, 127 << MB_Q8_SHIFT);

            accY_row[x] = yv;
            accU_row[x] = uv;
            accV_row[x] = vv;

            yo = motionblur_y_from_q8(yv);
            Y_row[x] = yo;
            U_row[x] = motionblur_uv_from_q8(uv);
            V_row[x] = motionblur_uv_from_q8(vv);
            prevY_row[x] = yo;

            col_bias_q16 += col_step_q16;
        }
    }
}
