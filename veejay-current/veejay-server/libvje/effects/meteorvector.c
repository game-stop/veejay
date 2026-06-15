/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include "meteorvector.h"
#include <stdint.h>

#define BONECOMETAUTOPSY_PARAMS 13
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
#define P_COMET_BUDGET  12

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
    return (uint8_t)bca_clampi(v, 0, 255);
}

static inline int bca_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline int bca_maxi(int a, int b)
{
    return a > b ? a : b;
}

static inline int bca_div255(int v)
{
    return ((v + 128) * 257) >> 16;
}

static inline int bca_soft_luma_ceiling(int y)
{
    if(y > 230) {
        y = 230 + ((y - 230) >> 2);
        if(y > 242)
            y = 242;
    }

    return y < 0 ? 0 : y;
}

static inline int bca_param1000_to_range(int v, int lo, int hi)
{
    v = bca_clampi(v, 0, 1000);
    return lo + (((hi - lo) * v + 500) / 1000);
}

static inline int bca_param1000_to_100(int v)
{
    v = bca_clampi(v, 0, 1000);
    return (v * 100 + 500) / 1000;
}

static inline int bca_percent_to_param1000(int v)
{
    v = bca_clampi(v, 0, 100);
    return (v * 1000 + 50) / 100;
}

static inline int bca_range_to_param1000(int v, int lo, int hi)
{
    v = bca_clampi(v, lo, hi);

    if(hi <= lo)
        return 0;

    return ((v - lo) * 1000 + ((hi - lo) >> 1)) / (hi - lo);
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
    unsigned int h = (unsigned int)x * 374761393U;

    h ^= (unsigned int)y * 668265263U;
    h ^= (unsigned int)z * 2246822519U;

    return bca_hash_u32(h);
}

static inline int bca_ramp255(int v, int range)
{
    if(v <= 0)
        return 0;
    if(v >= range)
        return 255;

    return (v * 255) / range;
}

static inline int bca_slot_for_age(int write_slot, int age)
{
    int slot = write_slot - age;

    if(slot < 0)
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

    if(alpha <= 0)
        return;

    if(alpha >= 255) {
        yy = sy;
        uu = su;
        vv = sv;
    }
    else {
        const int inv = 255 - alpha;

        yy = bca_div255((int)c->trail_y[idx] * inv + sy * alpha);
        uu = bca_div255((int)c->trail_u[idx] * inv + su * alpha);
        vv = bca_div255((int)c->trail_v[idx] * inv + sv * alpha);
    }

    c->trail_y[idx] = (uint8_t)bca_soft_luma_ceiling(yy);
    c->trail_u[idx] = (uint8_t)bca_clampi(uu, 0, 255);
    c->trail_v[idx] = (uint8_t)bca_clampi(vv, 0, 255);

    Y[idx] = c->trail_y[idx];
    U[idx] = c->trail_u[idx];
    V[idx] = c->trail_v[idx];
}

static inline void bca_make_bone_color(
    uint8_t *restrict srcY,
    uint8_t *restrict srcU,
    uint8_t *restrict srcV,
    int si,
    int forge_alpha,
    int chroma_gain_q8,
    int color_push,
    int *yy,
    int *uu,
    int *vv
) {
    int yv = srcY[si];
    int uv = srcU[si];
    int vv0 = srcV[si];

    yv += (((255 - yv) * forge_alpha) >> 8);

    if(chroma_gain_q8 <= 40) {
        uv = 128 + color_push;
        vv0 = 128 - color_push;
    }
    else {
        uv = 128 + (((uv - 128) * chroma_gain_q8) >> 8) + color_push;
        vv0 = 128 + (((vv0 - 128) * chroma_gain_q8) >> 8) - color_push;
    }

    *yy = bca_soft_luma_ceiling(yv);
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
    const int w = c->w;
    int fall_q16;

    radius = bca_clampi(radius, 1, 8);
    fall_q16 = (160 << 16) / (radius + 1);
    int forge_alpha = bca_clampi((white_forge_q8 * (strength + 96 + age * 8)) >> 8, 0, 255);
    int color_push = bca_clampi((color_bias * (strength + 64 + age * 18)) >> 8, -82, 82);

    for(int y = cy - radius; y <= cy + radius; y++) {
        const int dy = bca_absi(y - cy);

        if(y < 0 || y >= rows)
            continue;

        for(int x = cx - radius; x <= cx + radius; x++) {
            const int dx = bca_absi(x - cx);
            const int dist = dx + dy;
            int idx;
            int yy;
            int uu;
            int vv;
            int alpha;

            if(x < 0 || x >= w || dist > radius)
                continue;

            idx = y * w + x;
            alpha = strength + 48 - ((dist * fall_q16 + 32768) >> 16);
            alpha = bca_clampi(alpha, 0, 255);
            alpha = (alpha * opacity_q8 + 128) >> 8;

            if(alpha <= 0)
                continue;

            bca_make_bone_color(
                srcY, srcU, srcV,
                idx,
                forge_alpha,
                chroma_gain_q8,
                color_push,
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
    const int w = c->w;
    int tx = -gy;
    int ty = gx;
    int mag = bca_maxi(bca_absi(tx), bca_absi(ty));
    int dx_q8;
    int dy_q8;
    int nx_q8;
    int ny_q8;
    int total_len;
    int t_step;
    int bend_px;
    int age_fade;
    int sign;
    int denom;
    int taper_q16;
    int bend_q16;

    if(mag <= 0)
        return;

    sign = (shape_hash & 1) ? 1 : -1;

    tx *= sign;
    ty *= sign;

    dx_q8 = (tx * 256) / mag;
    dy_q8 = (ty * 256) / mag;
    nx_q8 = -dy_q8;
    ny_q8 = dx_q8;

    total_len = tail_length + ((tail_length * age) >> 1) + ((motion_launch * (age + 1)) >> 3);
    total_len = bca_clampi(total_len, 2, 112);

    t_step = fast_tail ? 2 : 1;

    bend_px = ((((int)((shape_hash >> 16) & 255)) - 128) *
               (motion_launch + age * 16)) / 1250;

    if(max_age > 0)
        age_fade = 255 - ((age * 100) / (max_age + 1));
    else
        age_fade = 255;

    age_fade = bca_clampi(age_fade, 108, 255);
    int forge_alpha = bca_clampi((white_forge_q8 * (strength + 96 + age * 8)) >> 8, 0, 255);
    int color_push = bca_clampi((color_bias * (strength + 64 + age * 18)) >> 8, -82, 82);
    denom = total_len + 1;
    taper_q16 = (220 << 16) / denom;
    bend_q16 = (bend_px << 16) / denom;

    for(int t = head_size; t <= total_len; t += t_step) {
        int taper = 255 - ((t * taper_q16 + 32768) >> 16);
        int curve_px = (t * bend_q16) >> 16;
        int base_x_q8 = (cx << 8) - dx_q8 * t + nx_q8 * curve_px;
        int base_y_q8 = (cy << 8) - dy_q8 * t + ny_q8 * curve_px;
        int src_x_q8 = (cx << 8) - dx_q8 * t;
        int src_y_q8 = (cy << 8) - dy_q8 * t;
        int width = 1;
        int q_fall_scale;

        taper = bca_clampi(taper, 18, 230);

        if(!fast_tail && head_size > 3 && t < (total_len >> 2))
            width = 2;

        q_fall_scale = width == 1 ? 48 : 32;

        for(int q = -width; q <= width; q++) {
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

            if(ox < 0 || ox >= w || oy < 0 || oy >= rows)
                continue;

            sx = bca_clampi(sx, 0, w - 1);
            sy = bca_clampi(sy, 0, rows - 1);

            oi = oy * w + ox;
            si = sy * w + sx;

            alpha = bca_div255(strength * taper);
            alpha = bca_div255(alpha * age_fade);

            q_fall = bca_absi(q) * q_fall_scale;
            alpha = bca_div255(alpha * (255 - q_fall));

            if(q == 0)
                alpha = bca_clampi(alpha + 10, 0, 255);

            if(fast_tail)
                alpha = bca_clampi((alpha * 297 + 128) >> 8, 0, 255);

            alpha = (alpha * opacity_q8 + 128) >> 8;

            if(alpha <= 0)
                continue;

            bca_make_bone_color(
                srcY, srcU, srcV,
                si,
                forge_alpha,
                chroma_gain_q8,
                color_push,
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

    if(!ve)
        return NULL;

    ve->num_params = BONECOMETAUTOPSY_PARAMS;
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

    ve->defaults[P_OPACITY] = 100;
    ve->defaults[P_STEP] = 3;
    ve->defaults[P_TIME_DEPTH] = 4;
    ve->defaults[P_HEAD_SIZE] = 3;
    ve->defaults[P_TAIL_LENGTH] = bca_range_to_param1000(56, 2, 112);
    ve->defaults[P_EDGE] = bca_percent_to_param1000(86);
    ve->defaults[P_MOTION_LAUNCH] = bca_percent_to_param1000(54);
    ve->defaults[P_COMET_DENSITY] = bca_percent_to_param1000(54);
    ve->defaults[P_WHITE_FORGE] = bca_percent_to_param1000(88);
    ve->defaults[P_TRAIL] = bca_percent_to_param1000(97);
    ve->defaults[P_STROKE_CHROMA] = bca_percent_to_param1000(10);
    ve->defaults[P_COLOR_BIAS] = bca_percent_to_param1000(92);
    ve->defaults[P_COMET_BUDGET] = 620;

    ve->limits[0][P_OPACITY] = 0;       ve->limits[1][P_OPACITY] = 100;
    ve->limits[0][P_STEP] = 3;          ve->limits[1][P_STEP] = 14;
    ve->limits[0][P_TIME_DEPTH] = 1;    ve->limits[1][P_TIME_DEPTH] = BCA_MAX_FRAMES;
    ve->limits[0][P_HEAD_SIZE] = 1;     ve->limits[1][P_HEAD_SIZE] = 8;
    ve->limits[0][P_TAIL_LENGTH] = 0;   ve->limits[1][P_TAIL_LENGTH] = 1000;
    ve->limits[0][P_EDGE] = 0;          ve->limits[1][P_EDGE] = 1000;
    ve->limits[0][P_MOTION_LAUNCH] = 0; ve->limits[1][P_MOTION_LAUNCH] = 1000;
    ve->limits[0][P_COMET_DENSITY] = 0; ve->limits[1][P_COMET_DENSITY] = 1000;
    ve->limits[0][P_WHITE_FORGE] = 0;   ve->limits[1][P_WHITE_FORGE] = 1000;
    ve->limits[0][P_TRAIL] = 0;         ve->limits[1][P_TRAIL] = 1000;
    ve->limits[0][P_STROKE_CHROMA] = 0; ve->limits[1][P_STROKE_CHROMA] = 1000;
    ve->limits[0][P_COLOR_BIAS] = 0;    ve->limits[1][P_COLOR_BIAS] = 1000;
    ve->limits[0][P_COMET_BUDGET] = 0; ve->limits[1][P_COMET_BUDGET] = 5000;

    ve->description = "Meteor Static";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

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
        "Color Bias",
        "Comet Budget"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                                                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,   -1000,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,    0,   -1000,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,                            3,   8,   56, 100,  70, 1100, 180,  98,
        VJ_BEAT_WINDOW_RADIUS,    VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,                            2,   8,   48, 100,  70,  850,  80,  84,
        VJ_BEAT_TRAIL_LENGTH,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         520,1000, 68, 100,  45,  620,   0, 100,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         760,1000, 62, 100,  45,  620,   0,  98,
        VJ_BEAT_MOTION_REACT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         420,1000, 72, 100,  35,  520,   0, 100,
        VJ_BEAT_DENSITY,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         500,1000, 66, 100,  45,  620,   0,  98,
        VJ_BEAT_GLOW,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         600, 940, 44,  94,  70,  900,   0,  80,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         900,1000, 56, 100,  70, 2400, 320, 100,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          80, 940, 52, 100,  70,  850,   0,  88,
        VJ_BEAT_COLOR_PHASE,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                         640,1000, 36,  88, 180, 1400,   0,  60,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );
    
    return ve;
}

void *meteorvector_malloc(int w, int h)
{
    meteorvector_t *c = (meteorvector_t *) vj_calloc(sizeof(meteorvector_t));
    const size_t len = (size_t)w * (size_t)h;
    const size_t total = len * ((size_t)BCA_MAX_FRAMES * 3u + 5u);
    uint8_t *p;

    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->filled = 0;
    c->n_threads = vje_advise_num_threads(w * h);

    c->region = (uint8_t *) vj_malloc(total);

    if(!c->region) {
        free(c);
        return NULL;
    }

    p = c->region;

    for(int s = 0; s < BCA_MAX_FRAMES; s++) {
        c->ring_y[s] = p; p += len;
        c->ring_u[s] = p; p += len;
        c->ring_v[s] = p; p += len;
    }

    c->stable_y = p; p += len;
    c->last_stable_y = p; p += len;
    c->trail_y = p; p += len;
    c->trail_u = p; p += len;
    c->trail_v = p;

    for(int s = 0; s < BCA_MAX_FRAMES; s++) {
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

void meteorvector_free(void *ptr)
{
    meteorvector_t *c = (meteorvector_t *) ptr;

    free(c->region);
    free(c);
}

void meteorvector_apply(void *ptr, VJFrame *frame, int *args)
{
    meteorvector_t *c = (meteorvector_t *) ptr;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = c->w;
    const int rows = c->h;
    const int process_len = c->len;
    const int write_slot = c->frame & (BCA_MAX_FRAMES - 1);

    uint8_t *restrict curY = c->ring_y[write_slot];
    uint8_t *restrict curU = c->ring_u[write_slot];
    uint8_t *restrict curV = c->ring_v[write_slot];
    uint8_t *restrict edgeY = c->stable_y;
    uint8_t *restrict lastEdgeY = c->last_stable_y;

    int opacity = bca_clampi(args[P_OPACITY], 0, 100);
    int step = bca_clampi(args[P_STEP], 3, 14);
    int time_depth = bca_clampi(args[P_TIME_DEPTH], 1, BCA_MAX_FRAMES);
    int head_size = bca_clampi(args[P_HEAD_SIZE], 1, 8);
    int tail_length = bca_param1000_to_range(args[P_TAIL_LENGTH], 2, 112);
    int edge_sens = bca_param1000_to_100(args[P_EDGE]);
    int motion_launch = bca_param1000_to_100(args[P_MOTION_LAUNCH]);
    int comet_density = bca_param1000_to_100(args[P_COMET_DENSITY]);
    int white_forge = bca_param1000_to_100(args[P_WHITE_FORGE]);
    int trail = bca_param1000_to_100(args[P_TRAIL]);
    int stroke_chroma = bca_param1000_to_100(args[P_STROKE_CHROMA]);
    int color_bias = bca_param1000_to_100(args[P_COLOR_BIAS]) - 50;
    int comet_budget = bca_clampi(args[P_COMET_BUDGET], 0, 1000);
    int chroma_gain_age_q8[BCA_MAX_FRAMES];

    const int opacity_q8 = (opacity * 255 + 50) / 100;
    const int white_forge_q8 = (white_forge * 256 + 50) / 100;
    const int trail_q8 = (trail * 255 + 50) / 100;
    int stroke_chroma_q8;

    if(stroke_chroma <= 50)
        stroke_chroma_q8 = (stroke_chroma * 256 + 25) / 50;
    else
        stroke_chroma_q8 = 256 + (((stroke_chroma - 50) * 384 + 25) / 50);

    for(int a = 0; a < BCA_MAX_FRAMES; a++) {
        const int base = 100 + a * 7;
        int gain_q8 = (base * 256 + 50) / 100;

        gain_q8 = (gain_q8 * stroke_chroma_q8 + 128) >> 8;
        chroma_gain_age_q8[a] = bca_clampi(gain_q8, 0, 2048);
    }

    int available = c->filled + 1;

    if(available > BCA_MAX_FRAMES)
        available = BCA_MAX_FRAMES;

    int max_age = time_depth - 1;

    if(max_age > available - 1)
        max_age = available - 1;

    const int tail_energy = bca_clampi(((tail_length - 2) * 100 + 55) / 110, 0, 100);
    const int launch_energy = bca_clampi((motion_launch * 3 + comet_density * 2 + tail_energy + (white_forge >> 1)) / 6, 0, 100);
    const int edge_threshold_base = 242 - edge_sens * 2 - (launch_energy / 3);
    int seed_floor = 30 - (edge_sens / 8) - (launch_energy / 16);
    int motion_min = 14 + ((100 - motion_launch) / 5) - (comet_density / 18) - (tail_energy / 24);
    const int tail_comp_q8 = bca_clampi((52 * 256) / (tail_length + 32), 96, 256);
    int motion_only_keep = 4 + (((comet_density / 5) + (motion_launch / 7) + (tail_energy / 9)) * tail_comp_q8 >> 8);
    const int reactive_edge_floor = bca_clampi(18 - (edge_sens / 10) - (launch_energy / 20), 4, 18);
    const int reactive_motion_floor = bca_clampi(11 + ((100 - motion_launch) / 10) - (comet_density / 25), 4, 20);
    const int reactive_seed_keep = bca_clampi(((launch_energy * (14 + (comet_density >> 2)) * tail_comp_q8 + 32768) >> 16), 0, 92);
    const int long_fast = (step <= 4 && tail_length >= 48 && comet_density >= 42);
    int row_comet_limit = 1 + ((comet_budget + 250) / 330);
    int comet_gate = 104 + ((comet_budget * 126 + 500) / 1000);

    if(step >= 7)
        row_comet_limit++;
    if(step >= 11)
        row_comet_limit++;
    if(step <= 4 && time_depth >= 7 && tail_length >= 96 && comet_density >= 90 && row_comet_limit > 1)
        row_comet_limit--;
    if(long_fast && comet_gate > 210)
        comet_gate = 210;
    row_comet_limit = bca_clampi(row_comet_limit, 1, 7);
    comet_gate = bca_clampi(comet_gate, 72, 255);

    seed_floor = bca_clampi(seed_floor, 6, 32);
    motion_min = bca_clampi(motion_min, 4, 38);
    motion_only_keep = bca_clampi(motion_only_keep, 4, 84);

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(int i = 0; i < process_len; i++) {
        const int raw_y = Y[i];
        const int old_stable = c->stable_y[i];
        int new_stable;
        int yy;
        int uu;
        int vv;

        curY[i] = Y[i];
        curU[i] = U[i];
        curV[i] = V[i];

        if(c->filled <= 0) {
            c->last_stable_y[i] = (uint8_t)raw_y;
            c->stable_y[i] = (uint8_t)raw_y;
            c->trail_y[i] = Y[i];
            c->trail_u[i] = U[i];
            c->trail_v[i] = V[i];
            continue;
        }

        c->last_stable_y[i] = (uint8_t)old_stable;

        {
            const int delta = bca_absi(raw_y - old_stable);
            const int w_new = (delta > 56) ? 6 : 3;

            new_stable = ((old_stable * (16 - w_new)) + raw_y * w_new + 8) >> 4;
        }

        c->stable_y[i] = bca_u8(new_stable);

        yy = (c->trail_y[i] * trail_q8) >> 8;
        uu = 128 + ((((int)c->trail_u[i] - 128) * trail_q8) >> 8);
        vv = 128 + ((((int)c->trail_v[i] - 128) * trail_q8) >> 8);

        c->trail_y[i] = (uint8_t)yy;
        c->trail_u[i] = (uint8_t)uu;
        c->trail_v[i] = (uint8_t)vv;

        Y[i] = (uint8_t)yy;
        U[i] = (uint8_t)uu;
        V[i] = (uint8_t)vv;
    }

    int ycell = 0;
    for(int y = 1; y < rows - 1; y += step, ycell++) {
        const int row = y * w;
        int row_phase = (ycell & 1) ? (step >> 1) : 0;
        int x_start = 1 + row_phase;
        int xcell = 0;
        int row_comets_used = 0;

        if(x_start >= w - 1)
            x_start = 1;

        for(int x = x_start; x < w - 1; x += step, xcell++) {
            const unsigned int spatial_hash = bca_hash3(xcell, ycell, 7331);

            if(row_comets_used >= row_comet_limit)
                continue;
            if(comet_gate < 255 && (int)((spatial_hash >> 24) & 255) > comet_gate)
                continue;

            const int idx = row + x;
            const int l = edgeY[idx - 1];
            const int r = edgeY[idx + 1];
            const int u0 = edgeY[idx - w];
            const int d = edgeY[idx + w];
            int gx = r - l;
            int gy = d - u0;
            const int edge = bca_absi(gx) + bca_absi(gy);
            int motion = 0;
            unsigned int shape_hash = bca_hash3(xcell, ycell, 9917);
            const int hnoise = (int)(spatial_hash & 63) - 31;
            int edge_threshold = edge_threshold_base + hnoise;
            int strength = 0;
            int accepted = 0;
            int motion_only = 0;

            edge_threshold = bca_clampi(edge_threshold, 32, 275);

            if(available > 1)
                motion = bca_absi((int)edgeY[idx] - (int)lastEdgeY[idx]);

            if(edge >= seed_floor) {
                const int edge_core = edge - edge_threshold;
                const int motion_boost = (motion * motion_launch) / 145;
                int keep;
                int density_hash;

                strength = bca_ramp255(edge_core + motion_boost, 180);
                keep = 12 + ((comet_density * tail_comp_q8) >> 7) + (motion_launch >> 3) + (strength >> 2);

                if(edge > 160)
                    keep += (edge - 160) >> 3;

                if(motion > 6)
                    keep += (motion * motion_launch) >> 9;

                if(strength > 210)
                    keep += 18;

                keep = bca_clampi(keep, 6, 230);
                density_hash = (int)((spatial_hash >> 8) & 255);

                if(density_hash <= keep)
                    accepted = 1;
            }
            else if(available > 1 && motion_launch > 0) {
                if(motion >= motion_min &&
                   (int)((spatial_hash >> 8) & 255) <= motion_only_keep) {
                    accepted = 1;
                    motion_only = 1;

                    gx = (int)((shape_hash >> 16) & 255) - 128;
                    gy = (int)((shape_hash >> 24) & 255) - 128;

                    if(gx == 0 && gy == 0)
                        gx = 1;

                    strength = 54 + ((motion * motion_launch) / 150) + (launch_energy >> 2);
                    strength = bca_clampi(strength, 0, 168);
                }
            }

            if(!accepted && launch_energy > 38 && available > 1) {
                const int reactive_hash = (int)((spatial_hash >> 18) & 255);
                const int edge_active = edge >= reactive_edge_floor;
                const int motion_active = motion >= reactive_motion_floor;
                int reactive_score = 0;
                int reactive_keep = reactive_seed_keep;

                if(edge_active)
                    reactive_score += bca_ramp255(edge - reactive_edge_floor, 180) >> 1;
                if(motion_active)
                    reactive_score += (motion * motion_launch) / 130;

                if(edge > 96)
                    reactive_keep += (edge - 96) >> 4;
                if(motion > 6)
                    reactive_keep += (motion * launch_energy) >> 10;
                if(edge_active && motion_active)
                    reactive_keep += 8;

                reactive_keep = bca_clampi(reactive_keep, 0, 112);

                if((edge_active || motion_active) && reactive_score > 0 && reactive_hash <= reactive_keep) {
                    accepted = 1;
                    motion_only = edge_active ? 0 : 1;
                    strength = 48 + reactive_score + (launch_energy >> 2);
                    strength = bca_clampi(strength, 0, 224);

                    if(!edge_active || (gx == 0 && gy == 0)) {
                        gx = (int)((shape_hash >> 16) & 255) - 128;
                        gy = (int)((shape_hash >> 24) & 255) - 128;

                        if(gx == 0 && gy == 0)
                            gx = 1;
                    }
                }
            }

            if(!accepted || strength <= 0)
                continue;

            row_comets_used++;

            int age;

            if(max_age > 0) {
                int motion_part = (motion * (motion_launch + (launch_energy >> 1)) * max_age) / 6000;
                int jitter_part = 0;

                if(motion > 3 &&
                   (int)((spatial_hash >> 16) & 255) < (comet_density + 40)) {
                    jitter_part = (int)((spatial_hash >> 24) & 3);
                }

                if(launch_energy > 70 && strength > 100 &&
                   (int)((spatial_hash >> 21) & 255) < (comet_density + 24))
                    jitter_part++;

                age = bca_clampi(motion_part + jitter_part, 0, max_age);
            }
            else {
                age = 0;
            }

            const int head_slot = write_slot;
            const int tail_slot = bca_slot_for_age(write_slot, age);
            int draw_x = x;
            int draw_y = y;
            int local_tail;
            int local_head;

            if(step > 3) {
                const int jlim = step / 3;
                const int jx = ((((int)(shape_hash & 15)) - 8) * jlim) / 8;
                const int jy = ((((int)((shape_hash >> 4) & 15)) - 8) * jlim) / 8;

                draw_x = bca_clampi(x + jx, 1, w - 2);
                draw_y = bca_clampi(y + jy, 1, rows - 2);
            }

            local_tail = tail_length + (((int)((shape_hash >> 6) & 15) - 7) * tail_length) / 64;
            local_tail = bca_clampi(local_tail, 2, 112);

            local_head = head_size;

            if(motion_only && local_head > 1)
                local_head--;

            if(!motion_only && motion > 10 && local_head < 8)
                local_head++;

            if(launch_energy > 72 && strength > 96 && local_head < 8)
                local_head++;

            if(launch_energy > 84 && strength > 160 && local_head < 8)
                local_head++;

            if(launch_energy > 66 && strength > 90) {
                int tail_boost = (local_tail * launch_energy) / 520;
                if(tail_boost > 18)
                    tail_boost = 18;
                local_tail = bca_clampi(local_tail + tail_boost, 2, 112);
            }

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

    if(c->filled < BCA_MAX_FRAMES)
        c->filled++;

    c->frame++;
}
