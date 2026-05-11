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

#define BONECOMETAUTOPSY_PARAMS 12
#define BCA_MAX_FRAMES 8

#define P_OPACITY        0
#define P_STEP           1
#define P_TIME_DEPTH     2
#define P_HEAD_SIZE      3
#define P_TAIL_LENGTH    4
#define P_EDGE           5
#define P_MOTION_LAUNCH  6
#define P_COMET_DENSITY  7
#define P_WHITE_FORGE    8
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

    uint8_t *ring_y[BCA_MAX_FRAMES];
    uint8_t *ring_u[BCA_MAX_FRAMES];
    uint8_t *ring_v[BCA_MAX_FRAMES];

    uint8_t *stable_y;
    uint8_t *last_stable_y;

    uint8_t *trail_y;
    uint8_t *trail_u;
    uint8_t *trail_v;
} meteorvector_t;

static inline int bca_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t bca_u8(int v)
{
    return (uint8_t) bca_clampi(v, 0, 255);
}

static inline int bca_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline int bca_maxi(int a, int b)
{
    return a > b ? a : b;
}

static inline unsigned int bca_hash_u32(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline unsigned int bca_hash3(int x, int y, int z)
{
    unsigned int h = (unsigned int) x * 374761393U;
    h ^= (unsigned int) y * 668265263U;
    h ^= (unsigned int) z * 2246822519U;
    return bca_hash_u32(h);
}

static inline int bca_ramp255(int v, int range)
{
    if (v <= 0)
        return 0;
    if (v >= range)
        return 255;
    return (v * 255) / range;
}

static inline int bca_slot_for_age(int write_slot, int age)
{
    int slot = write_slot - age;
    if (slot < 0)
        slot += BCA_MAX_FRAMES;
    return slot;
}

static inline void bca_blend_canvas_pixel(
    meteorvector_t *c,
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

    alpha = bca_clampi(alpha, 0, 255);

    if (alpha <= 0)
        return;

    sy = bca_clampi(sy, 0, 255);
    su = bca_clampi(su, 0, 255);
    sv = bca_clampi(sv, 0, 255);

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

    yy = bca_clampi(yy, 0, 255);
    uu = bca_clampi(uu, 0, 255);
    vv = bca_clampi(vv, 0, 255);

    c->trail_y[idx] = (uint8_t) yy;
    c->trail_u[idx] = (uint8_t) uu;
    c->trail_v[idx] = (uint8_t) vv;

    Y[idx] = (uint8_t) yy;
    U[idx] = (uint8_t) uu;
    V[idx] = (uint8_t) vv;
}

static inline void bca_make_bone_color(
    uint8_t *restrict srcY,
    uint8_t *restrict srcU,
    uint8_t *restrict srcV,
    int si,
    int strength,
    int white_forge_q8,
    int chroma_gain_q8,
    int color_bias,
    int age,
    int *yy,
    int *uu,
    int *vv
) {
    int yv = srcY[si];
    int uv = srcU[si];
    int vv0 = srcV[si];
    int forge_alpha;
    int color_push;

    forge_alpha = (white_forge_q8 * (strength + 96 + age * 8)) >> 8;
    forge_alpha = bca_clampi(forge_alpha, 0, 255);

    yv = yv + (((255 - yv) * forge_alpha) >> 8);
    yv = bca_clampi(yv, 0, 255);

    color_push = (color_bias * (strength + 64 + age * 18)) >> 8;
    color_push = bca_clampi(color_push, -82, 82);

    if (chroma_gain_q8 <= 40) {
        uv = 128 + color_push;
        vv0 = 128 - color_push;
    } else {
        uv = 128 + (((uv - 128) * chroma_gain_q8) >> 8) + color_push;
        vv0 = 128 + (((vv0 - 128) * chroma_gain_q8) >> 8) - color_push;
    }

    *yy = bca_clampi(yv, 0, 255);
    *uu = bca_clampi(uv, 0, 255);
    *vv = bca_clampi(vv0, 0, 255);
}

static inline void bca_draw_head(
    meteorvector_t *c,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict srcY,
    uint8_t *restrict srcU,
    uint8_t *restrict srcV,
    int rows,
    int cx,
    int cy,
    int radius,
    int strength,
    int opacity_q8,
    int white_forge_q8,
    int chroma_gain_q8,
    int color_bias,
    int age
) {
    int w = c->w;
    int y;

    radius = bca_clampi(radius, 1, 8);

    for (y = cy - radius; y <= cy + radius; y++) {
        int x;
        int dy = bca_absi(y - cy);

        if (y < 0 || y >= rows)
            continue;

        for (x = cx - radius; x <= cx + radius; x++) {
            int dx;
            int dist;
            int idx;
            int yy;
            int uu;
            int vv;
            int alpha;

            if (x < 0 || x >= w)
                continue;

            dx = bca_absi(x - cx);
            dist = dx + dy;

            if (dist > radius)
                continue;

            idx = y * w + x;

            alpha = strength + 48 - ((dist * 160) / (radius + 1));
            alpha = bca_clampi(alpha, 0, 255);
            alpha = (alpha * opacity_q8 + 128) >> 8;

            if (alpha <= 0)
                continue;

            bca_make_bone_color(
                srcY, srcU, srcV,
                idx,
                strength,
                white_forge_q8,
                chroma_gain_q8,
                color_bias,
                age,
                &yy, &uu, &vv
            );

            bca_blend_canvas_pixel(c, Y, U, V, idx, yy, uu, vv, alpha);
        }
    }
}

static inline void bca_draw_tail(
    meteorvector_t *c,
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
    int tail_length,
    int head_size,
    int opacity_q8,
    int age,
    int max_age,
    int motion_launch,
    int white_forge_q8,
    int chroma_gain_q8,
    int color_bias,
    unsigned int shape_hash,
    int fast_tail
) {
    int w = c->w;

    int tx = -gy;
    int ty = gx;
    int mag = bca_maxi(bca_absi(tx), bca_absi(ty));

    int dx_q8;
    int dy_q8;
    int nx_q8;
    int ny_q8;

    int bend_px;
    int age_fade;
    int total_len;
    int t_step;

    int sign;
    int t;

    if (mag <= 0)
        return;

    sign = (shape_hash & 1) ? 1 : -1;

    tx *= sign;
    ty *= sign;

    dx_q8 = (tx * 256) / mag;
    dy_q8 = (ty * 256) / mag;

    nx_q8 = -dy_q8;
    ny_q8 = dx_q8;

    total_len = tail_length;
    total_len += (tail_length * age) / 2;
    total_len += (motion_launch * (age + 1)) / 8;
    total_len = bca_clampi(total_len, 2, 112);

    t_step = fast_tail ? 2 : 1;

    bend_px = ((((int)((shape_hash >> 16) & 255)) - 128) *
               (motion_launch + age * 16)) / 1250;

    if (max_age > 0)
        age_fade = 255 - ((age * 100) / (max_age + 1));
    else
        age_fade = 255;

    age_fade = bca_clampi(age_fade, 108, 255);

    for (t = head_size; t <= total_len; t += t_step) {
        int q;
        int taper = 255 - ((t * 220) / (total_len + 1));
        int curve_px = (t * bend_px) / (total_len + 1);

        int base_x_q8 = (cx << 8) - dx_q8 * t + nx_q8 * curve_px;
        int base_y_q8 = (cy << 8) - dy_q8 * t + ny_q8 * curve_px;

        int src_x_q8 = (cx << 8) - dx_q8 * t;
        int src_y_q8 = (cy << 8) - dy_q8 * t;

        int width;

        taper = bca_clampi(taper, 18, 230);

        width = 1;
        if (!fast_tail && head_size > 3 && t < (total_len >> 2))
            width = 2;

        for (q = -width; q <= width; q++) {
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
            int q_fall;

            if (ox < 0 || ox >= w || oy < 0 || oy >= rows)
                continue;

            sx = bca_clampi(sx, 0, w - 1);
            sy = bca_clampi(sy, 0, rows - 1);

            oi = oy * w + ox;
            si = sy * w + sx;

            alpha = (strength * taper + 127) / 255;
            alpha = (alpha * age_fade + 127) / 255;

            q_fall = (bca_absi(q) * 96) / (width + 1);
            alpha = (alpha * (255 - q_fall) + 127) / 255;

            if (q == 0)
                alpha = bca_clampi(alpha + 10, 0, 255);

            if (fast_tail)
                alpha = bca_clampi((alpha * 116) / 100, 0, 255);

            alpha = (alpha * opacity_q8 + 128) >> 8;

            if (alpha <= 0)
                continue;

            bca_make_bone_color(
                srcY, srcU, srcV,
                si,
                strength,
                white_forge_q8,
                chroma_gain_q8,
                color_bias,
                age,
                &yy, &uu, &vv
            );

            bca_blend_canvas_pixel(c, Y, U, V, oi, yy, uu, vv, alpha);
        }
    }
}

static inline void bca_draw_comet(
    meteorvector_t *c,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict headY,
    uint8_t *restrict headU,
    uint8_t *restrict headV,
    uint8_t *restrict tailY,
    uint8_t *restrict tailU,
    uint8_t *restrict tailV,
    int rows,
    int cx,
    int cy,
    int gx,
    int gy,
    int strength,
    int head_size,
    int tail_length,
    int opacity_q8,
    int age,
    int max_age,
    int motion_launch,
    int white_forge_q8,
    int chroma_gain_q8,
    int color_bias,
    unsigned int shape_hash,
    int fast_tail
) {
    bca_draw_tail(
        c,
        Y, U, V,
        tailY, tailU, tailV,
        rows,
        cx, cy,
        gx, gy,
        strength,
        tail_length,
        head_size,
        opacity_q8,
        age,
        max_age,
        motion_launch,
        white_forge_q8,
        chroma_gain_q8,
        color_bias,
        shape_hash,
        fast_tail
    );

    bca_draw_head(
        c,
        Y, U, V,
        headY, headU, headV,
        rows,
        cx, cy,
        head_size,
        strength,
        opacity_q8,
        white_forge_q8,
        chroma_gain_q8,
        color_bias,
        age
    );
}

vj_effect *meteorvector_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if (!ve)
        return NULL;

    ve->num_params = BONECOMETAUTOPSY_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[P_OPACITY]       = 100;
    ve->defaults[P_STEP]          = 3;
    ve->defaults[P_TIME_DEPTH]    = 4;
    ve->defaults[P_HEAD_SIZE]     = 3;
    ve->defaults[P_TAIL_LENGTH]   = 56;
    ve->defaults[P_EDGE]          = 86;
    ve->defaults[P_MOTION_LAUNCH] = 54;
    ve->defaults[P_COMET_DENSITY] = 54;
    ve->defaults[P_WHITE_FORGE]   = 88;
    ve->defaults[P_TRAIL]         = 97;
    ve->defaults[P_STROKE_CHROMA] = 10;
    ve->defaults[P_COLOR_BIAS]    = 92;

    ve->limits[0][P_OPACITY]       = 0;
    ve->limits[1][P_OPACITY]       = 100;

    ve->limits[0][P_STEP]          = 3;
    ve->limits[1][P_STEP]          = 14;

    ve->limits[0][P_TIME_DEPTH]    = 1;
    ve->limits[1][P_TIME_DEPTH]    = BCA_MAX_FRAMES;

    ve->limits[0][P_HEAD_SIZE]     = 1;
    ve->limits[1][P_HEAD_SIZE]     = 8;

    ve->limits[0][P_TAIL_LENGTH]   = 2;
    ve->limits[1][P_TAIL_LENGTH]   = 96;

    ve->limits[0][P_EDGE]          = 0;
    ve->limits[1][P_EDGE]          = 100;

    ve->limits[0][P_MOTION_LAUNCH] = 0;
    ve->limits[1][P_MOTION_LAUNCH] = 100;

    ve->limits[0][P_COMET_DENSITY] = 0;
    ve->limits[1][P_COMET_DENSITY] = 100;

    ve->limits[0][P_WHITE_FORGE]   = 0;
    ve->limits[1][P_WHITE_FORGE]   = 100;

    ve->limits[0][P_TRAIL]         = 0;
    ve->limits[1][P_TRAIL]         = 100;

    ve->limits[0][P_STROKE_CHROMA] = 0;
    ve->limits[1][P_STROKE_CHROMA] = 100;

    ve->limits[0][P_COLOR_BIAS]    = 0;
    ve->limits[1][P_COLOR_BIAS]    = 100;

    ve->description = "Meteor Static";

    ve->sub_format = 1;
    
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Step Size",
        "Time Depth",
        "Head Size",
        "Tail Length",
        "Edge Sensitivity",
        "Motion Launch",
        "Comet Density",
        "White Forge",
        "Trail Memory",
        "Stroke Chroma",
        "Color Bias"
    );

    return ve;
}

void *meteorvector_malloc(int w, int h)
{
    meteorvector_t *c;
    size_t len;
    size_t total;
    uint8_t *p;
    int s;
    int i;

    if (w <= 0 || h <= 0)
        return NULL;

    c = (meteorvector_t *) vj_calloc(sizeof(meteorvector_t));
    if (!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->filled = 0;
    c->n_threads = vje_advise_num_threads(w * h);

    len = (size_t) c->len;

    total = len * ((size_t)BCA_MAX_FRAMES * 3u + 5u);

    c->region = (uint8_t *) vj_malloc(total);
    if (!c->region) {
        free(c);
        return NULL;
    }

    p = c->region;

    for (s = 0; s < BCA_MAX_FRAMES; s++) {
        c->ring_y[s] = p; p += len;
        c->ring_u[s] = p; p += len;
        c->ring_v[s] = p; p += len;
    }

    c->stable_y = p; p += len;
    c->last_stable_y = p; p += len;

    c->trail_y = p; p += len;
    c->trail_u = p; p += len;
    c->trail_v = p;

    for (s = 0; s < BCA_MAX_FRAMES; s++) {
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

void meteorvector_free(void *ptr)
{
    meteorvector_t *c = (meteorvector_t *) ptr;

    if (!c)
        return;

    if (c->region)
        free(c->region);

    free(c);
}

void meteorvector_apply(void *ptr, VJFrame *frame, int *args)
{
    meteorvector_t *c = (meteorvector_t *) ptr;

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
    int head_size;
    int tail_length;
    int edge_sens;
    int motion_launch;
    int comet_density;
    int white_forge;
    int white_forge_q8;
    int trail;
    int trail_q8;
    int stroke_chroma;
    int stroke_chroma_q8;
    int color_bias;

    int chroma_gain_age_q8[BCA_MAX_FRAMES];

    int process_len;
    int rows;
    int w;
    int write_slot;
    int available;
    int max_age;

    int edge_threshold_base;
    int seed_floor;
    int motion_min;
    int motion_only_keep;
    int long_fast;
    int tail_comp_q8;

    int a;
    int y;

    if (!c || !frame || !args)
        return;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    if (!Y || !U || !V)
        return;

    w = c->w;

    process_len = c->len;
    if (frame->len > 0 && frame->len < process_len)
        process_len = frame->len;

    rows = process_len / w;

    if (rows < 3 || w < 3)
        return;

    opacity       = bca_clampi(args[P_OPACITY],       0, 100);
    step          = bca_clampi(args[P_STEP],          3, 14);
    time_depth    = bca_clampi(args[P_TIME_DEPTH],    1, BCA_MAX_FRAMES);
    head_size     = bca_clampi(args[P_HEAD_SIZE],     1, 8);
    tail_length   = bca_clampi(args[P_TAIL_LENGTH],   2, 96);
    edge_sens     = bca_clampi(args[P_EDGE],          0, 100);
    motion_launch = bca_clampi(args[P_MOTION_LAUNCH], 0, 100);
    comet_density = bca_clampi(args[P_COMET_DENSITY], 0, 100);
    white_forge   = bca_clampi(args[P_WHITE_FORGE],   0, 100);
    trail         = bca_clampi(args[P_TRAIL],         0, 100);
    stroke_chroma = bca_clampi(args[P_STROKE_CHROMA], 0, 100);
    color_bias    = bca_clampi(args[P_COLOR_BIAS],    0, 100) - 50;

    opacity_q8 = (opacity * 255 + 50) / 100;
    white_forge_q8 = (white_forge * 256 + 50) / 100;

    trail_q8 = (trail * 255 + 50) / 100;

    if (stroke_chroma <= 50)
        stroke_chroma_q8 = (stroke_chroma * 256 + 25) / 50;
    else
        stroke_chroma_q8 = 256 + (((stroke_chroma - 50) * 384 + 25) / 50);

    for (a = 0; a < BCA_MAX_FRAMES; a++) {
        int base = 100 + a * 7;
        int gain_q8 = (base * 256 + 50) / 100;

        gain_q8 = (gain_q8 * stroke_chroma_q8 + 128) >> 8;
        chroma_gain_age_q8[a] = bca_clampi(gain_q8, 0, 2048);
    }

    write_slot = c->frame % BCA_MAX_FRAMES;

    curY = c->ring_y[write_slot];
    curU = c->ring_u[write_slot];
    curV = c->ring_v[write_slot];

    edgeY = c->stable_y;
    lastEdgeY = c->last_stable_y;

    available = c->filled + 1;
    if (available > BCA_MAX_FRAMES)
        available = BCA_MAX_FRAMES;

    max_age = time_depth - 1;
    if (max_age > available - 1)
        max_age = available - 1;

    edge_threshold_base = 250 - edge_sens * 2;

    seed_floor = 32 - (edge_sens / 7);
    seed_floor = bca_clampi(seed_floor, 10, 32);

    motion_min = 18 + ((100 - motion_launch) / 4);

    tail_comp_q8 = (52 * 256) / (tail_length + 32);
    tail_comp_q8 = bca_clampi(tail_comp_q8, 96, 256);

    motion_only_keep = 2 + (((comet_density / 8) + (motion_launch / 12)) * tail_comp_q8 >> 8);

    long_fast = (step <= 4 && tail_length >= 48 && comet_density >= 45);

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
            delta = bca_absi(raw_y - old_stable);
            w_new = (delta > 56) ? 6 : 3;
            new_stable = ((old_stable * (16 - w_new)) + raw_y * w_new + 8) >> 4;
        }

        c->stable_y[i] = bca_u8(new_stable);

        yy = (c->trail_y[i] * trail_q8) >> 8;
        uu = 128 + ((((int)c->trail_u[i] - 128) * trail_q8) >> 8);
        vv = 128 + ((((int)c->trail_v[i] - 128) * trail_q8) >> 8);

        yy = bca_clampi(yy, 0, 255);
        uu = bca_clampi(uu, 0, 255);
        vv = bca_clampi(vv, 0, 255);

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

            int edge = bca_absi(gx) + bca_absi(gy);
            int motion = 0;

            unsigned int spatial_hash = bca_hash3(xcell, ycell, 7331);
            unsigned int shape_hash = bca_hash3(xcell, ycell, 9917);

            int hnoise;
            int edge_threshold;
            int edge_core;
            int motion_boost;
            int strength;

            int accepted = 0;
            int motion_only = 0;

            int age = 0;
            int tail_slot;
            int head_slot;

            int draw_x = x;
            int draw_y = y;

            int local_tail;
            int local_head;

            if (available > 1)
                motion = bca_absi((int)edgeY[idx] - (int)lastEdgeY[idx]);

            hnoise = (int)(spatial_hash & 63) - 31;

            edge_threshold = edge_threshold_base + hnoise;
            edge_threshold = bca_clampi(edge_threshold, 32, 275);

            if (edge >= seed_floor) {
                int keep;
                int density_hash;

                edge_core = edge - edge_threshold;
                motion_boost = (motion * motion_launch) / 145;

                strength = bca_ramp255(edge_core + motion_boost, 180);

                keep = 8 + ((comet_density * tail_comp_q8) >> 8) + (strength >> 3);

                if (edge > 200)
                    keep += (edge - 200) >> 3;

                if (strength > 225)
                    keep += 10;

                keep = bca_clampi(keep, 5, 190);

                density_hash = (int)((spatial_hash >> 8) & 255);

                if (density_hash <= keep)
                    accepted = 1;
            } else {
                if (available > 1 && motion_launch > 0) {
                    strength = 0;

                    if (motion >= motion_min &&
                        (int)((spatial_hash >> 8) & 255) <= motion_only_keep) {
                        accepted = 1;
                        motion_only = 1;

                        gx = (int)((shape_hash >> 16) & 255) - 128;
                        gy = (int)((shape_hash >> 24) & 255) - 128;

                        if (gx == 0 && gy == 0)
                            gx = 1;

                        strength = 48 + ((motion * motion_launch) / 175);
                        strength = bca_clampi(strength, 0, 140);
                    }
                } else {
                    strength = 0;
                }
            }

            if (!accepted || strength <= 0)
                continue;

            if (max_age > 0) {
                int motion_part = (motion * motion_launch * max_age) / 7200;
                int jitter_part = 0;

                if (motion > 3 &&
                    (int)((spatial_hash >> 16) & 255) < (comet_density + 34)) {
                    jitter_part = (int)((spatial_hash >> 24) & 3);
                }

                age = motion_part + jitter_part;
                age = bca_clampi(age, 0, max_age);
            } else {
                age = 0;
            }

            head_slot = write_slot;
            tail_slot = bca_slot_for_age(write_slot, age);

            if (step > 3) {
                int jlim = step / 3;
                int jx = ((((int)(shape_hash & 15)) - 8) * jlim) / 8;
                int jy = ((((int)((shape_hash >> 4) & 15)) - 8) * jlim) / 8;

                draw_x = bca_clampi(x + jx, 1, w - 2);
                draw_y = bca_clampi(y + jy, 1, rows - 2);
            }

            local_tail = tail_length + (((int)((shape_hash >> 6) & 15) - 7) * tail_length) / 64;
            local_tail = bca_clampi(local_tail, 2, 96);

            local_head = head_size;
            if (motion_only && local_head > 1)
                local_head--;

            if (!motion_only && motion > 12 && local_head < 8)
                local_head++;

            bca_draw_comet(
                c,
                Y, U, V,
                c->ring_y[head_slot],
                c->ring_u[head_slot],
                c->ring_v[head_slot],
                c->ring_y[tail_slot],
                c->ring_u[tail_slot],
                c->ring_v[tail_slot],
                rows,
                draw_x, draw_y,
                gx, gy,
                strength,
                local_head,
                local_tail,
                opacity_q8,
                age,
                max_age,
                motion_launch,
                white_forge_q8,
                chroma_gain_age_q8[age],
                color_bias,
                shape_hash ^ ((unsigned int)age * 0x45d9f3bU),
                long_fast
            );
        }
    }

    if (c->filled < BCA_MAX_FRAMES)
        c->filled++;

    c->frame++;
}