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

#include <config.h>
#include "common.h"
#include <math.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "trimirror.h"

#define TRIMIRROR_MAX_SEGMENTS 48
#define TRIMIRROR_TWO_PI 6.28318530718f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum {
    P_SEGMENTS = 0,
    P_ROTATION,
    P_SPIN_SPEED,
    P_ZOOM,
    P_CENTER_X,
    P_CENTER_Y,
    P_SPIN_DRIVE,
    P_ZOOM_DRIVE,
    TRIMIRROR_PARAMS
};

typedef struct {
    uint8_t *region;
    uint8_t *buf[3];
    float *vec_x;
    float *vec_y;
    int w;
    int h;
    int n_threads;
    float phase;

    float segment_state;
    float rotation_state;
    float spin_state;
    float zoom_state;
    float center_x_state;
    float center_y_state;
    float spin_drive_state;
    float zoom_drive_state;
    int state_ready;
} trimirror_t;

static inline int trimirror_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float trimirror_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t trimirror_y(int v)
{
    return (uint8_t)((v < pixel_Y_lo_) ? pixel_Y_lo_ : ((v > pixel_Y_hi_) ? pixel_Y_hi_ : v));
}

static inline uint8_t trimirror_uv(int v)
{
    return (uint8_t)((v < pixel_U_lo_) ? pixel_U_lo_ : ((v > pixel_U_hi_) ? pixel_U_hi_ : v));
}

static inline float trimirror_wrap_angle(float a)
{
    if(a < 0.0f) {
        a += TRIMIRROR_TWO_PI;
        if(a < 0.0f)
            a = fmodf(a, TRIMIRROR_TWO_PI) + TRIMIRROR_TWO_PI;
    }
    else if(a >= TRIMIRROR_TWO_PI) {
        a -= TRIMIRROR_TWO_PI;
        if(a >= TRIMIRROR_TWO_PI)
            a = fmodf(a, TRIMIRROR_TWO_PI);
    }
    return a;
}



static inline float trimirror_smooth(float current, float target, float coeff)
{
    return current + (target - current) * coeff;
}

vj_effect *trimirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRIMIRROR_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_SEGMENTS]    = 1;     ve->limits[1][P_SEGMENTS]    = TRIMIRROR_MAX_SEGMENTS; ve->defaults[P_SEGMENTS]    = 3;
    ve->limits[0][P_ROTATION]    = 0;     ve->limits[1][P_ROTATION]    = 360;                     ve->defaults[P_ROTATION]    = 0;
    ve->limits[0][P_SPIN_SPEED]  = -100;  ve->limits[1][P_SPIN_SPEED]  = 100;                     ve->defaults[P_SPIN_SPEED]  = 0;
    ve->limits[0][P_ZOOM]        = 250;   ve->limits[1][P_ZOOM]        = 2000;                    ve->defaults[P_ZOOM]        = 1000;
    ve->limits[0][P_CENTER_X]    = -1000; ve->limits[1][P_CENTER_X]    = 1000;                    ve->defaults[P_CENTER_X]    = 0;
    ve->limits[0][P_CENTER_Y]    = -1000; ve->limits[1][P_CENTER_Y]    = 1000;                    ve->defaults[P_CENTER_Y]    = 0;
    ve->limits[0][P_SPIN_DRIVE]  = 0;     ve->limits[1][P_SPIN_DRIVE]  = 1000;                    ve->defaults[P_SPIN_DRIVE]  = 520;
    ve->limits[0][P_ZOOM_DRIVE]  = 0;     ve->limits[1][P_ZOOM_DRIVE]  = 1000;                    ve->defaults[P_ZOOM_DRIVE]  = 420;

    ve->description = "Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Segments",
        "Rotation",
        "Spin Speed",
        "Zoom",
        "Center X",
        "Center Y",
        "Spin Drive",
        "Zoom Drive"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 360, 82, 100, 0, 260, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -80, 80, 96, 100, 0, 320, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 600, 1600, 92, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_SMOOTHSTEP, -500, 500, 60, 88, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 60, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, -500, 500, 64, 92, 80, 720, 0, 5, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 94, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void trimirror_build_vectors(trimirror_t *s, int w, int h)
{
    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const float dy = (float)y - cy;

        for(int x = 0; x < w; x++) {
            const int i = row + x;

            s->vec_x[i] = (float)x - cx;
            s->vec_y[i] = dy;
        }
    }

    s->w = w;
    s->h = h;
}

void *trimirror_malloc(int w, int h)
{
    trimirror_t *s = (trimirror_t*) vj_calloc(sizeof(trimirror_t));
    if(!s)
        return NULL;

    const int len = w * h;
    const size_t frame_bytes = (size_t)len * 3u;
    const size_t vec_bytes = sizeof(float) * (size_t)len * 2u;
    const size_t total = frame_bytes + vec_bytes + 32u;

    s->region = (uint8_t*) vj_malloc(total);
    if(!s->region) {
        free(s);
        return NULL;
    }

    uint8_t *p = (uint8_t*)(((uintptr_t)s->region + 15u) & ~(uintptr_t)15u);

    s->buf[0] = p;
    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    p += frame_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    s->vec_x = (float*)p;
    s->vec_y = s->vec_x + len;

    s->n_threads = vje_advise_num_threads(len);

    s->phase = 0.0f;
    s->segment_state = 3.0f;
    s->rotation_state = 0.0f;
    s->spin_state = 0.0f;
    s->zoom_state = 1000.0f;
    s->center_x_state = 0.0f;
    s->center_y_state = 0.0f;
    s->spin_drive_state = 0.0f;
    s->zoom_drive_state = 0.0f;
    s->state_ready = 0;

    trimirror_build_vectors(s, w, h);

    return (void*) s;
}

void trimirror_free(void *ptr)
{
    trimirror_t *s = (trimirror_t*) ptr;

    free(s->region);
    free(s);
}

void trimirror_apply(void *ptr, VJFrame *frame, int *args)
{
    trimirror_t *s = (trimirror_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    const int raw_segments = args[P_SEGMENTS];
    const int raw_rotation = args[P_ROTATION];
    const int raw_spin = args[P_SPIN_SPEED];
    const int raw_zoom = args[P_ZOOM];
    const int raw_center_x = args[P_CENTER_X];
    const int raw_center_y = args[P_CENTER_Y];
    const int raw_spin_drive = args[P_SPIN_DRIVE];
    const int raw_zoom_drive = args[P_ZOOM_DRIVE];

    if(!s->state_ready) {
        s->segment_state = (float)raw_segments;
        s->rotation_state = (float)raw_rotation;
        s->spin_state = (float)raw_spin;
        s->zoom_state = (float)raw_zoom;
        s->center_x_state = (float)raw_center_x;
        s->center_y_state = (float)raw_center_y;
        s->spin_drive_state = (float)raw_spin_drive;
        s->zoom_drive_state = (float)raw_zoom_drive;
        s->state_ready = 1;
    }

    const float geom_fast = 0.210f;
    const float geom_slow = 0.105f;

    s->segment_state = trimirror_smooth(s->segment_state, (float)raw_segments, geom_slow);
    s->rotation_state = trimirror_smooth(s->rotation_state, (float)raw_rotation, geom_fast);
    s->spin_state = trimirror_smooth(s->spin_state, (float)raw_spin, geom_fast);
    s->zoom_state = trimirror_smooth(s->zoom_state, (float)raw_zoom, geom_slow);
    s->center_x_state = trimirror_smooth(s->center_x_state, (float)raw_center_x, geom_slow);
    s->center_y_state = trimirror_smooth(s->center_y_state, (float)raw_center_y, geom_slow);
    s->spin_drive_state = trimirror_smooth(s->spin_drive_state, (float)raw_spin_drive, geom_fast);
    s->zoom_drive_state = trimirror_smooth(s->zoom_drive_state, (float)raw_zoom_drive, geom_fast);

    int segments = trimirror_clampi((int)(s->segment_state + 0.5f), 1, TRIMIRROR_MAX_SEGMENTS);

    const float spin_drive = s->spin_drive_state * 0.001f;
    const float zoom_drive = s->zoom_drive_state * 0.001f;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict U  = frame->data[1];
    uint8_t *restrict V  = frame->data[2];

    uint8_t *restrict srcY = s->buf[0];
    uint8_t *restrict srcU = s->buf[1];
    uint8_t *restrict srcV = s->buf[2];

    veejay_memcpy(srcY, Y, len);
    veejay_memcpy(srcU, U, len);
    veejay_memcpy(srcV, V, len);

    const float spin = s->spin_state * 0.00125f;
    const float direct_spin = spin_drive * 0.0125f;

    s->phase = trimirror_wrap_angle(s->phase + spin + direct_spin);

    const float user_rot = s->rotation_state * (TRIMIRROR_TWO_PI / 360.0f);
    const float base_angle = trimirror_wrap_angle(user_rot + s->phase + spin_drive * 0.16f * sinf(s->phase * 1.37f));

    const float zoom_base = s->zoom_state * 0.001f;
    const float zoom_breathe = 1.0f + zoom_drive * 0.055f * sinf(s->phase * 2.10f);
    const float zoom_pulse = 1.0f + zoom_drive * 0.34f;
    const float zoom = trimirror_clampf(zoom_base * zoom_breathe * zoom_pulse, 0.10f, 3.00f);

    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;
    const float sample_cx = cx + (s->center_x_state * 0.001f) * cx;
    const float sample_cy = cy + (s->center_y_state * 0.001f) * cy;

    float m00[TRIMIRROR_MAX_SEGMENTS];
    float m01[TRIMIRROR_MAX_SEGMENTS];
    float m10[TRIMIRROR_MAX_SEGMENTS];
    float m11[TRIMIRROR_MAX_SEGMENTS];

    const float angle_step = TRIMIRROR_TWO_PI / (float)segments;

    for(int i = 0; i < segments; i++) {
        const float a = base_angle + (float)i * angle_step;
        const float cs = cosf(a) * zoom;
        const float sn = sinf(a) * zoom;

        m00[i] = cs;
        m01[i] = -sn;
        m10[i] = sn;
        m11[i] = cs;
    }

    const float *restrict vx = s->vec_x;
    const float *restrict vy = s->vec_y;
    const int half = segments >> 1;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int p = 0; p < len; p++) {
        const float dx = vx[p];
        const float dy = vy[p];

        int acc_y = 0;
        int acc_u = 0;
        int acc_v = 0;

        for(int seg = 0; seg < segments; seg++) {
            int sx = (int)(sample_cx + dx * m00[seg] + dy * m01[seg] + 0.5f);
            int sy = (int)(sample_cy + dx * m10[seg] + dy * m11[seg] + 0.5f);

            sx = ((unsigned)sx < (unsigned)w) ? sx : (int)cx;
            sy = ((unsigned)sy < (unsigned)h) ? sy : (int)cy;

            const int idx = sy * w + sx;

            acc_y += srcY[idx];
            acc_u += (int)srcU[idx] - 128;
            acc_v += (int)srcV[idx] - 128;
        }

        Y[p] = trimirror_y((acc_y + half) / segments);
        U[p] = trimirror_uv(128 + ((acc_u >= 0)
            ? ((acc_u + half) / segments)
            : -((-acc_u + half) / segments)));
        V[p] = trimirror_uv(128 + ((acc_v >= 0)
            ? ((acc_v + half) / segments)
            : -((-acc_v + half) / segments)));
    }
}
