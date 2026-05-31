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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "warppers.h"

#include <math.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LUT_SIZE 3600

#define WARPPERS_PARAMS 12

#define P_X_ANGLE        0
#define P_Y_ANGLE        1
#define P_ZOOM           2
#define P_X_CENTER       3
#define P_Y_CENTER       4
#define P_FALLOFF        5
#define P_STRENGTH       6
#define P_SPIN_SPEED     7
#define P_BEAT_ZOOM      8
#define P_BEAT_WARP      9
#define P_BEAT_PUSH     10
#define P_BEAT_SMOOTH   11

typedef struct {
    uint8_t *buf[3];
    double *lut;
    double *cos_lut;
    double *sin_lut;
    int n_threads;
    int w;
    int h;
    double spin_phase;
    double beat_env;
    double beat_kick;
    double beat_prev;
} warppers_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int warppers_wrap_lut(int v)
{
    v %= LUT_SIZE;

    if(v < 0)
        v += LUT_SIZE;

    return v;
}

static inline double warppers_wrap_phase(double v)
{
    if(!isfinite(v))
        return 0.0;

    while(v >= (double)LUT_SIZE)
        v -= (double)LUT_SIZE;

    while(v < 0.0)
        v += (double)LUT_SIZE;

    return v;
}

static inline int warppers_wrap_double(double v, int max)
{
    if(max <= 1 || !isfinite(v))
        return 0;

    if(v > 2147480000.0 || v < -2147480000.0) {
        v = fmod(v, (double)max);
        if(v < 0.0)
            v += (double)max;
        if(v >= (double)max)
            return max - 1;
        return (int)v;
    }

    int iv = (int)v;

    if(v < 0.0 && (double)iv != v)
        iv--;

    iv %= max;

    if(iv < 0)
        iv += max;

    return iv;
}

static inline int warppers_beat_shape(int beat_push)
{
    int lin;
    int sq;

    beat_push = clampi(beat_push, 0, 1000);

    lin = beat_push;
    sq = (beat_push * beat_push + 500) / 1000;

    return clampi((lin * 32 + sq * 68 + 50) / 100, 0, 1000);
}

static void warppers_init_trig_lut(warppers_t *f)
{
    for(int i = 0; i < LUT_SIZE; i++) {
        const double a = ((double)i * 0.1) * (M_PI / 180.0);

        f->cos_lut[i] = cos(a);
        f->sin_lut[i] = sin(a);
    }
}

vj_effect *warppers_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WARPPERS_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    const int max_x = w > 0 ? w - 1 : 0;
    const int max_y = h > 0 ? h - 1 : 0;

    ve->limits[0][P_X_ANGLE]    = 0;     ve->limits[1][P_X_ANGLE]    = 3600; ve->defaults[P_X_ANGLE]    = 15;
    ve->limits[0][P_Y_ANGLE]    = 0;     ve->limits[1][P_Y_ANGLE]    = 3600; ve->defaults[P_Y_ANGLE]    = 0;
    ve->limits[0][P_ZOOM]       = 1;     ve->limits[1][P_ZOOM]       = 1000; ve->defaults[P_ZOOM]       = 100;
    ve->limits[0][P_X_CENTER]   = 0;     ve->limits[1][P_X_CENTER]   = max_x; ve->defaults[P_X_CENTER]   = w > 0 ? w / 2 : 0;
    ve->limits[0][P_Y_CENTER]   = 0;     ve->limits[1][P_Y_CENTER]   = max_y; ve->defaults[P_Y_CENTER]   = h > 0 ? h / 2 : 0;
    ve->limits[0][P_FALLOFF]    = 0;     ve->limits[1][P_FALLOFF]    = 1000; ve->defaults[P_FALLOFF]    = 0;
    ve->limits[0][P_STRENGTH]   = 0;     ve->limits[1][P_STRENGTH]   = 1000; ve->defaults[P_STRENGTH]   = 0;
    ve->limits[0][P_SPIN_SPEED] = -1000; ve->limits[1][P_SPIN_SPEED] = 1000; ve->defaults[P_SPIN_SPEED] = 0;
    ve->limits[0][P_BEAT_ZOOM]  = 0;     ve->limits[1][P_BEAT_ZOOM]  = 1000; ve->defaults[P_BEAT_ZOOM]  = 420;
    ve->limits[0][P_BEAT_WARP]  = 0;     ve->limits[1][P_BEAT_WARP]  = 1000; ve->defaults[P_BEAT_WARP]  = 480;
    ve->limits[0][P_BEAT_PUSH]  = 0;     ve->limits[1][P_BEAT_PUSH]  = 1000; ve->defaults[P_BEAT_PUSH]  = 0;
    ve->limits[0][P_BEAT_SMOOTH]= 0;     ve->limits[1][P_BEAT_SMOOTH]= 1000; ve->defaults[P_BEAT_SMOOTH]= 520;

    ve->description = "Warp Perspective";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "X Angle",
        "Y Angle",
        "Zoom",
        "X Center",
        "Y Center",
        "Distance Falloff",
        "Perspective Strength",
        "Spin Speed",
        "Beat Zoom",
        "Beat Warp",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                                0,                  3600,               8,  30, 1200, 3000, 0,    48,    /* X Angle */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                                0,                  3600,               8,  30, 1200, 3000, 0,    48,    /* Y Angle */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                                                 40,                 420,                8,  30, 1200, 3000, 0,    46,    /* Zoom */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY,                                                0,                  max_x,              5,  18, 2200, 5200, 1200, 18,    /* X Center */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY,                                                0,                  max_y,              5,  18, 2200, 5200, 1200, 18,    /* Y Center */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                                                 0,                  560,                8,  30, 1200, 3000, 0,    42,    /* Distance Falloff */
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS,                                                 0,                  520,                8,  30, 1200, 3000, 0,    42,    /* Perspective Strength */
        VJ_BEAT_SIGNED_SPEED,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, -360,               360,                10, 42, 900,  2400, 0,    58,    /* Spin Speed */

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Zoom */
        VJ_BEAT_WARP,          VJ_BEAT_F_REJECT,                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Warp */

        VJ_BEAT_KICK,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                             0,                  780,                18, 68, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,        VJ_BEAT_F_PHRASE_ONLY,                                                260,                840,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );
    return ve;
}

void *warppers_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    warppers_t *s = (warppers_t*) vj_calloc(sizeof(warppers_t));
    if(!s)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;

    s->buf[0] = (uint8_t*) vj_malloc(len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->lut = (double*) vj_malloc(sizeof(double) * (size_t)LUT_SIZE * 2u);
    if(!s->lut) {
        free(s->buf[0]);
        free(s);
        return NULL;
    }

    s->sin_lut = s->lut;
    s->cos_lut = s->sin_lut + LUT_SIZE;

    s->w = w;
    s->h = h;
    s->spin_phase = 0.0;
    s->beat_env = 0.0;
    s->beat_kick = 0.0;
    s->beat_prev = 0.0;

    s->n_threads = vje_advise_num_threads((int)len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    warppers_init_trig_lut(s);

    return (void*) s;
}

void warppers_free(void *ptr)
{
    warppers_t *s = (warppers_t*) ptr;

    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    if(s->lut)
        free(s->lut);

    free(s);
}

void warppers_apply(void *ptr, VJFrame *frame, int *args)
{
    warppers_t *warp = (warppers_t*) ptr;

    if(!warp || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    if(w != warp->w || h != warp->h)
        return;

    const int x_angle_arg = warppers_wrap_lut(clampi(args[P_X_ANGLE], 0, 3600));
    const int y_angle_arg = warppers_wrap_lut(clampi(args[P_Y_ANGLE], 0, 3600));
    const int zoom_arg = clampi(args[P_ZOOM], 1, 1000);
    const int x_center = clampi(args[P_X_CENTER], 0, w - 1);
    const int y_center = clampi(args[P_Y_CENTER], 0, h - 1);
    int falloff_arg = clampi(args[P_FALLOFF], 0, 1000);
    int strength_arg = clampi(args[P_STRENGTH], 0, 1000);
    const int spin_arg = clampi(args[P_SPIN_SPEED], -1000, 1000);
    const int beat_zoom = clampi(args[P_BEAT_ZOOM], 0, 1000);
    const int beat_warp = clampi(args[P_BEAT_WARP], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int smooth_arg = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int shaped = warppers_beat_shape(beat_push);
    const double target = (double)shaped * 0.001;
    const double smooth = (double)smooth_arg * 0.001;
    const double attack = 0.16 + (1.0 - smooth) * 0.36;
    const double release = 0.025 + (1.0 - smooth) * 0.095;

    if(target > warp->beat_env)
        warp->beat_env += (target - warp->beat_env) * attack;
    else
        warp->beat_env += (target - warp->beat_env) * release;

    if(warp->beat_env < 0.0001)
        warp->beat_env = 0.0;
    else if(warp->beat_env > 1.0)
        warp->beat_env = 1.0;

    const double rise = target - warp->beat_prev;
    if(rise > warp->beat_kick)
        warp->beat_kick = rise;

    warp->beat_prev = target;
    warp->beat_kick *= 0.56 + smooth * 0.22;

    if(warp->beat_kick < 0.0001)
        warp->beat_kick = 0.0;
    else if(warp->beat_kick > 1.0)
        warp->beat_kick = 1.0;

    const double beat_env = warp->beat_env;
    const double beat_kick = warp->beat_kick;
    const double beat_drive = beat_env * beat_env;
    const double kick_drive = beat_kick * beat_kick;

    const double spin_step = (double)spin_arg * 0.018;
    const double beat_spin = ((double)beat_warp * 0.001) * (beat_drive * 8.0 + kick_drive * 22.0);

    warp->spin_phase = warppers_wrap_phase(warp->spin_phase + spin_step + beat_spin);

    const int spin_i = (int)(warp->spin_phase + 0.5);
    const int beat_angle = (int)(((double)beat_warp * 0.001) * (beat_env * 18.0 + beat_kick * 46.0) + 0.5);

    const int x_angle = warppers_wrap_lut(x_angle_arg + spin_i + beat_angle);
    const int y_angle = warppers_wrap_lut(y_angle_arg + (spin_i * 7) / 10 - (beat_angle * 3) / 5);

    const double zoom_drive = ((double)beat_zoom * 0.001) * (beat_env * 0.45 + beat_kick * 0.55);
    int zoom_eff = zoom_arg + (int)(((1200 - zoom_arg) > 0 ? (1200 - zoom_arg) : 0) * zoom_drive * 0.72 + 0.5);
    zoom_eff = clampi(zoom_eff, 1, 1400);

    const int warp_q = clampi((int)(((beat_drive * 0.65 + kick_drive * 0.35) * (double)beat_warp) + 0.5), 0, 1000);
    falloff_arg = clampi(falloff_arg + (warp_q * 130 + 500) / 1000, 0, 1000);
    strength_arg = clampi(strength_arg + (warp_q * 240 + 500) / 1000, 0, 1000);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = warp->buf[0];
    uint8_t *restrict srcU = warp->buf[1];
    uint8_t *restrict srcV = warp->buf[2];

    const size_t plane_size = (size_t)w * (size_t)h;

    veejay_memcpy(srcY, dstY, plane_size);
    veejay_memcpy(srcU, dstU, plane_size);
    veejay_memcpy(srcV, dstV, plane_size);

    const double zoom = (double)zoom_eff * 0.01;
    double falloff = (double)falloff_arg * 0.01;
    const double strength = (double)strength_arg * 0.01;

    falloff *= falloff;

    const double strength_factor = 1.0 - strength;
    const double cos_val = warp->cos_lut[x_angle];
    const double sin_val = warp->sin_lut[y_angle];

    int64_t half_w = w >> 1;
    int64_t half_h = h >> 1;
    int64_t max_dist_i = half_w * half_w + half_h * half_h;

    if(max_dist_i <= 0)
        max_dist_i = 1;

    const double inv_max_dist = 1.0 / (double)max_dist_i;

#pragma omp parallel for schedule(static) num_threads(warp->n_threads)
    for(int y_pos = 0; y_pos < h; y_pos++) {
        const int row = y_pos * w;
        const int dy = y_pos - y_center;
        const int64_t dy2 = (int64_t)dy * (int64_t)dy;
        const double dy_d = (double)dy;

        for(int x_pos = 0; x_pos < w; x_pos++) {
            const int idx = row + x_pos;

            const int dx = x_pos - x_center;
            const int64_t dist = (int64_t)dx * (int64_t)dx + dy2;
            const double dmd = (double)dist * inv_max_dist;
            const double dx_d = (double)dx;

            const double factor =
                (1.0 - falloff * dmd) *
                (strength_factor + strength * dmd);

            const double zf = zoom * factor;

            const double sx =
                (double)x_center +
                (zf * ((cos_val * dx_d) - (sin_val * dy_d)));

            const double sy =
                (double)y_center +
                (zf * ((sin_val * dx_d) + (cos_val * dy_d)));

            const int x = warppers_wrap_double(sx, w);
            const int y = warppers_wrap_double(sy, h);
            const int src = y * w + x;

            dstY[idx] = srcY[src];
            dstU[idx] = srcU[src];
            dstV[idx] = srcV[src];
        }
    }
}
