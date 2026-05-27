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
#include <veejaycore/vjmem.h>
#include "trimirror.h"
#include <math.h>

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
    P_BEAT_SPIN,
    P_BEAT_ZOOM,
    P_BEAT_PUSH,
    P_BEAT_SMOOTH,
    TRIMIRROR_PARAMS
};

typedef struct {
    uint8_t *buf[3];
    float *vec_x;
    float *vec_y;
    int w;
    int h;
    int n_threads;
    float phase;
    float beat_env;
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

static inline int trimirror_beat_shape(int v)
{
    v = trimirror_clampi(v, 0, 1000);
    const int sq = (v * v + 500) / 1000;
    return trimirror_clampi((v * 40 + sq * 60 + 50) / 100, 0, 1000);
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
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_SEGMENTS]    = 1;     ve->limits[1][P_SEGMENTS]    = TRIMIRROR_MAX_SEGMENTS; ve->defaults[P_SEGMENTS]    = 3;
    ve->limits[0][P_ROTATION]    = 0;     ve->limits[1][P_ROTATION]    = 360;                     ve->defaults[P_ROTATION]    = 0;
    ve->limits[0][P_SPIN_SPEED]  = -100;  ve->limits[1][P_SPIN_SPEED]  = 100;                     ve->defaults[P_SPIN_SPEED]  = 0;
    ve->limits[0][P_ZOOM]        = 250;   ve->limits[1][P_ZOOM]        = 2000;                    ve->defaults[P_ZOOM]        = 1000;
    ve->limits[0][P_CENTER_X]    = -1000; ve->limits[1][P_CENTER_X]    = 1000;                    ve->defaults[P_CENTER_X]    = 0;
    ve->limits[0][P_CENTER_Y]    = -1000; ve->limits[1][P_CENTER_Y]    = 1000;                    ve->defaults[P_CENTER_Y]    = 0;
    ve->limits[0][P_BEAT_SPIN]   = 0;     ve->limits[1][P_BEAT_SPIN]   = 1000;                    ve->defaults[P_BEAT_SPIN]   = 520;
    ve->limits[0][P_BEAT_ZOOM]   = 0;     ve->limits[1][P_BEAT_ZOOM]   = 1000;                    ve->defaults[P_BEAT_ZOOM]   = 420;
    ve->limits[0][P_BEAT_PUSH]   = 0;     ve->limits[1][P_BEAT_PUSH]   = 1000;                    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->limits[0][P_BEAT_SMOOTH] = 0;     ve->limits[1][P_BEAT_SMOOTH] = 1000;                    ve->defaults[P_BEAT_SMOOTH] = 460;

    ve->description = "Kaleidoscope";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Segments",
        "Rotation",
        "Spin Speed",
        "Zoom",
        "Center X",
        "Center Y",
        "Beat Spin",
        "Beat Zoom",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE,          VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                              2,                  16,                 6,  22, 2200, 5200, 1800, 24,    /* Segments */
        VJ_BEAT_GEOMETRY_PHASE,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                                   0,                  360,                8,  32, 1200, 3200, 0,    48,    /* Rotation */
        VJ_BEAT_SIGNED_SPEED,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,     -70,                70,                 10, 40, 900,  2400, 0,    62,    /* Spin Speed */
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS,                                                    720,                1420,               8,  34, 1000, 2800, 0,    46,    /* Zoom */
        VJ_BEAT_SIGNED_CURVE,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,    -320,               320,                5,  18, 2200, 5200, 1200, 18,    /* Center X */
        VJ_BEAT_SIGNED_CURVE,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,    -320,               320,                5,  18, 2200, 5200, 1200, 18,    /* Center Y */
        VJ_BEAT_SPEED,              VJ_BEAT_F_REJECT,                                                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Spin */
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_REJECT,                                                         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Zoom */
        VJ_BEAT_INTENSITY,          VJ_BEAT_F_CONTINUOUS,                                                     0,                  860,                18, 72, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,             VJ_BEAT_F_PHRASE_ONLY,                                                    220,                820,                5,  18, 2200, 5200, 1200, 16     /* Beat Smooth */
    );

    (void) w;
    (void) h;

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

    if(len <= 0) {
        free(s);
        return NULL;
    }

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->vec_x = (float*) vj_malloc(sizeof(float) * (size_t)len * 2u);
    if(!s->vec_x) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->vec_y = s->vec_x + len;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    s->phase = 0.0f;
    s->beat_env = 0.0f;

    trimirror_build_vectors(s, w, h);

    return (void*) s;
}

void trimirror_free(void *ptr)
{
    trimirror_t *s = (trimirror_t*) ptr;

    if(!s)
        return;

    if(s->vec_x)
        free(s->vec_x);

    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

void trimirror_apply(void *ptr, VJFrame *frame, int *args)
{
    trimirror_t *s = (trimirror_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    if(s->w != w || s->h != h)
        trimirror_build_vectors(s, w, h);

    const int segments = trimirror_clampi(args[P_SEGMENTS], 1, TRIMIRROR_MAX_SEGMENTS);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict U  = frame->data[1];
    uint8_t *restrict V  = frame->data[2];

    uint8_t *restrict srcY = s->buf[0];
    uint8_t *restrict srcU = s->buf[1];
    uint8_t *restrict srcV = s->buf[2];

    if(segments <= 1 && args[P_ROTATION] == 0 && args[P_SPIN_SPEED] == 0 && args[P_BEAT_PUSH] == 0 && args[P_ZOOM] == 1000 && args[P_CENTER_X] == 0 && args[P_CENTER_Y] == 0)
        return;

    veejay_memcpy(srcY, Y, len);
    veejay_memcpy(srcU, U, len);
    veejay_memcpy(srcV, V, len);

    const int beat_push = trimirror_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int shaped = trimirror_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)trimirror_clampi(args[P_BEAT_SMOOTH], 0, 1000) * 0.001f;
    const float attack = 0.22f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.035f + (1.0f - smooth_t) * 0.090f;

    if(target > s->beat_env)
        s->beat_env += (target - s->beat_env) * attack;
    else
        s->beat_env += (target - s->beat_env) * release;

    s->beat_env = trimirror_clampf(s->beat_env, 0.0f, 1.0f);
    if(s->beat_env < 0.0001f)
        s->beat_env = 0.0f;

    const float beat_drive = s->beat_env * s->beat_env;
    const float beat_spin = (float)trimirror_clampi(args[P_BEAT_SPIN], 0, 1000) * 0.001f;
    const float beat_zoom = (float)trimirror_clampi(args[P_BEAT_ZOOM], 0, 1000) * 0.001f;

    const float spin = (float)trimirror_clampi(args[P_SPIN_SPEED], -100, 100) * 0.00125f;
    s->phase = trimirror_wrap_angle(s->phase + spin + beat_drive * beat_spin * 0.045f);

    const float user_rot = ((float)trimirror_clampi(args[P_ROTATION], 0, 360) * (TRIMIRROR_TWO_PI / 360.0f));
    const float base_angle = trimirror_wrap_angle(user_rot + s->phase + beat_drive * beat_spin * 0.12f);

    const float zoom_base = (float)trimirror_clampi(args[P_ZOOM], 250, 2000) * 0.001f;
    const float zoom = zoom_base * (1.0f + beat_drive * beat_zoom * 0.28f);

    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;
    const float sample_cx = cx + ((float)trimirror_clampi(args[P_CENTER_X], -1000, 1000) * 0.001f) * cx;
    const float sample_cy = cy + ((float)trimirror_clampi(args[P_CENTER_Y], -1000, 1000) * 0.001f) * cy;

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
