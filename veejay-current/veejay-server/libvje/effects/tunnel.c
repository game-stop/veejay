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
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define SIN_LUT_SIZE 4096
#define SIN_LUT_MASK 4095
#define SIN_LUT_MUL 651.898646904f
#define GAMMA_LUT_SIZE 1024

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)
#define FP_MASK (FP_ONE - 1)
#define FP_INV (1.0f / (float)FP_ONE)
#define TO_FP(x) ((int32_t)((x) * FP_ONE))
#define FROM_FP(x) ((float)(x) * FP_INV)

#define EH_PI 3.14159265358979323846f
#define EH_TWO_PI 6.28318530717958647692f
#define EH_INV_TWO_PI 0.15915494309189533577f

#define TUNNEL_PARAMS 12

#define P_SPEED        0
#define P_A            1
#define P_B            2
#define P_SWIRL        3
#define P_ZOOM         4
#define P_OFFSET       5
#define P_FEEDBACK     6
#define P_SHAPE        7
#define P_HIGH_QUALITY 8
#define P_TRAVEL_DRIVE 9
#define P_ZOOM_DRIVE  10
#define P_CHROMA_FLOW 11

enum {
    MODE_RECT = 0,
    MODE_CIRCLE,
    MODE_DIAMOND,
    MODE_STAR,
    MODE_FLOWER,
    MODE_FLOW_TURBULENCE
};

typedef struct {
    uint8_t *region;

    uint8_t *dstY;
    uint8_t *dstU;
    uint8_t *dstV;

    int *u_lut;
    int *v_lut;
    int *shade_lut;

    int *histY;
    int *histU;
    int *histV;

    float *warp_pos_lut;
    float *warp_neg_lut;
    float *wave_lut;

    float sin_lut[SIN_LUT_SIZE];

    uint8_t gamma_lut[GAMMA_LUT_SIZE];
    uint8_t gamma8[256];

    double time;

    int width;
    int height;
    int n_threads;

    int last_shape;

    float vel_state;
    float acc_state;
    float phase;
    float phase_vel;

    float drive_phase;

    float travel_drive_state;
    float zoom_drive_state;
    float chroma_flow_state;
    float feedback_state;
    int drive_state_ready;

    float zoom_state;
    float zoom_vel;
    float swirl_state;
    int state_ready;
} box_tunnel_t;

#define FAST_SIN_T(t, val) ((t)->sin_lut[(int)((val) * SIN_LUT_MUL) & SIN_LUT_MASK])

static inline int tunnel_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int tunnel_absi(int v)
{
    return (v < 0) ? -v : v;
}

static inline uint8_t clamp_u8_float(float v)
{
    int i = (int)(v + 0.5f);

    if(i < 0)
        return 0;

    if(i > 255)
        return 255;

    return (uint8_t)i;
}

static inline uint8_t clamp_u8_int(int v)
{
    if((unsigned int)v > 255U)
        return (v < 0) ? 0 : 255;

    return (uint8_t)v;
}

static inline int32_t frac_to_fp_fast(float x)
{
    int xi = (int)x;
    float f = x - (float)xi;

    if(f < 0.0f)
        f += 1.0f;

    return ((int32_t)(f * (float)FP_ONE)) & FP_MASK;
}

static inline int bilerp_i(int p00, int p10, int p01, int p11, int fx, int fy)
{
    int a = p00 + (((p10 - p00) * fx) >> FP_SHIFT);
    int b = p01 + (((p11 - p01) * fx) >> FP_SHIFT);

    return a + (((b - a) * fy) >> FP_SHIFT);
}

static inline void tunnel_limit_chroma_i(int *u, int *v, int limit)
{
    int au = tunnel_absi(*u);
    int av = tunnel_absi(*v);
    int m = (au > av) ? (au + (av >> 1)) : (av + (au >> 1));

    if(m > limit && m > 0) {
        const int scale = (limit << FP_SHIFT) / m;
        *u = (*u * scale) >> FP_SHIFT;
        *v = (*v * scale) >> FP_SHIFT;
    }
}

static inline int tunnel_soft_black_floor(int y)
{
    if(y <= 0)
        return 0;

    if(y < 5)
        return 5;

    if(y < 12)
        return 5 + ((y - 5) >> 1);

    return y;
}

static inline void sample_bilinear_yuv_fast(
    const uint8_t *srcY,
    const uint8_t *srcU,
    const uint8_t *srcV,
    int32_t u_fp,
    int32_t v_fp,
    int w,
    int h,
    int *outY,
    int *outU,
    int *outV)
{
    int32_t u = u_fp & FP_MASK;
    int32_t v = v_fp & FP_MASK;

    int32_t xf = u * (w - 1);
    int32_t yf = v * (h - 1);

    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;

    int fx = xf & FP_MASK;
    int fy = yf & FP_MASK;

    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;

    int row0 = y * w;
    int row1 = y1 * w;

    int i00 = row0 + x;
    int i10 = row0 + x1;
    int i01 = row1 + x;
    int i11 = row1 + x1;

    *outY = bilerp_i(srcY[i00], srcY[i10], srcY[i01], srcY[i11], fx, fy);

    *outU = bilerp_i(
        (int)srcU[i00] - 128,
        (int)srcU[i10] - 128,
        (int)srcU[i01] - 128,
        (int)srcU[i11] - 128,
        fx,
        fy
    );

    *outV = bilerp_i(
        (int)srcV[i00] - 128,
        (int)srcV[i10] - 128,
        (int)srcV[i01] - 128,
        (int)srcV[i11] - 128,
        fx,
        fy
    );
}

static inline void tunnel_write_pixel(
    box_tunnel_t *t,
    int i,
    int32_t accY,
    int32_t accU,
    int32_t accV,
    int32_t fb_fp,
    int32_t inv_fb_fp,
    int32_t chroma_fb_fp,
    int32_t inv_chroma_fp,
    int chroma_limit)
{
    int hy = (int)(((int64_t)accY * inv_fb_fp + (int64_t)t->histY[i] * fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);
    int hu = (int)(((int64_t)accU * inv_chroma_fp + (int64_t)t->histU[i] * chroma_fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);
    int hv = (int)(((int64_t)accV * inv_chroma_fp + (int64_t)t->histV[i] * chroma_fb_fp + (1LL << (FP_SHIFT - 1))) >> FP_SHIFT);

    t->histY[i] = hy;
    t->histU[i] = hu;
    t->histV[i] = hv;

    int y_val = hy >> FP_SHIFT;
    int u_val = hu >> FP_SHIFT;
    int v_val = hv >> FP_SHIFT;

    u_val = (u_val * 1056) >> 10;
    v_val = (v_val * 1056) >> 10;

    tunnel_limit_chroma_i(&u_val, &v_val, chroma_limit);

    y_val = tunnel_soft_black_floor(y_val);

    t->dstY[i] = t->gamma8[clamp_u8_int(y_val)];
    t->dstU[i] = clamp_u8_int(u_val + 128);
    t->dstV[i] = clamp_u8_int(v_val + 128);
}

static void tunnel_build_value_hints(vj_effect *ve)
{
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_SHAPE],
        P_SHAPE,
        "Rectangle",
        "Circle",
        "Diamond",
        "Star",
        "Flower",
        "Flow Turbulence"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_HIGH_QUALITY],
        P_HIGH_QUALITY,
        "Fast",
        "High Quality"
    );
}

static void tunnel_build_beat_hints(vj_effect *ve)
{
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SIGNED_SPEED,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,   0,   -1000,
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          12,                 96,                 62, 100,  55,  680, 0,     96,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                           4,                 52,                 44,  90, 120, 1100, 0,     72,
        VJ_BEAT_SIGNED_CURVE,  VJ_BEAT_F_CONTINUOUS,                                                   -82,                 82,                 54,  96,  80,  900, 0,     86,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,   0,   -1000,
        VJ_BEAT_GEOMETRY_PHASE,VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_WRAP,                                  0,                1600,                18,  64, 900, 3600, 800,   58,
        VJ_BEAT_MEMORY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          38,                 88,                 42,  92, 140, 1300, 120,   82,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,   0,   -1000,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,   0,   -1000,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                           0,                100,                82, 100,  45,  520, 0,    100,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,    0,    0,   0,   -1000,
        VJ_BEAT_COLOR_AMOUNT,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          80,                900,                58, 100,  60,  720, 0,     94
    );
}

static void precompute_warp_luts(box_tunnel_t *t)
{
    int w = t->width;
    int h = t->height;

    float cx = (float)w * 0.5f;
    float cy = (float)h * 0.5f;

    float inv_cx = 1.0f / cx;
    float inv_cy = 1.0f / cy;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int y = 0; y < h; y++) {
        float dy = ((float)y - cy) * inv_cy;
        int row = y * w;

        for(int x = 0; x < w; x++) {
            int i = row + x;
            float dx = ((float)x - cx) * inv_cx;
            float d = sqrtf(dx * dx + dy * dy);
            float ang = atan2f(dy, dx);
            float aterm = 0.3f * ang * EH_INV_TWO_PI;

            t->warp_pos_lut[i] = aterm + d * 0.5f;
            t->warp_neg_lut[i] = aterm - d * 0.5f;
            t->wave_lut[i] = d * 2.5f + dx * 1.7f - dy * 1.3f;
        }
    }
}

static void generate_geometry_work(box_tunnel_t *t, int shape)
{
    int w = t->width;
    int h = t->height;

    float cx = (float)w * 0.5f;
    float cy = (float)h * 0.5f;

    float inv_cx = 1.0f / cx;
    float inv_cy = 1.0f / cy;

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        float dy = ((float)y - cy) * inv_cy;
        int row = y * w;

        for(int x = 0; x < w; x++) {
            int i = row + x;
            float dx = ((float)x - cx) * inv_cx;

            float d = 0.0f;
            float u = 0.0f;

            switch(shape) {
                case MODE_CIRCLE:
                    d = sqrtf(dx * dx + dy * dy);
                    u = atan2f(dy, dx) * EH_INV_TWO_PI + 0.5f;
                    break;

                case MODE_DIAMOND:
                    d = fabsf(dx) + fabsf(dy);
                    u = atan2f(dy, dx) * EH_INV_TWO_PI + 0.5f;
                    break;

                case MODE_STAR:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx * dx + dy * dy);

                    float a = (ang + EH_PI) * 0.7957747f;
                    float tri = fabsf(2.0f * (a - floorf(a + 0.5f)));
                    float mod = 0.65f + 0.35f * tri;

                    d /= fmaxf(mod, 0.2f);
                    u = ang * EH_INV_TWO_PI + 0.5f;
                    break;
                }

                case MODE_FLOWER:
                {
                    float ang = atan2f(dy, dx);
                    d = sqrtf(dx * dx + dy * dy);

                    float petal = 0.75f + 0.25f * cosf(5.0f * ang);
                    d /= fmaxf(petal, 0.2f);

                    u = ang * EH_INV_TWO_PI + 0.5f;
                    break;
                }

                case MODE_FLOW_TURBULENCE:
                {
                    float gx = dx;
                    float gy = dy;

                    float w1 = sinf(gx * 2.1f + gy * 1.7f);
                    float w2 = cosf(gy * 2.3f - gx * 1.4f);

                    float wx = gx + 0.35f * w1;
                    float wy = gy + 0.35f * w2;

                    float ang = atan2f(wy, wx);
                    float r = sqrtf(wx * wx + wy * wy);
                    float swirl = sinf(3.0f * ang + r * 6.0f);

                    float fx = wx + 0.25f * swirl;
                    float fy = wy + 0.25f * swirl;

                    d = fx * fx + fy * fy;
                    u = fx * 0.5f + 0.5f;

                    t->v_lut[i] = (int)((1.0f / (1.0f + d * 3.0f)) * FP_ONE);
                    t->shade_lut[i] = (int)(fminf(1.0f, d * 2.5f) * FP_ONE);
                    t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);
                    continue;
                }

                case MODE_RECT:
                default:
                {
                    float ax = fabsf(dx);
                    float ay = fabsf(dy);

                    d = fmaxf(ax, ay);

                    if(d > 1e-6f) {
                        if(ax > ay)
                            u = (dx > 0.0f) ? (dy / ax) : (dy / ax + 4.0f);
                        else
                            u = (dy > 0.0f) ? (2.0f - dx / ay) : (6.0f + dx / ay);

                        u = (u + 2.0f) * 0.125f;
                    }

                    break;
                }
            }

            t->v_lut[i] = (int)(logf(d + 1e-6f) * FP_ONE);
            t->shade_lut[i] = (int)(fminf(1.0f, d * 5.0f) * FP_ONE);
            t->u_lut[i] = (int)((u - floorf(u)) * FP_ONE);
        }
    }

#pragma omp single
    t->last_shape = shape;
}

static void generate_geometry(box_tunnel_t *t, int shape)
{
#pragma omp parallel num_threads(t->n_threads)
    {
        generate_geometry_work(t, shape);
    }
}

vj_effect *tunnel_init(int width, int height)
{
    vj_effect *ve = (vj_effect*) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TUNNEL_PARAMS;

    ve->defaults  = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int*) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int*) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_SPEED] = -5;
    ve->defaults[P_A] = 40;
    ve->defaults[P_B] = 20;
    ve->defaults[P_SWIRL] = 0;
    ve->defaults[P_ZOOM] = 15;
    ve->defaults[P_OFFSET] = 0;
    ve->defaults[P_FEEDBACK] = 58;
    ve->defaults[P_SHAPE] = 1;
    ve->defaults[P_HIGH_QUALITY] = 1;
    ve->defaults[P_TRAVEL_DRIVE] = 650;
    ve->defaults[P_ZOOM_DRIVE] = 260;
    ve->defaults[P_CHROMA_FLOW] = 180;

    ve->limits[0][P_SPEED] = -100;       ve->limits[1][P_SPEED] = 100;
    ve->limits[0][P_A] = 0;              ve->limits[1][P_A] = 100;
    ve->limits[0][P_B] = 0;              ve->limits[1][P_B] = 100;
    ve->limits[0][P_SWIRL] = -100;       ve->limits[1][P_SWIRL] = 100;
    ve->limits[0][P_ZOOM] = 0;           ve->limits[1][P_ZOOM] = 400;
    ve->limits[0][P_OFFSET] = 0;         ve->limits[1][P_OFFSET] = 1000;
    ve->limits[0][P_FEEDBACK] = 0;       ve->limits[1][P_FEEDBACK] = 100;
    ve->limits[0][P_SHAPE] = 0;          ve->limits[1][P_SHAPE] = 5;
    ve->limits[0][P_HIGH_QUALITY] = 0;   ve->limits[1][P_HIGH_QUALITY] = 1;
    ve->limits[0][P_TRAVEL_DRIVE] = 0;   ve->limits[1][P_TRAVEL_DRIVE] = 1000;
    ve->limits[0][P_ZOOM_DRIVE] = 0;     ve->limits[1][P_ZOOM_DRIVE] = 1000;
    ve->limits[0][P_CHROMA_FLOW] = 0;    ve->limits[1][P_CHROMA_FLOW] = 1000;

    ve->description = "Tunnel";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Curve Int",
        "Curve Speed",
        "Swirl",
        "Zoom",
        "Offset",
        "Feedback",
        "Shape",
        "High Quality",
        "Travel Drive",
        "Zoom Drive",
        "Chroma Flow"
    );

    tunnel_build_value_hints(ve);
    tunnel_build_beat_hints(ve);

    return ve;
}

void *tunnel_malloc(int width, int height)
{
    box_tunnel_t *t = (box_tunnel_t*) vj_calloc(sizeof(box_tunnel_t));
    if(!t)
        return NULL;

    t->width = width;
    t->height = height;

    const int size = width * height;
    const size_t map_bytes = sizeof(int) * (size_t)size * 3u;
    const size_t hist_bytes = sizeof(int) * (size_t)size * 3u;
    const size_t dst_bytes = (size_t)size * 3u;
    const size_t warp_bytes = sizeof(float) * (size_t)size * 3u;
    const size_t total = map_bytes + hist_bytes + dst_bytes + warp_bytes + 128u;

    t->region = (uint8_t*) vj_malloc(total);
    if(!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *p = (uint8_t*)(((uintptr_t)t->region + 15u) & ~(uintptr_t)15u);

    t->u_lut = (int*)p;
    t->v_lut = t->u_lut + size;
    t->shade_lut = t->v_lut + size;

    p += map_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    t->histY = (int*)p;
    t->histU = t->histY + size;
    t->histV = t->histU + size;

    p += hist_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    t->dstY = p;
    t->dstU = t->dstY + size;
    t->dstV = t->dstU + size;

    p += dst_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    t->warp_pos_lut = (float*)p;
    t->warp_neg_lut = t->warp_pos_lut + size;
    t->wave_lut = t->warp_neg_lut + size;

    veejay_memset(t->histY, 0, hist_bytes);

    t->n_threads = vje_advise_num_threads(size);
    t->last_shape = -1;

    for(int i = 0; i < SIN_LUT_SIZE; i++)
        t->sin_lut[i] = sinf((float)i * EH_TWO_PI / (float)SIN_LUT_SIZE);

    for(int i = 0; i < GAMMA_LUT_SIZE; i++) {
        float val = (float)i / (float)(GAMMA_LUT_SIZE - 1);
        t->gamma_lut[i] = clamp_u8_float(powf(val, 0.85f) * 255.0f);
    }

    for(int i = 0; i < 256; i++)
        t->gamma8[i] = t->gamma_lut[(i * (GAMMA_LUT_SIZE - 1)) / 255];

    precompute_warp_luts(t);
    generate_geometry(t, MODE_RECT);

    return t;
}

void tunnel_free(void *ptr)
{
    box_tunnel_t *t = (box_tunnel_t*) ptr;

    free(t->region);
    free(t);
}

void tunnel_apply(void *ptr, VJFrame *frame, int *args)
{
    box_tunnel_t *t = (box_tunnel_t*) ptr;

    const int w = t->width;
    const int h = t->height;
    const int size = w * h;

    const int speed_arg = args[P_SPEED];
    const int p1_arg = args[P_A];
    const int p2_arg = args[P_B];
    const int swirl_arg = args[P_SWIRL];
    const int zoom_arg = args[P_ZOOM];
    const int offset_arg = args[P_OFFSET];
    int feedback = args[P_FEEDBACK];
    const int shape = args[P_SHAPE];
    const int high_quality = args[P_HIGH_QUALITY] ? 1 : 0;
    const int travel_drive = args[P_TRAVEL_DRIVE];
    const int zoom_drive = args[P_ZOOM_DRIVE];
    const int chroma_flow = args[P_CHROMA_FLOW];
    const int rebuild_shape = (shape != t->last_shape);

    const float travel_target = (float)travel_drive * 0.001f;
    const float zoom_target_drive = (float)zoom_drive * 0.001f;
    const float chroma_flow_target = (float)chroma_flow * 0.001f;
    const float feedback_target = (float)feedback;

    if(!t->drive_state_ready) {
        t->travel_drive_state = travel_target;
        t->zoom_drive_state = zoom_target_drive;
        t->chroma_flow_state = chroma_flow_target;
        t->feedback_state = feedback_target;
        t->drive_state_ready = 1;
    } else {
        t->travel_drive_state += (travel_target - t->travel_drive_state) * 0.115f;
        t->zoom_drive_state += (zoom_target_drive - t->zoom_drive_state) * 0.105f;
        t->chroma_flow_state += (chroma_flow_target - t->chroma_flow_state) * 0.105f;
        t->feedback_state += (feedback_target - t->feedback_state) * 0.075f;
    }

    const float travel_f = t->travel_drive_state;
    const float zoom_drive_f = t->zoom_drive_state;
    const float chroma_flow_f = t->chroma_flow_state;

    feedback = tunnel_clampi((int)(t->feedback_state + 0.5f), 0, 100);

    float raw_speed = (float)speed_arg * 0.01f;
    float base_vel = ((float)speed_arg * 0.005f) + (raw_speed * raw_speed * raw_speed * 0.15f);
    float drive_dir = (base_vel < 0.0f) ? -1.0f : 1.0f;
    float drive_vel = drive_dir * travel_f * 0.145f;
    float speed_target = base_vel + drive_vel;

    t->vel_state += (speed_target - t->vel_state) * 0.135f;
    t->time += t->vel_state;

    float ci_target = tanhf((float)p1_arg * 0.015f) * 0.75f;
    ci_target += travel_f * 0.125f * ((ci_target < 0.0f) ? -1.0f : 1.0f);
    t->acc_state += (ci_target - t->acc_state) * 0.070f;
    float curve_int = t->acc_state;

    float cs_input = (float)p2_arg * 0.01f;
    float cs_target = 0.2f * cs_input * cs_input + 0.02f;
    cs_target += travel_f * 0.032f;

    t->phase_vel += (cs_target - t->phase_vel) * 0.055f;
    float curve_spd = t->phase_vel;

    float swirl_target = tanhf((float)swirl_arg * 0.02f) * 1.2f;
    swirl_target += travel_f * 0.115f * ((swirl_target < 0.0f) ? -1.0f : 1.0f);
    t->swirl_state += (swirl_target - t->swirl_state) * 0.135f;
    float swirl = t->swirl_state;
    float swirl_sin = swirl * 0.7f;

    float zoom_base = ((float)zoom_arg * 0.01f) + 0.2f;

    if(!t->state_ready) {
        t->zoom_state = zoom_base;
        t->state_ready = 1;
    }

    float zoom_target = zoom_base * (1.0f + zoom_drive_f * 0.110f);
    float z_force = zoom_target - t->zoom_state;

    t->zoom_vel += z_force * 0.060f;
    t->zoom_vel *= 0.84f;
    t->zoom_state += t->zoom_vel;

    float zoom_breathe = 1.0f + zoom_drive_f * 0.030f * FAST_SIN_T(t, t->drive_phase * 0.37f + (float)t->time * 0.63f);
    float zoom = t->zoom_state * zoom_breathe;

    float po_target = (float)offset_arg * 0.002f;
    po_target += travel_f * 0.165f;
    t->phase += (po_target - t->phase) * 0.055f;
    float phase_offset = t->phase;

    t->drive_phase += t->vel_state * 0.35f + travel_f * 0.030f + zoom_drive_f * 0.012f;

    if(travel_f > 0.001f) {
        int fb_drop = (int)(travel_f * 8.0f + 0.5f);
        feedback = tunnel_clampi(feedback - fb_drop, 0, 96);
    }

    int two_layers = high_quality;

    int32_t fb_fp = TO_FP((float)feedback * 0.01f);
    int32_t inv_fb_fp = FP_ONE - fb_fp;

    int32_t chroma_fb_fp = (fb_fp * 3) >> 2;
    int32_t inv_chroma_fp = FP_ONE - chroma_fb_fp;

    uint8_t *srcY = frame->data[0];
    uint8_t *srcU = frame->data[1];
    uint8_t *srcV = frame->data[2];

    float timef = (float)t->time;

    float liss_x = cosf(timef * curve_spd * 2.0f) * curve_int;
    float liss_y = sinf(timef * curve_spd * 3.0f) * curve_int;

    float v_phase_time = timef * 0.5f;
    float swirl_time = timef * 0.8f;
    float layer_step = phase_offset * 0.25f;
    float chroma_phase = t->drive_phase + timef * 0.21f;

    int swirl_active = (swirl > 0.00001f || swirl < -0.00001f);
    int phase_active = (phase_offset > 0.00001f || phase_offset < -0.00001f);
    int layer_active = two_layers && (layer_step > 0.00001f || layer_step < -0.00001f);
    int chroma_active = chroma_flow_f > 0.0001f;
    int chroma_bias = (int)(chroma_flow_f * (3.0f + travel_f * 24.0f) + 0.5f);
    int chroma_limit = 112 - (int)(travel_f * 8.0f);

    if(chroma_limit < 96)
        chroma_limit = 96;

    const float *warp_lut = (t->vel_state >= 0.0f) ? t->warp_pos_lut : t->warp_neg_lut;
    const float *wave_lut = t->wave_lut;

    const int wm1 = w - 1;
    const int hm1 = h - 1;

#pragma omp parallel num_threads(t->n_threads)
    {
        if(rebuild_shape)
            generate_geometry_work(t, shape);

        if(high_quality) {
#pragma omp for schedule(static)
            for(int i = 0; i < size; i++) {
                float u_base = FROM_FP(t->u_lut[i]);
                float v_base = FROM_FP(t->v_lut[i]);

                float v = v_base * zoom + timef + liss_y;

                if(phase_active)
                    v += phase_offset * FAST_SIN_T(t, v_base * 4.0f + v_phase_time);

                float u = u_base + liss_x;

                if(swirl_active) {
                    u += swirl * warp_lut[i];
                    u += swirl_sin * FAST_SIN_T(t, wave_lut[i] + swirl_time);
                }

                int y0;
                int u0;
                int v0;

                int32_t u_fp = frac_to_fp_fast(u);
                int32_t v_fp = frac_to_fp_fast(v);

                sample_bilinear_yuv_fast(srcY, srcU, srcV, u_fp, v_fp, w, h, &y0, &u0, &v0);

                int32_t accY;
                int32_t accU;
                int32_t accV;

                if(layer_active) {
                    int y1;
                    int u1;
                    int v1;

                    v_fp = frac_to_fp_fast(v + layer_step);
                    sample_bilinear_yuv_fast(srcY, srcU, srcV, u_fp, v_fp, w, h, &y1, &u1, &v1);

                    accY = (int32_t)((y0 + y1) << (FP_SHIFT - 1));
                    accU = (int32_t)((u0 + u1) << (FP_SHIFT - 1));
                    accV = (int32_t)((v0 + v1) << (FP_SHIFT - 1));
                } else {
                    accY = (int32_t)y0 << FP_SHIFT;
                    accU = (int32_t)u0 << FP_SHIFT;
                    accV = (int32_t)v0 << FP_SHIFT;
                }

                if(chroma_active) {
                    int wave = (int)(FAST_SIN_T(t, wave_lut[i] * 0.85f + chroma_phase) * (float)chroma_bias);
                    accU += (int32_t)wave << FP_SHIFT;
                    accV -= (int32_t)(wave >> 1) << FP_SHIFT;
                }

                tunnel_write_pixel(t, i, accY, accU, accV, fb_fp, inv_fb_fp, chroma_fb_fp, inv_chroma_fp, chroma_limit);
            }
        } else {
#pragma omp for schedule(static)
            for(int i = 0; i < size; i++) {
                float u_base = FROM_FP(t->u_lut[i]);
                float v_base = FROM_FP(t->v_lut[i]);

                float v = v_base * zoom + timef + liss_y;

                if(phase_active)
                    v += phase_offset * FAST_SIN_T(t, v_base * 4.0f + v_phase_time);

                float u = u_base + liss_x;

                if(swirl_active) {
                    u += swirl * warp_lut[i];
                    u += swirl_sin * FAST_SIN_T(t, wave_lut[i] + swirl_time);
                }

                int32_t u_fp = frac_to_fp_fast(u);
                int32_t v_fp = frac_to_fp_fast(v);

                int tx = ((u_fp >> 8) * wm1) >> 8;
                int ty = ((v_fp >> 8) * hm1) >> 8;
                int si = ty * w + tx;

                int32_t accY = (int32_t)srcY[si] << FP_SHIFT;
                int32_t accU = ((int32_t)srcU[si] - 128) << FP_SHIFT;
                int32_t accV = ((int32_t)srcV[si] - 128) << FP_SHIFT;

                if(layer_active) {
                    v_fp = frac_to_fp_fast(v + layer_step);

                    tx = ((u_fp >> 8) * wm1) >> 8;
                    ty = ((v_fp >> 8) * hm1) >> 8;
                    si = ty * w + tx;

                    accY = (accY + ((int32_t)srcY[si] << FP_SHIFT)) >> 1;
                    accU = (accU + (((int32_t)srcU[si] - 128) << FP_SHIFT)) >> 1;
                    accV = (accV + (((int32_t)srcV[si] - 128) << FP_SHIFT)) >> 1;
                }

                if(chroma_active) {
                    int wave = (int)(FAST_SIN_T(t, wave_lut[i] * 0.85f + chroma_phase) * (float)chroma_bias);
                    accU += (int32_t)wave << FP_SHIFT;
                    accV -= (int32_t)(wave >> 1) << FP_SHIFT;
                }

                tunnel_write_pixel(t, i, accY, accU, accV, fb_fp, inv_fb_fp, chroma_fb_fp, inv_chroma_fp, chroma_limit);
            }
        }
    }

    veejay_memcpy(srcY, t->dstY, size);
    veejay_memcpy(srcU, t->dstU, size);
    veejay_memcpy(srcV, t->dstV, size);
}
