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
#include "ghostwash.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
    uint8_t *canvas_y;
    uint8_t *canvas_u;
    uint8_t *canvas_v;
    uint8_t *next_y;
    uint8_t *next_u;
    uint8_t *next_v;
    uint8_t *prev_src_y;
    float *grid_x;
    float *grid_y;
    float *shear_x;
    float *shear_y;
    float *poly_rx_x;
    float *poly_ry_x;
    float *poly_rx_y;
    float *poly_ry_y;
    int grid_capacity;
    int w;
    int h;
    int cw;
    int ch;
    int initialized;
    int n_threads;
    float maturity;
    uint32_t frame_no;
} ghostwash_t;

static inline uint8_t clamp_u8f(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (uint8_t)(v + 0.5f);
}

static inline float clampf_local(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float lerpf_local(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float smoothstepf_local(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static inline uint8_t avg2_u8(uint8_t a, uint8_t b)
{
    return (uint8_t)(((int)a + (int)b + 1) >> 1);
}

static inline uint8_t avg4_u8(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint8_t)(((int)a + (int)b + (int)c + (int)d + 2) >> 2);
}

static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float hash_signed_u32(uint32_t x)
{
    return ((float)(hash_u32(x) & 0xffffU) * (1.0f / 65535.0f)) * 2.0f - 1.0f;
}

static inline float grid_noise_component(int x, int y, uint32_t seed, uint32_t salt)
{
    return hash_signed_u32(((uint32_t)x * 374761393U) ^ ((uint32_t)y * 668265263U) ^ (seed * 2246822519U) ^ salt);
}

static inline float sample_plane_bilinear(const uint8_t *restrict plane, float x, float y, int w, int h)
{
    int x1, y1, x2, y2;
    float fx, fy;

    if (x >= 0.0f && y >= 0.0f && x < (float)(w - 1) && y < (float)(h - 1)) {
        x1 = (int)x;
        y1 = (int)y;
        x2 = x1 + 1;
        y2 = y1 + 1;
        fx = x - (float)x1;
        fy = y - (float)y1;
    } else {
        x = clampf_local(x, 0.0f, (float)(w - 1));
        y = clampf_local(y, 0.0f, (float)(h - 1));
        x1 = (int)x;
        y1 = (int)y;
        x2 = (x1 + 1 < w) ? x1 + 1 : x1;
        y2 = (y1 + 1 < h) ? y1 + 1 : y1;
        fx = x - (float)x1;
        fy = y - (float)y1;
    }

    const float wx0 = 1.0f - fx;
    const float wy0 = 1.0f - fy;
    const float w00 = wx0 * wy0;
    const float w10 = fx * wy0;
    const float w01 = wx0 * fy;
    const float w11 = fx * fy;

    const int y1w = y1 * w;
    const int y2w = y2 * w;
    const int i00 = y1w + x1;
    const int i10 = y1w + x2;
    const int i01 = y2w + x1;
    const int i11 = y2w + x2;

    return (float)plane[i00] * w00 + (float)plane[i10] * w10 + (float)plane[i01] * w01 + (float)plane[i11] * w11;
}

static inline float sample_plane_nearest(const uint8_t *restrict plane, float x, float y, int w, int h)
{
    int ix = (int)(x + 0.5f);
    int iy = (int)(y + 0.5f);

    if (ix < 0) ix = 0;
    else if (ix >= w) ix = w - 1;

    if (iy < 0) iy = 0;
    else if (iy >= h) iy = h - 1;

    return (float)plane[iy * w + ix];
}

static inline uint8_t downsample_src_plane_2x2(const uint8_t *restrict plane, int x, int y, int w, int h)
{
    const int fx = x << 1;
    const int fy = y << 1;
    const int x1 = (fx + 1 < w) ? fx + 1 : fx;
    const int y1 = (fy + 1 < h) ? fy + 1 : fy;
    const int i00 = fy * w + fx;
    const int i10 = fy * w + x1;
    const int i01 = y1 * w + fx;
    const int i11 = y1 * w + x1;
    return (uint8_t)(((int)plane[i00] + (int)plane[i10] + (int)plane[i01] + (int)plane[i11] + 2) >> 2);
}

static int ghostwash_grid_required(int w, int h, int cell)
{
    if (w <= 0 || h <= 0 || cell <= 0)
        return 0;

    const int gw = (w + cell - 1) / cell + 2;
    const int gh = (h + cell - 1) / cell + 2;

    if (gw <= 0 || gh <= 0)
        return 0;

    if (gw > INT_MAX / gh)
        return 0;

    return gw * gh;
}

static int ghostwash_ensure_grid(ghostwash_t *p, int w, int h, int cell)
{
    if (!p)
        return 0;

    const int required = ghostwash_grid_required(w, h, cell);

    if (required <= 0)
        return 0;

    if (required <= p->grid_capacity && p->grid_x && p->grid_y)
        return 1;

    float *new_x = (float *)vj_malloc(sizeof(float) * (size_t)required);
    float *new_y = (float *)vj_malloc(sizeof(float) * (size_t)required);

    if (!new_x || !new_y) {
        if (new_x) free(new_x);
        if (new_y) free(new_y);
        return 0;
    }

    veejay_memset(new_x, 0, sizeof(float) * (size_t)required);
    veejay_memset(new_y, 0, sizeof(float) * (size_t)required);

    if (p->grid_x) free(p->grid_x);
    if (p->grid_y) free(p->grid_y);

    p->grid_x = new_x;
    p->grid_y = new_y;
    p->grid_capacity = required;

    return 1;
}

static void build_flow_grid(ghostwash_t *p, int w, int h, int cell)
{
    const int gw = (w + cell - 1) / cell + 2;
    const int gh = (h + cell - 1) / cell + 2;
    const uint32_t seed0 = p->frame_no >> 4;
    const uint32_t seed1 = seed0 + 1U;
    const float phase = ((float)(p->frame_no & 15U)) * (1.0f / 16.0f);

    for (int gy = 0; gy < gh; gy++) {
        for (int gx = 0; gx < gw; gx++) {
            const int gi = gy * gw + gx;
            const float ax0 = grid_noise_component(gx, gy, seed0, 0x1234abcdU);
            const float ay0 = grid_noise_component(gx, gy, seed0, 0x9182a6f1U);
            const float ax1 = grid_noise_component(gx, gy, seed1, 0x1234abcdU);
            const float ay1 = grid_noise_component(gx, gy, seed1, 0x9182a6f1U);
            p->grid_x[gi] = lerpf_local(ax0, ax1, phase);
            p->grid_y[gi] = lerpf_local(ay0, ay1, phase);
        }
    }
}

vj_effect *ghostwash_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    ve->num_params = 13;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 100; ve->defaults[0] = 35;
    ve->limits[0][1] = 0; ve->limits[1][1] = 100; ve->defaults[1] = 14;
    ve->limits[0][2] = 0; ve->limits[1][2] = 100; ve->defaults[2] = 38;
    ve->limits[0][3] = 0; ve->limits[1][3] = 100; ve->defaults[3] = 18;
    ve->limits[0][4] = 0; ve->limits[1][4] = 100; ve->defaults[4] = 42;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100; ve->defaults[5] = 22;
    ve->limits[0][6] = 0; ve->limits[1][6] = 100; ve->defaults[6] = 94;
    ve->limits[0][7] = 0; ve->limits[1][7] = 100; ve->defaults[7] = 20;
    ve->limits[0][8] = 0; ve->limits[1][8] = 100; ve->defaults[8] = 85;
    ve->limits[0][9] = 0; ve->limits[1][9] = 100; ve->defaults[9] = 55;
    ve->limits[0][10] = 0; ve->limits[1][10] = 100; ve->defaults[10] = 58;
    ve->limits[0][11] = 0; ve->limits[1][11] = 1; ve->defaults[11] = 0;
    ve->limits[0][12] = 0; ve->limits[1][12] = 9; ve->defaults[12] = 0;

    ve->description = "Ghost Wash";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Fade In",
        "Source",
        "Drift",
        "Warp",
        "Color Bleed",
        "Detail",
        "Persistence",
        "Instability",
        "Flow Size",
        "Motion Pull",
        "Color Strength",
        "Color Mode",
        "Geometry"
    );

    return ve;
}

void ghostwash_free(void *ptr)
{
    ghostwash_t *p = (ghostwash_t *)ptr;

    if (!p)
        return;

    if (p->canvas_y) free(p->canvas_y);
    if (p->canvas_u) free(p->canvas_u);
    if (p->canvas_v) free(p->canvas_v);
    if (p->next_y) free(p->next_y);
    if (p->next_u) free(p->next_u);
    if (p->next_v) free(p->next_v);
    if (p->prev_src_y) free(p->prev_src_y);
    if (p->grid_x) free(p->grid_x);
    if (p->grid_y) free(p->grid_y);
    if (p->shear_x) free(p->shear_x);
    if (p->shear_y) free(p->shear_y);
    if (p->poly_rx_x) free(p->poly_rx_x);
    if (p->poly_ry_x) free(p->poly_ry_x);
    if (p->poly_rx_y) free(p->poly_rx_y);
    if (p->poly_ry_y) free(p->poly_ry_y);

    free(p);
}

void *ghostwash_malloc(int w, int h)
{
    if (w <= 2 || h <= 2)
        return NULL;

    const int cw = (w + 1) >> 1;
    const int ch = (h + 1) >> 1;
    const size_t len_sz = (size_t)w * (size_t)h;
    const size_t clen_sz = (size_t)cw * (size_t)ch;

    if (len_sz == 0 || len_sz > (size_t)INT_MAX || clen_sz == 0 || clen_sz > (size_t)INT_MAX)
        return NULL;

    ghostwash_t *p = (ghostwash_t *)vj_calloc(sizeof(ghostwash_t));

    if (!p)
        return NULL;

    const int min_cell = 7;
    const int capacity = ghostwash_grid_required(cw, ch, min_cell);

    if (capacity <= 0) {
        free(p);
        return NULL;
    }

    p->w = w;
    p->h = h;
    p->cw = cw;
    p->ch = ch;
    p->grid_capacity = capacity;

    p->canvas_y = (uint8_t *)vj_malloc(clen_sz);
    p->canvas_u = (uint8_t *)vj_malloc(clen_sz);
    p->canvas_v = (uint8_t *)vj_malloc(clen_sz);
    p->next_y = (uint8_t *)vj_malloc(clen_sz);
    p->next_u = (uint8_t *)vj_malloc(clen_sz);
    p->next_v = (uint8_t *)vj_malloc(clen_sz);
    p->prev_src_y = (uint8_t *)vj_malloc(clen_sz);
    p->grid_x = (float *)vj_malloc(sizeof(float) * (size_t)p->grid_capacity);
    p->grid_y = (float *)vj_malloc(sizeof(float) * (size_t)p->grid_capacity);
    p->shear_x = (float *)vj_malloc(sizeof(float) * (size_t)cw);
    p->shear_y = (float *)vj_malloc(sizeof(float) * (size_t)ch);
    p->poly_rx_x = (float *)vj_malloc(sizeof(float) * (size_t)cw);
    p->poly_ry_x = (float *)vj_malloc(sizeof(float) * (size_t)cw);
    p->poly_rx_y = (float *)vj_malloc(sizeof(float) * (size_t)ch);
    p->poly_ry_y = (float *)vj_malloc(sizeof(float) * (size_t)ch);

    if (!p->canvas_y || !p->canvas_u || !p->canvas_v || !p->next_y || !p->next_u || !p->next_v || !p->prev_src_y || !p->grid_x || !p->grid_y || !p->shear_x || !p->shear_y || !p->poly_rx_x || !p->poly_ry_x || !p->poly_rx_y || !p->poly_ry_y) {
        ghostwash_free(p);
        return NULL;
    }

    veejay_memset(p->canvas_y, 0, clen_sz);
    veejay_memset(p->canvas_u, 128, clen_sz);
    veejay_memset(p->canvas_v, 128, clen_sz);
    veejay_memset(p->next_y, 0, clen_sz);
    veejay_memset(p->next_u, 128, clen_sz);
    veejay_memset(p->next_v, 128, clen_sz);
    veejay_memset(p->prev_src_y, 0, clen_sz);
    veejay_memset(p->grid_x, 0, sizeof(float) * (size_t)p->grid_capacity);
    veejay_memset(p->grid_y, 0, sizeof(float) * (size_t)p->grid_capacity);
    veejay_memset(p->shear_x, 0, sizeof(float) * (size_t)cw);
    veejay_memset(p->shear_y, 0, sizeof(float) * (size_t)ch);
    veejay_memset(p->poly_rx_x, 0, sizeof(float) * (size_t)cw);
    veejay_memset(p->poly_ry_x, 0, sizeof(float) * (size_t)cw);
    veejay_memset(p->poly_rx_y, 0, sizeof(float) * (size_t)ch);
    veejay_memset(p->poly_ry_y, 0, sizeof(float) * (size_t)ch);

    p->initialized = 0;
    p->maturity = 0.0f;
    p->frame_no = 0;
    p->n_threads = vje_advise_num_threads((int)clen_sz);

    if (p->n_threads <= 0)
        p->n_threads = 1;

    return (void *)p;
}

#define GHOSTWASH_GEOMETRY(MODE) \
    do { \
        switch (MODE) { \
            case 0: \
                if (warp_gain > 0.0f) { \
                    vx += (-cy * inv_max_dim) * warp_gain; \
                    vy += ( cx * inv_max_dim) * warp_gain; \
                } \
                break; \
            case 1: \
                vx *= 1.0f + warp_gain * 0.10f; \
                vy *= 1.0f + warp_gain * 0.10f; \
                break; \
            case 2: { \
                const int xm = (x > 0) ? x - 1 : x; \
                const int xp = (x < cw - 1) ? x + 1 : x; \
                const float gyx = ((float)old_y[row + xp] - (float)old_y[row + xm]) * (1.0f / 255.0f); \
                const float gyy = ((float)old_y[yp * cw + x] - (float)old_y[ym * cw + x]) * (1.0f / 255.0f); \
                vx += (gyx * 1.8f - gyy * 1.2f) * warp_gain; \
                vy += (gyy * 1.8f + gyx * 1.2f) * warp_gain; \
                break; \
            } \
            case 3: \
                vx += (-cy * inv_max_dim) * warp_gain * 0.75f; \
                vy += ( cx * inv_max_dim) * warp_gain * 0.75f; \
                vx += shear_y[y] * warp_gain * 0.95f; \
                vy += shear_x[x] * warp_gain * 0.28f; \
                break; \
            case 4: { \
                const float nx = cx * inv_max_dim; \
                const float ny = cy * inv_max_dim; \
                const float axn = fabsf(nx); \
                const float ayn = fabsf(ny); \
                const float px = (nx >= 0.0f) ? 1.0f : -1.0f; \
                const float py = (ny >= 0.0f) ? 1.0f : -1.0f; \
                vx += px * (0.35f - axn) * warp_gain * 1.15f; \
                vy += py * (0.35f - ayn) * warp_gain * 1.15f; \
                vx += (-cy * inv_max_dim) * warp_gain * 0.25f; \
                vy += ( cx * inv_max_dim) * warp_gain * 0.25f; \
                break; \
            } \
            case 5: { \
                const float nx = cx * inv_max_dim; \
                const float ny = cy * inv_max_dim; \
                const float r2 = nx * nx + ny * ny; \
                const float tunnel = (0.35f + r2 * 1.40f) * warp_gain; \
                vx += nx * tunnel; \
                vy += ny * tunnel; \
                vx += (-cy * inv_max_dim) * warp_gain * 0.18f; \
                vy += ( cx * inv_max_dim) * warp_gain * 0.18f; \
                break; \
            } \
            case 6: { \
                const int xm = (x > 0) ? x - 1 : x; \
                const int xp = (x < cw - 1) ? x + 1 : x; \
                const float gyx = ((float)old_y[row + xp] - (float)old_y[row + xm]) * (1.0f / 255.0f); \
                const float gyy = ((float)old_y[yp * cw + x] - (float)old_y[ym * cw + x]) * (1.0f / 255.0f); \
                vx += gyx * warp_gain * 0.75f; \
                vy += gyy * warp_gain * 0.75f; \
                break; \
            } \
            case 7: { \
                const int xm = (x > 0) ? x - 1 : x; \
                const int xp = (x < cw - 1) ? x + 1 : x; \
                const float gyx = ((float)old_y[row + xp] - (float)old_y[row + xm]) * (1.0f / 255.0f); \
                const float gyy = ((float)old_y[yp * cw + x] - (float)old_y[ym * cw + x]) * (1.0f / 255.0f); \
                const float edge = fabsf(gyx) + fabsf(gyy); \
                const float bend = warp_gain * (0.35f + edge * 3.5f); \
                vx += (gyx + noise_x * 0.45f) * bend; \
                vy += (gyy + noise_y * 0.45f) * bend; \
                vx += (-gyy) * bend * 0.75f; \
                vy += ( gyx) * bend * 0.75f; \
                break; \
            } \
            case 8: { \
                const float nx = cx * inv_max_dim; \
                const float ny = cy * inv_max_dim; \
                const float rx = poly_rx_x[x] + poly_rx_y[y]; \
                const float ry = poly_ry_x[x] + poly_ry_y[y]; \
                const float s0 = ry; \
                const float s1 = -0.8660254038f * rx - 0.5f * ry; \
                const float s2 =  0.8660254038f * rx - 0.5f * ry; \
                const float tri = fmaxf(s0, fmaxf(s1, s2)); \
                const float tri_shape = clampf_local(0.75f + fabsf(tri) * 1.65f, 0.55f, 2.15f); \
                vx += (-ny * tri_shape) * warp_gain; \
                vy += ( nx * tri_shape) * warp_gain; \
                break; \
            } \
            case 9: \
            default: { \
                const float nx = cx * inv_max_dim; \
                const float ny = cy * inv_max_dim; \
                const float rx = poly_rx_x[x] + poly_rx_y[y]; \
                const float ry = poly_ry_x[x] + poly_ry_y[y]; \
                const float axr = fabsf(rx) * 1.45f; \
                const float ayr = fabsf(ry); \
                const float rect_shape = clampf_local(0.70f + fmaxf(axr, ayr) * 1.75f, 0.55f, 2.30f); \
                vx += (-ny * rect_shape) * warp_gain; \
                vy += ( nx * rect_shape) * warp_gain; \
                break; \
            } \
        } \
    } while (0)

#define GHOSTWASH_ADVECTION_LOOP(MODE, MONO) \
    do { \
        _Pragma("omp parallel for collapse(2) schedule(static) num_threads(p->n_threads)") \
        for (int gy = 0; gy < gh - 1; gy++) { \
            for (int gx = 0; gx < gw - 1; gx++) { \
                const int y0 = gy * cell; \
                const int y1 = y0 + cell; \
                const int ye = (y1 < ch) ? y1 : ch; \
                const int x0 = gx * cell; \
                const int x1 = x0 + cell; \
                const int xe = (x1 < cw) ? x1 : cw; \
                const int gi00 = gy * gw + gx; \
                const int gi10 = gy * gw + gx + 1; \
                const int gi01 = (gy + 1) * gw + gx; \
                const int gi11 = (gy + 1) * gw + gx + 1; \
                const float vx00 = p->grid_x[gi00]; \
                const float vx10 = p->grid_x[gi10]; \
                const float vx01 = p->grid_x[gi01]; \
                const float vx11 = p->grid_x[gi11]; \
                const float vy00 = p->grid_y[gi00]; \
                const float vy10 = p->grid_y[gi10]; \
                const float vy01 = p->grid_y[gi01]; \
                const float vy11 = p->grid_y[gi11]; \
                for (int y = y0; y < ye; y++) { \
                    const int ly = y - y0; \
                    const float fy = lut[ly]; \
                    const float ax = lerpf_local(vx00, vx01, fy); \
                    const float bx = lerpf_local(vx10, vx11, fy); \
                    const float ay = lerpf_local(vy00, vy01, fy); \
                    const float by = lerpf_local(vy10, vy11, fy); \
                    const int row = y * cw; \
                    const int ym = (y > 0) ? y - 1 : y; \
                    const int yp = (y < ch - 1) ? y + 1 : y; \
                    const float cy = (float)y - half_ch; \
                    for (int x = x0; x < xe; x++) { \
                        const int idx = row + x; \
                        const int lx = x - x0; \
                        const float fx = lut[lx]; \
                        const uint8_t src_y = downsample_src_plane_2x2(Y, x, y, w, h); \
                        int dm = (int)src_y - (int)prev_y[idx]; \
                        if (dm < 0) dm = -dm; \
                        float vx = lerpf_local(ax, bx, fx); \
                        float vy = lerpf_local(ay, by, fx); \
                        const float noise_x = vx; \
                        const float noise_y = vy; \
                        const float cx = (float)x - half_cw; \
                        GHOSTWASH_GEOMETRY(MODE); \
                        vx *= amp; \
                        vy *= amp; \
                        const float mag2 = vx * vx + vy * vy; \
                        if (mag2 > 4.0f) { \
                            const float s = 1.0f / (1.0f + (mag2 - 4.0f) * 0.160f); \
                            vx *= s; \
                            vy *= s; \
                        } \
                        const float motion_boost = 0.22f + (float)dm * motion_scale; \
                        const float disp = flow_pixels * motion_boost; \
                        const float sx = (float)x - vx * disp; \
                        const float sy = (float)y - vy * disp; \
                        const float adv_y = sample_plane_bilinear(old_y, sx, sy, cw, ch); \
                        float out_y = adv_y * hist_mix + (float)src_y * src_mix; \
                        out_y += ((float)src_y - adv_y) * detail_boost; \
                        next_y[idx] = clamp_u8f(out_y); \
                        if (MONO) { \
                            prev_y[idx] = src_y; \
                        } else { \
                            const uint8_t src_u = downsample_src_plane_2x2(U, x, y, w, h); \
                            const uint8_t src_v = downsample_src_plane_2x2(V, x, y, w, h); \
                            float slip = chroma_slip_gain * (0.35f + motion_boost * 0.75f); \
                            if ((MODE) == 6) \
                                slip += warp_gain * prism_slip_boost; \
                            const float adv_u = sample_plane_nearest(old_u, sx + vx * slip, sy + vy * slip, cw, ch); \
                            const float adv_v = sample_plane_nearest(old_v, sx - vx * slip, sy - vy * slip, cw, ch); \
                            float cu = (adv_u - 128.0f) * chroma_hist_mix + ((float)src_u - 128.0f) * chroma_src_mix; \
                            float cv = (adv_v - 128.0f) * chroma_hist_mix + ((float)src_v - 128.0f) * chroma_src_mix; \
                            cu *= chroma_keep; \
                            cv *= chroma_keep; \
                            next_u[idx] = clamp_u8f(128.0f + cu * color_gain); \
                            next_v[idx] = clamp_u8f(128.0f + cv * color_gain); \
                            prev_y[idx] = src_y; \
                        } \
                    } \
                } \
            } \
        } \
    } while (0)

static inline void ghostwash_dispatch_advection(
    ghostwash_t *p,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict old_y,
    const uint8_t *restrict old_u,
    const uint8_t *restrict old_v,
    uint8_t *restrict next_y,
    uint8_t *restrict next_u,
    uint8_t *restrict next_v,
    uint8_t *restrict prev_y,
    const float *restrict shear_x,
    const float *restrict shear_y,
    const float *restrict poly_rx_x,
    const float *restrict poly_ry_x,
    const float *restrict poly_rx_y,
    const float *restrict poly_ry_y,
    int w,
    int h,
    int cw,
    int ch,
    int gw,
    int gh,
    int cell,
    int geometry_mode,
    int mono_mode,
    float *restrict lut,
    float hist_mix,
    float src_mix,
    float chroma_hist_mix,
    float chroma_src_mix,
    float color_gain,
    float chroma_keep,
    float detail_boost,
    float flow_pixels,
    float warp_gain,
    float instability_gain,
    float chroma_slip_gain,
    float prism_slip_boost,
    float motion_scale)
{
    const float half_cw = (float)cw * 0.5f;
    const float half_ch = (float)ch * 0.5f;
    const float inv_max_dim = 1.0f / ((float)((cw > ch) ? cw : ch) * 0.5f + 1.0f);
    const float amp = 1.0f + instability_gain * 0.10f;

    if (mono_mode) {
        switch (geometry_mode) {
            case 0: GHOSTWASH_ADVECTION_LOOP(0, 1); break;
            case 1: GHOSTWASH_ADVECTION_LOOP(1, 1); break;
            case 2: GHOSTWASH_ADVECTION_LOOP(2, 1); break;
            case 3: GHOSTWASH_ADVECTION_LOOP(3, 1); break;
            case 4: GHOSTWASH_ADVECTION_LOOP(4, 1); break;
            case 5: GHOSTWASH_ADVECTION_LOOP(5, 1); break;
            case 6: GHOSTWASH_ADVECTION_LOOP(6, 1); break;
            case 7: GHOSTWASH_ADVECTION_LOOP(7, 1); break;
            case 8: GHOSTWASH_ADVECTION_LOOP(8, 1); break;
            case 9:
            default: GHOSTWASH_ADVECTION_LOOP(9, 1); break;
        }
    } else {
        switch (geometry_mode) {
            case 0: GHOSTWASH_ADVECTION_LOOP(0, 0); break;
            case 1: GHOSTWASH_ADVECTION_LOOP(1, 0); break;
            case 2: GHOSTWASH_ADVECTION_LOOP(2, 0); break;
            case 3: GHOSTWASH_ADVECTION_LOOP(3, 0); break;
            case 4: GHOSTWASH_ADVECTION_LOOP(4, 0); break;
            case 5: GHOSTWASH_ADVECTION_LOOP(5, 0); break;
            case 6: GHOSTWASH_ADVECTION_LOOP(6, 0); break;
            case 7: GHOSTWASH_ADVECTION_LOOP(7, 0); break;
            case 8: GHOSTWASH_ADVECTION_LOOP(8, 0); break;
            case 9:
            default: GHOSTWASH_ADVECTION_LOOP(9, 0); break;
        }
    }
}

static void ghostwash_render_fullres(ghostwash_t *p, VJFrame *frame, int mono_mode)
{
    const int w = p->w;
    const int h = p->h;
    const int cw = p->cw;
    const int ch = p->ch;
    const uint8_t *restrict gy = p->next_y;
    const uint8_t *restrict gu = p->next_u;
    const uint8_t *restrict gv = p->next_v;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    #pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for (int cy = 0; cy < ch; cy++) {
        const int fy = cy << 1;
        const int cy1 = (cy + 1 < ch) ? cy + 1 : cy;
        const int row = cy * cw;
        const int row1 = cy1 * cw;

        for (int cx = 0; cx < cw; cx++) {
            const int fx = cx << 1;
            const int cx1 = (cx + 1 < cw) ? cx + 1 : cx;
            const int c00 = row + cx;
            const int c10 = row + cx1;
            const int c01 = row1 + cx;
            const int c11 = row1 + cx1;

            const uint8_t y00 = gy[c00];
            const uint8_t y10 = avg2_u8(gy[c00], gy[c10]);
            const uint8_t y01 = avg2_u8(gy[c00], gy[c01]);
            const uint8_t y11 = avg4_u8(gy[c00], gy[c10], gy[c01], gy[c11]);

            const int o00 = fy * w + fx;
            Y[o00] = y00;

            if (fx + 1 < w)
                Y[o00 + 1] = y10;

            if (fy + 1 < h) {
                const int o01 = o00 + w;
                Y[o01] = y01;

                if (fx + 1 < w)
                    Y[o01 + 1] = y11;
            }

            if (mono_mode) {
                U[o00] = 128;
                V[o00] = 128;

                if (fx + 1 < w) {
                    U[o00 + 1] = 128;
                    V[o00 + 1] = 128;
                }

                if (fy + 1 < h) {
                    const int o01 = o00 + w;
                    U[o01] = 128;
                    V[o01] = 128;

                    if (fx + 1 < w) {
                        U[o01 + 1] = 128;
                        V[o01 + 1] = 128;
                    }
                }
            } else {
                const uint8_t u00 = gu[c00];
                const uint8_t u10 = avg2_u8(gu[c00], gu[c10]);
                const uint8_t u01 = avg2_u8(gu[c00], gu[c01]);
                const uint8_t u11 = avg4_u8(gu[c00], gu[c10], gu[c01], gu[c11]);
                const uint8_t v00 = gv[c00];
                const uint8_t v10 = avg2_u8(gv[c00], gv[c10]);
                const uint8_t v01 = avg2_u8(gv[c00], gv[c01]);
                const uint8_t v11 = avg4_u8(gv[c00], gv[c10], gv[c01], gv[c11]);

                U[o00] = u00;
                V[o00] = v00;

                if (fx + 1 < w) {
                    U[o00 + 1] = u10;
                    V[o00 + 1] = v10;
                }

                if (fy + 1 < h) {
                    const int o01 = o00 + w;
                    U[o01] = u01;
                    V[o01] = v01;

                    if (fx + 1 < w) {
                        U[o01 + 1] = u11;
                        V[o01 + 1] = v11;
                    }
                }
            }
        }
    }
}

void ghostwash_apply(void *ptr, VJFrame *frame, int *args)
{
    ghostwash_t *p = (ghostwash_t *)ptr;

    if (!p || !frame || !args)
        return;

    if (!frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int cw = (w + 1) >> 1;
    const int ch = (h + 1) >> 1;

    if (w <= 2 || h <= 2 || cw <= 1 || ch <= 1)
        return;

    const size_t len_sz = (size_t)w * (size_t)h;
    const size_t clen_sz = (size_t)cw * (size_t)ch;

    if (len_sz == 0 || len_sz > (size_t)INT_MAX || clen_sz == 0 || clen_sz > (size_t)INT_MAX)
        return;

    if (p->w != w || p->h != h || p->cw != cw || p->ch != ch)
        return;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    if (frame->uv_len > 0 && frame->uv_len < (int)len_sz)
        return;

    if (!p->initialized) {
        for (int y = 0; y < ch; y++) {
            const int row = y * cw;
            for (int x = 0; x < cw; x++) {
                const int idx = row + x;
                p->canvas_y[idx] = downsample_src_plane_2x2(Y, x, y, w, h);
                p->canvas_u[idx] = downsample_src_plane_2x2(U, x, y, w, h);
                p->canvas_v[idx] = downsample_src_plane_2x2(V, x, y, w, h);
                p->next_y[idx] = p->canvas_y[idx];
                p->next_u[idx] = p->canvas_u[idx];
                p->next_v[idx] = p->canvas_v[idx];
                p->prev_src_y[idx] = p->canvas_y[idx];
            }
        }

        p->initialized = 1;
        p->maturity = 0.0f;
        p->frame_no = 0;
    }

    const float fade_param = clampf_local((float)args[0] * 0.01f, 0.0f, 1.0f);
    const float source_param = clampf_local((float)args[1] * 0.01f, 0.0f, 1.0f);
    const float drift_param = clampf_local((float)args[2] * 0.01f, 0.0f, 1.0f);
    const float warp_param = clampf_local((float)args[3] * 0.01f, 0.0f, 1.0f);
    const float bleed_param = clampf_local((float)args[4] * 0.01f, 0.0f, 1.0f);
    const float detail_param = clampf_local((float)args[5] * 0.01f, 0.0f, 1.0f);
    const float persist_param = clampf_local((float)args[6] * 0.01f, 0.0f, 1.0f);
    const float instability_param = clampf_local((float)args[7] * 0.01f, 0.0f, 1.0f);
    const float flow_size_param = clampf_local((float)args[8] * 0.01f, 0.0f, 1.0f);
    const float motion_param = clampf_local((float)args[9] * 0.01f, 0.0f, 1.0f);
    const float color_strength_param = clampf_local((float)args[10] * 0.01f, 0.0f, 1.0f);
    const int mono_mode = (args[11] != 0);

    int geometry_mode = args[12];

    if (geometry_mode < 0)
        geometry_mode = 0;
    else if (geometry_mode > 9)
        geometry_mode = 9;

    const float fade_curve = fade_param * fade_param;
    const float source_curve = source_param * source_param;
    const float drift_curve = drift_param * drift_param;
    const float warp_curve = warp_param * warp_param;
    const float bleed_curve = bleed_param * bleed_param;
    const float detail_curve = detail_param * detail_param;
    const float persist_curve = persist_param * persist_param;
    const float instability_curve = instability_param * instability_param;
    const float flow_size_curve = flow_size_param * flow_size_param;
    const float motion_curve = motion_param * motion_param;
    const float color_strength_curve = color_strength_param * color_strength_param;

    const float mature_rate = 0.0004f + fade_curve * 0.0350f;
    p->maturity += (1.0f - p->maturity) * mature_rate;

    if (p->maturity > 1.0f)
        p->maturity = 1.0f;

    const float maturity = p->maturity;
    const float early_source = 0.004f + source_curve * 0.360f;
    const float mature_source = 0.003f + source_curve * 0.150f;

    float src_mix = lerpf_local(early_source, mature_source, maturity);
    src_mix *= (1.0f - persist_curve * 0.78f);

    if (src_mix < 0.0015f) src_mix = 0.0015f;
    if (src_mix > 0.48f) src_mix = 0.48f;

    float hist_mix = 1.0f - src_mix;
    const float persistence_boost = 0.10f + persist_curve * 0.12f;

    hist_mix = clampf_local(hist_mix + persistence_boost, 0.0f, 0.995f);
    src_mix = 1.0f - hist_mix;

    float chroma_src_mix = src_mix + 0.030f + source_curve * 0.090f + bleed_curve * 0.130f + color_strength_curve * 0.140f;

    if (chroma_src_mix < 0.018f) chroma_src_mix = 0.018f;
    if (chroma_src_mix > 0.70f) chroma_src_mix = 0.70f;

    const float chroma_hist_mix = 1.0f - chroma_src_mix;
    const float color_gain = 0.90f + color_strength_curve * 0.42f + bleed_curve * 0.16f;
    const float desat = (1.0f - bleed_curve * 0.55f) * (0.10f + persist_curve * 0.20f) * (1.0f - color_strength_curve * 0.70f);
    const float chroma_keep = clampf_local(1.0f - desat, 0.30f, 1.15f);
    const float detail_gain = detail_curve * lerpf_local(0.55f, 0.20f, maturity);
    const float detail_boost = detail_gain * 0.20f + 0.08f;
    const float flow_pixels = ((0.20f + maturity * 1.55f) * (0.3f + drift_curve * 26.0f)) * 0.5f;
    const float warp_gain = warp_curve * 3.0f;
    const float instability_gain = instability_curve * 8.0f * (0.25f + maturity * 0.75f);
    const float chroma_slip_gain = bleed_curve * (0.20f + color_strength_curve * 1.40f);
    const float prism_slip_boost = 0.65f + bleed_curve * 1.35f;
    const float motion_scale = (0.20f + motion_curve * 3.20f) * (1.0f / 255.0f);

    int cell = 9 + (int)(flow_size_curve * 31.0f);
    cell -= (int)(instability_curve * 5.0f);

    if (cell < 7) cell = 7;
    if (cell > 40) cell = 40;

    if (!ghostwash_ensure_grid(p, cw, ch, cell))
        return;

    float lut[41];

    for (int i = 0; i <= cell; i++)
        lut[i] = smoothstepf_local((float)i / (float)cell);

    build_flow_grid(p, cw, ch, cell);

    const int gw = (cw + cell - 1) / cell + 2;
    const int gh = (ch + cell - 1) / cell + 2;

    if (gw <= 1 || gh <= 1)
        return;

    if (ghostwash_grid_required(cw, ch, cell) > p->grid_capacity)
        return;

    const float time_phase = (float)(p->frame_no & 1023U) * (6.28318530717958647692f / 1024.0f);
    const float shear_phase = time_phase * (0.40f + instability_param * 1.60f);
    const float shear_freq = 0.020f + flow_size_param * 0.090f;

    if (geometry_mode == 3) {
        for (int y = 0; y < ch; y++)
            p->shear_y[y] = sinf((float)y * shear_freq + shear_phase);

        for (int x = 0; x < cw; x++)
            p->shear_x[x] = sinf((float)x * shear_freq * 0.73f + shear_phase * 1.31f);
    }

    const float inv_max_dim = 1.0f / ((float)((cw > ch) ? cw : ch) * 0.5f + 1.0f);
    const float half_cw = (float)cw * 0.5f;
    const float half_ch = (float)ch * 0.5f;

    if (geometry_mode == 8) {
        const float tri_angle = time_phase * (0.18f + instability_param * 0.42f);
        const float tri_ca = cosf(tri_angle);
        const float tri_sa = sinf(tri_angle);

        for (int x = 0; x < cw; x++) {
            const float nx = ((float)x - half_cw) * inv_max_dim;
            p->poly_rx_x[x] = nx * tri_ca;
            p->poly_ry_x[x] = nx * tri_sa;
        }

        for (int y = 0; y < ch; y++) {
            const float ny = ((float)y - half_ch) * inv_max_dim;
            p->poly_rx_y[y] = -ny * tri_sa;
            p->poly_ry_y[y] =  ny * tri_ca;
        }
    } else if (geometry_mode == 9) {
        const float rect_angle = -time_phase * (0.14f + instability_param * 0.36f);
        const float rect_ca = cosf(rect_angle);
        const float rect_sa = sinf(rect_angle);

        for (int x = 0; x < cw; x++) {
            const float nx = ((float)x - half_cw) * inv_max_dim;
            p->poly_rx_x[x] = nx * rect_ca;
            p->poly_ry_x[x] = nx * rect_sa;
        }

        for (int y = 0; y < ch; y++) {
            const float ny = ((float)y - half_ch) * inv_max_dim;
            p->poly_rx_y[y] = -ny * rect_sa;
            p->poly_ry_y[y] =  ny * rect_ca;
        }
    }

    const uint8_t *restrict old_y = p->canvas_y;
    const uint8_t *restrict old_u = p->canvas_u;
    const uint8_t *restrict old_v = p->canvas_v;

    uint8_t *restrict next_y = p->next_y;
    uint8_t *restrict next_u = p->next_u;
    uint8_t *restrict next_v = p->next_v;
    uint8_t *restrict prev_y = p->prev_src_y;

    ghostwash_dispatch_advection(
        p,
        Y,
        U,
        V,
        old_y,
        old_u,
        old_v,
        next_y,
        next_u,
        next_v,
        prev_y,
        p->shear_x,
        p->shear_y,
        p->poly_rx_x,
        p->poly_ry_x,
        p->poly_rx_y,
        p->poly_ry_y,
        w,
        h,
        cw,
        ch,
        gw,
        gh,
        cell,
        geometry_mode,
        mono_mode,
        lut,
        hist_mix,
        src_mix,
        chroma_hist_mix,
        chroma_src_mix,
        color_gain,
        chroma_keep,
        detail_boost,
        flow_pixels,
        warp_gain,
        instability_gain,
        chroma_slip_gain,
        prism_slip_boost,
        motion_scale
    );

    ghostwash_render_fullres(p, frame, mono_mode);

    {
        uint8_t *tmp;
        tmp = p->canvas_y; p->canvas_y = p->next_y; p->next_y = tmp;
        tmp = p->canvas_u; p->canvas_u = p->next_u; p->next_u = tmp;
        tmp = p->canvas_v; p->canvas_v = p->next_v; p->next_v = tmp;
    }

    p->frame_no++;
}