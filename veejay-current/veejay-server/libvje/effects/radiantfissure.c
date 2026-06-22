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
#include "radiantfissure.h"

#define RADIANTFISSURE_PARAMS 13
#define WBA_MAX_FRAMES 8

#define P_OPACITY        0
#define P_STEP           1
#define P_TIME_DEPTH     2
#define P_BONE_LENGTH    3
#define P_EDGE           4
#define P_MOTION_AGE     5
#define P_BONE_DENSITY   6
#define P_WHITE_FORGE    7
#define P_FISSURE        8
#define P_TRAIL          9
#define P_STROKE_CHROMA 10
#define P_COLOR_BIAS    11
#define P_STROKE_BUDGET 12

typedef struct {
    int w;
    int h;
    int len;
    int frame;
    int filled;
    int n_threads;

    uint8_t *region;

    uint8_t *ring_y[WBA_MAX_FRAMES];
    uint8_t *ring_u[WBA_MAX_FRAMES];
    uint8_t *ring_v[WBA_MAX_FRAMES];

    uint8_t *stable_y;
    uint8_t *last_stable_y;

    uint8_t *trail_y;
    uint8_t *trail_u;
    uint8_t *trail_v;
} radiantfissure_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t wba_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline int wba_div255(int v)
{
    v += 128;
    return (v + (v >> 8)) >> 8;
}

static inline int wba_param1000_to_100(int v)
{
    v = clampi(v, 0, 1000);
    return (v * 100 + 500) / 1000;
}

static inline int wba_100_to_param1000(int v)
{
    v = clampi(v, 0, 100);
    return v * 10;
}

static inline int wba_range1000_to_i(int v, int lo, int hi)
{
    v = clampi(v, 0, 1000);
    return lo + ((hi - lo) * v + 500) / 1000;
}

static inline int wba_i_to_range1000(int v, int lo, int hi)
{
    int d = hi - lo;
    if(d <= 0)
        return 0;
    v = clampi(v, lo, hi);
    return ((v - lo) * 1000 + d / 2) / d;
}

static inline int wba_soft_ceiling_y(int y)
{
    if(y > 238) {
        y = 238 + ((y - 238) >> 2);
        if(y > 248)
            y = 248;
    }
    return clampi(y, 0, 255);
}

static inline int wba_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline int wba_maxi(int a, int b)
{
    return a > b ? a : b;
}

static inline unsigned int wba_hash_u32(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline unsigned int wba_hash3(int x, int y, int z)
{
    unsigned int h = (unsigned int) x * 374761393U;
    h ^= (unsigned int) y * 668265263U;
    h ^= (unsigned int) z * 2246822519U;
    return wba_hash_u32(h);
}

static inline int wba_ramp255(int v, int range)
{
    if (v <= 0)
        return 0;
    if (v >= range)
        return 255;
    return (v * 255) / range;
}

static inline int wba_slot_for_age(int write_slot, int age)
{
    int slot = write_slot - age;
    if (slot < 0)
        slot += WBA_MAX_FRAMES;
    return slot;
}

static inline void wba_blend_canvas_pixel(
    radiantfissure_t *c,
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

    if (alpha <= 0)
        return;

    if (alpha >= 255) {
        yy = sy;
        uu = su;
        vv = sv;
    } else {
        const int inv = 255 - alpha;
        yy = wba_div255(c->trail_y[idx] * inv + sy * alpha);
        uu = wba_div255(c->trail_u[idx] * inv + su * alpha);
        vv = wba_div255(c->trail_v[idx] * inv + sv * alpha);
    }

    yy = wba_soft_ceiling_y(yy);
    uu = clampi(uu, 0, 255);
    vv = clampi(vv, 0, 255);

    c->trail_y[idx] = (uint8_t) yy;
    c->trail_u[idx] = (uint8_t) uu;
    c->trail_v[idx] = (uint8_t) vv;

    Y[idx] = (uint8_t) yy;
    U[idx] = (uint8_t) uu;
    V[idx] = (uint8_t) vv;
}

static inline void wba_draw_stroke(
    radiantfissure_t *c,
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
    int bone_length,
    int opacity_q8,
    int age,
    int max_age,
    int motion_age,
    int white_forge_q8,
    int fissure,
    int chroma_gain_q8,
    int color_bias,
    unsigned int shape_hash,
    int black_mode,
    int fast_long,
    int load_shed
) {
    int w = c->w;
    int h = rows;

    int tx = -gy;
    int ty = gx;
    int mag = wba_maxi(wba_absi(tx), wba_absi(ty));

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
    int src_base_x_q8;
    int src_base_y_q8;

    int age_fade;
    int color_push;
    int t_step;
    int thickness;
    int edge_fall_scale;
    int taper_scale_q8;
    int inv_half_sq_q16;
    int fray_scale_q8;
    int forge_alpha;
    int black_alpha_q8;

    int t;

    if (mag <= 0)
        return;

    {
        const int inv_mag_q16 = 65536 / mag;
        dx_q8 = (tx * inv_mag_q16) >> 8;
        dy_q8 = (ty * inv_mag_q16) >> 8;
    }

    nx_q8 = -dy_q8;
    ny_q8 = dx_q8;

    if (!black_mode) {
        bone_length += (bone_length * age) / 5;
    } else {
        bone_length = (bone_length * (48 + fissure)) / 150;
    }

    bone_length = clampi(bone_length, 2, 96);

    half = bone_length >> 1;
    if (half < 1)
        half = 1;

    half_sq = half * half;
    if (half_sq < 1)
        half_sq = 1;

    if (fast_long) {
        t_step = bone_length >= 72 ? 3 : 2;
        if (black_mode)
            t_step = 3;
        if (load_shed > 58)
            t_step++;
        if (load_shed > 78)
            t_step++;
    } else {
        t_step = (load_shed > 66 && bone_length >= 52) ? 2 : 1;
    }

    age_push = ((motion_age + fissure) * (age + 1) * (2 + (bone_length >> 2))) / 235;

    jx = ((((int)(shape_hash & 255)) - 128) * age_push) >> 7;
    jy = ((((int)((shape_hash >> 8) & 255)) - 128) * age_push) >> 7;

    if (motion_age > 0 && age > 0) {
        int nmag = wba_maxi(wba_absi(gx), wba_absi(gy));
        if (nmag > 0) {
            const int inv_nmag_q16 = 65536 / (nmag * 20 + 1);
            const int age_motion = age * motion_age;
            jx += (gx * age_motion * inv_nmag_q16) >> 16;
            jy += (gy * age_motion * inv_nmag_q16) >> 16;
        }
    }

    bend_px = ((((int)((shape_hash >> 16) & 255)) - 128) *
               (motion_age + fissure + age * 10)) / 1550;

    ax_q8 = (cx + jx) << 8;
    ay_q8 = (cy + jy) << 8;
    src_base_x_q8 = cx << 8;
    src_base_y_q8 = cy << 8;

    if (max_age > 0)
        age_fade = 255 - ((age * 108) / (max_age + 1));
    else
        age_fade = 255;

    age_fade = clampi(age_fade, 116, 255);

    color_push = (color_bias * (strength + 64 + age * 16)) >> 8;
    color_push = clampi(color_push, -72, 72);

    if (black_mode) {
        thickness = 1;
    } else {
        thickness = 1 + ((age + (white_forge_q8 >= 470)) > 0);
        if (!fast_long && white_forge_q8 > 220 && age > 1)
            thickness++;
    }

    if (fast_long && thickness > 1)
        thickness = 1;

    thickness = clampi(thickness, 1, 3);
    edge_fall_scale = 122 / (thickness + 1);
    taper_scale_q8 = (150 << 8) / (half + 1);
    inv_half_sq_q16 = 65536 / half_sq;
    fray_scale_q8 = ((motion_age + fissure) * (age + 1) * 256) / (half * 260 + 1);
    forge_alpha = (white_forge_q8 * (strength + 92)) >> 8;
    forge_alpha = clampi(forge_alpha, 0, 255);
    black_alpha_q8 = ((110 + fissure) * 256 + 110) / 220;

    for (t = -half; t <= half; t += t_step) {
        int t_abs = wba_absi(t);
        int curve_px = (((half_sq - (t * t)) * bend_px * inv_half_sq_q16) >> 16);

        int base_x_q8 = ax_q8 + dx_q8 * t + nx_q8 * curve_px;
        int base_y_q8 = ay_q8 + dy_q8 * t + ny_q8 * curve_px;

        int taper;
        int q;

        taper = 255 - ((t_abs * taper_scale_q8) >> 8);
        taper = clampi(taper, 70, 255);

        if (!fast_long && t_abs > (half >> 1)) {
            unsigned int eh = wba_hash3(cx + t, cy - t, (int)(shape_hash & 32767));
            int fray = (fray_scale_q8 * t_abs) >> 8;

            base_x_q8 += ((((int)(eh & 255)) - 128) * fray);
            base_y_q8 += ((((int)((eh >> 8) & 255)) - 128) * fray);
        }

        for (q = -thickness; q <= thickness; q++) {
            int q_abs = wba_absi(q);

            int ox = (base_x_q8 + nx_q8 * q + 128) >> 8;
            int oy = (base_y_q8 + ny_q8 * q + 128) >> 8;
            int oi;
            int yy;
            int uu;
            int vv;
            int alpha;
            int edge_fall;

            if (ox < 0 || ox >= w || oy < 0 || oy >= h)
                continue;

            oi = oy * w + ox;

            alpha = wba_div255(strength * taper);
            alpha = wba_div255(alpha * age_fade);

            edge_fall = q_abs * edge_fall_scale;
            alpha = wba_div255(alpha * (255 - edge_fall));

            if (q_abs == 0)
                alpha = clampi(alpha + 22, 0, 255);

            if (fast_long)
                alpha = clampi((alpha * 302 + 128) >> 8, 0, 255);

            alpha = (alpha * opacity_q8 + 128) >> 8;

            if (black_mode) {
                alpha = (alpha * black_alpha_q8 + 128) >> 8;
                if (alpha <= 0)
                    continue;

                wba_blend_canvas_pixel(c, Y, U, V, oi, 0, 128, 128, alpha);
                continue;
            }

            if (alpha <= 0)
                continue;

            {
                int src_x_q8 = src_base_x_q8 + dx_q8 * t;
                int src_y_q8 = src_base_y_q8 + dy_q8 * t;
                int sx = (src_x_q8 + nx_q8 * q + 128) >> 8;
                int sy = (src_y_q8 + ny_q8 * q + 128) >> 8;
                int si;

                sx = clampi(sx, 0, w - 1);
                sy = clampi(sy, 0, h - 1);
                si = sy * w + sx;
                yy = srcY[si];

                if (chroma_gain_q8 <= 40) {
                    uu = 128 + color_push;
                    vv = 128 - color_push;
                } else {
                    uu = 128 + (((srcU[si] - 128) * chroma_gain_q8) >> 8) + color_push;
                    vv = 128 + (((srcV[si] - 128) * chroma_gain_q8) >> 8) - color_push;
                }
            }

            yy = yy + (((255 - yy) * forge_alpha) >> 8);
            yy = clampi(yy, 0, 255);

            wba_blend_canvas_pixel(c, Y, U, V, oi, yy, uu, vv, alpha);
        }
    }
}

vj_effect *radiantfissure_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if (!ve)
        return NULL;

    ve->num_params = RADIANTFISSURE_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults)
            free(ve->defaults);
        if (ve->limits[0])
            free(ve->limits[0]);
        if (ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_OPACITY]       = 100;
    ve->defaults[P_STEP]          = 3;
    ve->defaults[P_TIME_DEPTH]    = 4;
    ve->defaults[P_BONE_LENGTH]   = wba_i_to_range1000(64, 2, 96);
    ve->defaults[P_EDGE]          = wba_100_to_param1000(88);
    ve->defaults[P_MOTION_AGE]    = wba_100_to_param1000(40);
    ve->defaults[P_BONE_DENSITY]  = wba_100_to_param1000(70);
    ve->defaults[P_WHITE_FORGE]   = wba_100_to_param1000(86);
    ve->defaults[P_FISSURE]       = wba_100_to_param1000(42);
    ve->defaults[P_TRAIL]         = wba_100_to_param1000(98);
    ve->defaults[P_STROKE_CHROMA] = wba_100_to_param1000(5);
    ve->defaults[P_COLOR_BIAS]    = wba_100_to_param1000(95);
    ve->defaults[P_STROKE_BUDGET] = wba_100_to_param1000(62);

    ve->limits[0][P_OPACITY]       = 0;
    ve->limits[1][P_OPACITY]       = 100;

    ve->limits[0][P_STEP]          = 3;
    ve->limits[1][P_STEP]          = 14;

    ve->limits[0][P_TIME_DEPTH]    = 1;
    ve->limits[1][P_TIME_DEPTH]    = WBA_MAX_FRAMES;

    ve->limits[0][P_BONE_LENGTH]   = 0;
    ve->limits[1][P_BONE_LENGTH]   = 1000;

    ve->limits[0][P_EDGE]          = 0;
    ve->limits[1][P_EDGE]          = 1000;

    ve->limits[0][P_MOTION_AGE]    = 0;
    ve->limits[1][P_MOTION_AGE]    = 1000;

    ve->limits[0][P_BONE_DENSITY]  = 0;
    ve->limits[1][P_BONE_DENSITY]  = 1000;

    ve->limits[0][P_WHITE_FORGE]   = 0;
    ve->limits[1][P_WHITE_FORGE]   = 1000;

    ve->limits[0][P_FISSURE]       = 0;
    ve->limits[1][P_FISSURE]       = 1000;

    ve->limits[0][P_TRAIL]         = 0;
    ve->limits[1][P_TRAIL]         = 1000;

    ve->limits[0][P_STROKE_CHROMA] = 0;
    ve->limits[1][P_STROKE_CHROMA] = 1000;

    ve->limits[0][P_COLOR_BIAS]    = 0;
    ve->limits[1][P_COLOR_BIAS]    = 1000;

    ve->limits[0][P_STROKE_BUDGET] = 0;
    ve->limits[1][P_STROKE_BUDGET] = 5000;

    ve->description = "Radiant Fissure";

    ve->sub_format = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity",
        "Step Size",
        "Time Depth",
        "Bone Length",
        "Edge Sensitivity",
        "Motion Ageing",
        "Bone Density",
        "White Forge",
        "Fissure Amount",
        "Trail Memory",
        "Stroke Chroma",
        "Color Bias",
        "Stroke Budget"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                                                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,   -1000,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,   -1000,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,                            2,   8,   44, 100, 100, 1500, 250,  96,
        VJ_BEAT_TRAIL_LENGTH,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         420,1000, 58, 100,  60,  760,   0, 100,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         700,1000, 56, 100,  60,  760,   0, 100,
        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         260,1000, 62, 100,  50,  700,   0, 100,
        VJ_BEAT_DENSITY,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         420,1000, 58, 100,  60,  760,   0, 100,
        VJ_BEAT_GLOW,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         620, 960, 42,  92,  80, 1000,   0,  84,
        VJ_BEAT_TURBULENCE,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         260,1000, 56, 100,  60,  800,   0,  96,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         900,1000, 50, 100, 120, 2800, 300, 100,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          30, 760, 44,  96, 100, 1000,   0,  86,
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         680,1000, 28,  84, 220, 1600,   0,  58,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT,                                                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,   -1000
    );
    return ve;
}

void *radiantfissure_malloc(int w, int h)
{
    radiantfissure_t *c;
    size_t len;
    size_t total;
    uint8_t *p;
    int s;
    c = (radiantfissure_t *) vj_calloc(sizeof(radiantfissure_t));
    if (!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->filled = 0;
    c->n_threads = vje_advise_num_threads(w * h);

    len = (size_t) c->len;

    total = len * ((size_t)WBA_MAX_FRAMES * 3u + 5u);

    c->region = (uint8_t *) vj_malloc(total);
    if (!c->region) {
        free(c);
        return NULL;
    }

    p = c->region;

    for (s = 0; s < WBA_MAX_FRAMES; s++) {
        c->ring_y[s] = p; p += len;
        c->ring_u[s] = p; p += len;
        c->ring_v[s] = p; p += len;
    }

    c->stable_y = p; p += len;
    c->last_stable_y = p; p += len;

    c->trail_y = p; p += len;
    c->trail_u = p; p += len;
    c->trail_v = p;

    for (s = 0; s < WBA_MAX_FRAMES; s++) {
        veejay_memset(c->ring_y[s], 0, len);
        veejay_memset(c->ring_u[s], 128, len);
        veejay_memset(c->ring_v[s], 128, len);
    }

    veejay_memset(c->stable_y, 0, len);
    veejay_memset(c->last_stable_y, 0, len);
    veejay_memset(c->trail_y, 0, len);
    veejay_memset(c->trail_u, 128, len);
    veejay_memset(c->trail_v, 128, len);

    return (void *) c;
}

void radiantfissure_free(void *ptr)
{
    radiantfissure_t *c = (radiantfissure_t *) ptr;

    free(c->region);
    free(c);
}

void radiantfissure_apply(void *ptr, VJFrame *frame, int *args)
{
    radiantfissure_t *c = (radiantfissure_t *) ptr;

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
    int bone_length;
    int edge_sens;
    int motion_age;
    int bone_density;
    int white_forge;
    int white_forge_q8;
    int fissure;
    int trail;
    int trail_q8;
    int stroke_chroma;
    int stroke_chroma_q8;
    int color_bias;
    int stroke_budget;

    int chroma_gain_age_q8[WBA_MAX_FRAMES];

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

    int long_dense_fast;
    int length_comp_q8;
    int load_shed;
    int keep_cap;
    int current_strength_floor;
    int area_q8;
    int grid_cells;
    int main_gate;
    int optional_gate;
    int main_stroke_limit;
    int optional_stroke_limit;
    int main_strokes_used;
    int optional_strokes_used;

    int a;
    int y;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    w = c->w;
    process_len = c->len;
    rows = c->h;

    opacity       = args[P_OPACITY];
    step          = args[P_STEP];
    time_depth    = args[P_TIME_DEPTH];
    bone_length   = wba_range1000_to_i(args[P_BONE_LENGTH], 2, 96);
    edge_sens     = wba_param1000_to_100(args[P_EDGE]);
    motion_age    = wba_param1000_to_100(args[P_MOTION_AGE]);
    bone_density  = wba_param1000_to_100(args[P_BONE_DENSITY]);
    white_forge   = wba_param1000_to_100(args[P_WHITE_FORGE]);
    fissure       = wba_param1000_to_100(args[P_FISSURE]);
    trail         = wba_param1000_to_100(args[P_TRAIL]);
    stroke_chroma = wba_param1000_to_100(args[P_STROKE_CHROMA]);
    color_bias    = wba_param1000_to_100(args[P_COLOR_BIAS]) - 50;
    stroke_budget = wba_param1000_to_100(args[P_STROKE_BUDGET]);

    opacity = clampi(opacity, 0, 100);
    step = clampi(step, 3, 14);
    time_depth = clampi(time_depth, 1, WBA_MAX_FRAMES);

    opacity_q8 = (opacity * 255 + 50) / 100;
    white_forge_q8 = (white_forge * 256 + 50) / 100;

    trail_q8 = (trail * 255 + 50) / 100;

    if (stroke_chroma <= 50)
        stroke_chroma_q8 = (stroke_chroma * 256 + 25) / 50;
    else
        stroke_chroma_q8 = 256 + (((stroke_chroma - 50) * 384 + 25) / 50);

    for (a = 0; a < WBA_MAX_FRAMES; a++) {
        int base = 100 + a * 6;
        int gain_q8 = (base * 256 + 50) / 100;

        gain_q8 = (gain_q8 * stroke_chroma_q8 + 128) >> 8;
        chroma_gain_age_q8[a] = clampi(gain_q8, 0, 2048);
    }

    write_slot = c->frame % WBA_MAX_FRAMES;

    curY = c->ring_y[write_slot];
    curU = c->ring_u[write_slot];
    curV = c->ring_v[write_slot];

    edgeY = c->stable_y;
    lastEdgeY = c->last_stable_y;

    available = c->filled + 1;
    if (available > WBA_MAX_FRAMES)
        available = WBA_MAX_FRAMES;

    max_age = time_depth - 1;
    if (max_age > available - 1)
        max_age = available - 1;

    edge_threshold_base = 250 - edge_sens * 2;

    seed_floor = 32 - (edge_sens / 7);
    seed_floor = clampi(seed_floor, 10, 32);

    motion_min = 18 + ((100 - motion_age) / 4);

    length_comp_q8 = (48 * 256) / (bone_length + 32);
    length_comp_q8 = clampi(length_comp_q8, 96, 256);

    motion_fracture_keep = 2 + (((bone_density / 7) + (motion_age / 10)) * length_comp_q8 >> 8);
    current_keep_base = 8 + (((bone_density / 4) + 6) * length_comp_q8 >> 8);

    motion_fracture_keep = clampi(motion_fracture_keep, 1, 58);
    current_keep_base = clampi(current_keep_base, 4, 74);

    load_shed = 0;
    if (step <= 4)
        load_shed += 22;
    if (bone_length >= 56)
        load_shed += (bone_length - 52) >> 1;
    if (bone_density >= 62)
        load_shed += (bone_density - 58) >> 1;
    if (fissure >= 48)
        load_shed += (fissure - 44) >> 1;
    if (time_depth >= 5)
        load_shed += (time_depth - 4) * 4;
    if (stroke_budget < 62)
        load_shed += (62 - stroke_budget) >> 1;
    else
        load_shed -= (stroke_budget - 62) >> 2;
    load_shed = clampi(load_shed, 0, 84);

    long_dense_fast = (step <= 4 && bone_length >= 44 && bone_density >= 44) || (load_shed >= 34);
    keep_cap = 218 - load_shed;
    if (keep_cap < 124)
        keep_cap = 124;
    current_strength_floor = 148 + (load_shed >> 1);
    if (current_strength_floor > 186)
        current_strength_floor = 186;

    area_q8 = (process_len * 256 + 207360) / 414720;
    area_q8 = clampi(area_q8, 128, 768);

    grid_cells = ((rows + step - 1) / step) * ((w + step - 1) / step);
    if (grid_cells < 1)
        grid_cells = 1;

    main_stroke_limit = ((160 + stroke_budget * 6) * area_q8 + 128) >> 8;
    if (step <= 4 && time_depth >= 7 && bone_density >= 90 && fissure >= 90)
        main_stroke_limit = (main_stroke_limit * 3) >> 2;
    if (main_stroke_limit < 96)
        main_stroke_limit = 96;

    optional_stroke_limit = (main_stroke_limit * (10 + stroke_budget / 5) + 50) / 100;
    if (optional_stroke_limit < 24)
        optional_stroke_limit = 24;

    main_gate = (main_stroke_limit * 255 + (grid_cells >> 1)) / grid_cells;
    main_gate = clampi(main_gate + 10 + (bone_density >> 3), 4, 255);

    optional_gate = (optional_stroke_limit * 255 + (grid_cells >> 1)) / grid_cells;
    optional_gate = clampi(optional_gate + 4 + (fissure >> 4), 2, 192);

    main_strokes_used = 0;
    optional_strokes_used = 0;

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
            delta = wba_absi(raw_y - old_stable);
            w_new = (delta > 56) ? 6 : 3;
            new_stable = ((old_stable * (16 - w_new)) + raw_y * w_new + 8) >> 4;
        }

        c->stable_y[i] = wba_u8(new_stable);

        yy = (c->trail_y[i] * trail_q8) >> 8;
        uu = 128 + ((((int)c->trail_u[i] - 128) * trail_q8) >> 8);
        vv = 128 + ((((int)c->trail_v[i] - 128) * trail_q8) >> 8);

        yy = clampi(yy, 0, 255);
        uu = clampi(uu, 0, 255);
        vv = clampi(vv, 0, 255);

        c->trail_y[i] = (uint8_t) yy;
        c->trail_u[i] = (uint8_t) uu;
        c->trail_v[i] = (uint8_t) vv;

        Y[i] = (uint8_t) yy;
        U[i] = (uint8_t) uu;
        V[i] = (uint8_t) vv;
    }

    for (y = 1, a = 0; y < rows - 1; y += step, a++) {
        int x;
        int row = y * w;
        int row_phase = (a & 1) ? (step >> 1) : 0;
        int x_start = 1 + row_phase;
        int xcell = 0;

        if (x_start >= w - 1)
            x_start = 1;

        for (x = x_start; x < w - 1; x += step, xcell++) {
            int idx = row + x;

            int l = edgeY[idx - 1];
            int r = edgeY[idx + 1];
            int u0 = edgeY[idx - w];
            int d = edgeY[idx + w];

            int gx = r - l;
            int gy = d - u0;

            int edge = wba_absi(gx) + wba_absi(gy);
            int motion = 0;

            unsigned int spatial_hash = wba_hash3(xcell, a, 7331);
            unsigned int shape_hash = wba_hash3(xcell, a, 9917);

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
                motion = wba_absi((int)edgeY[idx] - (int)lastEdgeY[idx]);

            hnoise = (int)(spatial_hash & 63) - 31;

            edge_threshold = edge_threshold_base + hnoise;
            edge_threshold = clampi(edge_threshold, 32, 275);

            if (edge >= seed_floor) {
                int keep;
                int density_hash;

                edge_core = edge - edge_threshold;
                motion_boost = (motion * motion_age) / 140;

                strength = wba_ramp255(edge_core + motion_boost, 180);

                keep = 8 + ((bone_density * length_comp_q8) >> 8) + (strength >> 3);

                if (edge > 200)
                    keep += (edge - 200) >> 3;

                if (strength > 225)
                    keep += 12;

                keep = clampi(keep, 6, keep_cap);

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

                        strength = 46 + ((motion * motion_age) / 190);
                        strength = clampi(strength, 0, 176);
                    }
                } else {
                    strength = 0;
                }
            }

            if (!accepted || strength <= 0)
                continue;

            if (max_age > 0) {
                int motion_part = (motion * motion_age * max_age) / 7800;
                int jitter_part = 0;

                if (motion > 3 &&
                    (int)((spatial_hash >> 16) & 255) < (bone_density + 30)) {
                    jitter_part = (int)((spatial_hash >> 24) & 3);
                }

                age = motion_part + jitter_part;

                age = clampi(age, 0, max_age);
            } else {
                age = 0;
            }

            y_age = age;

            u_age = age + (((int)((spatial_hash >> 10) & 1) * stroke_chroma) / 100);
            v_age = age + (((int)((spatial_hash >> 12) & 1) * stroke_chroma) / 100);

            u_age = clampi(u_age, 0, max_age);
            v_age = clampi(v_age, 0, max_age);

            y_slot = wba_slot_for_age(write_slot, y_age);
            u_slot = wba_slot_for_age(write_slot, u_age);
            v_slot = wba_slot_for_age(write_slot, v_age);

            if (step > 3) {
                int jlim = step / 3;
                int jx = ((((int)(shape_hash & 15)) - 8) * jlim) / 8;
                int jy = ((((int)((shape_hash >> 4) & 15)) - 8) * jlim) / 8;

                draw_x = clampi(x + jx, 1, w - 2);
                draw_y = clampi(y + jy, 1, rows - 2);
            }

            local_length = bone_length + (((int)((shape_hash >> 6) & 15) - 7) * bone_length) / 64;
            local_length = clampi(local_length, 2, 96);

            if(load_shed > 68 && local_length > 64)
                local_length = 64 + ((local_length - 64) >> 1);
            if(load_shed > 80 && local_length > 56)
                local_length = 56 + ((local_length - 56) >> 1);

            if (main_strokes_used >= main_stroke_limit)
                continue;

            if (main_gate < 255 && (int)((spatial_hash >> 24) & 255) > main_gate)
                continue;

            main_strokes_used++;

            wba_draw_stroke(
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
                motion_age,
                white_forge_q8,
                fissure,
                chroma_gain_age_q8[age],
                color_bias,
                shape_hash ^ ((unsigned int)age * 0x45d9f3bU),
                0,
                long_dense_fast,
                load_shed
            );

            if (age > 0 && !motion_fracture && strength > current_strength_floor) {
                int cur_gate = current_keep_base;
                if (load_shed > 34)
                    cur_gate = (cur_gate * (224 - load_shed) + 128) >> 8;
                if ((int)((spatial_hash >> 2) & 255) <= cur_gate) {
                    int cur_strength = (strength * 58) / 100;
                    int cur_length = (local_length * 2) / 3;

                    if (cur_length < 2)
                        cur_length = 2;

                    if (optional_strokes_used < optional_stroke_limit &&
                        (optional_gate >= 255 || (int)((spatial_hash >> 23) & 255) <= optional_gate)) {
                        optional_strokes_used++;

                        wba_draw_stroke(
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
                            motion_age / 2,
                            white_forge_q8,
                            fissure,
                            chroma_gain_age_q8[0],
                            color_bias,
                            shape_hash ^ 0x91e10da5U,
                            0,
                            long_dense_fast,
                            load_shed
                        );
                    }
                }
            }

            if (fissure > 0 && !motion_fracture && strength > 82) {
                int fiss_keep = (fissure * (18 + (bone_density >> 1))) / 100;
                int fiss_hash = (int)((spatial_hash >> 18) & 255);

                if (edge > 220)
                    fiss_keep += 12;

                if (long_dense_fast)
                    fiss_keep = (fiss_keep * (load_shed > 42 ? 140 : 176)) >> 8;

                fiss_keep = clampi(fiss_keep, 0, load_shed > 42 ? 64 : 88);

                if (fiss_hash <= fiss_keep) {
                    int fiss_strength = (strength * (62 + fissure)) / 170;
                    int fiss_len = (local_length * (36 + fissure)) / 170;

                    if (optional_strokes_used >= optional_stroke_limit)
                        continue;

                    if (optional_gate < 255 && (int)((spatial_hash >> 21) & 255) > optional_gate)
                        continue;

                    optional_strokes_used++;

                    fiss_strength = clampi(fiss_strength, 0, 220);
                    fiss_len = clampi(fiss_len, 2, 42);

                    wba_draw_stroke(
                        c,
                        Y, U, V,
                        c->ring_y[write_slot],
                        c->ring_u[write_slot],
                        c->ring_v[write_slot],
                        rows,
                        draw_x, draw_y,
                        gx, gy,
                        fiss_strength,
                        fiss_len,
                        255,
                        0,
                        max_age,
                        motion_age / 2,
                        white_forge_q8,
                        fissure,
                        0,
                        0,
                        shape_hash ^ 0xb5297a4dU,
                        1,
                        long_dense_fast,
                        load_shed
                    );
                }
            }
        }
    }

    if (c->filled < WBA_MAX_FRAMES)
        c->filled++;

    c->frame++;
}
