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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"

#include <math.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "warppers.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LUT_SIZE 3600

#define WARPPERS_PARAMS 10

#define P_X_ANGLE      0
#define P_Y_ANGLE      1
#define P_ZOOM         2
#define P_X_CENTER     3
#define P_Y_CENTER     4
#define P_FALLOFF      5
#define P_STRENGTH     6
#define P_SPIN_SPEED   7
#define P_ZOOM_DRIVE   8
#define P_WARP_DRIVE   9

typedef struct {
    uint8_t *region;
    uint8_t *buf[3];
    double *lut;
    double *cos_lut;
    double *sin_lut;
    int n_threads;
    int w;
    int h;
    double spin_phase;

    double x_angle_env;
    double y_angle_env;
    double zoom_env;
    double x_center_env;
    double y_center_env;
    double falloff_env;
    double strength_env;
    double spin_speed_env;
    double zoom_drive_env;
    double warp_drive_env;
    int env_ready;
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
    while(v >= (double)LUT_SIZE)
        v -= (double)LUT_SIZE;

    while(v < 0.0)
        v += (double)LUT_SIZE;

    return v;
}

static inline double warppers_smooth(double oldv, double target, double alpha)
{
    return oldv + (target - oldv) * alpha;
}

static inline double warppers_smooth_angle(double oldv, double target, double alpha)
{
    double d;

    target = warppers_wrap_phase(target);
    oldv = warppers_wrap_phase(oldv);

    d = target - oldv;

    if(d > (double)LUT_SIZE * 0.5)
        d -= (double)LUT_SIZE;
    else if(d < -(double)LUT_SIZE * 0.5)
        d += (double)LUT_SIZE;

    return warppers_wrap_phase(oldv + d * alpha);
}

static inline int warppers_wrap_double(double v, int max)
{
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
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    const int max_x = w - 1;
    const int max_y = h - 1;

    ve->limits[0][P_X_ANGLE]    = 0;     ve->limits[1][P_X_ANGLE]    = 3600; ve->defaults[P_X_ANGLE]    = 180;
    ve->limits[0][P_Y_ANGLE]    = 0;     ve->limits[1][P_Y_ANGLE]    = 3600; ve->defaults[P_Y_ANGLE]    = 70;
    ve->limits[0][P_ZOOM]       = 1;     ve->limits[1][P_ZOOM]       = 1000; ve->defaults[P_ZOOM]       = 105;
    ve->limits[0][P_X_CENTER]   = 0;     ve->limits[1][P_X_CENTER]   = max_x; ve->defaults[P_X_CENTER]   = w / 2;
    ve->limits[0][P_Y_CENTER]   = 0;     ve->limits[1][P_Y_CENTER]   = max_y; ve->defaults[P_Y_CENTER]   = h / 2;
    ve->limits[0][P_FALLOFF]    = 0;     ve->limits[1][P_FALLOFF]    = 1000; ve->defaults[P_FALLOFF]    = 180;
    ve->limits[0][P_STRENGTH]   = 0;     ve->limits[1][P_STRENGTH]   = 1000; ve->defaults[P_STRENGTH]   = 260;
    ve->limits[0][P_SPIN_SPEED] = -1000; ve->limits[1][P_SPIN_SPEED] = 1000; ve->defaults[P_SPIN_SPEED] = 8;
    ve->limits[0][P_ZOOM_DRIVE] = 0;     ve->limits[1][P_ZOOM_DRIVE] = 1000; ve->defaults[P_ZOOM_DRIVE] = 420;
    ve->limits[0][P_WARP_DRIVE] = 0;     ve->limits[1][P_WARP_DRIVE] = 1000; ve->defaults[P_WARP_DRIVE] = 480;

    ve->description = "Warp Perspective";
    ve->sub_format = 1;
    ve->extra_frame = 0;
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
        "Zoom Drive",
        "Warp Drive"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SIGNED_SPEED, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 98, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 96, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *warppers_malloc(int w, int h)
{
    warppers_t *s = (warppers_t*) vj_calloc(sizeof(warppers_t));
    if(!s)
        return NULL;

    const size_t len = (size_t)w * (size_t)h;
    const size_t frame_bytes = len * 3u;
    const size_t lut_bytes = sizeof(double) * (size_t)LUT_SIZE * 2u;
    const size_t total = frame_bytes + lut_bytes + 16u;

    s->region = (uint8_t*) vj_malloc(total);
    if(!s->region) {
        free(s);
        return NULL;
    }

    s->buf[0] = s->region;
    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    uint8_t *p = s->buf[2] + len;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    s->lut = (double*)p;
    s->sin_lut = s->lut;
    s->cos_lut = s->sin_lut + LUT_SIZE;

    s->w = w;
    s->h = h;
    s->spin_phase = 0.0;
    s->x_angle_env = 0.0;
    s->y_angle_env = 0.0;
    s->zoom_env = 0.0;
    s->x_center_env = 0.0;
    s->y_center_env = 0.0;
    s->falloff_env = 0.0;
    s->strength_env = 0.0;
    s->spin_speed_env = 0.0;
    s->zoom_drive_env = 0.0;
    s->warp_drive_env = 0.0;
    s->env_ready = 0;

    s->n_threads = vje_advise_num_threads((int)len);

    warppers_init_trig_lut(s);

    return (void*) s;
}

void warppers_free(void *ptr)
{
    warppers_t *s = (warppers_t*) ptr;

    free(s->region);
    free(s);
}

void warppers_apply(void *ptr, VJFrame *frame, int *args)
{
    warppers_t *warp = (warppers_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;

    const int x_angle_arg = args[P_X_ANGLE];
    const int y_angle_arg = args[P_Y_ANGLE];
    const int zoom_arg = args[P_ZOOM];
    const int x_center_arg = args[P_X_CENTER];
    const int y_center_arg = args[P_Y_CENTER];
    const int falloff_arg_in = args[P_FALLOFF];
    const int strength_arg_in = args[P_STRENGTH];
    const int spin_arg_in = args[P_SPIN_SPEED];
    const int zoom_drive_in = args[P_ZOOM_DRIVE];
    const int warp_drive_in = args[P_WARP_DRIVE];

    const double angle_alpha = 0.115;
    const double center_alpha = 0.092;
    const double scalar_alpha = 0.152;
    const double drive_alpha = 0.218;

    if(!warp->env_ready) {
        warp->x_angle_env = (double)x_angle_arg;
        warp->y_angle_env = (double)y_angle_arg;
        warp->zoom_env = (double)zoom_arg;
        warp->x_center_env = (double)x_center_arg;
        warp->y_center_env = (double)y_center_arg;
        warp->falloff_env = (double)falloff_arg_in;
        warp->strength_env = (double)strength_arg_in;
        warp->spin_speed_env = (double)spin_arg_in;
        warp->zoom_drive_env = (double)zoom_drive_in;
        warp->warp_drive_env = (double)warp_drive_in;
        warp->env_ready = 1;
    }
    else {
        warp->x_angle_env = warppers_smooth_angle(warp->x_angle_env, (double)x_angle_arg, angle_alpha);
        warp->y_angle_env = warppers_smooth_angle(warp->y_angle_env, (double)y_angle_arg, angle_alpha);
        warp->zoom_env = warppers_smooth(warp->zoom_env, (double)zoom_arg, scalar_alpha);
        warp->x_center_env = warppers_smooth(warp->x_center_env, (double)x_center_arg, center_alpha);
        warp->y_center_env = warppers_smooth(warp->y_center_env, (double)y_center_arg, center_alpha);
        warp->falloff_env = warppers_smooth(warp->falloff_env, (double)falloff_arg_in, scalar_alpha);
        warp->strength_env = warppers_smooth(warp->strength_env, (double)strength_arg_in, scalar_alpha);
        warp->spin_speed_env = warppers_smooth(warp->spin_speed_env, (double)spin_arg_in, scalar_alpha);
        warp->zoom_drive_env = warppers_smooth(warp->zoom_drive_env, (double)zoom_drive_in, drive_alpha);
        warp->warp_drive_env = warppers_smooth(warp->warp_drive_env, (double)warp_drive_in, drive_alpha);
    }

    const int x_angle_base = warppers_wrap_lut((int)(warp->x_angle_env + 0.5));
    const int y_angle_base = warppers_wrap_lut((int)(warp->y_angle_env + 0.5));
    const int zoom_base = clampi((int)(warp->zoom_env + 0.5), 1, 1000);
    const int x_center = clampi((int)(warp->x_center_env + 0.5), 0, w - 1);
    const int y_center = clampi((int)(warp->y_center_env + 0.5), 0, h - 1);
    int falloff_arg = clampi((int)(warp->falloff_env + 0.5), 0, 1000);
    int strength_arg = clampi((int)(warp->strength_env + 0.5), 0, 1000);
    const int spin_arg = clampi((int)(warp->spin_speed_env + (warp->spin_speed_env >= 0.0 ? 0.5 : -0.5)), -1000, 1000);
    const int zoom_drive = clampi((int)(warp->zoom_drive_env + 0.5), 0, 1000);
    const int warp_drive = clampi((int)(warp->warp_drive_env + 0.5), 0, 1000);

    const double zoom_t = (double)zoom_drive * 0.001;
    const double warp_t = (double)warp_drive * 0.001;

    const double spin_step = (double)spin_arg * 0.018;
    const double drive_spin = warp_t * 15.5;

    warp->spin_phase = warppers_wrap_phase(warp->spin_phase + spin_step + drive_spin);

    const int spin_i = (int)(warp->spin_phase + 0.5);
    const int drive_angle = (int)(warp_t * 112.0 + 0.5);

    const int x_angle = warppers_wrap_lut(x_angle_base + spin_i + drive_angle);
    const int y_angle = warppers_wrap_lut(y_angle_base + (spin_i * 7) / 10 - (drive_angle * 3) / 5);

    int zoom_eff = zoom_base + (int)(((1450 - zoom_base) > 0 ? (1450 - zoom_base) : 0) * zoom_t * 0.82 + 0.5);
    zoom_eff = clampi(zoom_eff, 1, 1500);

    const int warp_q = clampi(warp_drive, 0, 1000);
    falloff_arg = clampi(falloff_arg + (warp_q * 210 + 500) / 1000, 0, 1000);
    strength_arg = clampi(strength_arg + (warp_q * 320 + 500) / 1000, 0, 1000);

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
    const int64_t max_dist_i = half_w * half_w + half_h * half_h;
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
