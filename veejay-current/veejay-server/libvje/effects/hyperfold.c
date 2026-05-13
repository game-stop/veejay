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
#include "hyperfold.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define AFM_PARAMS 8

#define P_MODE      0
#define P_MINWIDTH  1
#define P_MAXWIDTH  2
#define P_OFFSET    3
#define P_SEAM      4
#define P_SPEED     5
#define P_MONO      6
#define P_BG        7

#define AFM_MODE_VERTICAL             0
#define AFM_MODE_BARCODE             1
#define AFM_MODE_VENETIAN_FAN        2
#define AFM_MODE_ELASTIC_STRIP       3
#define AFM_MODE_HORIZONTAL           4
#define AFM_MODE_LETTERBOX            5
#define AFM_MODE_STAIRCASE            6
#define AFM_MODE_WAVE_BARS            7
#define AFM_MODE_SERPENTINE_SLITS     8
#define AFM_MODE_TORN_POSTER          9
#define AFM_MODE_DIAGONAL_SLATS       10
#define AFM_MODE_ANTIDIAGONAL_SLATS   11
#define AFM_MODE_SLICED_LENS          12
#define AFM_MODE_SLIT_SCAN            13
#define AFM_MODE_SQUARE               14
#define AFM_MODE_BROKEN_WINDOW        15
#define AFM_MODE_SPLIT_PORTRAIT       16
#define AFM_MODE_CENTER_STACK         17
#define AFM_MODE_CONTACT_SHEET        18
#define AFM_MODE_CASCADE_COLLAGE      19
#define AFM_MODE_PRISM_STACK          20
#define AFM_MODE_FILM_GATE            21
#define AFM_MODE_ORBIT_COLLAGE        22
#define AFM_MODE_ROUND                23
#define AFM_MODE_IRIS                 24
#define AFM_MODE_SHUTTER              25
#define AFM_MODE_PINWHEEL_PANELS      26
#define AFM_MODE_CORNER_KALEIDO       27
#define AFM_MODE_CORNER_PULL          28
#define AFM_MODE_TRIANGLE_MIRROR      29
#define AFM_MODE_HEX_MIRROR           30
#define AFM_MODE_TUNNEL               31
#define AFM_MODE_TUNNEL_XL            32
#define AFM_MODE_SPIRAL_STAIR         33
#define AFM_MODE_QUAD_PORTAL          34
#define AFM_MODE_MOEBIUS_RIBBON       35
#define AFM_MODE_ACCORDION            36
#define AFM_MODE_HOURGLASS            37
#define AFM_MODE_SPIRAL_SHARDS        38
#define AFM_MODE_SLIDING_DOORS        39
#define AFM_MODE_DIAMOND_TUNNEL       40
#define AFM_MODE_FAN_BLADES           41
#define AFM_MODE_WATERFALL_STRIPS     42
#define AFM_MODE_NESTED_FILM_GATES    43
#define AFM_MODE_VORONOI_PLATES       44
#define AFM_MODE_POLAR_BARCODE        45
#define AFM_MODE_RIBBON_LATTICE       46
#define AFM_MODE_WOVEN_MIRROR         47
#define AFM_MODE_CORNER_TUNNEL        48
#define AFM_MODE_LENS_ARRAY           49
#define AFM_MODE_AXIS_ROULETTE        50
#define AFM_MODE_LAST                 AFM_MODE_AXIS_ROULETTE

#define AFM_MAX_PANELS 64

#define AFM_TRIG_SIZE 1024
#define AFM_TRIG_MASK 1023
#define AFM_TWO_PI 6.28318530718f
#define AFM_INV_TWO_PI 0.15915494309f

typedef struct {
    float x;
    float y;
    float ww;
    float hh;
    float inv_w;
    float inv_h;
    float src_x;
    float src_y;
    float src_w;
    float src_h;
    int x0;
    int y0;
    int x1;
    int y1;
    uint8_t flip_x;
    uint8_t flip_y;
    uint8_t mono;
    uint8_t round;
    uint8_t shade;
} afm_panel_t;

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int frame;
    int n_threads;

    void *region;

    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;

    float *x_src;
    uint8_t *x_mask;
    uint8_t *x_edge;
    float *y_src;
    uint8_t *y_mask;
    uint8_t *y_edge;

    int *x_i0;
    int *x_i1;
    int *y_i0;
    int *y_i1;
    int *x_n;
    int *y_n;
    uint8_t *x_w;
    uint8_t *y_w;

    int panel_count;
    afm_panel_t panels[AFM_MAX_PANELS];

    float sin_lut[AFM_TRIG_SIZE];
    uint8_t tone_lut[256];

    int lut_valid;
    int last_strength;
    int skip_base_panel;
    float time;
} mirrormadness_t;

static inline int afm_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float afm_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int afm_absi(int v)
{
    int m = v >> 31;
    return (v + m) ^ m;
}

static inline float afm_absf(float x)
{
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i &= 0x7fffffffU;
    return u.f;
}

static inline uint8_t afm_u8i(int v)
{
    return (uint8_t) afm_clampi(v, 0, 255);
}

static inline float afm_lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float afm_smooth01(float t)
{
    t = afm_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline int afm_floor_to_int(float v)
{
    int i = (int) v;
    if (v < (float) i)
        i--;
    return i;
}

static inline size_t afm_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

static inline float afm_wrap_2pi(float v)
{
    if (v >= AFM_TWO_PI || v < 0.0f) {
        int k = (int) (v * AFM_INV_TWO_PI);
        v -= (float) k * AFM_TWO_PI;
        if (v < 0.0f)
            v += AFM_TWO_PI;
        else if (v >= AFM_TWO_PI)
            v -= AFM_TWO_PI;
    }
    return v;
}

static inline uint32_t afm_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float afm_hash01(uint32_t x)
{
    return (float) (afm_hash_u32(x) & 0xffffU) * (1.0f / 65535.0f);
}



static inline float afm_lut_sin(const mirrormadness_t *m, float phase)
{
    float fidx = phase * ((float) AFM_TRIG_SIZE * AFM_INV_TWO_PI);
    int idx0 = afm_floor_to_int(fidx);
    float frac = fidx - (float) idx0;
    int i0 = idx0 & AFM_TRIG_MASK;
    int i1 = (i0 + 1) & AFM_TRIG_MASK;
    float a = m->sin_lut[i0];
    float b = m->sin_lut[i1];
    return a + (b - a) * frac;
}





static inline float afm_reflect_coord(float v, float maxv)
{
    float period;
    int k;

    if (maxv <= 0.0f)
        return 0.0f;

    period = maxv * 2.0f;
    v = fmodf(v, period);

    if (v < 0.0f)
        v += period;

    k = v > maxv;
    return k ? period - v : v;
}

static inline int afm_sample_y_bilinear(const uint8_t *restrict Y, float fx, float fy, int w, int h)
{
    int ix;
    int iy;
    int wx;
    int wy;
    int x1;
    int y1;
    int p00;
    int p10;
    int p01;
    int p11;
    int a;
    int b;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);

    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    ix = (int) fx;
    iy = (int) fy;
    wx = (int) ((fx - (float) ix) * 256.0f);
    wy = (int) ((fy - (float) iy) * 256.0f);

    x1 = ix + 1;
    if (x1 >= w)
        x1 = ix;
    y1 = iy + 1;
    if (y1 >= h)
        y1 = iy;

    p00 = iy * w + ix;
    p10 = iy * w + x1;
    p01 = y1 * w + ix;
    p11 = y1 * w + x1;

    a = (int) Y[p00] * (256 - wx) + (int) Y[p10] * wx;
    b = (int) Y[p01] * (256 - wx) + (int) Y[p11] * wx;

    return ((a * (256 - wy) + b * wy) + 32768) >> 16;
}

static inline int afm_sample_nearest(const uint8_t *restrict P, float fx, float fy, int w, int h)
{
    int ix;
    int iy;

    if (fx < 0.0f)
        fx = 0.0f;
    else if (fx > (float) (w - 1))
        fx = (float) (w - 1);

    if (fy < 0.0f)
        fy = 0.0f;
    else if (fy > (float) (h - 1))
        fy = (float) (h - 1);

    ix = afm_floor_to_int(fx + 0.5f);
    iy = afm_floor_to_int(fy + 0.5f);

    if (ix < 0)
        ix = 0;
    else if (ix >= w)
        ix = w - 1;

    if (iy < 0)
        iy = 0;
    else if (iy >= h)
        iy = h - 1;

    return P[iy * w + ix];
}

static void afm_build_luts(mirrormadness_t *m, int strength)
{
    int i;
    float contrast;

    if (m->lut_valid && m->last_strength == strength)
        return;

    contrast = 0.94f + (float) strength * 0.0026f;

    for (i = 0; i < 256; i++) {
        float t = (float) i * (1.0f / 255.0f);
        int yv;
        t = (t - 0.5f) * contrast + 0.5f;
        t = afm_clampf(t, 0.0f, 1.0f);
        yv = (int) (t * 255.0f + 0.5f);
        m->tone_lut[i] = afm_u8i(yv);
    }

    m->last_strength = strength;
    m->lut_valid = 1;
}

static inline void afm_fill_full_segment(float *src,
                                        uint8_t *mask,
                                        uint8_t *edge,
                                        int n,
                                        float start,
                                        float width,
                                        float src_center,
                                        float src_width,
                                        int flip,
                                        float seam_width)
{
    int x0 = afm_clampi((int) floorf(start), 0, n - 1);
    int x1 = afm_clampi((int) ceilf(start + width), 0, n - 1);
    int x;
    float inv_w;

    if (width <= 1.0f)
        return;

    inv_w = 1.0f / width;
    if (seam_width < 1.0f)
        seam_width = 1.0f;

    for (x = x0; x <= x1; x++) {
        float t = ((float) x + 0.5f - start) * inv_w;
        float mt;
        float d;
        int e;

        if (t < 0.0f || t > 1.0f)
            continue;

        mt = flip ? 1.0f - t : t;
        src[x] = afm_reflect_coord(src_center + (mt - 0.5f) * src_width, (float) (n - 1));
        mask[x] = 255;

        d = t < 1.0f - t ? t : 1.0f - t;
        e = (int) ((1.0f - afm_smooth01(afm_clampf(d * width / seam_width, 0.0f, 1.0f))) * 255.0f + 0.5f);
        if (e > edge[x])
            edge[x] = (uint8_t) e;
    }
}


static inline float afm_width_from_param(int n, float t)
{
    float domain = (float) n;
    float min_px = 2.0f;
    float max_px = domain * 0.46f;
    if (max_px < 8.0f)
        max_px = 8.0f;
    if (max_px > domain * 0.92f)
        max_px = domain * 0.92f;
    t = afm_clampf(t, 0.0f, 1.0f);
    return min_px + (max_px - min_px) * t * t;
}

static void afm_build_axis_map(mirrormadness_t *m,
                               int layer,
                               int horizontal_axis,
                               int n,
                               float min_frac,
                               float max_frac,
                               float offset,
                               float time,
                               float *src,
                               uint8_t *mask,
                               uint8_t *edge)
{
    uint32_t seed = (uint32_t) (0x9e3779b9U + (uint32_t) layer * 0x85ebca6bU + (horizontal_axis ? 0x51ed270bU : 0x27d4eb2fU));
    float domain = (float) n;
    float min_w;
    float max_w;
    float avg;
    float spread;
    float range_t;
    float scroll;
    float pos;
    int i;
    int strip;

    min_frac = afm_clampf(min_frac, 0.0f, 1.0f);
    max_frac = afm_clampf(max_frac, 0.0f, 1.0f);

    min_w = afm_width_from_param(n, min_frac);
    max_w = afm_width_from_param(n, max_frac);

    if (max_w < min_w) {
        float tmp = max_w;
        max_w = min_w;
        min_w = tmp;
    }

    spread = max_w - min_w;
    if (spread < 0.50f)
        spread = 0.0f;

    avg = (spread <= 0.0f) ? min_w : (min_w + spread * 0.34f);
    if (avg < 2.0f)
        avg = 2.0f;

    range_t = afm_clampf(spread / (domain * 0.30f + 1.0f), 0.0f, 1.0f);

    for (i = 0; i < n; i++) {
        src[i] = (float) i;
        mask[i] = 0;
        edge[i] = 0;
    }

    scroll = offset * domain * (0.42f + 0.12f * (float) (layer & 3));
    scroll += time * domain * (0.004f + 0.0018f * (float) layer);
    scroll += afm_lut_sin(m, time * (0.62f + 0.07f * (float) layer) + (float) horizontal_axis) * avg * range_t * 0.28f;

    pos = fmodf(scroll, avg * (1.10f + range_t * 0.45f));
    if (pos > 0.0f)
        pos -= avg * (1.10f + range_t * 0.45f);
    pos -= avg * 0.55f;

    strip = 0;
    while (pos < domain + avg * 2.0f && strip < 192) {
        float r0 = afm_hash01(seed ^ ((uint32_t) strip * 0x632be59bU));
        float r1 = afm_hash01(seed ^ ((uint32_t) strip * 0x85157af5U));
        float r2 = afm_hash01(seed ^ ((uint32_t) strip * 0x58f38dedU));
        float r3 = afm_hash01(seed ^ ((uint32_t) strip * 0xc2b2ae35U));
        float shaped;
        float width;
        float center;
        float side;
        float src_center;
        float src_width;
        float seam;
        int flip;

        if (spread <= 0.0f) {
            shaped = 0.0f;
            width = min_w;
        }
        else {
            if (r3 < 0.24f)
                shaped = r0 * r0 * 0.055f;
            else if (r3 > 0.86f)
                shaped = 0.68f + r1 * 0.32f;
            else
                shaped = r0 * r0 * r0 * 0.58f + r2 * 0.10f;

            width = min_w + spread * shaped;
            if (width < min_w)
                width = min_w;
            if (width > max_w)
                width = max_w;
        }

        center = pos + width * 0.5f;
        side = center - domain * 0.5f;
        src_width = width * (0.95f + r1 * 0.58f);
        src_center = domain * 0.5f - side * (0.92f + range_t * 0.28f);
        src_center += (r0 - 0.5f) * avg * range_t * 0.72f;
        src_center += afm_lut_sin(m, time + (float) strip * 0.61f) * avg * range_t * 0.14f;
        flip = ((strip + layer + horizontal_axis) & 1);
        seam = 0.75f + width * (0.012f + range_t * 0.008f);

        afm_fill_full_segment(src, mask, edge, n, pos, width, src_center, src_width, flip, seam);
        pos += width;
        strip++;
    }
}


static void afm_prepare_axis_index(const float *restrict src,
                                   const uint8_t *restrict mask,
                                   int n,
                                   int *restrict i0,
                                   int *restrict i1,
                                   uint8_t *restrict frac,
                                   int *restrict nearest)
{
    int i;

    for (i = 0; i < n; i++) {
        float f = src[i];
        int a;
        int b;
        int wx;
        int ni;

        if (mask[i] == 0) {
            i0[i] = 0;
            i1[i] = 0;
            frac[i] = 0;
            nearest[i] = 0;
            continue;
        }

        if (f < 0.0f)
            f = 0.0f;
        else if (f > (float) (n - 1))
            f = (float) (n - 1);

        a = (int) f;
        b = a + 1;
        if (b >= n)
            b = a;

        wx = (int) ((f - (float) a) * 256.0f);
        if (wx < 0)
            wx = 0;
        else if (wx > 255)
            wx = 255;

        ni = (int) (f + 0.5f);
        if (ni < 0)
            ni = 0;
        else if (ni >= n)
            ni = n - 1;

        i0[i] = a;
        i1[i] = b;
        frac[i] = (uint8_t) wx;
        nearest[i] = ni;
    }
}

static void afm_render_vertical_fast(mirrormadness_t *m,
                                     VJFrame *frame,
                                     float seam_t)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const uint8_t *restrict mask = m->x_mask;
    const uint8_t *restrict edge = m->x_edge;
    const int *restrict i0 = m->x_i0;
    const int *restrict i1 = m->x_i1;
    const uint8_t *restrict wxp = m->x_w;
    const int *restrict nxp = m->x_n;
    const uint8_t *restrict tone = m->tone_lut;
    const int w = m->w;
    const int h = m->h;
    const int bg_y = 3 + (int) (seam_t * 4.0f);
    const int seam_gain = (int) (6.0f + seam_t * 62.0f + 0.5f);
    int y;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        const uint8_t *restrict syrow = sy + row;
        const uint8_t *restrict surow = su + row;
        const uint8_t *restrict svrow = sv + row;
        uint8_t *restrict dy = Y + row;
        uint8_t *restrict du = U + row;
        uint8_t *restrict dv = V + row;

        for (x = 0; x < w; x++) {
            if (mask[x] == 0) {
                dy[x] = (uint8_t) bg_y;
                du[x] = 128;
                dv[x] = 128;
            }
            else {
                int wx = wxp[x];
                int inv = 256 - wx;
                int yy = ((int) syrow[i0[x]] * inv + (int) syrow[i1[x]] * wx + 128) >> 8;
                yy = tone[yy] - (((int) edge[x] * seam_gain + 127) / 255);
                dy[x] = afm_u8i(yy);
                du[x] = surow[nxp[x]];
                dv[x] = svrow[nxp[x]];
            }
        }
    }
}

static void afm_render_horizontal_fast(mirrormadness_t *m,
                                       VJFrame *frame,
                                       float seam_t)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const uint8_t *restrict mask = m->y_mask;
    const uint8_t *restrict edge = m->y_edge;
    const int *restrict i0 = m->y_i0;
    const int *restrict i1 = m->y_i1;
    const uint8_t *restrict wyp = m->y_w;
    const int *restrict nyp = m->y_n;
    const uint8_t *restrict tone = m->tone_lut;
    const int w = m->w;
    const int h = m->h;
    const int bg_y = 3 + (int) (seam_t * 4.0f);
    const int seam_gain = (int) (6.0f + seam_t * 62.0f + 0.5f);
    int y;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        uint8_t *restrict dy = Y + row;
        uint8_t *restrict du = U + row;
        uint8_t *restrict dv = V + row;

        if (mask[y] == 0) {
            for (x = 0; x < w; x++) {
                dy[x] = (uint8_t) bg_y;
                du[x] = 128;
                dv[x] = 128;
            }
        }
        else {
            int wy = wyp[y];
            int inv = 256 - wy;
            int row0 = i0[y] * w;
            int row1 = i1[y] * w;
            int rown = nyp[y] * w;
            int shade = (((int) edge[y] * seam_gain + 127) / 255);

            for (x = 0; x < w; x++) {
                int yy = ((int) sy[row0 + x] * inv + (int) sy[row1 + x] * wy + 128) >> 8;
                yy = tone[yy] - shade;
                dy[x] = afm_u8i(yy);
                du[x] = su[rown + x];
                dv[x] = sv[rown + x];
            }
        }
    }
}

static void afm_build_strip_maps(mirrormadness_t *m,
                                 int mode,
                                 float min_width_t,
                                 float max_width_t,
                                 float offset,
                                 float time)
{
    if (mode == AFM_MODE_HORIZONTAL) {
        afm_build_axis_map(m, 0, 1, m->h, min_width_t, max_width_t, -offset * 0.83f, time * 0.91f,
                           m->y_src,
                           m->y_mask,
                           m->y_edge);
    }
    else {
        afm_build_axis_map(m, 0, 0, m->w, min_width_t, max_width_t, offset, time,
                           m->x_src,
                           m->x_mask,
                           m->x_edge);
    }
}



static inline void afm_sample_yuv_layer(const uint8_t *restrict sy,
                                        const uint8_t *restrict su,
                                        const uint8_t *restrict sv,
                                        int w,
                                        int h,
                                        float sx,
                                        float syf,
                                        int *oy,
                                        int *ou,
                                        int *ov);

static inline int afm_size_from_range(float base, float min_px, float max_px)
{
    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }
    if (base < min_px)
        base = min_px;
    if (base > max_px)
        base = max_px;
    return (int) (base + 0.5f);
}

static void afm_add_panel(mirrormadness_t *m,
                          float x,
                          float y,
                          float ww,
                          float hh,
                          float src_x,
                          float src_y,
                          float src_w,
                          float src_h,
                          int flip_x,
                          int flip_y,
                          int mono,
                          int round,
                          int shade)
{
    afm_panel_t *p;

    if (m->panel_count >= AFM_MAX_PANELS)
        return;

    if (m->skip_base_panel &&
        x <= 0.0f && y <= 0.0f &&
        ww >= (float) m->w && hh >= (float) m->h)
        return;

    if (ww < 4.0f || hh < 4.0f)
        return;

    p = &m->panels[m->panel_count++];
    p->x = x;
    p->y = y;
    p->ww = ww;
    p->hh = hh;
    p->inv_w = 1.0f / ww;
    p->inv_h = 1.0f / hh;
    p->src_x = src_x;
    p->src_y = src_y;
    p->src_w = src_w;
    p->src_h = src_h;
    p->x0 = afm_clampi((int) floorf(x), 0, m->w - 1);
    p->y0 = afm_clampi((int) floorf(y), 0, m->h - 1);
    p->x1 = afm_clampi((int) ceilf(x + ww), 0, m->w);
    p->y1 = afm_clampi((int) ceilf(y + hh), 0, m->h);
    p->flip_x = (uint8_t) (flip_x ? 1 : 0);
    p->flip_y = (uint8_t) (flip_y ? 1 : 0);
    p->mono = (uint8_t) (mono ? 1 : 0);
    p->round = (uint8_t) (round ? 1 : 0);
    p->shade = (uint8_t) afm_clampi(shade, 0, 255);
}

static void afm_build_panel_layout(mirrormadness_t *m,
                                   int mode,
                                   float min_width_t,
                                   float max_width_t,
                                   float offset_t,
                                   float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float ox = offset_t * (float) w * 0.105f;
    float oy = -offset_t * (float) h * 0.070f;
    float drift = afm_lut_sin(m, time * 0.73f) * min_dim * 0.018f;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    (void) mode;

    m->panel_count = 0;

    {
        int big_w = afm_size_from_range((float) w * 0.58f, min_px, max_px * 1.55f);
        int big_h = afm_size_from_range((float) h * 0.54f, min_px, max_px * 1.55f);
        int side_w = afm_size_from_range((float) w * 0.36f, min_px, max_px * 1.30f);
        int side_h = afm_size_from_range((float) h * 0.48f, min_px, max_px * 1.25f);
        int eye_w = afm_size_from_range((float) w * 0.42f, min_px, max_px * 1.35f);
        int eye_h = afm_size_from_range((float) h * 0.30f, min_px, max_px * 1.20f);
        int strip_h = afm_size_from_range((float) h * 0.13f, min_px * 0.60f, max_px);
        int strip_w = afm_size_from_range((float) w * 0.72f, min_px, max_px * 1.70f);
        int small_w = afm_size_from_range((float) w * 0.24f, min_px, max_px);
        int small_h = afm_size_from_range((float) h * 0.22f, min_px, max_px);
        int nose_w = afm_size_from_range((float) w * 0.18f, min_px * 0.70f, max_px);
        int nose_h = afm_size_from_range((float) h * 0.42f, min_px, max_px * 1.25f);

        afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                      cx, cy, (float) w * 1.10f, (float) h * 1.10f,
                      1, 0, 1, 0, 54);

        afm_add_panel(m, -side_w * 0.10f + ox * 0.50f, h * 0.03f + oy * 0.55f,
                      side_w, side_h,
                      w * 0.76f, h * 0.24f, side_w * 1.22f, side_h * 1.12f,
                      1, 0, 1, 0, 76);

        afm_add_panel(m, w - side_w * 0.86f - ox * 0.35f, h * 0.02f - oy * 0.35f,
                      side_w, side_h * 1.14f,
                      w * 0.24f, h * 0.27f, side_w * 1.18f, side_h * 1.24f,
                      1, 1, 1, 0, 72);

        afm_add_panel(m, w * 0.03f - ox * 0.24f, h * 0.39f + drift,
                      strip_w, strip_h,
                      w * 0.50f, h * 0.49f, strip_w * 1.02f, strip_h * 2.10f,
                      0, 1, 1, 0, 82);

        afm_add_panel(m, w * 0.62f + ox * 0.20f, h * 0.43f - drift * 0.50f,
                      small_w * 1.12f, small_h * 1.35f,
                      w * 0.40f, h * 0.53f, small_w * 1.34f, small_h * 1.48f,
                      1, 0, 1, 0, 50);

        afm_add_panel(m, w * 0.10f + ox * 0.30f, h * 0.66f - oy * 0.45f,
                      small_w, small_h,
                      w * 0.70f, h * 0.66f, small_w * 1.10f, small_h * 1.10f,
                      1, 1, 1, 0, 46);

        afm_add_panel(m, w * 0.46f - nose_w * 0.28f - ox * 0.12f, h * 0.39f + oy * 0.10f,
                      nose_w, nose_h,
                      w * 0.53f, h * 0.60f, nose_w * 1.34f, nose_h * 1.08f,
                      0, 0, 1, 0, 56);

        afm_add_panel(m, cx - big_w * 0.50f + ox * 0.10f, cy - big_h * 0.50f + oy * 0.20f,
                      big_w, big_h,
                      cx, cy, big_w * 1.06f, big_h * 1.06f,
                      0, 0, 0, 0, 0);

        afm_add_panel(m, cx - eye_w * 0.50f - ox * 0.18f, cy - big_h * 0.31f - eye_h * 0.15f,
                      eye_w, eye_h,
                      cx, cy - h * 0.08f, eye_w * 1.08f, eye_h * 1.16f,
                      1, 0, 0, 0, 18);
    }
}

static void afm_render_panels(mirrormadness_t *m,
                              VJFrame *frame,
                              float seam_t,
                              float mono_t,
                              int background_source)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const int w = m->w;
    const int h = m->h;
    const int panel_count = m->panel_count;
    const int bg_y = 3 + (int) (seam_t * 6.0f);
    const int seam_dark = (int) (18.0f + seam_t * 80.0f + 0.5f);
    const float seam_px = 1.5f + seam_t * 9.0f;
    const float inv_seam_px = 1.0f / seam_px;
    const int mono_amount_base = (int) (mono_t * 255.0f + 0.5f);
    int y;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;

        for (x = 0; x < w; x++) {
            const afm_panel_t *p = NULL;
            float u = 0.0f;
            float v = 0.0f;
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            int pi;
            int pos = row + x;
            int out_y = background_source ? (int) sy[pos] : bg_y;
            int out_u = background_source ? (int) su[pos] : 128;
            int out_v = background_source ? (int) sv[pos] : 128;

            for (pi = panel_count - 1; pi >= 0; pi--) {
                const afm_panel_t *q = &m->panels[pi];

                if (x < q->x0 || x >= q->x1 || y < q->y0 || y >= q->y1)
                    continue;

                u = (px - q->x) * q->inv_w;
                v = (py - q->y) * q->inv_h;

                if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                    continue;

                if (q->round) {
                    float dx = u * 2.0f - 1.0f;
                    float dy = v * 2.0f - 1.0f;
                    if (dx * dx + dy * dy > 1.0f)
                        continue;
                }

                p = q;
                break;
            }

            if (p) {
                float uu = p->flip_x ? 1.0f - u : u;
                float vv = p->flip_y ? 1.0f - v : v;
                float sx = afm_reflect_coord(p->src_x + (uu - 0.5f) * p->src_w, (float) (w - 1));
                float syf = afm_reflect_coord(p->src_y + (vv - 0.5f) * p->src_h, (float) (h - 1));
                float du = u < 1.0f - u ? u : 1.0f - u;
                float dv = v < 1.0f - v ? v : 1.0f - v;
                float edge_dist = (du * p->ww < dv * p->hh) ? du * p->ww : dv * p->hh;
                int mono_amount;

                afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);

                out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
                out_y -= (int) ((float) p->shade * (0.08f + seam_t * 0.34f));

                if (edge_dist < seam_px) {
                    float t = edge_dist * inv_seam_px;
                    out_y -= (int) ((1.0f - t) * (float) seam_dark);
                }

                mono_amount = p->mono ? mono_amount_base : 0;
                if (mono_amount > 0) {
                    int inv_mono = 255 - mono_amount;
                    out_u = ((out_u * inv_mono) + 128 * mono_amount + 127) / 255;
                    out_v = ((out_v * inv_mono) + 128 * mono_amount + 127) / 255;
                }
            }

            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}


static void afm_render_circular_mirror(mirrormadness_t *m,
                                       VJFrame *frame,
                                       float min_width_t,
                                       float max_width_t,
                                       float offset_t,
                                       float seam_t,
                                       float mono_t,
                                       float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_dim = (float) (w < h ? w : h);
    float max_r = sqrtf(cx * cx + cy * cy);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float ring_w;
    float phase;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    ring_w = min_px + (max_px - min_px) * 0.56f;
    if (ring_w < 6.0f)
        ring_w = 6.0f;
    if (ring_w > min_dim * 0.50f)
        ring_w = min_dim * 0.50f;

    phase = offset_t * ring_w * 0.85f + time * ring_w * 0.085f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;

        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r2 = dx * dx + dy * dy;
            float r = sqrtf(r2);
            float inv_r = (r > 0.0001f) ? (1.0f / r) : 0.0f;
            float rr = r + phase;
            float band = floorf(rr / ring_w);
            float local = rr - band * ring_w;
            float mirrored;
            float src_r;
            float sx;
            float syf;
            float edge;
            float center_gain;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            if (((int) band) & 1)
                mirrored = ring_w - local;
            else
                mirrored = local;

            src_r = band * ring_w + mirrored - phase;
            src_r = afm_reflect_coord(src_r, max_r);

            sx = cx + dx * inv_r * src_r;
            syf = cy + dy * inv_r * src_r;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);

            edge = local < ring_w - local ? local : ring_w - local;
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.5f + seam_t * 7.5f), 0.0f, 1.0f));

            center_gain = 1.0f - afm_clampf(r / (ring_w * 1.45f), 0.0f, 1.0f);
            center_gain = afm_smooth01(center_gain);

            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            out_y -= (int) (edge * (12.0f + seam_t * 72.0f));
            out_y += (int) (center_gain * (4.0f + seam_t * 8.0f));

            if (mono_amount > 0) {
                out_u = ((out_u * (255 - mono_amount)) + 128 * mono_amount + 127) / 255;
                out_v = ((out_v * (255 - mono_amount)) + 128 * mono_amount + 127) / 255;
            }

            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}


static inline void afm_apply_mono_amount(int *u, int *v, int mono_amount)
{
    if (mono_amount > 0) {
        int inv = 255 - mono_amount;
        *u = ((*u * inv) + 128 * mono_amount + 127) / 255;
        *v = ((*v * inv) + 128 * mono_amount + 127) / 255;
    }
}

static void afm_build_split_portrait_layout(mirrormadness_t *m,
                                            float min_width_t,
                                            float max_width_t,
                                            float offset_t,
                                            float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float ox = offset_t * (float) w * 0.10f;
    float drift = afm_lut_sin(m, time * 0.80f) * min_dim * 0.020f;
    int center_w;
    int center_h;
    int side_w;
    int side_h;
    int thin_w;
    int slab_w;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    center_w = afm_size_from_range((float) w * 0.44f, min_px * 1.20f, max_px * 1.90f);
    center_h = afm_size_from_range((float) h * 0.82f, min_px * 1.20f, max_px * 2.15f);
    side_w   = afm_size_from_range((float) w * 0.34f, min_px,        max_px * 1.55f);
    side_h   = afm_size_from_range((float) h * 0.76f, min_px,        max_px * 1.95f);
    slab_w   = afm_size_from_range((float) w * 0.21f, min_px,        max_px * 1.05f);
    thin_w   = afm_size_from_range((float) w * 0.09f, min_px * 0.60f, max_px * 0.74f);

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.12f, (float) h * 1.12f,
                  1, 0, 1, 0, 50);

    afm_add_panel(m, cx - side_w * 1.18f + ox * 0.55f, cy - side_h * 0.52f + drift,
                  side_w, side_h,
                  w * 0.74f, cy, side_w * 1.22f, side_h * 1.08f,
                  1, 0, 1, 0, 70);

    afm_add_panel(m, cx + side_w * 0.20f - ox * 0.40f, cy - side_h * 0.51f - drift * 0.60f,
                  side_w, side_h * 1.06f,
                  w * 0.26f, cy, side_w * 1.18f, side_h * 1.14f,
                  1, 1, 1, 0, 72);

    afm_add_panel(m, cx - slab_w * 1.95f - ox * 0.10f, h * 0.04f,
                  slab_w, h * 0.92f,
                  w * 0.60f, cy, slab_w * 1.44f, h * 0.98f,
                  0, 0, 1, 0, 58);

    afm_add_panel(m, cx + slab_w * 1.02f + ox * 0.18f, h * 0.03f,
                  slab_w * 1.10f, h * 0.94f,
                  w * 0.38f, cy, slab_w * 1.54f, h * 1.02f,
                  1, 0, 1, 0, 62);

    afm_add_panel(m, cx - thin_w * 0.55f + ox * 0.05f, h * 0.00f,
                  thin_w, (float) h,
                  cx, cy, thin_w * 1.80f, h * 1.08f,
                  1, 0, 1, 0, 88);

    afm_add_panel(m, cx - center_w * 0.50f + ox * 0.10f, cy - center_h * 0.50f,
                  center_w, center_h,
                  cx, cy, center_w * 1.04f, center_h * 1.04f,
                  0, 0, 0, 0, 0);
}

static void afm_render_staircase_mirror(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float mono_t,
                                        float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_w = afm_width_from_param(w, min_width_t);
    float max_w = afm_width_from_param(w, max_width_t);
    float step_w;
    float step_y;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_w < min_w) {
        float t = max_w;
        max_w = min_w;
        min_w = t;
    }

    step_w = min_w + (max_w - min_w) * 0.42f;
    if (step_w < 2.0f)
        step_w = 2.0f;
    step_y = (0.045f + afm_absf(offset_t) * 0.23f + max_width_t * 0.060f) * (float) h;

    afm_build_axis_map(m, 0, 0, w, min_width_t, max_width_t, offset_t, time,
                       m->x_src, m->x_mask, m->x_edge);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        for (x = 0; x < w; x++) {
            int pos = row + x;
            int out_y;
            int out_u;
            int out_v;
            int msk = m->x_mask[x];
            if (msk <= 0) {
                out_y = 3 + (int) (seam_t * 4.0f);
                out_u = 128;
                out_v = 128;
            }
            else {
                float sx = m->x_src[x];
                float bandf = floorf(((float) x + offset_t * (float) w * 0.32f + time * (float) w * 0.010f) / step_w);
                int band = (int) bandf;
                float syf = afm_reflect_coord((float) y + (float) band * step_y, (float) (h - 1));
                float edge = (float) m->x_edge[x] * (1.0f / 255.0f);
                afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
                out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
                out_y -= (int) (edge * (8.0f + seam_t * 68.0f));
                afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 2);
            }
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_tunnel_mirror(mirrormadness_t *m,
                                     VJFrame *frame,
                                     float min_width_t,
                                     float max_width_t,
                                     float offset_t,
                                     float seam_t,
                                     float mono_t,
                                     float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float rect_w;
    float phase;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }
    rect_w = (min_px + (max_px - min_px) * 0.48f) / (min_dim * 0.5f);
    rect_w = afm_clampf(rect_w, 0.025f, 0.65f);
    phase = offset_t * rect_w * 0.88f + time * 0.075f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float py = ((float) y + 0.5f - cy) / cy;
        float ay = afm_absf(py);
        for (x = 0; x < w; x++) {
            float px = ((float) x + 0.5f - cx) / cx;
            float ax = afm_absf(px);
            float r = ax > ay ? ax : ay;
            float rr = r + phase;
            float band = floorf(rr / rect_w);
            float local = rr - band * rect_w;
            float mirrored = (((int) band) & 1) ? (rect_w - local) : local;
            float src_r = band * rect_w + mirrored - phase;
            float scale = (r > 0.0001f) ? (src_r / r) : 0.0f;
            float sx = cx + px * scale * cx;
            float syf = cy + py * scale * cy;
            float edge = local < rect_w - local ? local : rect_w - local;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (rect_w * (0.10f + seam_t * 0.22f) + 0.00001f), 0.0f, 1.0f));
            out_y -= (int) (edge * (12.0f + seam_t * 84.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_iris_mirror(mirrormadness_t *m,
                                   VJFrame *frame,
                                   float min_width_t,
                                   float max_width_t,
                                   float offset_t,
                                   float seam_t,
                                   float mono_t,
                                   float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_dim = (float) (w < h ? w : h);
    float max_r = sqrtf(cx * cx + cy * cy);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float ring_w;
    float phase_r;
    int sectors = 8 + (int) (max_width_t * 10.0f + 0.5f);
    float sector_w;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (sectors < 6) sectors = 6;
    if (sectors > 24) sectors = 24;
    sector_w = AFM_TWO_PI / (float) sectors;
    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }
    ring_w = min_px + (max_px - min_px) * 0.52f;
    if (ring_w < 6.0f) ring_w = 6.0f;
    phase_r = offset_t * ring_w * 0.90f + time * ring_w * 0.080f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(dy, dx) + AFM_TWO_PI + time * 0.18f;
            float sb = floorf(theta / sector_w);
            float sl = theta - sb * sector_w;
            float mt = (((int) sb) & 1) ? (sector_w - sl) : sl;
            float theta2 = sb * sector_w + mt - time * 0.18f;
            float rr = r + phase_r;
            float rb = floorf(rr / ring_w);
            float rl = rr - rb * ring_w;
            float mr = (((int) rb) & 1) ? (ring_w - rl) : rl;
            float src_r = afm_reflect_coord(rb * ring_w + mr - phase_r, max_r);
            float sx = cx + cosf(theta2) * src_r;
            float syf = cy + sinf(theta2) * src_r;
            float ring_edge = rl < ring_w - rl ? rl : ring_w - rl;
            float sector_edge = sl < sector_w - sl ? sl : sector_w - sl;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            ring_edge = 1.0f - afm_smooth01(afm_clampf(ring_edge / (1.4f + seam_t * 8.0f), 0.0f, 1.0f));
            sector_edge = 1.0f - afm_smooth01(afm_clampf(sector_edge / (sector_w * (0.12f + seam_t * 0.10f)), 0.0f, 1.0f));
            out_y -= (int) ((ring_edge * 58.0f + sector_edge * 34.0f) * (0.25f + seam_t));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_shutter_mirror(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float max_r = sqrtf(cx * cx + cy * cy);
    int blades = 5 + (int) (max_width_t * 13.0f + 0.5f);
    float blade_w;
    float radial_w = 22.0f + afm_width_from_param((w < h ? w : h), min_width_t) * 0.42f;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (blades < 5) blades = 5;
    if (blades > 28) blades = 28;
    blade_w = AFM_TWO_PI / (float) blades;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(dy, dx) + AFM_TWO_PI + offset_t * 0.90f + time * 0.24f;
            float b = floorf(theta / blade_w);
            float l = theta - b * blade_w;
            float mirrored_l = (((int) b) & 1) ? (blade_w - l) : l;
            float blade_center = b * blade_w + blade_w * 0.5f;
            float theta2 = blade_center + (mirrored_l - blade_w * 0.5f) * (0.58f + max_width_t * 0.34f) - offset_t * 0.90f;
            float rb = floorf((r + time * 11.0f) / radial_w);
            float rl = r + time * 11.0f - rb * radial_w;
            float src_r = afm_reflect_coord((((int) rb) & 1) ? (rb * radial_w + radial_w - rl) : (rb * radial_w + rl), max_r);
            float sx = cx + cosf(theta2) * src_r;
            float syf = cy + sinf(theta2) * src_r;
            float edge = l < blade_w - l ? l : blade_w - l;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (blade_w * (0.11f + seam_t * 0.12f)), 0.0f, 1.0f));
            out_y -= (int) (edge * (16.0f + seam_t * 78.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}



static void afm_build_broken_window_layout(mirrormadness_t *m,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float ox = offset_t * (float) w * 0.13f;
    float oy = afm_lut_sin(m, time * 0.91f) * (float) h * 0.030f;
    int a_w;
    int a_h;
    int b_w;
    int b_h;
    int c_w;
    int c_h;
    int d_w;
    int d_h;
    int e_w;
    int e_h;
    int f_w;
    int f_h;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    a_w = afm_size_from_range((float) w * 0.47f, min_px * 1.25f, max_px * 1.95f);
    a_h = afm_size_from_range((float) h * 0.56f, min_px * 1.10f, max_px * 1.85f);
    b_w = afm_size_from_range((float) w * 0.34f, min_px,        max_px * 1.30f);
    b_h = afm_size_from_range((float) h * 0.38f, min_px,        max_px * 1.45f);
    c_w = afm_size_from_range((float) w * 0.28f, min_px,        max_px * 1.12f);
    c_h = afm_size_from_range((float) h * 0.72f, min_px * 1.05f, max_px * 2.00f);
    d_w = afm_size_from_range((float) w * 0.58f, min_px * 1.20f, max_px * 2.05f);
    d_h = afm_size_from_range((float) h * 0.22f, min_px * 0.75f, max_px * 1.05f);
    e_w = afm_size_from_range((float) w * 0.18f, min_px * 0.65f, max_px * 0.92f);
    e_h = afm_size_from_range((float) h * 0.48f, min_px,        max_px * 1.40f);
    f_w = afm_size_from_range((float) w * 0.25f, min_px * 0.80f, max_px * 1.00f);
    f_h = afm_size_from_range((float) h * 0.27f, min_px * 0.80f, max_px * 1.10f);

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.14f, (float) h * 1.14f,
                  1, 0, 1, 0, 62);

    afm_add_panel(m, w * 0.03f + ox * 0.30f, h * 0.06f + oy,
                  b_w, b_h,
                  w * 0.72f, h * 0.28f, b_w * 1.22f, b_h * 1.08f,
                  1, 0, 1, 0, 86);

    afm_add_panel(m, w * 0.68f - ox * 0.24f, h * 0.02f - oy * 0.30f,
                  c_w, c_h,
                  w * 0.30f, h * 0.52f, c_w * 1.16f, c_h * 1.04f,
                  1, 1, 1, 0, 74);

    afm_add_panel(m, w * 0.13f - ox * 0.18f, h * 0.58f + oy * 0.40f,
                  d_w, d_h,
                  w * 0.52f, h * 0.60f, d_w * 1.02f, d_h * 1.80f,
                  0, 1, 1, 0, 90);

    afm_add_panel(m, w * 0.46f + ox * 0.16f, h * 0.34f - oy * 0.20f,
                  e_w, e_h,
                  w * 0.52f, h * 0.42f, e_w * 1.50f, e_h * 1.10f,
                  1, 0, 1, 0, 66);

    afm_add_panel(m, w * 0.02f + ox * 0.08f, h * 0.78f,
                  f_w, f_h,
                  w * 0.70f, h * 0.75f, f_w * 1.22f, f_h * 1.24f,
                  1, 1, 1, 0, 54);

    afm_add_panel(m, cx - a_w * 0.50f - ox * 0.05f, cy - a_h * 0.52f,
                  a_w, a_h,
                  cx, cy, a_w * 1.04f, a_h * 1.04f,
                  0, 0, 0, 0, 0);

    afm_add_panel(m, w * 0.58f - f_w * 0.25f - ox * 0.10f, h * 0.70f + oy * 0.20f,
                  f_w * 1.18f, f_h * 1.08f,
                  w * 0.44f, h * 0.70f, f_w * 1.32f, f_h * 1.30f,
                  0, 1, 0, 0, 24);
}

static void afm_render_barcode_mirror(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float time)
{
    float min_t = afm_clampf(min_width_t * 0.42f, 0.0f, 1.0f);
    float max_t = afm_clampf(max_width_t * 1.08f, 0.0f, 1.0f);

    if (max_t < min_t) {
        float t = max_t;
        max_t = min_t;
        min_t = t;
    }

    afm_build_axis_map(m, 3, 0, m->w, min_t, max_t, offset_t * 1.25f, time * 1.10f,
                       m->x_src, m->x_mask, m->x_edge);
    afm_prepare_axis_index(m->x_src, m->x_mask, m->w, m->x_i0, m->x_i1, m->x_w, m->x_n);
    afm_render_vertical_fast(m, frame, afm_clampf(seam_t * 1.12f, 0.0f, 1.0f));
}

static void afm_render_letterbox_mirror(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float time)
{
    float min_t = afm_clampf(min_width_t * 0.55f, 0.0f, 1.0f);
    float max_t = afm_clampf(max_width_t * 1.12f, 0.0f, 1.0f);

    if (max_t < min_t) {
        float t = max_t;
        max_t = min_t;
        min_t = t;
    }

    afm_build_axis_map(m, 4, 1, m->h, min_t, max_t, -offset_t * 0.92f, time * 0.84f,
                       m->y_src, m->y_mask, m->y_edge);
    afm_prepare_axis_index(m->y_src, m->y_mask, m->h, m->y_i0, m->y_i1, m->y_w, m->y_n);
    afm_render_horizontal_fast(m, frame, afm_clampf(seam_t * 1.08f, 0.0f, 1.0f));
}

static void afm_render_diagonal_slats(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time,
                                      int reverse_dir)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float stripe_w;
    float angle = (reverse_dir ? -0.58f : 0.58f) + offset_t * 0.70f + time * 0.12f;
    float ca = cosf(angle);
    float sa = sinf(angle);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float phase;
    int mono_amount = (int) (mono_t * 170.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    stripe_w = min_px + (max_px - min_px) * 0.38f;
    if (stripe_w < 3.0f)
        stripe_w = 3.0f;
    phase = offset_t * stripe_w * 1.35f + time * stripe_w * 0.075f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float py = ((float) y + 0.5f) - cy;
        int row = y * w;
        for (x = 0; x < w; x++) {
            float px = ((float) x + 0.5f) - cx;
            float d = px * ca + py * sa + phase;
            float tng = -px * sa + py * ca;
            float bandf = floorf(d / stripe_w);
            int band = (int) bandf;
            float local = d - bandf * stripe_w;
            float mirrored = (band & 1) ? (stripe_w - local) : local;
            float src_d = bandf * stripe_w + mirrored - phase;
            float shift = (float) band * stripe_w * (0.05f + afm_absf(offset_t) * 0.035f);
            float sx = cx + src_d * ca - (tng + shift) * sa;
            float syf = cy + src_d * sa + (tng + shift) * ca;
            float edge = local < stripe_w - local ? local : stripe_w - local;
            int pos = row + x;
            int out_y;
            int out_u;
            int out_v;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 7.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (10.0f + seam_t * 70.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}


static void afm_render_serpentine_slits(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float mono_t,
                                        float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float strip_w;
    float phase;
    float wobble;
    float freq;
    int mono_amount = (int) (mono_t * 160.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    strip_w = min_px + (max_px - min_px) * 0.36f;
    if (strip_w < 3.0f)
        strip_w = 3.0f;

    phase = offset_t * (float) w * 0.58f + time * (float) w * 0.035f;
    wobble = strip_w * (0.70f + max_width_t * 1.85f);
    freq = (5.0f + min_width_t * 18.0f) / (float) (h > 1 ? h : 1);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        float yy = (float) y + 0.5f;
        float wv0 = afm_lut_sin(m, yy * freq + time * 1.70f);
        float wv1 = afm_lut_sin(m, yy * freq * 2.31f - time * 0.94f + 1.9f);
        float row_shift = (wv0 * 0.72f + wv1 * 0.28f) * wobble;

        for (x = 0; x < w; x++) {
            float d = (float) x + 0.5f + row_shift + phase;
            float bandf = floorf(d / strip_w);
            int band = (int) bandf;
            float local = d - bandf * strip_w;
            float mirrored = (band & 1) ? (strip_w - local) : local;
            float sx = bandf * strip_w + mirrored - row_shift - phase;
            float syf = yy + afm_lut_sin(m, (float) band * 0.77f + time * 1.11f) * strip_w * 0.18f;
            float edge = local < strip_w - local ? local : strip_w - local;
            int pos = row + x;
            int out_y;
            int out_u;
            int out_v;

            sx = afm_reflect_coord(sx, (float) (w - 1));
            syf = afm_reflect_coord(syf, (float) (h - 1));

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 6.5f), 0.0f, 1.0f));
            out_y -= (int) (edge * (8.0f + seam_t * 64.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);

            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_quad_portal(mirrormadness_t *m,
                                   VJFrame *frame,
                                   float min_width_t,
                                   float max_width_t,
                                   float offset_t,
                                   float seam_t,
                                   float mono_t,
                                   float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float scale = 0.86f + max_width_t * 0.46f;
    float fold_x = afm_width_from_param(w, min_width_t) * (0.70f + max_width_t * 0.60f);
    float fold_y = afm_width_from_param(h, min_width_t) * (0.70f + max_width_t * 0.60f);
    float phase_x = offset_t * fold_x * 1.7f + time * fold_x * 0.16f;
    float phase_y = -offset_t * fold_y * 1.3f + time * fold_y * 0.11f;
    int mono_amount = (int) (mono_t * 120.0f + 0.5f);
    int y;

    if (fold_x < 8.0f) fold_x = 8.0f;
    if (fold_y < 8.0f) fold_y = 8.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        float py = ((float) y + 0.5f) - cy;
        float sygn_y = py < 0.0f ? -1.0f : 1.0f;
        float ay = afm_absf(py) * scale + phase_y;
        float by = floorf(ay / fold_y);
        float ly = ay - by * fold_y;
        float my = (((int) by) & 1) ? (fold_y - ly) : ly;
        float src_ay = by * fold_y + my - phase_y;
        float sy_base = cy + sygn_y * src_ay;

        for (x = 0; x < w; x++) {
            float px = ((float) x + 0.5f) - cx;
            float sygn_x = px < 0.0f ? -1.0f : 1.0f;
            float ax = afm_absf(px) * scale + phase_x;
            float bx = floorf(ax / fold_x);
            float lx = ax - bx * fold_x;
            float mx = (((int) bx) & 1) ? (fold_x - lx) : lx;
            float src_ax = bx * fold_x + mx - phase_x;
            float sx = cx + sygn_x * src_ax;
            float syf = sy_base;
            float ex = lx < fold_x - lx ? lx : fold_x - lx;
            float ey = ly < fold_y - ly ? ly : fold_y - ly;
            float edge = ex < ey ? ex : ey;
            int pos = row + x;
            int out_y;
            int out_u;
            int out_v;

            sx = afm_reflect_coord(sx, (float) (w - 1));
            syf = afm_reflect_coord(syf, (float) (h - 1));
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.4f + seam_t * 8.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (9.0f + seam_t * 68.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_moebius_ribbon(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float strip_w = afm_width_from_param((int) min_dim, min_width_t) * (0.70f + max_width_t * 0.80f);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float angle = 0.42f + offset_t * 0.92f + time * 0.18f;
    float ca = cosf(angle);
    float sa = sinf(angle);
    float phase = time * strip_w * 0.24f + offset_t * strip_w * 1.60f;
    int mono_amount = (int) (mono_t * 130.0f + 0.5f);
    int y;

    if (strip_w < 5.0f)
        strip_w = 5.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        int row = y * w;
        float py = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float px = ((float) x + 0.5f) - cx;
            float u = px * ca + py * sa;
            float v = -px * sa + py * ca;
            float curve = afm_lut_sin(m, v * (0.010f + max_width_t * 0.020f) + time * 1.40f) * strip_w * (0.80f + max_width_t * 1.20f);
            float d = u + curve + phase;
            float bandf = floorf(d / strip_w);
            int band = (int) bandf;
            float local = d - bandf * strip_w;
            float twist = (((band & 1) ? -1.0f : 1.0f) * (0.18f + max_width_t * 0.36f));
            float mirrored = (band & 1) ? (strip_w - local) : local;
            float src_u = bandf * strip_w + mirrored - curve - phase;
            float src_v = v + afm_lut_sin(m, (float) band * 0.73f + time) * strip_w * 0.22f;
            float rx = src_u * ca - (src_v + u * twist * 0.08f) * sa;
            float ry = src_u * sa + (src_v + u * twist * 0.08f) * ca;
            float edge = local < strip_w - local ? local : strip_w - local;
            float sx = afm_reflect_coord(cx + rx, (float) (w - 1));
            float syf = afm_reflect_coord(cy + ry, (float) (h - 1));
            int pos = row + x;
            int out_y;
            int out_u;
            int out_v;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.3f + seam_t * 7.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (9.0f + seam_t * 70.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_build_orbit_collage_layout(mirrormadness_t *m,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float orbit_r = min_dim * (0.23f + max_width_t * 0.12f);
    int center_w;
    int center_h;
    int p_w;
    int p_h;
    int i;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    center_w = afm_size_from_range((float) w * 0.46f, min_px * 1.10f, max_px * 1.80f);
    center_h = afm_size_from_range((float) h * 0.52f, min_px * 1.10f, max_px * 1.85f);
    p_w = afm_size_from_range((float) w * 0.28f, min_px, max_px * 1.20f);
    p_h = afm_size_from_range((float) h * 0.30f, min_px, max_px * 1.22f);

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.10f, (float) h * 1.10f,
                  1, 0, 1, 0, 62);

    for (i = 0; i < 6; i++) {
        float a = time * (0.40f + 0.03f * (float) i) + offset_t * 0.70f + (float) i * (AFM_TWO_PI / 6.0f);
        float px = cx + cosf(a) * orbit_r - (float) p_w * 0.5f;
        float py = cy + sinf(a * 0.83f) * orbit_r * 0.62f - (float) p_h * 0.5f;
        int shade = 44 + (i * 9) % 42;
        afm_add_panel(m, px, py, (float) p_w, (float) p_h,
                      cx + cosf(a + 1.2f) * (float) w * 0.17f,
                      cy + sinf(a - 0.6f) * (float) h * 0.14f,
                      (float) p_w * (1.10f + 0.05f * (float) (i & 1)),
                      (float) p_h * (1.12f + 0.05f * (float) ((i + 1) & 1)),
                      i & 1, (i >> 1) & 1, 1, 0, shade);
    }

    afm_add_panel(m, cx - (float) center_w * 0.5f, cy - (float) center_h * 0.5f,
                  (float) center_w, (float) center_h,
                  cx, cy, (float) center_w * 1.04f, (float) center_h * 1.04f,
                  0, 0, 0, 0, 0);
}


static void afm_render_triangle_mirror(mirrormadness_t *m,
                                       VJFrame *frame,
                                       float min_width_t,
                                       float max_width_t,
                                       float offset_t,
                                       float seam_t,
                                       float mono_t,
                                       float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const int w = m->w;
    const int h = m->h;
    const float cx = (float) w * 0.5f;
    const float cy = (float) h * 0.5f;
    const float max_r = sqrtf(cx * cx + cy * cy);
    const float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float band_w;
    int sectors = 3 + (int) (max_width_t * 9.0f + 0.5f);
    float sector_w;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (sectors < 3) sectors = 3;
    if (sectors > 18) sectors = 18;
    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    band_w = min_px + (max_px - min_px) * 0.45f;
    if (band_w < 5.0f) band_w = 5.0f;
    sector_w = AFM_TWO_PI / (float) sectors;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(dy, dx) + AFM_TWO_PI + offset_t * 0.55f + time * 0.20f;
            float sb = floorf(theta / sector_w);
            float sl = theta - sb * sector_w;
            float tri_l = (((int) sb) & 1) ? (sector_w - sl) : sl;
            float theta2 = sb * sector_w + tri_l - offset_t * 0.55f;
            float rr = r + time * band_w * 0.22f;
            float rb = floorf(rr / band_w);
            float rl = rr - rb * band_w;
            float mr = (((int) rb) & 1) ? (band_w - rl) : rl;
            float src_r = afm_reflect_coord(rb * band_w + mr - time * band_w * 0.22f, max_r);
            float sx = cx + cosf(theta2) * src_r;
            float syf = cy + sinf(theta2) * src_r;
            float edge_a = sl < sector_w - sl ? sl : sector_w - sl;
            float edge_r = rl < band_w - rl ? rl : band_w - rl;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge_a = 1.0f - afm_smooth01(afm_clampf(edge_a / (sector_w * (0.08f + seam_t * 0.13f)), 0.0f, 1.0f));
            edge_r = 1.0f - afm_smooth01(afm_clampf(edge_r / (1.0f + seam_t * 7.0f), 0.0f, 1.0f));
            out_y -= (int) ((edge_a * 42.0f + edge_r * 36.0f) * (0.28f + seam_t));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_hex_mirror(mirrormadness_t *m,
                                  VJFrame *frame,
                                  float min_width_t,
                                  float max_width_t,
                                  float offset_t,
                                  float seam_t,
                                  float mono_t,
                                  float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const int w = m->w;
    const int h = m->h;
    const float cx = (float) w * 0.5f;
    const float cy = (float) h * 0.5f;
    const float min_dim = (float) (w < h ? w : h);
    float hex_min_t = 1.0f - max_width_t;
    float hex_max_t = 1.0f - min_width_t;
    float min_px = afm_width_from_param((int) min_dim, hex_min_t);
    float max_px = afm_width_from_param((int) min_dim, hex_max_t);
    float cell;
    float inv_cell;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    cell = min_px + (max_px - min_px) * 0.72f;
    cell *= 1.22f;
    if (cell < 10.0f) cell = 10.0f;
    inv_cell = 1.0f / cell;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float py = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float px = ((float) x + 0.5f) - cx;
            float p0 = (px + offset_t * cell + time * cell * 0.16f) * inv_cell;
            float p1 = (px * 0.5f + py * 0.8660254f - offset_t * cell * 0.5f) * inv_cell;
            float p2 = (-px * 0.5f + py * 0.8660254f + time * cell * 0.10f) * inv_cell;
            float f0 = p0 - floorf(p0);
            float f1 = p1 - floorf(p1);
            float f2 = p2 - floorf(p2);
            float m0 = f0 < 0.5f ? f0 : 1.0f - f0;
            float m1 = f1 < 0.5f ? f1 : 1.0f - f1;
            float m2 = f2 < 0.5f ? f2 : 1.0f - f2;
            float sx = cx + (m0 - 0.25f) * cell * 2.10f + (m1 - m2) * cell * 0.95f;
            float syf = cy + (m1 + m2 - 0.50f) * cell * 1.52f;
            float edge = m0;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            if (m1 < edge) edge = m1;
            if (m2 < edge) edge = m2;
            sx = afm_reflect_coord(sx, (float) (w - 1));
            syf = afm_reflect_coord(syf, (float) (h - 1));
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge * cell / (1.0f + seam_t * 7.5f), 0.0f, 1.0f));
            out_y -= (int) (edge * (10.0f + seam_t * 72.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_tunnel_xl(mirrormadness_t *m,
                                 VJFrame *frame,
                                 float min_width_t,
                                 float max_width_t,
                                 float offset_t,
                                 float seam_t,
                                 float mono_t,
                                 float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    const int w = m->w;
    const int h = m->h;
    const float cx = (float) w * 0.5f;
    const float cy = (float) h * 0.5f;
    const float min_dim = (float) (w < h ? w : h);
    float band = afm_width_from_param((int) min_dim, max_width_t) * 0.72f + min_dim * 0.06f;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    (void) min_width_t;
    if (band < 24.0f) band = 24.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float ny = (((float) y + 0.5f) - cy) / cy;
        for (x = 0; x < w; x++) {
            float nx = (((float) x + 0.5f) - cx) / cx;
            float ax = afm_absf(nx);
            float ay = afm_absf(ny);
            float d = ax > ay ? ax : ay;
            float pix_d = d * min_dim * 0.5f + offset_t * band + time * band * 0.13f;
            float layer = floorf(pix_d / band);
            float local = pix_d - layer * band;
            float zoom = 1.0f + layer * (0.34f + max_width_t * 0.38f);
            float signx = nx < 0.0f ? -1.0f : 1.0f;
            float signy = ny < 0.0f ? -1.0f : 1.0f;
            float lx = (((int) layer) & 1) ? -nx : nx;
            float ly = (((int) layer) & 2) ? -ny : ny;
            float sx = cx + lx * cx / zoom + signx * local * 0.18f;
            float syf = cy + ly * cy / zoom + signy * local * 0.18f;
            float edge = local < band - local ? local : band - local;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (band * (0.035f + seam_t * 0.10f)), 0.0f, 1.0f));
            out_y -= (int) (edge * (16.0f + seam_t * 92.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_wave_bars(mirrormadness_t *m,
                                 VJFrame *frame,
                                 float min_width_t,
                                 float max_width_t,
                                 float offset_t,
                                 float seam_t,
                                 float mono_t,
                                 float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float stripe_w;
    float wave_amp = (float) w * (0.025f + max_width_t * 0.060f);
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    stripe_w = min_px + (max_px - min_px) * 0.32f;
    if (stripe_w < 4.0f) stripe_w = 4.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float wave = sinf(((float) y * (0.010f + max_width_t * 0.010f)) + time * 1.7f) * wave_amp;
        float wave2 = sinf(((float) y * 0.021f) - time * 1.1f) * wave_amp * 0.34f;
        for (x = 0; x < w; x++) {
            float coord = (float) x + wave + wave2 + offset_t * (float) w * 0.35f;
            float band = floorf(coord / stripe_w);
            float local = coord - band * stripe_w;
            float mt = (((int) band) & 1) ? (stripe_w - local) : local;
            float sx = afm_reflect_coord(band * stripe_w + mt - wave * 0.42f, (float) (w - 1));
            float syf = afm_reflect_coord((float) y + sinf((float) x * 0.012f + time) * stripe_w * 0.24f, (float) (h - 1));
            float edge = local < stripe_w - local ? local : stripe_w - local;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.0f + seam_t * 7.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (9.0f + seam_t * 62.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_build_center_stack_layout(mirrormadness_t *m,
                                          float min_width_t,
                                          float max_width_t,
                                          float offset_t,
                                          float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    int i;
    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    m->panel_count = 0;
    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h, cx, cy, (float) w * 1.08f, (float) h * 1.08f, 1, 0, 1, 0, 54);
    for (i = 0; i < 7; i++) {
        float k = (float) i / 6.0f;
        float ww = afm_size_from_range((float) w * (0.18f + k * 0.66f), min_px, max_px * 2.2f);
        float hh = afm_size_from_range((float) h * (0.92f - k * 0.62f), min_px, max_px * 1.8f);
        float x = cx - ww * 0.5f + sinf(time * (0.5f + k) + k * 3.1f) * (float) w * 0.025f + offset_t * (float) w * (0.04f - k * 0.02f);
        float y = cy - hh * 0.5f + (k - 0.5f) * (float) h * 0.10f;
        int mono = (i < 5);
        afm_add_panel(m, x, y, ww, hh, cx, cy + (k - 0.5f) * (float) h * 0.12f, ww * 1.04f, hh * 1.02f, i & 1, 0, mono, 0, 42 + i * 6);
    }
}

static void afm_render_corner_pull(mirrormadness_t *m,
                                   VJFrame *frame,
                                   float min_width_t,
                                   float max_width_t,
                                   float offset_t,
                                   float seam_t,
                                   float mono_t,
                                   float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float band = afm_width_from_param((int) min_dim, min_width_t) * 0.85f + 12.0f;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    (void) max_width_t;
    if (band < 12.0f) band = 12.0f;
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            int right = x >= (w >> 1);
            int bottom = y >= (h >> 1);
            float cx = right ? (float) (w - 1) : 0.0f;
            float cy = bottom ? (float) (h - 1) : 0.0f;
            float dx = afm_absf((float) x - cx);
            float dy = afm_absf((float) y - cy);
            float d = dx > dy ? dx : dy;
            float b = floorf((d + offset_t * band + time * band * 0.18f) / band);
            float l = d + offset_t * band + time * band * 0.18f - b * band;
            float ml = (((int) b) & 1) ? (band - l) : l;
            float sx = right ? (float) (w - 1) - afm_reflect_coord(dx - d + b * band + ml, (float) (w - 1)) : afm_reflect_coord(dx - d + b * band + ml, (float) (w - 1));
            float syf = bottom ? (float) (h - 1) - afm_reflect_coord(dy - d + b * band + ml, (float) (h - 1)) : afm_reflect_coord(dy - d + b * band + ml, (float) (h - 1));
            float edge = l < band - l ? l : band - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 8.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (13.0f + seam_t * 78.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_slit_scan(mirrormadness_t *m,
                                 VJFrame *frame,
                                 float min_width_t,
                                 float max_width_t,
                                 float offset_t,
                                 float seam_t,
                                 float mono_t,
                                 float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float stripe_w;
    float shift_scale = (float) h * (0.05f + max_width_t * 0.22f);
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    stripe_w = min_px + (max_px - min_px) * 0.28f;
    if (stripe_w < 3.0f) stripe_w = 3.0f;
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float coord = (float) x + offset_t * (float) w * 0.40f;
            float b = floorf(coord / stripe_w);
            float l = coord - b * stripe_w;
            float ml = (((int) b) & 1) ? (stripe_w - l) : l;
            float sx = afm_reflect_coord(b * stripe_w + ml, (float) (w - 1));
            float syf = afm_reflect_coord((float) y + sinf((float) b * 0.71f + time * 2.8f) * shift_scale + ((int) b & 3) * stripe_w * 0.20f, (float) (h - 1));
            float edge = l < stripe_w - l ? l : stripe_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (0.9f + seam_t * 6.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (8.0f + seam_t * 58.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}



static void afm_build_contact_sheet_layout(mirrormadness_t *m,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float gap = 4.0f + min_px * 0.055f;
    float drift_x = offset_t * (float) w * 0.035f;
    float drift_y = afm_lut_sin(m, time * 0.83f) * (float) h * 0.018f;
    int cols = 4;
    int rows = 3;
    int r;
    int c;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  (float) w * 0.5f, (float) h * 0.5f,
                  (float) w * 1.08f, (float) h * 1.08f,
                  1, 0, 1, 0, 72);

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            float jitter_x = (afm_hash01((uint32_t) (r * 19 + c * 73 + 31)) - 0.5f) * gap * 1.7f;
            float jitter_y = (afm_hash01((uint32_t) (r * 29 + c * 41 + 89)) - 0.5f) * gap * 1.4f;
            float ww = ((float) w - gap * (float) (cols + 1)) / (float) cols;
            float hh = ((float) h - gap * (float) (rows + 1)) / (float) rows;
            float x = gap + (float) c * (ww + gap) + jitter_x + drift_x * ((float) c - 1.5f) * 0.42f;
            float y = gap + (float) r * (hh + gap) + jitter_y + drift_y * ((float) r - 1.0f) * 0.55f;
            float sw = ww * (1.04f + 0.08f * afm_hash01((uint32_t) (c * 97 + r * 151 + 11)));
            float sh = hh * (1.04f + 0.08f * afm_hash01((uint32_t) (c * 67 + r * 113 + 23)));
            int front = (r == 1 && c == 1) || (r == 1 && c == 2);

            if (front)
                continue;

            afm_add_panel(m, x, y, ww, hh,
                          ((float) c + 0.5f) * (float) w / (float) cols,
                          ((float) r + 0.5f) * (float) h / (float) rows,
                          sw, sh,
                          (c + r) & 1, c & 1, 1, 0, 46 + ((c + r) & 3) * 10);
        }
    }

    {
        float ww = ((float) w - gap * (float) (cols + 1)) / (float) cols * 2.08f;
        float hh = ((float) h - gap * (float) (rows + 1)) / (float) rows * 1.12f;
        float x = ((float) w - ww) * 0.5f + drift_x * 0.22f;
        float y = ((float) h - hh) * 0.5f + drift_y * 0.20f;
        afm_add_panel(m, x, y, ww, hh,
                      (float) w * 0.5f, (float) h * 0.5f,
                      ww * 1.04f, hh * 1.04f,
                      0, 0, 0, 0, 0);
    }
}

static void afm_render_venetian_fan(mirrormadness_t *m,
                                    VJFrame *frame,
                                    float min_width_t,
                                    float max_width_t,
                                    float offset_t,
                                    float seam_t,
                                    float mono_t,
                                    float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float pivot_x = offset_t < 0.0f ? (float) w * 1.02f : (float) w * -0.02f;
    float pivot_y = (float) h * (0.50f + offset_t * 0.12f);
    int blades = 7 + (int) ((1.0f - min_width_t) * 7.0f + max_width_t * 13.0f + 0.5f);
    float blade_w;
    float radius_shift = time * min_dim * (0.030f + max_width_t * 0.030f);
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (blades < 5) blades = 5;
    if (blades > 32) blades = 32;
    blade_w = AFM_TWO_PI / (float) blades;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - pivot_x;
            float dy = ((float) y + 0.5f) - pivot_y;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(dy, dx) + AFM_TWO_PI + offset_t * 0.55f + time * 0.15f;
            float b = floorf(theta / blade_w);
            float l = theta - b * blade_w;
            float ml = (((int) b) & 1) ? (blade_w - l) : l;
            float theta2 = b * blade_w + ml - offset_t * 0.55f;
            float src_r = afm_reflect_coord(r + radius_shift + ((int) b & 1) * min_dim * 0.035f, sqrtf((float) w * (float) w + (float) h * (float) h));
            float sx = pivot_x + cosf(theta2) * src_r;
            float syf = pivot_y + sinf(theta2) * src_r;
            float edge = l < blade_w - l ? l : blade_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (blade_w * (0.10f + seam_t * 0.14f)), 0.0f, 1.0f));
            out_y -= (int) (edge * (12.0f + seam_t * 70.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_hourglass_mirror(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float mono_t,
                                        float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_w = afm_width_from_param(w, min_width_t);
    float max_w = afm_width_from_param(w, max_width_t);
    float base_w;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_w < min_w) { float t = max_w; max_w = min_w; min_w = t; }
    base_w = min_w + (max_w - min_w) * 0.42f;
    if (base_w < 3.0f) base_w = 3.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float yn = (((float) y + 0.5f) - cy) / cy;
        float waist = 0.30f + 0.70f * afm_absf(yn);
        float strip_w = base_w * (0.38f + waist * 1.24f);
        float yshift = afm_lut_sin(m, time * 1.7f + yn * 2.4f) * (float) h * 0.018f * max_width_t;
        for (x = 0; x < w; x++) {
            float warped_x = cx + (((float) x + 0.5f) - cx) / waist;
            float coord = warped_x + offset_t * (float) w * 0.38f;
            float b = floorf(coord / strip_w);
            float l = coord - b * strip_w;
            float ml = (((int) b) & 1) ? (strip_w - l) : l;
            float sx = afm_reflect_coord(b * strip_w + ml - offset_t * (float) w * 0.18f, (float) (w - 1));
            float syf = afm_reflect_coord((float) y + yshift + ((int) b & 1 ? -1.0f : 1.0f) * yn * (float) h * 0.055f, (float) (h - 1));
            float edge = l < strip_w - l ? l : strip_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 7.2f), 0.0f, 1.0f));
            out_y -= (int) (edge * (8.0f + seam_t * 64.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 2);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_pinwheel_panels(mirrormadness_t *m,
                                       VJFrame *frame,
                                       float min_width_t,
                                       float max_width_t,
                                       float offset_t,
                                       float seam_t,
                                       float mono_t,
                                       float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float max_r = sqrtf(cx * cx + cy * cy);
    int panels = 4 + (int) (max_width_t * 8.0f + 0.5f);
    float pane_w;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (panels < 4) panels = 4;
    if (panels > 16) panels = 16;
    pane_w = AFM_TWO_PI / (float) panels;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;
        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(dy, dx) + AFM_TWO_PI + time * 0.23f + offset_t * 0.60f;
            float b = floorf(theta / pane_w);
            float l = theta - b * pane_w;
            float folded = (((int) b) & 1) ? (pane_w - l) : l;
            float twist = (r / (max_r + 1.0f)) * pane_w * (0.35f + min_width_t * 0.50f);
            float theta2 = b * pane_w + folded + twist - offset_t * 0.60f;
            float src_r = afm_reflect_coord(r * (0.82f + 0.32f * afm_lut_sin(m, (float) b * 0.9f + time)), max_r);
            float sx = cx + cosf(theta2) * src_r;
            float syf = cy + sinf(theta2) * src_r;
            float edge = l < pane_w - l ? l : pane_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (pane_w * (0.09f + seam_t * 0.12f)), 0.0f, 1.0f));
            out_y -= (int) (edge * (10.0f + seam_t * 72.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_accordion_mirror(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float mono_t,
                                        float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_w = afm_width_from_param(w, min_width_t);
    float max_w = afm_width_from_param(w, max_width_t);
    float fold_w;
    float amp = (float) h * (0.035f + max_width_t * 0.11f);
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    if (max_w < min_w) { float t = max_w; max_w = min_w; min_w = t; }
    fold_w = min_w + (max_w - min_w) * 0.50f;
    if (fold_w < 3.0f) fold_w = 3.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float coord = (float) x + offset_t * (float) w * 0.45f + time * (float) w * 0.010f;
            float b = floorf(coord / fold_w);
            float l = coord - b * fold_w;
            float t = l / fold_w;
            float mt = (((int) b) & 1) ? (1.0f - t) : t;
            float hinge = (t < 0.5f ? t : 1.0f - t) * 2.0f;
            float sx = afm_reflect_coord(b * fold_w + mt * fold_w - offset_t * (float) w * 0.25f, (float) (w - 1));
            float syf = afm_reflect_coord((float) y + (((int) b) & 1 ? -1.0f : 1.0f) * (hinge - 0.45f) * amp, (float) (h - 1));
            float edge = l < fold_w - l ? l : fold_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            out_y += (int) ((hinge - 0.5f) * (8.0f + seam_t * 14.0f));
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (0.8f + seam_t * 6.5f), 0.0f, 1.0f));
            out_y -= (int) (edge * (12.0f + seam_t * 70.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 2);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_build_prism_stack_layout(mirrormadness_t *m,
                                         float min_width_t,
                                         float max_width_t,
                                         float offset_t,
                                         float time)
{
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(h, min_width_t);
    float max_px = afm_width_from_param(h, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float drift = afm_lut_sin(m, time * 0.76f) * (float) h * 0.016f;
    int bands = 5;
    int i;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    m->panel_count = 0;
    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.10f, (float) h * 1.10f,
                  1, 0, 1, 0, 62);

    for (i = 0; i < bands; i++) {
        float hh = min_px + (max_px - min_px) * (0.22f + 0.16f * (float) ((i * 3) % 5));
        float ww = (float) w * (0.78f + 0.05f * (float) (i & 1));
        float y = ((float) i + 0.42f) * (float) h / (float) bands - hh * 0.5f + drift * ((float) i - 2.0f) * 0.45f;
        float x = ((float) w - ww) * 0.5f + offset_t * (float) w * 0.035f * ((float) i - 2.0f);
        int front = (i == 2);
        afm_add_panel(m, x, y, ww, hh,
                      cx + ((float) i - 2.0f) * (float) w * 0.035f,
                      y + hh * 0.5f,
                      ww * 1.02f,
                      hh * (1.50f + 0.18f * (float) (i & 1)),
                      i & 1, (i + 1) & 1, front ? 0 : 1, 0, front ? 0 : 40 + i * 8);
    }
}

static void afm_render_corner_kaleido(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float max_r = sqrtf((float) w * (float) w + (float) h * (float) h);
    int sectors = 4 + (int) ((max_width_t * 10.0f + min_width_t * 4.0f) + 0.5f);
    float sector_w;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;
    if (sectors < 4) sectors = 4;
    if (sectors > 24) sectors = 24;
    sector_w = (AFM_TWO_PI * 0.25f) / (float) sectors;
#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            int right = x >= (w >> 1);
            int bottom = y >= (h >> 1);
            float cx = right ? (float) (w - 1) : 0.0f;
            float cy = bottom ? (float) (h - 1) : 0.0f;
            float dx = px - cx;
            float dy = py - cy;
            float r = sqrtf(dx * dx + dy * dy);
            float theta = atan2f(bottom ? -dy : dy, right ? -dx : dx) + AFM_TWO_PI + offset_t * 0.35f + time * 0.20f;
            float b = floorf(theta / sector_w);
            float l = theta - b * sector_w;
            float ml = (((int) b) & 1) ? (sector_w - l) : l;
            float theta2 = b * sector_w + ml - offset_t * 0.35f;
            float src_r = afm_reflect_coord(r + time * max_r * 0.010f, max_r);
            float sx = cx + (right ? -1.0f : 1.0f) * cosf(theta2) * src_r;
            float syf = cy + (bottom ? -1.0f : 1.0f) * sinf(theta2) * src_r;
            float edge = l < sector_w - l ? l : sector_w - l;
            int pos = y * w + x;
            int out_y, out_u, out_v;
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (sector_w * (0.12f + seam_t * 0.12f)), 0.0f, 1.0f));
            out_y -= (int) (edge * (12.0f + seam_t * 74.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_build_film_gate_layout(mirrormadness_t *m,
                                       float min_width_t,
                                       float max_width_t,
                                       float offset_t,
                                       float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float side_w;
    float center_w;
    float center_h;
    float gate_h;
    float drift = afm_lut_sin(m, time * 0.91f) * (float) h * 0.012f;
    int i;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    m->panel_count = 0;
    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.08f, (float) h * 1.08f,
                  1, 0, 1, 0, 70);

    center_w = afm_size_from_range((float) w * 0.70f, min_px * 1.6f, max_px * 2.2f);
    center_h = afm_size_from_range((float) h * 0.78f, min_px * 1.4f, max_px * 2.2f);
    side_w = afm_size_from_range((float) w * 0.075f, min_px * 0.55f, max_px * 0.80f);
    gate_h = afm_size_from_range((float) h * 0.115f, min_px * 0.50f, max_px * 0.85f);

    for (i = 0; i < 5; i++) {
        float yy = (float) i * (float) h * 0.20f + drift - gate_h * 0.10f;
        afm_add_panel(m, side_w * 0.22f + offset_t * (float) w * 0.020f, yy, side_w, gate_h,
                      (float) w * 0.28f, yy + gate_h * 0.5f, side_w * 1.8f, gate_h * 1.5f,
                      i & 1, 0, 1, 0, 86);
        afm_add_panel(m, (float) w - side_w * 1.22f - offset_t * (float) w * 0.020f, yy + gate_h * 0.24f, side_w, gate_h,
                      (float) w * 0.72f, yy + gate_h * 0.5f, side_w * 1.8f, gate_h * 1.5f,
                      (i + 1) & 1, 0, 1, 0, 86);
    }

    afm_add_panel(m, cx - center_w * 0.5f, cy - center_h * 0.5f,
                  center_w, center_h,
                  cx, cy, center_w * 1.02f, center_h * 1.02f,
                  0, 0, 0, 0, 0);
}


static void afm_render_elastic_strip_mirror(mirrormadness_t *m,
                                            VJFrame *frame,
                                            float min_width_t,
                                            float max_width_t,
                                            float offset_t,
                                            float seam_t,
                                            float mono_t,
                                            float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float stripe_w;
    float amp = (float) w * (0.020f + max_width_t * 0.085f);
    float bend2 = (float) h * (0.010f + min_width_t * 0.030f);
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    stripe_w = min_px + (max_px - min_px) * 0.46f;
    if (stripe_w < 4.0f) stripe_w = 4.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float yf = (float) y;
        float wave = sinf(yf * (0.008f + max_width_t * 0.010f) + time * 1.40f) * amp;
        float wave_b = sinf(yf * 0.019f - time * 0.91f) * amp * 0.38f;
        for (x = 0; x < w; x++) {
            float xf = (float) x;
            float coord = xf + wave + wave_b + offset_t * (float) w * 0.42f;
            float bandf = floorf(coord / stripe_w);
            int band = (int) bandf;
            float local = coord - bandf * stripe_w;
            float u = local / stripe_w;
            float folded = (band & 1) ? (1.0f - u) : u;
            float lens = (u - 0.5f);
            float syf = yf + sinf((float) band * 0.77f + xf * 0.008f + time * 1.7f) * bend2;
            float sx = afm_reflect_coord(bandf * stripe_w + folded * stripe_w - wave * 0.36f, (float) (w - 1));
            float edge = local < stripe_w - local ? local : stripe_w - local;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            sx += lens * lens * lens * stripe_w * (0.45f + seam_t * 0.42f);
            syf = afm_reflect_coord(syf, (float) (h - 1));

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.0f + seam_t * 7.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (10.0f + seam_t * 72.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_sliced_lens_mirror(mirrormadness_t *m,
                                          VJFrame *frame,
                                          float min_width_t,
                                          float max_width_t,
                                          float offset_t,
                                          float seam_t,
                                          float mono_t,
                                          float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float stripe_w;
    float cy = (float) h * 0.5f;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    stripe_w = min_px + (max_px - min_px) * 0.58f;
    if (stripe_w < 5.0f) stripe_w = 5.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float ny = ((float) y - cy) / (cy + 1.0f);
        for (x = 0; x < w; x++) {
            float coord = (float) x + offset_t * (float) w * 0.31f + sinf((float) y * 0.006f + time) * stripe_w * 0.18f;
            float bandf = floorf(coord / stripe_w);
            int band = (int) bandf;
            float local = coord - bandf * stripe_w;
            float u = local / stripe_w;
            float f = u * 2.0f - 1.0f;
            float lens = f * (1.0f - f * f * 0.62f);
            float folded = (band & 1) ? -lens : lens;
            float sx = afm_reflect_coord(bandf * stripe_w + (folded * 0.5f + 0.5f) * stripe_w, (float) (w - 1));
            float syf = afm_reflect_coord((float) y + ny * ny * stripe_w * (0.28f + max_width_t * 0.44f) * (ny < 0.0f ? -1.0f : 1.0f), (float) (h - 1));
            float edge = local < stripe_w - local ? local : stripe_w - local;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 8.5f), 0.0f, 1.0f));
            out_y -= (int) (edge * (14.0f + seam_t * 78.0f));
            out_y += (int) ((1.0f - afm_absf(f)) * (3.0f + seam_t * 18.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 2);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_torn_poster_mirror(mirrormadness_t *m,
                                          VJFrame *frame,
                                          float min_width_t,
                                          float max_width_t,
                                          float offset_t,
                                          float seam_t,
                                          float mono_t,
                                          float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(h, min_width_t);
    float max_px = afm_width_from_param(h, max_width_t);
    float strip_h;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    strip_h = min_px + (max_px - min_px) * 0.44f;
    if (strip_h < 4.0f) strip_h = 4.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float yf = (float) y + offset_t * (float) h * 0.24f;
        float bandf = floorf(yf / strip_h);
        int band = (int) bandf;
        float local_y = yf - bandf * strip_h;
        float tear = (afm_hash01((uint32_t) band * 0x9e3779b9U) - 0.5f) * (float) w * (0.12f + max_width_t * 0.18f);
        float tear2 = sinf((float) band * 0.91f + time * 1.25f) * (float) w * (0.03f + min_width_t * 0.05f);
        float edge_y = local_y < strip_h - local_y ? local_y : strip_h - local_y;
        for (x = 0; x < w; x++) {
            float xf = (float) x;
            float ridge = sinf(xf * (0.012f + max_width_t * 0.014f) + (float) band * 1.7f + time) * strip_h * 0.08f;
            float folded_y = (band & 1) ? (strip_h - local_y) : local_y;
            float sx = afm_reflect_coord(xf + tear + tear2 + ridge * 1.5f, (float) (w - 1));
            float syf = afm_reflect_coord(bandf * strip_h + folded_y + ridge, (float) (h - 1));
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;
            float edge = 1.0f - afm_smooth01(afm_clampf(edge_y / (1.0f + seam_t * 7.5f), 0.0f, 1.0f));

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            out_y -= (int) (edge * (18.0f + seam_t * 84.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_build_cascade_collage_layout(mirrormadness_t *m,
                                             float min_width_t,
                                             float max_width_t,
                                             float offset_t,
                                             float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float drift = sinf(time * 0.90f) * min_dim * 0.020f;
    int i;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }

    m->panel_count = 0;
    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.10f, (float) h * 1.10f,
                  1, 0, 1, 0, 58);

    for (i = 0; i < 9; i++) {
        float k = (float) i;
        float ww = afm_size_from_range((float) w * (0.30f + 0.045f * k), min_px * 1.1f, max_px * 1.9f);
        float hh = afm_size_from_range((float) h * (0.22f + 0.030f * (8.0f - k)), min_px, max_px * 1.5f);
        float x = -ww * 0.20f + (float) w * 0.095f * k + offset_t * (float) w * 0.030f * (1.0f - k * 0.07f);
        float y = -hh * 0.05f + (float) h * 0.105f * k + drift * ((i & 1) ? -1.0f : 1.0f);
        float srcx = (float) w * (0.22f + 0.070f * (float) (i % 5));
        float srcy = (float) h * (0.18f + 0.080f * (float) (i % 4));
        afm_add_panel(m, x, y, ww, hh,
                      srcx, srcy, ww * (1.18f + 0.05f * (float) (i & 3)), hh * 1.18f,
                      i & 1, (i >> 1) & 1, 1, 0, 48 + i * 5);
    }

    {
        float fw = afm_size_from_range((float) w * 0.54f, min_px * 1.6f, max_px * 2.3f);
        float fh = afm_size_from_range((float) h * 0.48f, min_px * 1.4f, max_px * 2.1f);
        afm_add_panel(m, cx - fw * 0.50f + offset_t * (float) w * 0.035f,
                      cy - fh * 0.50f - drift * 0.35f,
                      fw, fh, cx, cy, fw * 1.05f, fh * 1.05f,
                      0, 0, 0, 0, 0);
    }
}

static void afm_render_spiral_stair_mirror(mirrormadness_t *m,
                                           VJFrame *frame,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float seam_t,
                                           float mono_t,
                                           float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float maxr = (float) (w > h ? w : h) * 0.72f;
    float band = afm_width_from_param((w < h) ? w : h, min_width_t + (max_width_t - min_width_t) * 0.56f);
    float twist = 2.8f + max_width_t * 7.2f;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (band < 5.0f)
        band = 5.0f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float dx = (float) x - cx;
            float dy = (float) y - cy;
            float r = sqrtf(dx * dx + dy * dy) + 1.0f;
            float a = atan2f(dy, dx);
            float stair = (r + (a + 3.14159265f) * band * twist + offset_t * maxr + time * maxr * 0.012f) / band;
            float layerf = floorf(stair);
            int layer = (int) layerf;
            float local = (stair - layerf) * band;
            float mt = (layer & 1) ? (band - local) : local;
            float aa = a + ((float) layer * 0.19f) + offset_t * 0.35f;
            float rr = afm_reflect_coord(layerf * band + mt, maxr);
            float sx = afm_reflect_coord(cx + cosf(aa) * rr, (float) (w - 1));
            float syf = afm_reflect_coord(cy + sinf(aa) * rr, (float) (h - 1));
            float edge = local < band - local ? local : band - local;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.2f + seam_t * 8.0f), 0.0f, 1.0f));
            out_y -= (int) (edge * (16.0f + seam_t * 88.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount >> 1);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}




static void afm_build_sliding_doors_layout(mirrormadness_t *m,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float time)
{
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w, min_width_t);
    float max_px = afm_width_from_param(w, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float wave = afm_lut_sin(m, time * 0.82f + offset_t * AFM_TWO_PI) * 0.5f + 0.5f;
    float open = ((offset_t - 0.5f) * 0.42f + (wave - 0.5f) * 0.10f) * (float) w;
    float door_w;
    float side_w;
    float thin_w;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    door_w = (float) afm_size_from_range((float) w * 0.42f, min_px * 1.25f, max_px * 2.30f);
    side_w = (float) afm_size_from_range((float) w * 0.24f, min_px, max_px * 1.35f);
    thin_w = (float) afm_size_from_range((float) w * 0.075f, min_px * 0.45f, max_px * 0.70f);

    m->panel_count = 0;

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.06f, (float) h * 1.06f,
                  1, 0, 1, 0, 62);

    afm_add_panel(m, -side_w * 0.18f + open * 0.18f, 0.0f, side_w, (float) h,
                  cx + side_w * 0.50f, cy, side_w * 1.35f, (float) h * 1.05f,
                  1, 0, 1, 0, 48);

    afm_add_panel(m, (float) w - side_w * 0.82f - open * 0.18f, 0.0f, side_w, (float) h,
                  cx - side_w * 0.50f, cy, side_w * 1.35f, (float) h * 1.05f,
                  0, 0, 1, 0, 48);

    afm_add_panel(m, cx - door_w + open, 0.0f, door_w, (float) h,
                  cx + door_w * 0.24f, cy, door_w * 1.16f, (float) h * 1.02f,
                  1, 0, 0, 0, 0);

    afm_add_panel(m, cx - open, 0.0f, door_w, (float) h,
                  cx - door_w * 0.24f, cy, door_w * 1.16f, (float) h * 1.02f,
                  0, 0, 0, 0, 0);

    afm_add_panel(m, cx - thin_w * 0.50f + open * 0.25f, 0.0f, thin_w, (float) h,
                  cx, cy, thin_w * 1.40f, (float) h,
                  1, 0, 0, 0, 18);
}

static void afm_build_spiral_shards_layout(mirrormadness_t *m,
                                           float min_width_t,
                                           float max_width_t,
                                           float offset_t,
                                           float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float phase = time * 0.55f + offset_t * AFM_TWO_PI;
    int i;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.08f, (float) h * 1.08f,
                  1, 0, 1, 0, 64);

    for (i = 0; i < 18; i++) {
        float k = (float) i / 17.0f;
        float a = phase + k * 10.85f;
        float s = afm_lut_sin(m, a);
        float c = afm_lut_sin(m, a + AFM_TWO_PI * 0.25f);
        float r = min_dim * (0.04f + k * 0.58f);
        float ww = (float) afm_size_from_range(min_dim * (0.42f - k * 0.20f), min_px * 0.85f, max_px * 2.05f);
        float hh = (float) afm_size_from_range(min_dim * (0.26f - k * 0.10f), min_px * 0.70f, max_px * 1.36f);
        float px = cx + c * r - ww * 0.5f;
        float py = cy + s * r - hh * 0.5f;
        int mono = (i < 14);
        int shade = 24 + (i * 5);

        afm_add_panel(m, px, py, ww, hh,
                      cx - c * r * 0.44f, cy - s * r * 0.44f,
                      ww * (1.06f + k * 0.22f), hh * (1.04f + k * 0.18f),
                      (i & 1), ((i + 1) & 1), mono, 0, shade);
    }

    {
        float ww = (float) afm_size_from_range((float) w * 0.34f, min_px, max_px * 1.70f);
        float hh = (float) afm_size_from_range((float) h * 0.44f, min_px, max_px * 1.90f);
        afm_add_panel(m, cx - ww * 0.50f, cy - hh * 0.50f, ww, hh,
                      cx, cy, ww * 1.10f, hh * 1.10f,
                      0, 0, 0, 0, 0);
    }
}

static void afm_build_nested_film_gates_layout(mirrormadness_t *m,
                                               float min_width_t,
                                               float max_width_t,
                                               float offset_t,
                                               float time)
{
    int w = m->w;
    int h = m->h;
    float min_dim = (float) (w < h ? w : h);
    float max_dim = (float) (w > h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) max_dim, max_width_t);
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float wobble = afm_lut_sin(m, time * 0.64f + offset_t * AFM_TWO_PI) * min_dim * 0.022f;
    int i;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    m->panel_count = 0;

    afm_add_panel(m, 0.0f, 0.0f, (float) w, (float) h,
                  cx, cy, (float) w * 1.12f, (float) h * 1.12f,
                  1, 0, 1, 0, 70);

    for (i = 0; i < 7; i++) {
        float k = (float) i / 6.0f;
        float scale = 1.0f - k * 0.125f;
        float ww = afm_size_from_range((float) w * (0.86f * scale), min_px * 2.0f, max_px * 3.80f);
        float hh = afm_size_from_range((float) h * (0.74f * scale), min_px * 1.6f, max_px * 3.20f);
        float x = cx - ww * 0.5f + wobble * (k - 0.45f);
        float y = cy - hh * 0.5f - wobble * (0.55f - k);
        int mono = (i < 5);
        int shade = 58 - i * 4;

        afm_add_panel(m, x, y, ww, hh,
                      cx + wobble * k, cy - wobble * k,
                      ww * (1.08f + k * 0.10f), hh * (1.08f + k * 0.10f),
                      i & 1, (i + 1) & 1, mono, 0, shade);
    }

    {
        float ww = afm_size_from_range((float) w * 0.42f, min_px * 1.3f, max_px * 2.0f);
        float hh = afm_size_from_range((float) h * 0.38f, min_px * 1.1f, max_px * 1.8f);
        afm_add_panel(m, cx - ww * 0.5f, cy - hh * 0.5f, ww, hh,
                      cx, cy, ww * 1.06f, hh * 1.06f,
                      0, 0, 0, 0, 0);
    }
}

static void afm_render_waterfall_strips(mirrormadness_t *m,
                                        VJFrame *frame,
                                        float min_width_t,
                                        float max_width_t,
                                        float offset_t,
                                        float seam_t,
                                        float mono_t,
                                        float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(h, min_width_t);
    float max_px = afm_width_from_param(h, max_width_t);
    float strip_h;
    float phase;
    float seam_px;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    strip_h = min_px + (max_px - min_px) * 0.42f;
    if (strip_h < 3.0f)
        strip_h = 3.0f;

    phase = offset_t * (float) h * 0.46f + time * (float) h * 0.035f;
    seam_px = 1.1f + seam_t * 6.8f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float yy = (float) y + 0.5f + phase;
        float bandf = floorf(yy / strip_h);
        int band = (int) bandf;
        float local = yy - bandf * strip_h;
        float mirrored = (band & 1) ? (strip_h - local) : local;
        float sy_base = afm_reflect_coord(bandf * strip_h + mirrored - phase, (float) (h - 1));
        float edge = local < strip_h - local ? local : strip_h - local;
        float ed = 1.0f - afm_smooth01(afm_clampf(edge / seam_px, 0.0f, 1.0f));
        float shift = (float) band * (min_px * 0.74f + offset_t * max_px * 0.34f);
        float wave = afm_lut_sin(m, time * 1.35f + (float) band * 0.73f) * max_px * 0.20f;

        for (x = 0; x < w; x++) {
            float sx = afm_reflect_coord((float) x + shift + wave, (float) (w - 1));
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, sy_base, &out_y, &out_u, &out_v);
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
            out_y -= (int) (ed * (12.0f + seam_t * 70.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_fan_blades(mirrormadness_t *m,
                                  VJFrame *frame,
                                  float min_width_t,
                                  float max_width_t,
                                  float offset_t,
                                  float seam_t,
                                  float mono_t,
                                  float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * (-0.18f + offset_t * 0.36f);
    float cy = (float) h * 0.55f;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float blades_f;
    float blade_w;
    float phase;
    float seam_ang;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    blades_f = 5.0f + afm_clampf((max_px - min_px) / (min_dim * 0.18f + 1.0f), 0.0f, 1.0f) * 7.0f;
    blade_w = AFM_TWO_PI / blades_f;
    phase = offset_t * AFM_TWO_PI + time * 0.42f;
    seam_ang = 0.018f + seam_t * 0.050f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;

        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float a = atan2f(dy, dx) + phase;
            float bandf;
            int band;
            float local;
            float mirrored;
            float src_a;
            float sx;
            float syf;
            float edge;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            if (a < 0.0f)
                a += AFM_TWO_PI * (1.0f + floorf(-a * AFM_INV_TWO_PI));
            bandf = floorf(a / blade_w);
            band = (int) bandf;
            local = a - bandf * blade_w;
            mirrored = (band & 1) ? (blade_w - local) : local;
            src_a = bandf * blade_w + mirrored - phase;
            sx = cx + cosf(src_a) * r;
            syf = cy + sinf(src_a) * r;
            sx = afm_reflect_coord(sx, (float) (w - 1));
            syf = afm_reflect_coord(syf, (float) (h - 1));

            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            edge = local < blade_w - local ? local : blade_w - local;
            edge = 1.0f - afm_smooth01(afm_clampf(edge / seam_ang, 0.0f, 1.0f));
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)] - (int) (edge * (10.0f + seam_t * 64.0f));
            afm_apply_mono_amount(&out_u, &out_v, mono_amount);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static void afm_render_diamond_tunnel(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict sy = m->src_y;
    const uint8_t *restrict su = m->src_u;
    const uint8_t *restrict sv = m->src_v;
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_dim = (float) (w < h ? w : h);
    float min_px = afm_width_from_param((int) min_dim, min_width_t);
    float max_px = afm_width_from_param((int) min_dim, max_width_t);
    float band_w;
    float phase;
    float seam_px;
    int mono_amount = (int) (mono_t * 255.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px;
        max_px = min_px;
        min_px = t;
    }

    band_w = min_px + (max_px - min_px) * 0.52f;
    if (band_w < 5.0f)
        band_w = 5.0f;
    phase = offset_t * band_w * 1.8f + time * band_w * 0.065f;
    seam_px = 1.4f + seam_t * 7.2f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = ((float) y + 0.5f) - cy;
        float ady = afm_absf(dy);

        for (x = 0; x < w; x++) {
            float dx = ((float) x + 0.5f) - cx;
            float d = afm_absf(dx) + ady;
            float dd = d + phase;
            float bandf = floorf(dd / band_w);
            int band = (int) bandf;
            float local = dd - bandf * band_w;
            float mirrored = (band & 1) ? (band_w - local) : local;
            float src_d = bandf * band_w + mirrored - phase;
            float scale = (d > 0.001f) ? (src_d / d) : 0.0f;
            float twist = afm_lut_sin(m, time * 0.85f + bandf * 0.41f) * 0.032f;
            float sx = cx + (dx * scale - dy * twist * scale);
            float syf = cy + (dy * scale + dx * twist * scale);
            float edge = local < band_w - local ? local : band_w - local;
            int pos = y * w + x;
            int out_y;
            int out_u;
            int out_v;

            sx = afm_reflect_coord(sx, (float) (w - 1));
            syf = afm_reflect_coord(syf, (float) (h - 1));
            afm_sample_yuv_layer(sy, su, sv, w, h, sx, syf, &out_y, &out_u, &out_v);
            edge = 1.0f - afm_smooth01(afm_clampf(edge / seam_px, 0.0f, 1.0f));
            out_y = m->tone_lut[afm_clampi(out_y, 0, 255)] - (int) (edge * (12.0f + seam_t * 74.0f));
            if (d < band_w * 0.80f)
                out_y += (int) ((1.0f - d / (band_w * 0.80f)) * 12.0f);
            afm_apply_mono_amount(&out_u, &out_v, mono_amount);
            Y[pos] = afm_u8i(out_y);
            U[pos] = afm_u8i(out_u);
            V[pos] = afm_u8i(out_v);
        }
    }
}

static inline void afm_emit_sample(mirrormadness_t *m,
                                   VJFrame *frame,
                                   int pos,
                                   float sx,
                                   float syf,
                                   float edge,
                                   float seam_t,
                                   int mono_amount,
                                   int extra_shade)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    int out_y;
    int out_u;
    int out_v;

    sx = afm_reflect_coord(sx, (float) (m->w - 1));
    syf = afm_reflect_coord(syf, (float) (m->h - 1));

    afm_sample_yuv_layer(m->src_y, m->src_u, m->src_v, m->w, m->h, sx, syf, &out_y, &out_u, &out_v);
    out_y = m->tone_lut[afm_clampi(out_y, 0, 255)];
    out_y -= (int) (edge * (10.0f + seam_t * 72.0f));
    out_y -= extra_shade;
    afm_apply_mono_amount(&out_u, &out_v, mono_amount);
    Y[pos] = afm_u8i(out_y);
    U[pos] = afm_u8i(out_u);
    V[pos] = afm_u8i(out_v);
}


static inline void afm_emit_background(mirrormadness_t *m,
                                       VJFrame *frame,
                                       int pos,
                                       float seam_t,
                                       int background_source)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    if (background_source) {
        Y[pos] = m->src_y[pos];
        U[pos] = m->src_u[pos];
        V[pos] = m->src_v[pos];
    }
    else {
        Y[pos] = (uint8_t) (3 + (int) (seam_t * 6.0f));
        U[pos] = 128;
        V[pos] = 128;
    }
}

static void afm_render_voronoi_plates(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time)
{
    int w = m->w;
    int h = m->h;
    int min_dim = w < h ? w : h;
    float min_px = afm_width_from_param(min_dim, min_width_t);
    float max_px = afm_width_from_param(min_dim, max_width_t);
    float cell;
    float inv_cell;
    int mono_amount = (int) (mono_t * 210.0f + 0.5f);
    int y;

    if (max_px < min_px) {
        float t = max_px; max_px = min_px; min_px = t;
    }

    cell = min_px + (max_px - min_px) * 0.72f;
    if (cell < 24.0f) cell = 24.0f;
    if (cell > (float) min_dim * 0.58f) cell = (float) min_dim * 0.58f;
    inv_cell = 1.0f / cell;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            int gx = afm_floor_to_int((px + offset_t * cell * 0.7f + time * cell * 0.05f) * inv_cell);
            int gy = afm_floor_to_int((py - offset_t * cell * 0.4f + time * cell * 0.035f) * inv_cell);
            float best_d = 1.0e30f;
            float second_d = 1.0e30f;
            float best_cx = 0.0f;
            float best_cy = 0.0f;
            uint32_t best_hash = 0;
            int oy;
            int ox;

            for (oy = -1; oy <= 1; oy++) {
                for (ox = -1; ox <= 1; ox++) {
                    int cx_i = gx + ox;
                    int cy_i = gy + oy;
                    uint32_t hs = afm_hash_u32((uint32_t) cx_i * 0x632be59bU ^ (uint32_t) cy_i * 0x85157af5U ^ 0x47d18f3dU);
                    float jx = (afm_hash01(hs ^ 0x1234567U) - 0.5f) * cell * 0.56f;
                    float jy = (afm_hash01(hs ^ 0x91e7a3dU) - 0.5f) * cell * 0.56f;
                    float ccx = ((float) cx_i + 0.5f) * cell - offset_t * cell * 0.7f - time * cell * 0.05f + jx;
                    float ccy = ((float) cy_i + 0.5f) * cell + offset_t * cell * 0.4f - time * cell * 0.035f + jy;
                    float dx = px - ccx;
                    float dy = py - ccy;
                    float d = dx * dx + dy * dy;
                    if (d < best_d) {
                        second_d = best_d;
                        best_d = d;
                        best_cx = ccx;
                        best_cy = ccy;
                        best_hash = hs;
                    }
                    else if (d < second_d) {
                        second_d = d;
                    }
                }
            }

            {
                float dx = px - best_cx;
                float dy = py - best_cy;
                int axis = (int) ((best_hash >> 3) & 3U);
                float sx = px;
                float syf = py;
                float edge = 1.0f - afm_smooth01(afm_clampf((second_d - best_d) / (cell * cell * (0.08f + seam_t * 0.16f)), 0.0f, 1.0f));
                int shade = (int) (afm_hash01(best_hash ^ 0xa53c91U) * 22.0f * seam_t);
                if (axis == 0) {
                    sx = best_cx - dx;
                    syf = best_cy + dy;
                }
                else if (axis == 1) {
                    sx = best_cx + dx;
                    syf = best_cy - dy;
                }
                else if (axis == 2) {
                    sx = best_cx + dy;
                    syf = best_cy + dx;
                }
                else {
                    sx = best_cx - dy;
                    syf = best_cy - dx;
                }
                afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, shade);
            }
        }
    }
}

static void afm_render_polar_barcode(mirrormadness_t *m,
                                     VJFrame *frame,
                                     float min_width_t,
                                     float max_width_t,
                                     float offset_t,
                                     float seam_t,
                                     float mono_t,
                                     float time)
{
    int w = m->w;
    int h = m->h;
    float cx = (float) w * 0.5f;
    float cy = (float) h * 0.5f;
    float min_px = afm_width_from_param((w < h ? w : h), min_width_t);
    float max_px = afm_width_from_param((w < h ? w : h), max_width_t);
    float avg_px;
    float bands;
    float band_w;
    float phase;
    int mono_amount = (int) (mono_t * 220.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    avg_px = min_px + (max_px - min_px) * 0.45f;
    bands = afm_clampf(((float) (w + h) * 0.42f) / (avg_px + 1.0f), 7.0f, 96.0f);
    band_w = AFM_TWO_PI / bands;
    phase = offset_t * 1.7f + time * 0.28f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float dy = (float) y + 0.5f - cy;
        for (x = 0; x < w; x++) {
            float dx = (float) x + 0.5f - cx;
            float r = sqrtf(dx * dx + dy * dy);
            float a = atan2f(dy, dx) + AFM_TWO_PI + phase;
            float bf = floorf(a / band_w);
            int bi = (int) bf;
            float l = a - bf * band_w;
            float sh = 0.62f + afm_hash01((uint32_t) bi * 0x9e3779b9U) * 0.76f;
            float local_w = band_w * sh;
            float local = fmodf(l, local_w);
            float mirrored = (bi & 1) ? (local_w - local) : local;
            float src_a = bf * band_w + mirrored - phase;
            float edge = local < local_w - local ? local : local_w - local;
            int pos = y * w + x;
            float sx = cx + cosf(src_a) * r;
            float syf = cy + sinf(src_a) * r;
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (local_w * (0.10f + seam_t * 0.13f)), 0.0f, 1.0f));
            afm_emit_sample(m, frame, pos, sx, syf, edge, seam_t, mono_amount, 0);
        }
    }
}

static void afm_render_ribbon_lattice(mirrormadness_t *m,
                                      VJFrame *frame,
                                      float min_width_t,
                                      float max_width_t,
                                      float offset_t,
                                      float seam_t,
                                      float mono_t,
                                      float time,
                                      int background_source)
{
    int w = m->w;
    int h = m->h;
    float diag = sqrtf((float) (w * w + h * h));
    float min_px = afm_width_from_param((w < h ? w : h), min_width_t);
    float max_px = afm_width_from_param((w < h ? w : h), max_width_t);
    float band_w;
    float gap;
    int mono_amount = (int) (mono_t * 210.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    band_w = min_px + (max_px - min_px) * 0.58f;
    if (band_w < 10.0f) band_w = 10.0f;
    gap = band_w * (1.65f - max_width_t * 0.45f);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            float u1 = (px + py) * 0.70710678f + offset_t * diag * 0.22f + time * diag * 0.010f;
            float u2 = (px - py) * 0.70710678f - offset_t * diag * 0.18f - time * diag * 0.008f;
            float p1 = fmodf(u1 + gap * 2048.0f, gap);
            float p2 = fmodf(u2 + gap * 2048.0f, gap);
            int use_first = p1 < p2;
            float p = use_first ? p1 : p2;
            float center = p - band_w * 0.5f;
            float edge;
            float sx;
            float syf;
            int shade;
            if (p > band_w) {
                afm_emit_background(m, frame, y * w + x, seam_t, background_source);
                continue;
            }
            else {
                if (use_first) {
                    sx = px - center * 0.70710678f;
                    syf = py - center * 0.70710678f;
                }
                else {
                    sx = px - center * 0.70710678f;
                    syf = py + center * 0.70710678f;
                }
                edge = p < band_w - p ? p : band_w - p;
                edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.6f + seam_t * 8.0f), 0.0f, 1.0f));
                shade = 0;
            }
            afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, shade);
        }
    }
}

static void afm_render_woven_mirror(mirrormadness_t *m,
                                    VJFrame *frame,
                                    float min_width_t,
                                    float max_width_t,
                                    float offset_t,
                                    float seam_t,
                                    float mono_t,
                                    float time,
                                    int background_source)
{
    int w = m->w;
    int h = m->h;
    float min_x = afm_width_from_param(w, min_width_t);
    float max_x = afm_width_from_param(w, max_width_t);
    float min_y = afm_width_from_param(h, min_width_t);
    float max_y = afm_width_from_param(h, max_width_t);
    float bwx;
    float bwy;
    float pitch_x;
    float pitch_y;
    int mono_amount = (int) (mono_t * 220.0f + 0.5f);
    int y;

    if (max_x < min_x) { float t = max_x; max_x = min_x; min_x = t; }
    if (max_y < min_y) { float t = max_y; max_y = min_y; min_y = t; }
    bwx = min_x + (max_x - min_x) * 0.50f;
    bwy = min_y + (max_y - min_y) * 0.50f;
    if (bwx < 8.0f) bwx = 8.0f;
    if (bwy < 8.0f) bwy = 8.0f;
    pitch_x = bwx * 1.72f;
    pitch_y = bwy * 1.72f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        float py = (float) y + 0.5f;
        float ly = fmodf(py + offset_t * pitch_y * 0.4f + time * pitch_y * 0.02f + pitch_y * 1024.0f, pitch_y);
        int hy = (int) floorf((py + offset_t * pitch_y * 0.4f + pitch_y * 1024.0f) / pitch_y);
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float lx = fmodf(px - offset_t * pitch_x * 0.5f - time * pitch_x * 0.018f + pitch_x * 1024.0f, pitch_x);
            int hx = (int) floorf((px - offset_t * pitch_x * 0.5f + pitch_x * 1024.0f) / pitch_x);
            int in_x = lx < bwx;
            int in_y = ly < bwy;
            int use_x = in_x && (!in_y || (((hx + hy) & 1) == 0));
            float sx;
            float syf;
            float edge = 0.0f;
            int shade = 0;
            if (use_x) {
                float local = lx;
                sx = ((hx & 1) ? (float) (w - 1) - px : px);
                syf = afm_reflect_coord(py + (local - bwx * 0.5f) * 0.62f, (float) (h - 1));
                edge = local < bwx - local ? local : bwx - local;
            }
            else if (in_y) {
                float local = ly;
                sx = afm_reflect_coord(px + (local - bwy * 0.5f) * 0.62f, (float) (w - 1));
                syf = ((hy & 1) ? (float) (h - 1) - py : py);
                edge = local < bwy - local ? local : bwy - local;
            }
            else {
                afm_emit_background(m, frame, y * w + x, seam_t, background_source);
                continue;
            }
            if (edge > 0.0f)
                edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.5f + seam_t * 7.5f), 0.0f, 1.0f));
            afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, shade);
        }
    }
}

static void afm_render_corner_tunnel(mirrormadness_t *m,
                                     VJFrame *frame,
                                     float min_width_t,
                                     float max_width_t,
                                     float offset_t,
                                     float seam_t,
                                     float mono_t,
                                     float time)
{
    int w = m->w;
    int h = m->h;
    int corner = ((int) ((offset_t + 0.825f) * 2.0f)) & 3;
    float ox = (corner & 1) ? (float) (w - 1) : 0.0f;
    float oy = (corner & 2) ? (float) (h - 1) : 0.0f;
    float min_px = afm_width_from_param((w < h ? w : h), min_width_t);
    float max_px = afm_width_from_param((w < h ? w : h), max_width_t);
    float band_w;
    float phase;
    int mono_amount = (int) (mono_t * 230.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    band_w = min_px + (max_px - min_px) * 0.62f;
    if (band_w < 8.0f) band_w = 8.0f;
    phase = time * band_w * 0.05f + offset_t * band_w * 1.6f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float dx = afm_absf(((float) x + 0.5f) - ox);
            float dy = afm_absf(((float) y + 0.5f) - oy);
            float d = dx > dy ? dx : dy;
            float dd = d + phase;
            float bf = floorf(dd / band_w);
            int bi = (int) bf;
            float local = dd - bf * band_w;
            float mirrored = (bi & 1) ? (band_w - local) : local;
            float src_d = bf * band_w + mirrored - phase;
            float scale = d > 0.001f ? (src_d / d) : 0.0f;
            float sx = ox + (((float) x + 0.5f) - ox) * scale;
            float syf = oy + (((float) y + 0.5f) - oy) * scale;
            float edge = local < band_w - local ? local : band_w - local;
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.5f + seam_t * 8.5f), 0.0f, 1.0f));
            afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, (int) (bf * 0.75f));
        }
    }
}

static void afm_render_lens_array(mirrormadness_t *m,
                                  VJFrame *frame,
                                  float min_width_t,
                                  float max_width_t,
                                  float offset_t,
                                  float seam_t,
                                  float mono_t,
                                  float time,
                                  int background_source)
{
    int w = m->w;
    int h = m->h;
    int min_dim = w < h ? w : h;
    float min_px = afm_width_from_param(min_dim, min_width_t);
    float max_px = afm_width_from_param(min_dim, max_width_t);
    float cell;
    float inv_cell;
    int mono_amount = (int) (mono_t * 190.0f + 0.5f);
    int y;

    (void) time;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    cell = min_px + (max_px - min_px) * 0.66f;
    if (cell < 28.0f) cell = 28.0f;
    inv_cell = 1.0f / cell;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            int gx = afm_floor_to_int((px + offset_t * cell * 0.35f) * inv_cell);
            int gy = afm_floor_to_int((py - offset_t * cell * 0.25f) * inv_cell);
            uint32_t hs = afm_hash_u32((uint32_t) gx * 0x632be59bU ^ (uint32_t) gy * 0x85157af5U ^ 0x5bd1e995U);
            float cx = ((float) gx + 0.5f) * cell - offset_t * cell * 0.35f + (afm_hash01(hs ^ 0x11U) - 0.5f) * cell * 0.26f;
            float cy = ((float) gy + 0.5f) * cell + offset_t * cell * 0.25f + (afm_hash01(hs ^ 0x22U) - 0.5f) * cell * 0.26f;
            float rx = cell * (0.42f + afm_hash01(hs ^ 0x33U) * 0.22f);
            float ry = cell * (0.42f + afm_hash01(hs ^ 0x44U) * 0.22f);
            float dx = px - cx;
            float dy = py - cy;
            float q = (dx * dx) / (rx * rx + 1.0f) + (dy * dy) / (ry * ry + 1.0f);
            float mag = 1.0f - afm_clampf(q, 0.0f, 1.0f);
            float sx;
            float syf;
            float edge = 0.0f;
            int shade = 0;
            if (q <= 1.0f) {
                float bend = 0.36f + mag * (0.62f + max_width_t * 0.30f);
                sx = cx - dx * bend;
                syf = cy - dy * bend;
                edge = 1.0f - afm_smooth01(afm_clampf((1.0f - sqrtf(q)) * rx / (1.3f + seam_t * 8.0f), 0.0f, 1.0f));
            }
            else {
                afm_emit_background(m, frame, y * w + x, seam_t, background_source);
                continue;
            }
            afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, shade);
        }
    }
}

static void afm_render_axis_roulette(mirrormadness_t *m,
                                     VJFrame *frame,
                                     float min_width_t,
                                     float max_width_t,
                                     float offset_t,
                                     float seam_t,
                                     float mono_t,
                                     float time)
{
    int w = m->w;
    int h = m->h;
    float min_px = afm_width_from_param(w > h ? w : h, min_width_t);
    float max_px = afm_width_from_param(w > h ? w : h, max_width_t);
    float band_w;
    float phase;
    int mono_amount = (int) (mono_t * 220.0f + 0.5f);
    int y;

    if (max_px < min_px) { float t = max_px; max_px = min_px; min_px = t; }
    band_w = min_px + (max_px - min_px) * 0.48f;
    if (band_w < 10.0f) band_w = 10.0f;
    phase = offset_t * band_w * 1.3f + time * band_w * 0.05f;

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for (y = 0; y < h; y++) {
        int x;
        for (x = 0; x < w; x++) {
            float px = (float) x + 0.5f;
            float py = (float) y + 0.5f;
            float selector = (px * 0.77f + py * 0.41f + phase);
            float bf = floorf(selector / band_w);
            int bi = (int) bf;
            float local = selector - bf * band_w;
            int axis = (bi + ((int) (time * 3.0f))) & 3;
            float sx = px;
            float syf = py;
            float edge;
            if (axis == 0)
                sx = (float) (w - 1) - px;
            else if (axis == 1)
                syf = (float) (h - 1) - py;
            else if (axis == 2) {
                sx = afm_reflect_coord(py * ((float) w / (float) h), (float) (w - 1));
                syf = afm_reflect_coord(px * ((float) h / (float) w), (float) (h - 1));
            }
            else {
                sx = afm_reflect_coord((float) w - py * ((float) w / (float) h), (float) (w - 1));
                syf = afm_reflect_coord((float) h - px * ((float) h / (float) w), (float) (h - 1));
            }
            edge = local < band_w - local ? local : band_w - local;
            edge = 1.0f - afm_smooth01(afm_clampf(edge / (1.4f + seam_t * 8.0f), 0.0f, 1.0f));
            afm_emit_sample(m, frame, y * w + x, sx, syf, edge, seam_t, mono_amount, 0);
        }
    }
}

vj_effect *hyperfold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if (!ve)
        return NULL;

    ve->num_params = AFM_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if (!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if (ve->defaults) free(ve->defaults);
        if (ve->limits[0]) free(ve->limits[0]);
        if (ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_MODE] = 0; ve->limits[1][P_MODE] = AFM_MODE_LAST; ve->defaults[P_MODE] = AFM_MODE_VERTICAL;
    ve->limits[0][P_MINWIDTH] = 0; ve->limits[1][P_MINWIDTH] = 100; ve->defaults[P_MINWIDTH] = 12;
    ve->limits[0][P_MAXWIDTH] = 0; ve->limits[1][P_MAXWIDTH] = 100; ve->defaults[P_MAXWIDTH] = 82;
    ve->limits[0][P_OFFSET] = 0; ve->limits[1][P_OFFSET] = 100; ve->defaults[P_OFFSET] = 50;
    ve->limits[0][P_SEAM] = 0; ve->limits[1][P_SEAM] = 100; ve->defaults[P_SEAM] = 58;
    ve->limits[0][P_SPEED] = -100; ve->limits[1][P_SPEED] = 100; ve->defaults[P_SPEED] = 0;
    ve->limits[0][P_MONO] = 0; ve->limits[1][P_MONO] = 100; ve->defaults[P_MONO] = 0;
    ve->limits[0][P_BG] = 0; ve->limits[1][P_BG] = 1; ve->defaults[P_BG] = 0;

    ve->description = "Mirror Madness";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Minimum Strip Width",
        "Maximum Strip Width",
        "Strip Shift",
        "Seam Darkness",
        "Drift Speed",
        "Monochrome Copies",
        "Background"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Vertical",
        "Barcode Mirror",
        "Venetian Fan",
        "Elastic Strip Mirror",
        "Horizontal",
        "Letterbox Mirror",
        "Staircase Mirror",
        "Wave Bars",
        "Serpentine Slits",
        "Torn Poster Mirror",
        "Diagonal Slats",
        "Anti-Diagonal Slats",
        "Sliced Lens Mirror",
        "Slit Scan Mirror",
        "Square",
        "Broken Window",
        "Split Portrait",
        "Center Slice Stack",
        "Contact Sheet Mirror",
        "Cascade Collage",
        "Prism Stack",
        "Film Gate Mirror",
        "Orbit Collage",
        "Round",
        "Iris Mirror",
        "Shutter Mirror",
        "Pinwheel Panels",
        "Corner Kaleido",
        "Corner Pull Mirror",
        "Triangle Mirror",
        "Hex Mirror",
        "Tunnel Mirror",
        "Mirror Tunnel XL",
        "Spiral Stair Mirror",
        "Quad Portal",
        "Mobius Ribbon",
        "Accordion Mirror",
        "Hourglass Mirror",
        "Spiral Shards",
        "Sliding Doors",
        "Diamond Tunnel",
        "Fan Blades",
        "Waterfall Strips",
        "Nested Film Gates",
        "Voronoi Mirror Plates",
        "Polar Barcode",
        "Ribbon Lattice",
        "Woven Mirror",
        "Corner Tunnel",
        "Lens Array Mirror",
        "Axis Roulette"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_BG],
        P_BG,
        "Black",
        "Original Source"
    );

    (void) w;
    (void) h;
    return ve;
}
void *hyperfold_malloc(int w, int h)
{
    mirrormadness_t *m;
    uint8_t *b;
    float *f;
    int *ip;
    size_t len;
    size_t axis;
    size_t byte_size;
    size_t float_count;
    size_t int_count;
    int i;

    if (w <= 0 || h <= 0)
        return NULL;

    if ((size_t) w > ((size_t) -1) / (size_t) h)
        return NULL;

    len = (size_t) w * (size_t) h;

    if (len == 0 || len > (size_t) 0x7fffffff)
        return NULL;

    axis = (size_t) w + (size_t) h;

    if (axis < (size_t) w)
        return NULL;

    if (len > ((size_t) -1) / 3)
        return NULL;

    if (axis > ((size_t) -1) / 3)
        return NULL;

    byte_size = len * 3;

    if (byte_size > ((size_t) -1) - axis * 3)
        return NULL;

    byte_size += axis * 3;

    float_count = axis;

    if (float_count > ((size_t) -1) / sizeof(float))
        return NULL;

    int_count = axis * 3;

    if (int_count > ((size_t) -1) / sizeof(int))
        return NULL;

    m = (mirrormadness_t *) vj_calloc(sizeof(mirrormadness_t));
    if (!m)
        return NULL;

    m->w = w;
    m->h = h;
    m->len = (int) len;
    m->seeded = 0;
    m->frame = 0;
    m->panel_count = 0;
    m->lut_valid = 0;
    m->last_strength = -1;
    m->skip_base_panel = 0;
    m->time = 0.0f;

    m->n_threads = vje_advise_num_threads((int) len);
    if (m->n_threads <= 0)
        m->n_threads = 1;

    m->region = vj_malloc(byte_size);
    if (!m->region) {
        free(m);
        return NULL;
    }

    m->x_src = (float *) vj_malloc(sizeof(float) * float_count);
    if (!m->x_src) {
        free(m->region);
        free(m);
        return NULL;
    }

    m->x_i0 = (int *) vj_malloc(sizeof(int) * int_count);
    if (!m->x_i0) {
        free(m->x_src);
        free(m->region);
        free(m);
        return NULL;
    }

    b = (uint8_t *) m->region;

    m->src_y = b; b += len;
    m->src_u = b; b += len;
    m->src_v = b; b += len;

    m->x_mask = b; b += (size_t) w;
    m->y_mask = b; b += (size_t) h;
    m->x_edge = b; b += (size_t) w;
    m->y_edge = b; b += (size_t) h;
    m->x_w    = b; b += (size_t) w;
    m->y_w    = b; b += (size_t) h;

    f = m->x_src;

    m->x_src = f;
    m->y_src = f + (size_t) w;

    ip = m->x_i0;

    m->x_i0 = ip; ip += (size_t) w;
    m->x_i1 = ip; ip += (size_t) w;
    m->y_i0 = ip; ip += (size_t) h;
    m->y_i1 = ip; ip += (size_t) h;
    m->x_n  = ip; ip += (size_t) w;
    m->y_n  = ip; ip += (size_t) h;

    veejay_memset(m->region, 0, byte_size);
    veejay_memset(m->src_u, 128, len);
    veejay_memset(m->src_v, 128, len);

    veejay_memset(m->x_src, 0, sizeof(float) * float_count);
    veejay_memset(m->x_i0, 0, sizeof(int) * int_count);

    for (i = 0; i < AFM_TRIG_SIZE; i++) {
        float a = AFM_TWO_PI * ((float) i / (float) AFM_TRIG_SIZE);
        m->sin_lut[i] = sinf(a);
    }

    return (void *) m;
}

void hyperfold_free(void *ptr)
{
    mirrormadness_t *m = (mirrormadness_t *) ptr;

    if (!m)
        return;

    if (m->x_i0)
        free(m->x_i0);

    if (m->x_src)
        free(m->x_src);

    if (m->region)
        free(m->region);

    free(m);
}

static void afm_seed(mirrormadness_t *m, VJFrame *frame)
{
    (void) frame;
    m->seeded = 1;
}

static inline void afm_sample_yuv_layer(const uint8_t *restrict sy,
                                        const uint8_t *restrict su,
                                        const uint8_t *restrict sv,
                                        int w,
                                        int h,
                                        float sx,
                                        float syf,
                                        int *oy,
                                        int *ou,
                                        int *ov)
{
    int ix;
    int iy;
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

    if (sx < 0.0f)
        sx = 0.0f;
    else if (sx > (float) (w - 1))
        sx = (float) (w - 1);

    if (syf < 0.0f)
        syf = 0.0f;
    else if (syf > (float) (h - 1))
        syf = (float) (h - 1);

    ix = (int) sx;
    iy = (int) syf;

    x1 = ix + 1;
    if (x1 >= w)
        x1 = ix;

    y1 = iy + 1;
    if (y1 >= h)
        y1 = iy;

    wx = (int) ((sx - (float) ix) * 256.0f);
    wy = (int) ((syf - (float) iy) * 256.0f);

    p00 = iy * w + ix;
    p10 = iy * w + x1;
    p01 = y1 * w + ix;
    p11 = y1 * w + x1;

    a = (int) sy[p00] * (256 - wx) + (int) sy[p10] * wx;
    b = (int) sy[p01] * (256 - wx) + (int) sy[p11] * wx;
    *oy = ((a * (256 - wy) + b * wy) + 32768) >> 16;

    nx = (int) (sx + 0.5f);
    ny = (int) (syf + 0.5f);
    if (nx >= w)
        nx = w - 1;
    if (ny >= h)
        ny = h - 1;

    p00 = ny * w + nx;
    *ou = su[p00];
    *ov = sv[p00];
}

void hyperfold_apply(void *ptr, VJFrame *frame, int *args)
{
    mirrormadness_t *m = (mirrormadness_t *) ptr;

    uint8_t *restrict Y;
    uint8_t *restrict U;
    uint8_t *restrict V;
    uint8_t *restrict src_y;
    uint8_t *restrict src_u;
    uint8_t *restrict src_v;

    int w;
    int h;
    int len;

    int mode_i;
    int minwidth_i;
    int maxwidth_i;
    int offset_i;
    int seam_i;
    int speed_i;
    int mono_i;
    int background_i;

    float minwidth_t;
    float maxwidth_t;
    float offset_t;
    float seam_t;
    float speed_t;
    float mono_t;
    float time;

    if (!m || !frame || !args)
        return;
    if (!frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    w = frame->width;
    h = frame->height;
    len = w * h;

    if (w != m->w || h != m->h || len != m->len || w < 4 || h < 4)
        return;

    Y = frame->data[0];
    U = frame->data[1];
    V = frame->data[2];

    if (!m->seeded)
        afm_seed(m, frame);

    mode_i = afm_clampi(args[P_MODE], AFM_MODE_VERTICAL, AFM_MODE_LAST);
    minwidth_i = afm_clampi(args[P_MINWIDTH], 0, 100);
    maxwidth_i = afm_clampi(args[P_MAXWIDTH], 0, 100);
    offset_i = afm_clampi(args[P_OFFSET], 0, 100);
    seam_i = afm_clampi(args[P_SEAM], 0, 100);
    speed_i = afm_clampi(args[P_SPEED], -100, 100);
    mono_i = afm_clampi(args[P_MONO], 0, 100);
    background_i = afm_clampi(args[P_BG], 0, 1);

    minwidth_t = (float) minwidth_i * 0.01f;
    maxwidth_t = (float) maxwidth_i * 0.01f;
    offset_t = ((float) offset_i * 0.01f - 0.5f) * 1.65f;
    seam_t = (float) seam_i * 0.01f;
    speed_t = (float) speed_i * 0.01f;
    mono_t = (float) mono_i * 0.01f;

    speed_t = (speed_t < 0.0f ? -1.0f : 1.0f) * afm_absf(speed_t) * afm_absf(speed_t);
    if (speed_i == 0)
        speed_t = 0.0f;

    afm_build_luts(m, seam_i);

    m->time = afm_wrap_2pi(m->time + (0.0012f + seam_t * 0.0020f + maxwidth_t * 0.0012f) * speed_t);
    time = m->time;

    src_y = m->src_y;
    src_u = m->src_u;
    src_v = m->src_v;

    veejay_memcpy(src_y, Y, (size_t) len);
    veejay_memcpy(src_u, U, (size_t) len);
    veejay_memcpy(src_v, V, (size_t) len);

    if (maxwidth_t < minwidth_t) {
        float t = maxwidth_t;
        maxwidth_t = minwidth_t;
        minwidth_t = t;
    }

    m->skip_base_panel = 1;

    switch (mode_i) {
        case AFM_MODE_VERTICAL:
        default:
            afm_build_strip_maps(m, mode_i, minwidth_t, maxwidth_t, offset_t, time);
            afm_prepare_axis_index(m->x_src, m->x_mask, w, m->x_i0, m->x_i1, m->x_w, m->x_n);
            afm_render_vertical_fast(m, frame, seam_t);
            break;

        case AFM_MODE_BARCODE:
            afm_render_barcode_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, time);
            break;

        case AFM_MODE_VENETIAN_FAN:
            afm_render_venetian_fan(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_ELASTIC_STRIP:
            afm_render_elastic_strip_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_HORIZONTAL:
            afm_build_strip_maps(m, mode_i, minwidth_t, maxwidth_t, offset_t, time);
            afm_prepare_axis_index(m->y_src, m->y_mask, h, m->y_i0, m->y_i1, m->y_w, m->y_n);
            afm_render_horizontal_fast(m, frame, seam_t);
            break;

        case AFM_MODE_LETTERBOX:
            afm_render_letterbox_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, time);
            break;

        case AFM_MODE_STAIRCASE:
            afm_render_staircase_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_WAVE_BARS:
            afm_render_wave_bars(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SERPENTINE_SLITS:
            afm_render_serpentine_slits(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_TORN_POSTER:
            afm_render_torn_poster_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_DIAGONAL_SLATS:
            afm_render_diagonal_slats(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time, 0);
            break;

        case AFM_MODE_ANTIDIAGONAL_SLATS:
            afm_render_diagonal_slats(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time, 1);
            break;

        case AFM_MODE_SLICED_LENS:
            afm_render_sliced_lens_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SLIT_SCAN:
            afm_render_slit_scan(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SQUARE:
            afm_build_panel_layout(m, mode_i, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_BROKEN_WINDOW:
            afm_build_broken_window_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_SPLIT_PORTRAIT:
            afm_build_split_portrait_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_CENTER_STACK:
            afm_build_center_stack_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_CONTACT_SHEET:
            afm_build_contact_sheet_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_CASCADE_COLLAGE:
            afm_build_cascade_collage_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_PRISM_STACK:
            afm_build_prism_stack_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_FILM_GATE:
            afm_build_film_gate_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_ORBIT_COLLAGE:
            afm_build_orbit_collage_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_ROUND:
            afm_render_circular_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_IRIS:
            afm_render_iris_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SHUTTER:
            afm_render_shutter_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_PINWHEEL_PANELS:
            afm_render_pinwheel_panels(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_CORNER_KALEIDO:
            afm_render_corner_kaleido(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_CORNER_PULL:
            afm_render_corner_pull(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_TRIANGLE_MIRROR:
            afm_render_triangle_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_HEX_MIRROR:
            afm_render_hex_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_TUNNEL:
            afm_render_tunnel_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_TUNNEL_XL:
            afm_render_tunnel_xl(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SPIRAL_STAIR:
            afm_render_spiral_stair_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_QUAD_PORTAL:
            afm_render_quad_portal(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_MOEBIUS_RIBBON:
            afm_render_moebius_ribbon(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_ACCORDION:
            afm_render_accordion_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_HOURGLASS:
            afm_render_hourglass_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_SPIRAL_SHARDS:
            afm_build_spiral_shards_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_SLIDING_DOORS:
            afm_build_sliding_doors_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_DIAMOND_TUNNEL:
            afm_render_diamond_tunnel(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_FAN_BLADES:
            afm_render_fan_blades(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_WATERFALL_STRIPS:
            afm_render_waterfall_strips(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_NESTED_FILM_GATES:
            afm_build_nested_film_gates_layout(m, minwidth_t, maxwidth_t, offset_t, time);
            afm_render_panels(m, frame, seam_t, mono_t, background_i);
            break;

        case AFM_MODE_VORONOI_PLATES:
            afm_render_voronoi_plates(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_POLAR_BARCODE:
            afm_render_polar_barcode(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_RIBBON_LATTICE:
            afm_render_ribbon_lattice(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time, background_i);
            break;

        case AFM_MODE_WOVEN_MIRROR:
            afm_render_woven_mirror(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time, background_i);
            break;

        case AFM_MODE_CORNER_TUNNEL:
            afm_render_corner_tunnel(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;

        case AFM_MODE_LENS_ARRAY:
            afm_render_lens_array(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time, background_i);
            break;

        case AFM_MODE_AXIS_ROULETTE:
            afm_render_axis_roulette(m, frame, minwidth_t, maxwidth_t, offset_t, seam_t, mono_t, time);
            break;
    }

    m->frame++;
}





