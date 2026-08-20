/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "squares.h"

#define SQUARES_PARAMS 8

#define P_RADIUS      0
#define P_MODE        1
#define P_ORIENTATION 2
#define P_PARITY      3
#define P_PHASE_X     4
#define P_PHASE_Y     5
#define P_SIZE_DRIVE  6
#define P_MIX_DRIVE   7

typedef struct {
    uint8_t *buf[3];
    int n_threads;
    int w;
    int h;

    float radius_state;
    float phase_x_state;
    float phase_y_state;
    float size_drive_state;
    float mix_drive_state;
    int initialized;
} squares_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t squares_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t squares_blend_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}



static inline int squares_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}



vj_effect *squares_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SQUARES_PARAMS;

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

    const int max_dim = (w > h) ? w : h;
    int max_radius = max_dim >> 1;
    int def_radius = max_dim >> 6;

    if(max_radius < 1)
        max_radius = 1;
    if(def_radius < 1)
        def_radius = 1;

    ve->limits[0][P_RADIUS] = 1;
    ve->limits[1][P_RADIUS] = max_radius;
    ve->defaults[P_RADIUS] = def_radius;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 2;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_ORIENTATION] = 0;
    ve->limits[1][P_ORIENTATION] = 7;
    ve->defaults[P_ORIENTATION] = 0;

    ve->limits[0][P_PARITY] = 0;
    ve->limits[1][P_PARITY] = 2;
    ve->defaults[P_PARITY] = 0;

    ve->limits[0][P_PHASE_X] = 0;
    ve->limits[1][P_PHASE_X] = 1000;
    ve->defaults[P_PHASE_X] = 0;

    ve->limits[0][P_PHASE_Y] = 0;
    ve->limits[1][P_PHASE_Y] = 1000;
    ve->defaults[P_PHASE_Y] = 0;

    ve->limits[0][P_SIZE_DRIVE] = 0;
    ve->limits[1][P_SIZE_DRIVE] = 1000;
    ve->defaults[P_SIZE_DRIVE] = 0;

    ve->limits[0][P_MIX_DRIVE] = 0;
    ve->limits[1][P_MIX_DRIVE] = 1000;
    ve->defaults[P_MIX_DRIVE] = 0;

    ve->description = "Squares";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Mode",
        "Orientation",
        "Parity",
        "Phase X",
        "Phase Y",
        "Size Drive",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Average",
        "Min",
        "Max"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_ORIENTATION],
        P_ORIENTATION,
        "Centered",
        "North",
        "North East",
        "East",
        "South East",
        "South West",
        "West",
        "North West"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_PARITY],
        P_PARITY,
        "Even",
        "Odd",
        "No parity"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, max_radius, 84, 100, 12, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 1000, 78, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 1000, 68, 92, 40, 520, 0, 2, 0, VJ_BEAT_COST_CHEAP, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 92, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *squares_malloc(int w, int h)
{
    squares_t *s = (squares_t*) vj_calloc(sizeof(squares_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->n_threads = vje_advise_num_threads(len);

    s->w = w;
    s->h = h;
    s->radius_state = 0.0f;
    s->phase_x_state = 0.0f;
    s->phase_y_state = 0.0f;
    s->size_drive_state = 0.0f;
    s->mix_drive_state = 0.0f;
    s->initialized = 0;

    return (void*)s;
}

void squares_free(void *ptr)
{
    squares_t *s = (squares_t*)ptr;

    free(s->buf[0]);
    free(s);
}

static void squares_apply_blocks(VJFrame *frame,
                                 const uint8_t *restrict srcY,
                                 const uint8_t *restrict srcU,
                                 const uint8_t *restrict srcV,
                                 int radius,
                                 int mode,
                                 int orientation,
                                 int parity,
                                 int phase_x,
                                 int phase_y,
                                 int source_mix_q8,
                                 int n_threads)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;

    int x_inf = 0;
    int y_inf = 0;
    int x_sup = w;
    int y_sup = h;

    grid_getbounds_from_orientation(
        radius,
        (vj_effect_orientation)orientation,
        (vj_effect_parity)parity,
        &x_inf,
        &y_inf,
        &x_sup,
        &y_sup,
        w,
        h
    );

    if(radius > 1) {
        phase_x %= radius;
        phase_y %= radius;
        if(phase_x < 0) phase_x += radius;
        if(phase_y < 0) phase_y += radius;
    } else {
        phase_x = 0;
        phase_y = 0;
    }

    x_inf += phase_x;
    y_inf += phase_y;
    x_sup += phase_x;
    y_sup += phase_y;

    x_inf = clampi(x_inf, -radius, w + radius);
    y_inf = clampi(y_inf, -radius, h + radius);
    x_sup = clampi(x_sup, -radius, w + radius);
    y_sup = clampi(y_sup, -radius, h + radius);

    if(x_sup <= x_inf || y_sup <= y_inf)
        return;

    const int nx = ((x_sup - x_inf) + radius - 1) / radius;
    const int ny = ((y_sup - y_inf) + radius - 1) / radius;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int by = 0; by < ny; by++) {
        const int y = y_inf + by * radius;

        for(int bx = 0; bx < nx; bx++) {
            const int x = x_inf + bx * radius;

            const int x0 = (x < 0) ? 0 : x;
            const int y0 = (y < 0) ? 0 : y;
            int x1 = x + radius;
            int y1 = y + radius;

            if(x1 > w) x1 = w;
            if(y1 > h) y1 = h;

            if(x1 <= x0 || y1 <= y0)
                continue;

            int sum_y = 0;
            int sum_u = 0;
            int sum_v = 0;
            int count = 0;
            uint8_t min_y = 255;
            uint8_t max_y = 0;

            for(int yy = y0; yy < y1; yy++) {
                const int row = yy * w;

                for(int xx = x0; xx < x1; xx++) {
                    const int idx = row + xx;
                    const uint8_t yv = srcY[idx];

                    sum_y += yv;
                    sum_u += (int)srcU[idx] - 128;
                    sum_v += (int)srcV[idx] - 128;
                    count++;

                    if(yv < min_y)
                        min_y = yv;
                    if(yv > max_y)
                        max_y = yv;
                }
            }

            if(count <= 0)
                continue;

            uint8_t out_y;

            if(mode == 1)
                out_y = min_y;
            else if(mode == 2)
                out_y = max_y;
            else
                out_y = (uint8_t)((sum_y + (count >> 1)) / count);

            const uint8_t out_u = squares_u8(128 + ((sum_u >= 0)
                ? ((sum_u + (count >> 1)) / count)
                : -((-sum_u + (count >> 1)) / count)));

            const uint8_t out_v = squares_u8(128 + ((sum_v >= 0)
                ? ((sum_v + (count >> 1)) / count)
                : -((-sum_v + (count >> 1)) / count)));

            for(int yy = y0; yy < y1; yy++) {
                const int row = yy * w;

                for(int xx = x0; xx < x1; xx++) {
                    const int idx = row + xx;

                    if(source_mix_q8 > 0) {
                        Y[idx] = squares_blend_u8(out_y, srcY[idx], source_mix_q8);
                        U[idx] = squares_blend_u8(out_u, srcU[idx], source_mix_q8);
                        V[idx] = squares_blend_u8(out_v, srcV[idx], source_mix_q8);
                    } else {
                        Y[idx] = out_y;
                        U[idx] = out_u;
                        V[idx] = out_v;
                    }
                }
            }
        }
    }
}

void squares_apply(void *ptr, VJFrame *frame, int *args)
{
    squares_t *s = (squares_t*)ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    const int max_dim = (w > h) ? w : h;
    int max_radius = max_dim >> 1;

    if(max_radius < 1)
        max_radius = 1;

    const int base_radius_arg = args[P_RADIUS];
    const int mode = args[P_MODE];
    const int orientation = args[P_ORIENTATION];
    const int parity = args[P_PARITY];
    const int phase_x_arg = args[P_PHASE_X];
    const int phase_y_arg = args[P_PHASE_Y];
    const int size_drive_arg = args[P_SIZE_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];

    veejay_memcpy(s->buf[0], frame->data[0], len);
    veejay_memcpy(s->buf[1], frame->data[1], len);
    veejay_memcpy(s->buf[2], frame->data[2], len);

    const uint8_t *srcY = s->buf[0];
    const uint8_t *srcU = s->buf[1];
    const uint8_t *srcV = s->buf[2];
    const int n_threads = s->n_threads;

    int phase_x = 0;
    int phase_y = 0;
    int source_mix_q8 = 0;

    const int base_radius = clampi(base_radius_arg, 1, max_radius);
    const int size_headroom = max_radius - base_radius;
    const int size_drive = clampi(size_drive_arg, 0, 1000);

    int target_radius = base_radius;

    if(size_headroom > 0)
        target_radius += (size_drive * size_headroom + 500) / 1000;

    target_radius = clampi(target_radius, 1, max_radius);

    int phase_x_target = (phase_x_arg * target_radius + 500) / 1000;
    int phase_y_target = (phase_y_arg * target_radius + 500) / 1000;
    int mix_target = clampi(mix_drive_arg, 0, 1000);

    if(!s->initialized) {
        s->radius_state = (float)target_radius;
        s->phase_x_state = (float)phase_x_target;
        s->phase_y_state = (float)phase_y_target;
        s->size_drive_state = (float)size_drive;
        s->mix_drive_state = (float)mix_target;
        s->initialized = 1;
    }

    const float fast = 0.172f;
    const float slow = 0.076f;

    int radius = squares_smooth_i(&s->radius_state, target_radius, fast, slow);
    phase_x = squares_smooth_i(&s->phase_x_state, phase_x_target, fast * 1.30f, slow);
    phase_y = squares_smooth_i(&s->phase_y_state, phase_y_target, fast * 1.30f, slow);
    mix_target = squares_smooth_i(&s->mix_drive_state, mix_target, fast * 1.18f, slow);

    radius = clampi(radius, 1, max_radius);
    source_mix_q8 = clampi((mix_target * 256 + 500) / 1000, 0, 256);

    squares_apply_blocks(
        frame,
        srcY,
        srcU,
        srcV,
        radius,
        mode,
        orientation,
        parity,
        phase_x,
        phase_y,
        source_mix_q8,
        n_threads
    );
}
