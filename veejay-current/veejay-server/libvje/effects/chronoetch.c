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
#include <veejaycore/vjmem.h>
#include <stdlib.h>
#include <math.h>

#define CHRONOETCHPARAMS 12
#define TAS_MAX_FRAMES 8

#define P_OPACITY        0
#define P_STEP           1
#define P_TIME_DEPTH     2
#define P_RIB_LENGTH     3
#define P_EDGE           4
#define P_MOTION_AGE     5
#define P_BONE_DENSITY   6
#define P_AGE_VIOLENCE   7
#define P_CHROMA_TEAR    8
#define P_TRAIL          9
#define P_STROKE_CHROMA 10
#define P_COLOR_BIAS    11

typedef struct {
    int w;
    int h;
    int len;
    int frame;
    int filled;
    int n_threads;

    uint8_t *region;

    uint8_t *ring_y[TAS_MAX_FRAMES];
    uint8_t *ring_u[TAS_MAX_FRAMES];
    uint8_t *ring_v[TAS_MAX_FRAMES];

    uint8_t *stable_y;
    uint8_t *last_stable_y;

    uint8_t *trail_y;
    uint8_t *trail_u;
    uint8_t *trail_v;
} chronoetch_t;

static inline int tas_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t tas_u8(int v)
{
    return (uint8_t) tas_clampi(v, 0, 255);
}

static inline int tas_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline int tas_maxi(int a, int b)
{
    return a > b ? a : b;
}

static inline unsigned int tas_hash_u32(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline unsigned int tas_hash3(int x, int y, int z)
{
    unsigned int h = (unsigned int) x * 374761393U;
    h ^= (unsigned int) y * 668265263U;
    h ^= (unsigned int) z * 2246822519U;
    return tas_hash_u32(h);
}

static inline int tas_ramp255(int v, int range)
{
    if (v <= 0)
        return 0;
    if (v >= range)
        return 255;
    return (v * 255) / range;
}

static inline int tas_slot_for_age(int write_slot, int age)
{
    int slot = write_slot - age;
    if (slot < 0)
        slot += TAS_MAX_FRAMES;
    return slot;
}

static inline void tas_blend_canvas_pixel(
    chronoetch_t *c,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    int idx,
    int sy,
    int su,
    int sv,
    int alpha
) {
    int yy;
    int uu;
    int vv;
    int inv;

    alpha = tas_clampi(alpha, 0, 255);

    if (alpha <= 0)
        return;

    sy = tas_clampi(sy, 0, 255);
    su = tas_clampi(su, 0, 255);
    sv = tas_clampi(sv, 0, 255);

    if (alpha >= 255) {
        yy = sy;
        uu = su;
        vv = sv;
    } else {
        inv = 255 - alpha;

        yy = (c->trail_y[idx] * inv + sy * alpha + 127) / 255;
        uu = (c->trail_u[idx] * inv + su * alpha + 127) / 255;
        vv = (c->trail_v[idx] * inv + sv * alpha + 127) / 255;
    }

    yy = tas_clampi(yy, 0, 255);
    uu = tas_clampi(uu, 0, 255);
    vv = tas_clampi(vv, 0, 255);

    c->trail_y[idx] = (uint8_t) yy;
    c->trail_u[idx] = (uint8_t) uu;
    c->trail_v[idx] = (uint8_t) vv;

    Y[idx] = (uint8_t) yy;
    U[idx] = (uint8_t) uu;
    V[idx] = (uint8_t) vv;
}

static inline void tas_draw_rib(
    chronoetch_t *c,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict srcY,
    uint8_t *restrict srcU,
    uint8_t *restrict srcV,
    int rows,
    int cx,
    int cy,
    int gx,
    int gy,
    int strength,
    int rib_length,
    int opacity_q8,
    int age,
    int max_age,
    int age_violence,
    int chroma_tear,
    int chroma_gain_q8,
    int color_bias,
    unsigned int shape_hash
) {
    int w = c->w;
    int h = rows;

    int tx = -gy;
    int ty = gx;
    int mag = tas_maxi(tas_absi(tx), tas_absi(ty));

    int dx_q8;
    int dy_q8;
    int nx_q8;
    int ny_q8;

    int half;
    int half_sq;

    int age_push;
    int jx;
    int jy;
    int bend_px;

    int ax_q8;
    int ay_q8;

    int age_fade;
    int color_push;

    int t;

    if (mag <= 0)
        return;

    dx_q8 = (tx * 256) / mag;
    dy_q8 = (ty * 256) / mag;

    nx_q8 = -dy_q8;
    ny_q8 = dx_q8;

    rib_length += (rib_length * age) / 5;
    rib_length = tas_clampi(rib_length, 2, 72);

    half = rib_length >> 1;
    if (half < 1)
        half = 1;

    half_sq = half * half;
    if (half_sq < 1)
        half_sq = 1;

    age_push = (age_violence * (age + 1) * (2 + (rib_length >> 2))) / 180;

    jx = ((((int)(shape_hash & 255)) - 128) * age_push) >> 7;
    jy = ((((int)((shape_hash >> 8) & 255)) - 128) * age_push) >> 7;

    if (age_violence > 0) {
        int nmag = tas_maxi(tas_absi(gx), tas_absi(gy));
        if (nmag > 0) {
            jx += (gx * age * age_violence) / (nmag * 16 + 1);
            jy += (gy * age * age_violence) / (nmag * 16 + 1);
        }
    }

    bend_px = ((((int)((shape_hash >> 16) & 255)) - 128) *
               (age_violence + chroma_tear + age * 12)) / 1300;

    ax_q8 = (cx + jx) << 8;
    ay_q8 = (cy + jy) << 8;

    if (max_age > 0)
        age_fade = 255 - ((age * 120) / (max_age + 1));
    else
        age_fade = 255;

    age_fade = tas_clampi(age_fade, 104, 255);

    color_push = (color_bias * (strength + 32 + age * 16)) >> 8;
    color_push = tas_clampi(color_push, -72, 72);

    for (t = -half; t <= half; t++) {
        int t_abs = tas_absi(t);
        int curve_px = ((half_sq - (t * t)) * bend_px) / half_sq;

        int base_x_q8 = ax_q8 + dx_q8 * t + nx_q8 * curve_px;
        int base_y_q8 = ay_q8 + dy_q8 * t + ny_q8 * curve_px;

        int src_x_q8 = (cx << 8) + dx_q8 * t;
        int src_y_q8 = (cy << 8) + dy_q8 * t;

        int taper;
        int q;
        int thickness;

        thickness = 1 + ((age + chroma_tear / 42) > 0);
        if (age_violence > 78 && age > 1)
            thickness++;
        thickness = tas_clampi(thickness, 1, 3);

        taper = 255 - ((t_abs * 145) / (half + 1));
        taper = tas_clampi(taper, 76, 255);

        if (age_violence > 0 && t_abs > (half >> 1)) {
            unsigned int eh = tas_hash3(cx + t, cy - t, (int)(shape_hash & 32767));
            int fray = (age_violence * (age + 1) * t_abs) / (half * 210 + 1);

            base_x_q8 += ((((int)(eh & 255)) - 128) * fray);
            base_y_q8 += ((((int)((eh >> 8) & 255)) - 128) * fray);
        }

        for (q = -thickness; q <= thickness; q++) {
            int q_abs = tas_absi(q);

            int ox = (base_x_q8 + nx_q8 * q + 128) >> 8;
            int oy = (base_y_q8 + ny_q8 * q + 128) >> 8;

            int sx = (src_x_q8 + nx_q8 * q + 128) >> 8;
            int sy = (src_y_q8 + ny_q8 * q + 128) >> 8;

            int oi;
            int si;

            int yy;
            int uu;
            int vv;

            int alpha;
            int edge_fall;
            int bone_lift;

            if (ox < 0 || ox >= w || oy < 0 || oy >= h)
                continue;

            sx = tas_clampi(sx, 0, w - 1);
            sy = tas_clampi(sy, 0, h - 1);

            oi = oy * w + ox;
            si = sy * w + sx;

            alpha = (strength * taper + 127) / 255;
            alpha = (alpha * age_fade + 127) / 255;

            edge_fall = (q_abs * 120) / (thickness + 1);
            alpha = (alpha * (255 - edge_fall) + 127) / 255;

            if (q_abs == 0)
                alpha = tas_clampi(alpha + 18, 0, 255);

            alpha = (alpha * opacity_q8 + 128) >> 8;

            if (alpha <= 0)
                continue;

            yy = srcY[si];
            uu = srcU[si];
            vv = srcV[si];

            bone_lift = (strength * (48 + age * 7)) / 255;
            yy = tas_clampi(yy + bone_lift, 0, 255);

            uu = 128 + (((uu - 128) * chroma_gain_q8) >> 8);
            vv = 128 + (((vv - 128) * chroma_gain_q8) >> 8);

            uu += color_push;
            vv -= color_push;

            tas_blend_canvas_pixel(c, Y, U, V, oi, yy, uu, vv, alpha);
        }
    }
}

vj_effect *chronoetch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if (!ve)
        return NULL;

    ve->num_params = CHRONOETCHPARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[P_OPACITY]       = 100;
    ve->defaults[P_STEP]          = 3;
    ve->defaults[P_TIME_DEPTH]    = 1;
    ve->defaults[P_RIB_LENGTH]    = 52;
    ve->defaults[P_EDGE]          = 88;
    ve->defaults[P_MOTION_AGE]    = 44;
    ve->defaults[P_BONE_DENSITY]  = 29;
    ve->defaults[P_AGE_VIOLENCE]  = 29;
    ve->defaults[P_CHROMA_TEAR]   = 28;
    ve->defaults[P_TRAIL]         = 98;
    ve->defaults[P_STROKE_CHROMA] = 50;
    ve->defaults[P_COLOR_BIAS]    = 50;

    ve->limits[0][P_OPACITY]       = 0;
    ve->limits[1][P_OPACITY]       = 100;

    ve->limits[0][P_STEP]          = 3;
    ve->limits[1][P_STEP]          = 14;

    ve->limits[0][P_TIME_DEPTH]    = 1;
    ve->limits[1][P_TIME_DEPTH]    = TAS_MAX_FRAMES;

    ve->limits[0][P_RIB_LENGTH]    = 2;
    ve->limits[1][P_RIB_LENGTH]    = 64;

    ve->limits[0][P_EDGE]          = 0;
    ve->limits[1][P_EDGE]          = 100;

    ve->limits[0][P_MOTION_AGE]    = 0;
    ve->limits[1][P_MOTION_AGE]    = 100;

    ve->limits[0][P_BONE_DENSITY]  = 0;
    ve->limits[1][P_BONE_DENSITY]  = 100;

    ve->limits[0][P_AGE_VIOLENCE]  = 0;
    ve->limits[1][P_AGE_VIOLENCE]  = 100;

    ve->limits[0][P_CHROMA_TEAR]   = 0;
    ve->limits[1][P_CHROMA_TEAR]   = 100;

    ve->limits[0][P_TRAIL]         = 0;
    ve->limits[1][P_TRAIL]         = 100;

    ve->limits[0][P_STROKE_CHROMA] = 0;
    ve->limits[1][P_STROKE_CHROMA] = 100;

    ve->limits[0][P_COLOR_BIAS]    = 0;
    ve->limits[1][P_COLOR_BIAS]    = 100;

    ve->description = "Chrono Etch";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Step Size",
        "Time Depth",
        "Rib Length",
        "Edge Sensitivity",
        "Motion Ageing",
        "Bone Density",
        "Age Violence",
        "Chroma Time Tear",
        "Trail Memory",
        "Stroke Chroma",
        "Color Bias"
    );

    return ve;
}

void *chronoetch_malloc(int w, int h)
{
    chronoetch_t *c;
    size_t len;
    size_t total;
    uint8_t *p;
    int s;
    int i;

    if (w <= 0 || h <= 0)
        return NULL;

    c = (chronoetch_t *) vj_calloc(sizeof(chronoetch_t));
    if (!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->filled = 0;
    c->n_threads = vje_advise_num_threads(w * h);

    len = (size_t) c->len;

    total = len * ((size_t)TAS_MAX_FRAMES * 3u + 5u);

    c->region = (uint8_t *) vj_malloc(total);
    if (!c->region) {
        free(c);
        return NULL;
    }

    p = c->region;

    for (s = 0; s < TAS_MAX_FRAMES; s++) {
        c->ring_y[s] = p; p += len;
        c->ring_u[s] = p; p += len;
        c->ring_v[s] = p; p += len;
    }

    c->stable_y = p; p += len;
    c->last_stable_y = p; p += len;

    c->trail_y = p; p += len;
    c->trail_u = p; p += len;
    c->trail_v = p;

    for (s = 0; s < TAS_MAX_FRAMES; s++) {
        for (i = 0; i < c->len; i++) {
            c->ring_y[s][i] = 0;
            c->ring_u[s][i] = 128;
            c->ring_v[s][i] = 128;
        }
    }

    for (i = 0; i < c->len; i++) {
        c->stable_y[i] = 0;
        c->last_stable_y[i] = 0;

        c->trail_y[i] = 0;
        c->trail_u[i] = 128;
        c->trail_v[i] = 128;
    }

    return (void *) c;
}

void chronoetch_free(void *ptr)
{
    chronoetch_t *c = (chronoetch_t *) ptr;

    if (!c)
        return;

    if (c->region)
        free(c->region);

    free(c);
}

void chronoetch_apply(void *ptr, VJFrame *frame, int *args)
{
    chronoetch_t *c = (chronoetch_t *) ptr;

    uint8_t *restrict Y;
    uint8_t *restrict U;
    uint8_t *restrict V;

    uint8_t *restrict curY;
    uint8_t *restrict curU;
    uint8_t *restrict curV;
    uint8_t *restrict edgeY;
    uint8_t *restrict lastEdgeY;

    int opacity;
    int opacity_q8;
    int step;
    int time_depth;
    int rib_length;
    int edge_sens;
    int motion_age;
    int bone_density;
    int age_violence;
    int chroma_tear;
    int trail;
    int trail_q8;
    int stroke_chroma;
    int stroke_chroma_q8;
    int color_bias;

    int chroma_gain_age_q8[TAS_MAX_FRAMES];

    int process_len;
    int rows;
    int w;
    int write_slot;
    int available;
    int max_age;

    int edge_threshold_base;
    int seed_floor;
    int motion_min;
    int motion_fracture_keep;
    int current_keep_base;

    int a;
    int y;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];


    w = c->w;

    process_len = c->len;
    if (frame->len > 0 && frame->len < process_len)
        process_len = frame->len;

    rows = process_len / w;

    opacity       = tas_clampi(args[P_OPACITY],       0, 100);
    step          = tas_clampi(args[P_STEP],          3, 14);
    time_depth    = tas_clampi(args[P_TIME_DEPTH],    1, TAS_MAX_FRAMES);
    rib_length    = tas_clampi(args[P_RIB_LENGTH],    2, 64);
    edge_sens     = tas_clampi(args[P_EDGE],          0, 100);
    motion_age    = tas_clampi(args[P_MOTION_AGE],    0, 100);
    bone_density  = tas_clampi(args[P_BONE_DENSITY],  0, 100);
    age_violence  = tas_clampi(args[P_AGE_VIOLENCE],  0, 100);
    chroma_tear   = tas_clampi(args[P_CHROMA_TEAR],   0, 100);
    trail         = tas_clampi(args[P_TRAIL],         0, 100);
    stroke_chroma = tas_clampi(args[P_STROKE_CHROMA], 0, 100);
    color_bias    = tas_clampi(args[P_COLOR_BIAS],    0, 100) - 50;

    opacity_q8 = (opacity * 255 + 50) / 100;

    trail_q8 = (trail * 255 + 50) / 100;

    if (stroke_chroma <= 50)
        stroke_chroma_q8 = (stroke_chroma * 256 + 25) / 50;
    else
        stroke_chroma_q8 = 256 + (((stroke_chroma - 50) * 384 + 25) / 50);

    for (a = 0; a < TAS_MAX_FRAMES; a++) {
        int base = 100 + chroma_tear * 2 + a * 8;
        int gain_q8 = (base * 256 + 50) / 100;

        gain_q8 = (gain_q8 * stroke_chroma_q8 + 128) >> 8;
        chroma_gain_age_q8[a] = tas_clampi(gain_q8, 0, 2048);
    }

    write_slot = c->frame % TAS_MAX_FRAMES;

    curY = c->ring_y[write_slot];
    curU = c->ring_u[write_slot];
    curV = c->ring_v[write_slot];

    edgeY = c->stable_y;
    lastEdgeY = c->last_stable_y;

    available = c->filled + 1;
    if (available > TAS_MAX_FRAMES)
        available = TAS_MAX_FRAMES;

    max_age = time_depth - 1;
    if (max_age > available - 1)
        max_age = available - 1;

    edge_threshold_base = 250 - edge_sens * 2;

    seed_floor = 32 - (edge_sens / 7);
    seed_floor = tas_clampi(seed_floor, 10, 32);

    motion_min = 18 + ((100 - motion_age) / 4);
    motion_fracture_keep = 3 + (bone_density / 7) + (motion_age / 10);
    current_keep_base = 18 + (bone_density / 2);

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for (int i = 0; i < process_len; i++) {
        int raw_y = Y[i];
        int old_stable = c->stable_y[i];
        int new_stable;
        int delta;
        int w_new;

        int yy;
        int uu;
        int vv;

        curY[i] = Y[i];
        curU[i] = U[i];
        curV[i] = V[i];

        c->last_stable_y[i] = (uint8_t) old_stable;

        if (c->filled <= 0) {
            new_stable = raw_y;
        } else {
            delta = tas_absi(raw_y - old_stable);
            w_new = (delta > 56) ? 6 : 3;
            new_stable = ((old_stable * (16 - w_new)) + raw_y * w_new + 8) >> 4;
        }

        c->stable_y[i] = tas_u8(new_stable);

        yy = (c->trail_y[i] * trail_q8) >> 8;
        uu = 128 + ((((int)c->trail_u[i] - 128) * trail_q8) >> 8);
        vv = 128 + ((((int)c->trail_v[i] - 128) * trail_q8) >> 8);

        yy = tas_clampi(yy, 0, 255);
        uu = tas_clampi(uu, 0, 255);
        vv = tas_clampi(vv, 0, 255);

        c->trail_y[i] = (uint8_t) yy;
        c->trail_u[i] = (uint8_t) uu;
        c->trail_v[i] = (uint8_t) vv;

        Y[i] = (uint8_t) yy;
        U[i] = (uint8_t) uu;
        V[i] = (uint8_t) vv;
    }

    for (y = 1; y < rows - 1; y += step) {
        int x;
        int row = y * w;
        int ycell = y / step;
        int row_phase = (ycell & 1) ? (step >> 1) : 0;
        int x_start = 1 + row_phase;
        int xcell;

        if (x_start >= w - 1)
            x_start = 1;

        xcell = x_start / step;

        for (x = x_start; x < w - 1; x += step, xcell++) {
            int idx = row + x;

            int l = edgeY[idx - 1];
            int r = edgeY[idx + 1];
            int u0 = edgeY[idx - w];
            int d = edgeY[idx + w];

            int gx = r - l;
            int gy = d - u0;

            int edge = tas_absi(gx) + tas_absi(gy);
            int motion = 0;

            unsigned int spatial_hash = tas_hash3(xcell, ycell, 7331);
            unsigned int shape_hash = tas_hash3(xcell, ycell, 9917);

            int hnoise;
            int edge_threshold;
            int edge_core;
            int motion_boost;
            int strength;

            int accepted = 0;
            int motion_fracture = 0;

            int age = 0;
            int y_age;
            int u_age;
            int v_age;

            int y_slot;
            int u_slot;
            int v_slot;

            int draw_x = x;
            int draw_y = y;

            int local_length;

            if (available > 1)
                motion = tas_absi((int)edgeY[idx] - (int)lastEdgeY[idx]);

            hnoise = (int)(spatial_hash & 63) - 31;

            edge_threshold = edge_threshold_base + hnoise;
            edge_threshold = tas_clampi(edge_threshold, 32, 275);

            if (edge >= seed_floor) {
                int keep;
                int density_hash;

                edge_core = edge - edge_threshold;
                motion_boost = (motion * motion_age) / 140;

                strength = tas_ramp255(edge_core + motion_boost, 180);

                keep = 10 + bone_density + (strength >> 2);

                if (edge > 180)
                    keep += (edge - 180) >> 2;

                if (strength > 210)
                    keep += 28;

                keep = tas_clampi(keep, 8, 230);

                density_hash = (int)((spatial_hash >> 8) & 255);

                if (density_hash <= keep)
                    accepted = 1;
            } else {
                if (available > 1 && motion_age > 0) {
                    strength = 0;

                    if (motion >= motion_min &&
                        (int)((spatial_hash >> 8) & 255) <= motion_fracture_keep) {
                        accepted = 1;
                        motion_fracture = 1;

                        gx = (int)((shape_hash >> 16) & 255) - 128;
                        gy = (int)((shape_hash >> 24) & 255) - 128;

                        if (gx == 0 && gy == 0)
                            gx = 1;

                        strength = 48 + ((motion * motion_age) / 170);
                        strength = tas_clampi(strength, 0, 140);
                    }
                } else {
                    strength = 0;
                }
            }

            if (!accepted || strength <= 0)
                continue;

            if (max_age > 0) {
                int motion_part = (motion * motion_age * max_age) / 7600;
                int jitter_part = 0;

                if (motion > 3 &&
                    (int)((spatial_hash >> 16) & 255) < (bone_density + 40)) {
                    jitter_part = (int)((spatial_hash >> 24) & 3);
                }

                age = motion_part + jitter_part;
                age = tas_clampi(age, 0, max_age);
            } else {
                age = 0;
            }

            y_age = age;

            u_age = age + (((int)((spatial_hash >> 10) & 3) * chroma_tear) / 100);
            v_age = age + (((int)((spatial_hash >> 12) & 3) * chroma_tear) / 85);

            u_age = tas_clampi(u_age, 0, max_age);
            v_age = tas_clampi(v_age, 0, max_age);

            y_slot = tas_slot_for_age(write_slot, y_age);
            u_slot = tas_slot_for_age(write_slot, u_age);
            v_slot = tas_slot_for_age(write_slot, v_age);

            if (step > 3) {
                int jlim = step / 3;
                int jx = ((((int)(shape_hash & 15)) - 8) * jlim) / 8;
                int jy = ((((int)((shape_hash >> 4) & 15)) - 8) * jlim) / 8;

                draw_x = tas_clampi(x + jx, 1, w - 2);
                draw_y = tas_clampi(y + jy, 1, rows - 2);
            }

            local_length = rib_length + (((int)((shape_hash >> 6) & 15) - 7) * rib_length) / 64;
            local_length = tas_clampi(local_length, 2, 64);

            tas_draw_rib(
                c,
                Y, U, V,
                c->ring_y[y_slot],
                c->ring_u[u_slot],
                c->ring_v[v_slot],
                rows,
                draw_x, draw_y,
                gx, gy,
                strength,
                local_length,
                opacity_q8,
                age,
                max_age,
                age_violence,
                chroma_tear,
                chroma_gain_age_q8[age],
                color_bias,
                shape_hash ^ ((unsigned int)age * 0x45d9f3bU)
            );

            if (age > 0 && !motion_fracture && strength > 150) {
                if ((int)((spatial_hash >> 2) & 255) <= current_keep_base) {
                    int cur_strength = (strength * 68) / 100;
                    int cur_length = (local_length * 2) / 3;

                    if (cur_length < 2)
                        cur_length = 2;

                    tas_draw_rib(
                        c,
                        Y, U, V,
                        c->ring_y[write_slot],
                        c->ring_u[write_slot],
                        c->ring_v[write_slot],
                        rows,
                        draw_x, draw_y,
                        gx, gy,
                        cur_strength,
                        cur_length,
                        opacity_q8,
                        0,
                        max_age,
                        age_violence / 2,
                        chroma_tear / 2,
                        chroma_gain_age_q8[0],
                        color_bias,
                        shape_hash ^ 0x91e10da5U
                    );
                }
            }
        }
    }

    if (c->filled < TAS_MAX_FRAMES)
        c->filled++;

    c->frame++;
}