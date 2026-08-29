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
#include "shocksilk.h"

#define SHOCKSILK_PARAMS 10
#define SS_LUT_SIZE 1024
#define SS_LUT_MASK 1023
#define SS_TWO_PI 6.28318530718f
#define SS_INV_TWO_PI 0.15915494309f

#define P_DIFFUSION  0
#define P_SHOCK      1
#define P_COHERENCE  2
#define P_FLOW       3
#define P_TWIST      4
#define P_RIDGES     5
#define P_ITER       6
#define P_CHROMA     7
#define P_MIX        8
#define P_SPEED      9

typedef struct {
    int w;
    int h;
    int len;

    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *field[2];
    float sin_lut[SS_LUT_SIZE];
    float cos_lut[SS_LUT_SIZE];
    float phase;
} shocksilk_t;

static inline int ss_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float ss_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int ss_abs(int v)
{
    return v < 0 ? -v : v;
}

static inline float ss_absf(float v)
{
    return v < 0.0f ? -v : v;
}

static inline float ss_wrap(float v)
{
    if (v >= SS_TWO_PI || v < 0.0f) {
        int k = (int) (v * SS_INV_TWO_PI);
        v -= (float) k * SS_TWO_PI;
        if (v < 0.0f)
            v += SS_TWO_PI;
        else if (v >= SS_TWO_PI)
            v -= SS_TWO_PI;
    }
    return v;
}

static inline float ss_lut_sin(const shocksilk_t *t, float phase)
{
    float fidx = phase * ((float) SS_LUT_SIZE * SS_INV_TWO_PI);
    int idx0 = (int) fidx;
    float frac;
    int i0;
    int i1;

    if (fidx < 0.0f && (float) idx0 != fidx)
        idx0--;

    frac = fidx - (float) idx0;
    i0 = idx0 & SS_LUT_MASK;
    i1 = (i0 + 1) & SS_LUT_MASK;
    return t->sin_lut[i0] + (t->sin_lut[i1] - t->sin_lut[i0]) * frac;
}

static inline float ss_lut_cos(const shocksilk_t *t, float phase)
{
    float fidx = phase * ((float) SS_LUT_SIZE * SS_INV_TWO_PI);
    int idx0 = (int) fidx;
    float frac;
    int i0;
    int i1;

    if (fidx < 0.0f && (float) idx0 != fidx)
        idx0--;

    frac = fidx - (float) idx0;
    i0 = idx0 & SS_LUT_MASK;
    i1 = (i0 + 1) & SS_LUT_MASK;
    return t->cos_lut[i0] + (t->cos_lut[i1] - t->cos_lut[i0]) * frac;
}

static inline float ss_time_step(int speed)
{
    float u;
    float mag;

    if (speed == 0)
        return 0.0f;

    u = ss_clampf(ss_absf((float) speed) * 0.001f, 0.0f, 1.0f);
    mag = 0.00020f * (exp2f(11.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static inline void ss_sample_yuv(
    const uint8_t *restrict Y,
    const uint8_t *restrict U,
    const uint8_t *restrict V,
    float fx,
    float fy,
    int w,
    int h,
    int *oy,
    int *ou,
    int *ov
) {
    int x0;
    int y0;
    int x1;
    int y1;
    int wx;
    int wy;
    int p00;
    int p10;
    int p01;
    int p11;
    int a;
    int b;
    int nx;
    int ny;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);

    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    x0 = (int) fx;
    y0 = (int) fy;
    x1 = x0 + 1;
    y1 = y0 + 1;

    if (x1 >= w)
        x1 = w - 1;
    if (y1 >= h)
        y1 = h - 1;

    wx = (int) ((fx - (float) x0) * 256.0f);
    wy = (int) ((fy - (float) y0) * 256.0f);

    p00 = y0 * w + x0;
    p10 = y0 * w + x1;
    p01 = y1 * w + x0;
    p11 = y1 * w + x1;

    a = (int) Y[p00] * (256 - wx) + (int) Y[p10] * wx;
    b = (int) Y[p01] * (256 - wx) + (int) Y[p11] * wx;
    *oy = (a * (256 - wy) + b * wy + 32768) >> 16;

    nx = (int) (fx + 0.5f);
    ny = (int) (fy + 0.5f);
    if (nx >= w)
        nx = w - 1;
    if (ny >= h)
        ny = h - 1;

    p00 = ny * w + nx;
    *ou = U[p00];
    *ov = V[p00];
}

vj_effect *shocksilk_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = SHOCKSILK_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Shock Silk / Anisotropic Diffusion-Shock";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_DIFFUSION] = 68;
    ve->defaults[P_SHOCK]     = 34;
    ve->defaults[P_COHERENCE] = 84;
    ve->defaults[P_FLOW]      = 58;
    ve->defaults[P_TWIST]     = 10;
    ve->defaults[P_RIDGES]    = 18;
    ve->defaults[P_ITER]      = 2;
    ve->defaults[P_CHROMA]    = 8;
    ve->defaults[P_MIX]       = 76;
    ve->defaults[P_SPEED]     = 260;

    ve->limits[0][P_DIFFUSION] = 0;     ve->limits[1][P_DIFFUSION] = 100;
    ve->limits[0][P_SHOCK]     = 0;     ve->limits[1][P_SHOCK]     = 100;
    ve->limits[0][P_COHERENCE] = 0;     ve->limits[1][P_COHERENCE] = 100;
    ve->limits[0][P_FLOW]      = -100;  ve->limits[1][P_FLOW]      = 100;
    ve->limits[0][P_TWIST]     = -100;  ve->limits[1][P_TWIST]     = 100;
    ve->limits[0][P_RIDGES]    = 0;     ve->limits[1][P_RIDGES]    = 100;
    ve->limits[0][P_ITER]      = 1;     ve->limits[1][P_ITER]      = 4;
    ve->limits[0][P_CHROMA]    = 0;     ve->limits[1][P_CHROMA]    = 100;
    ve->limits[0][P_MIX]       = 0;     ve->limits[1][P_MIX]       = 100;
    ve->limits[0][P_SPEED]     = -1000; ve->limits[1][P_SPEED]     = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Silk Diffusion",
        "Shock Strength",
        "Orientation Coherence",
        "Tangential Flow",
        "Normal Twist",
        "Shock Ridges",
        "PDE Iterations",
        "Chroma Silk",
        "Silk Mix",
        "Time Scale"
    );

    return ve;
}

void *shocksilk_malloc(int w, int h)
{
    shocksilk_t *t = (shocksilk_t *) vj_calloc(sizeof(shocksilk_t));
    const size_t len = (size_t) w * (size_t) h;
    const size_t total = len * 5;
    uint8_t *base;
    size_t off = 0;

    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    base = (uint8_t *) t->region;
    t->src_y = base + off; off += len;
    t->src_u = base + off; off += len;
    t->src_v = base + off; off += len;
    t->field[0] = base + off; off += len;
    t->field[1] = base + off;

    for (int i = 0; i < SS_LUT_SIZE; i++) {
        float a = SS_TWO_PI * (float) i / (float) SS_LUT_SIZE;
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    return t;
}

void shocksilk_free(void *ptr)
{
    shocksilk_t *t = (shocksilk_t *) ptr;

    if (t) {
        free(t->region);
        free(t);
    }
}

void shocksilk_apply(void *ptr, VJFrame *frame, int *args)
{
    shocksilk_t *t = (shocksilk_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    const int w = t->w;
    const int h = t->h;
    const int len = t->len;
    const int diffusion = args[P_DIFFUSION];
    const int shock = args[P_SHOCK];
    const int coherence = args[P_COHERENCE];
    const int flow = args[P_FLOW];
    const int twist = args[P_TWIST];
    const int ridges = args[P_RIDGES];
    const int iterations = args[P_ITER];
    const int chroma = args[P_CHROMA];
    const int mix = args[P_MIX];
    int cur = 0;

#pragma omp for schedule(static)
    for (int plane = 0; plane < 3; plane++)
        veejay_memcpy(plane == 0 ? src_y : (plane == 1 ? src_u : src_v),
                      plane == 0 ? Y : (plane == 1 ? U : V), len);

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        int row = y * w;
        int rowm = ym * w;
        int rowp = yp * w;

        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            int i = row + x;
            t->field[0][i] = (uint8_t) (((int) src_y[i] * 4 + src_y[row + xm] + src_y[row + xp] + src_y[rowm + x] + src_y[rowp + x] + 4) >> 3);
        }
    }

    for (int pass = 0; pass < iterations; pass++) {
        const uint8_t *restrict in = t->field[cur];
        uint8_t *restrict out = t->field[cur ^ 1];

#pragma omp for schedule(static)
        for (int y = 0; y < h; y++) {
            int ym = y > 0 ? y - 1 : 0;
            int yp = y + 1 < h ? y + 1 : h - 1;
            int row = y * w;
            int rowm = ym * w;
            int rowp = yp * w;

            for (int x = 0; x < w; x++) {
                int xm = x > 0 ? x - 1 : 0;
                int xp = x + 1 < w ? x + 1 : w - 1;
                int i = row + x;
                int c = in[i];
                int l = in[row + xm];
                int r = in[row + xp];
                int u = in[rowm + x];
                int d = in[rowp + x];
                int ul = in[rowm + xm];
                int ur = in[rowm + xp];
                int dl = in[rowp + xm];
                int dr = in[rowp + xp];
                int gx = r - l;
                int gy = d - u;
                int ax = ss_abs(gx);
                int ay = ss_abs(gy);
                int tangent_avg;
                int iso = ((l + r + u + d + 2) >> 2) - c;
                int lap = l + r + u + d - (c << 2);
                int grad = ss_clampi(ax + ay, 0, 255);
                int diff_term;
                int shock_term;
                int value;

                if (ax > (ay << 1))
                    tangent_avg = ((u + d + 1) >> 1) - c;
                else if (ay > (ax << 1))
                    tangent_avg = ((l + r + 1) >> 1) - c;
                else if ((gx ^ gy) >= 0)
                    tangent_avg = ((ur + dl + 1) >> 1) - c;
                else
                    tangent_avg = ((ul + dr + 1) >> 1) - c;

                diff_term = (tangent_avg * coherence + iso * (100 - coherence)) / 100;
                diff_term = (diff_term * diffusion) / 118;

                if (lap > 0)
                    shock_term = -(grad * shock) / 170;
                else if (lap < 0)
                    shock_term = (grad * shock) / 170;
                else
                    shock_term = 0;

                value = c + diff_term + shock_term;
                out[i] = (uint8_t) ss_clampi(value, 0, 255);
            }
        }

        cur ^= 1;
    }

#pragma omp single
    t->phase = ss_wrap(t->phase + ss_time_step(args[P_SPEED]));

    {
        const uint8_t *restrict field = t->field[cur];
        uint8_t *restrict activity_map = t->field[cur ^ 1];

#pragma omp for schedule(static)
        for (int y = 0; y < h; y++) {
            int ym = y > 2 ? y - 3 : 0;
            int yp = y + 3 < h ? y + 3 : h - 1;
            int row = y * w;
            int rowm = ym * w;
            int rowp = yp * w;

            for (int x = 0; x < w; x++) {
                int xm = x > 2 ? x - 3 : 0;
                int xp = x + 3 < w ? x + 3 : w - 1;
                int i = row + x;
                int gx = (int) field[row + xp] - (int) field[row + xm];
                int gy = (int) field[rowp + x] - (int) field[rowm + x];
                int grad = ss_clampi(ss_abs(gx) + ss_abs(gy), 0, 255);
                int delta = ss_abs((int) field[i] - (int) src_y[i]);
                int ridge = (delta > 2 ? (delta - 2) * 9 : 0) + (grad > 52 ? (grad - 52) >> 1 : 0);
                activity_map[i] = (uint8_t) ss_clampi(ridge, 0, 255);
            }
        }
        const float flow_abs = (float) ss_abs(flow);
        const float twist_abs = (float) ss_abs(twist);
        const float flow_gain = (flow < 0 ? -1.0f : 1.0f) * flow_abs * (0.00018f + 0.0000028f * flow_abs);
        const float twist_gain = (twist < 0 ? -1.0f : 1.0f) * twist_abs * (0.00012f + 0.0000020f * twist_abs);
        const float ridge_gain = (float) ridges * 0.01f;
        const float chroma_gain = (float) chroma * 0.01f;
        const float mix_gain = (float) mix * 0.01f;
        const float phase = t->phase;

#pragma omp for schedule(static)
        for (int y = 0; y < h; y++) {
            int ym = y > 11 ? y - 12 : 0;
            int yp = y + 12 < h ? y + 12 : h - 1;
            int row = y * w;
            int rowm = ym * w;
            int rowp = yp * w;

            for (int x = 0; x < w; x++) {
                int xm = x > 11 ? x - 12 : 0;
                int xp = x + 12 < w ? x + 12 : w - 1;
                int i = row + x;
                int gx = (int) field[row + xp] - (int) field[row + xm];
                int gy = (int) field[rowp + x] - (int) field[rowm + x];
                int r2l = x > 1 ? x - 2 : 0;
                int r2r = x + 2 < w ? x + 2 : w - 1;
                int r2u = y > 1 ? y - 2 : 0;
                int r2d = y + 2 < h ? y + 2 : h - 1;
                int r4l = x > 3 ? x - 4 : 0;
                int r4r = x + 4 < w ? x + 4 : w - 1;
                int r4u = y > 3 ? y - 4 : 0;
                int r4d = y + 4 < h ? y + 4 : h - 1;
                int ridge = ((int) activity_map[i] * 4 +
                             activity_map[row + r2l] + activity_map[row + r2r] +
                             activity_map[r2u * w + x] + activity_map[r2d * w + x] +
                             activity_map[row + r4l] + activity_map[row + r4r] +
                             activity_map[r4u * w + x] + activity_map[r4d * w + x] + 6) / 12;
                float inv = 1.0f / (float) (ss_abs(gx) + ss_abs(gy) + 1);
                float nx = (float) gx * inv;
                float ny = (float) gy * inv;
                float tx = -ny;
                float ty = nx;
                float wave = ss_lut_sin(t, phase + (float) x * 0.0019f + (float) y * 0.0013f + (float) field[i] * 0.004f);
                float wave2 = ss_lut_cos(t, phase * 0.73f + (float) x * 0.0011f - (float) y * 0.0017f);
                float activity = ss_clampf(0.16f + (float) ridge * (1.0f / 360.0f), 0.0f, 1.0f);
                activity = activity * activity * (3.0f - 2.0f * activity);
                float along = flow_gain * (float) (w < h ? w : h) * activity * (0.72f + 0.28f * wave);
                float across = twist_gain * (float) (w < h ? w : h) * activity * wave2;
                float sx = (float) x + tx * along + nx * across;
                float sy = (float) y + ty * along + ny * across;
                int syv;
                int suv;
                int svv;
                int silk_y;
                int silk_u;
                int silk_v;
                int charge;

                ss_sample_yuv(src_y, src_u, src_v, sx, sy, w, h, &syv, &suv, &svv);

                {
                    int signed_shock = (int) field[i] - (int) src_y[i];
                    int crease = (int) ((float) signed_shock * ridge_gain * activity * 0.55f);
                    silk_y = ss_clampi(syv + crease, 0, 255);
                }

                charge = (int) ((float) ridge * chroma_gain * 0.32f * (0.55f + 0.45f * wave2));
                silk_u = ss_clampi(suv + (int) (wave * (float) charge * 0.35f), 0, 255);
                silk_v = ss_clampi(svv + (int) (wave2 * (float) charge * 0.40f), 0, 255);

                Y[i] = (uint8_t) ss_clampi((int) ((float) src_y[i] + ((float) silk_y - (float) src_y[i]) * mix_gain), 0, 255);
                U[i] = (uint8_t) ss_clampi((int) ((float) src_u[i] + ((float) silk_u - (float) src_u[i]) * mix_gain), 0, 255);
                V[i] = (uint8_t) ss_clampi((int) ((float) src_v[i] + ((float) silk_v - (float) src_v[i]) * mix_gain), 0, 255);
            }
        }
    }
}
