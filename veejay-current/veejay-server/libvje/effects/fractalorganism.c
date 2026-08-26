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
#include "fractalorganism.h"

#define FRACTALORGANISM_PARAMS 10
#define FB_AUTO_BLOCK 64
#define FR_TRIG_LUT_SIZE 1024
#define FR_TRIG_LUT_MASK 1023
#define FR_TWO_PI 6.28318530718f
#define FR_INV_TWO_PI 0.15915494309f
#define FR_LUT_SCALE 162.974661726f
#define FR_BLUR_TILE 32

#define P_ZOOM        0
#define P_DENSITY     1
#define P_ITER        2
#define P_WARP        3
#define P_FACET       4
#define P_SILHOUETTE  5
#define P_MIX         6
#define P_CHROMA      7
#define P_PULSE       8
#define P_SPEED       9

typedef struct {
    int w;
    int h;
    int len;
    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *blur;
    uint8_t *mask;
    uint8_t *coarse;
    uint8_t *tmp;
    float sin_lut[FR_TRIG_LUT_SIZE];
    float cos_lut[FR_TRIG_LUT_SIZE];
    float spectral_u[FR_TRIG_LUT_SIZE];
    float spectral_v[FR_TRIG_LUT_SIZE];
    float phase;
    float orbit;
} fractalorganism_t;

typedef struct {
    float density;
    float warp;
    float facet;
    float pulse;
    float pulse_wave;
    float freq_mult1; 
    float freq_mult2;
    float freq_mult3;
    float silhouette_scale;
    float colonize_scale;
    float chew_mesh_scale;
    float chew_silh_scale;
    float charge_base;
    float fluid_scale;
    float target_escape;
    float outy_mesh_scale;
    float color_phase_x_step;
    float color_phase_y_step;
} fo_render_consts_t;

static inline int fb_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fb_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float fb_absf(float x)
{
    return x < 0.0f ? -x : x;
}

static inline float fb_fast_log2(float x)
{
    union {
        float f;
        uint32_t u;
    } v;

    v.f = x;
    return (float) v.u * 1.1920928955078125e-7f - 127.0f;
}

static inline float fb_smooth01(float x)
{
    x = fb_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline float fb_wrap_2pi(float v)
{
    if (v >= FR_TWO_PI || v < 0.0f) {
        int k = (int) (v * FR_INV_TWO_PI);
        v -= (float) k * FR_TWO_PI;
        if (v < 0.0f)
            v += FR_TWO_PI;
        else if (v >= FR_TWO_PI)
            v -= FR_TWO_PI;
    }
    return v;
}

static inline float fb_lut_sin(const fractalorganism_t *t, float phase)
{
    return t->sin_lut[((int) (phase * FR_LUT_SCALE)) & FR_TRIG_LUT_MASK];
}

static inline float fb_lut_cos(const fractalorganism_t *t, float phase)
{
    return t->cos_lut[((int) (phase * FR_LUT_SCALE)) & FR_TRIG_LUT_MASK];
}

static inline void fb_spectral_uv(const fractalorganism_t *t, float phase, float *u, float *v)
{
    int i = ((int) (phase * FR_LUT_SCALE)) & FR_TRIG_LUT_MASK;
    *u = t->spectral_u[i];
    *v = t->spectral_v[i];
}

static inline float fb_time_step(int speed)
{
    if (speed == 0)
        return 0.0f;
    float u = fb_clampf(fb_absf((float) speed) * 0.001f, 0.0f, 1.0f);
    float mag = 0.00018f * (exp2f(9.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static void fb_box_blur(
    fractalorganism_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst,
    int radius
) {
    const int w = t->w;
    const int h = t->h;
    const int window = radius * 2 + 1;
    const int recip = (65536 + window / 2) / window;
    uint8_t *restrict tmp = t->tmp;

#pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + y * w;
        uint8_t *out = tmp + y * w;
        int sum = row[0] * (radius + 1);
        int x;

        for (x = 1; x <= radius; x++)
            sum += row[x < w ? x : w - 1];

        int right_edge = w - radius - 1;
        if (right_edge < radius + 1) { 
            for (x = 0; x < w; x++) {
                int subx = x - radius;
                int addx = x + radius + 1;
                out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
                if (subx < 0) subx = 0;
                if (addx >= w) addx = w - 1;
                sum += row[addx] - row[subx];
            }
        } else {
            for (x = 0; x <= radius; x++) {
                out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
                sum += row[x + radius + 1] - row[0];
            }
            for (; x < right_edge; x++) {
                out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
                sum += row[x + radius + 1] - row[x - radius];
            }
            for (; x < w; x++) {
                out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
                sum += row[w - 1] - row[x - radius];
            }
        }
    }

#pragma omp for schedule(static)
    for (int xb = 0; xb < w; xb += FR_BLUR_TILE) {
        int sums[FR_BLUR_TILE];
        int count = w - xb;
        if (count > FR_BLUR_TILE) count = FR_BLUR_TILE;

        for (int k = 0; k < count; k++) {
            int x = xb + k;
            int sum = tmp[x] * (radius + 1);
            for (int yy = 1; yy <= radius; yy++) {
                int sy = yy < h ? yy : h - 1;
                sum += tmp[sy * w + x];
            }
            sums[k] = sum;
        }

        int h_right_edge = h - radius - 1;
        if (h_right_edge < radius + 1) {
            for (int yy = 0; yy < h; yy++) {
                int suby = yy > radius ? yy - radius : 0;
                int addy = yy + radius + 1;
                if (addy >= h) addy = h - 1;
                int subrow = suby * w + xb;
                int addrow = addy * w + xb;
                for (int k = 0; k < count; k++) {
                    dst[yy * w + xb + k] = (uint8_t) ((sums[k] * recip + 32768) >> 16);
                    sums[k] += tmp[addrow + k] - tmp[subrow + k];
                }
            }
        } else {
            for (int yy = 0; yy <= radius; yy++) {
                int addrow = (yy + radius + 1) * w + xb;
                int subrow = xb; 
                for (int k = 0; k < count; k++) {
                    dst[yy * w + xb + k] = (uint8_t) ((sums[k] * recip + 32768) >> 16);
                    sums[k] += tmp[addrow + k] - tmp[subrow + k];
                }
            }
            for (int yy = radius + 1; yy < h_right_edge; yy++) {
                int addrow = (yy + radius + 1) * w + xb;
                int subrow = (yy - radius) * w + xb;
                for (int k = 0; k < count; k++) {
                    dst[yy * w + xb + k] = (uint8_t) ((sums[k] * recip + 32768) >> 16);
                    sums[k] += tmp[addrow + k] - tmp[subrow + k];
                }
            }
            for (int yy = h_right_edge; yy < h; yy++) {
                int addrow = (h - 1) * w + xb;
                int subrow = (yy - radius) * w + xb;
                for (int k = 0; k < count; k++) {
                    dst[yy * w + xb + k] = (uint8_t) ((sums[k] * recip + 32768) >> 16);
                    sums[k] += tmp[addrow + k] - tmp[subrow + k];
                }
            }
        }
    }
}

static inline void fb_sample_bilinear_y(
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
    int x0 = fb_clampi((int) fx, 0, w - 1);
    int y0 = fb_clampi((int) fy, 0, h - 1);
    int x1 = fb_clampi(x0 + 1, 0, w - 1);
    int y1 = fb_clampi(y0 + 1, 0, h - 1);

    int wx = (int) ((fx - (float) x0) * 256.0f);
    int wy = (int) ((fy - (float) y0) * 256.0f);

    int p00 = Y[y0 * w + x0];
    int p10 = Y[y0 * w + x1];
    int p01 = Y[y1 * w + x0];
    int p11 = Y[y1 * w + x1];
    
    int a = p00 * (256 - wx) + p10 * wx;
    int b = p01 * (256 - wx) + p11 * wx;
    *oy = (a * (256 - wy) + b * wy + 32768) >> 16;

    int ni = y0 * w + x0;
    *ou = U[ni];
    *ov = V[ni];
}

static inline void fb_fractal_scalar(
    float x0,
    float y0,
    float cr,
    float ci,
    int max_iter,
    float *mu_out,
    float *zr_out,
    float *zi_out
) {
    float zr = x0;
    float zi = y0;
    float mu = 0.0f;
    int iter;

    for (iter = 0; iter < max_iter; iter++) {
        float zr2 = zr * zr;
        float zi2 = zi * zi;

        if (zr2 + zi2 > 16.0f)
            break;

        zi = 2.0f * zr * zi + ci;
        zr = zr2 - zi2 + cr;
    }

    if (iter < max_iter) {
        float mag2 = zr * zr + zi * zi;
        if (mag2 < 1.0f)
            mag2 = 1.0f;
        mu = ((float) iter + 1.0f - fb_fast_log2(0.5f * fb_fast_log2(mag2))) / (float) max_iter;
    } else {
        mu = 1.0f;
    }

    *mu_out = fb_clampf(mu, 0.0f, 1.0f);
    *zr_out = zr;
    *zi_out = zi;
}

static inline void fb_fractal_auto(
    const float *restrict x0,
    const float *restrict y0,
    int n,
    float cr,
    float ci,
    int max_iter,
    float *restrict mu,
    float *restrict zr_out,
    float *restrict zi_out
) {
    float zr[FB_AUTO_BLOCK];
    float zi[FB_AUTO_BLOCK];
    int iter_count[FB_AUTO_BLOCK];

#pragma omp simd
    for (int k = 0; k < n; k++) {
        zr[k] = x0[k];
        zi[k] = y0[k];
        iter_count[k] = 0;
    }

    for (int iter = 0; iter < max_iter; iter++) {
        int active_lanes = 0;

#pragma omp simd reduction(+:active_lanes)
        for (int k = 0; k < n; k++) {
            float zrk = zr[k];
            float zik = zi[k];
            float zr2 = zrk * zrk;
            float zi2 = zik * zik;
            float mag2 = zr2 + zi2;
            
            int mask = (mag2 <= 16.0f);
            active_lanes += mask;
            
            float nzr = zr2 - zi2 + cr;
            float nzi = 2.0f * zrk * zik + ci;

            zr[k] = mask ? nzr : zrk;
            zi[k] = mask ? nzi : zik;
            iter_count[k] += mask;
        }

        if (active_lanes == 0)
            break;
    }

#pragma omp simd
    for (int k = 0; k < n; k++) {
        float smooth = 1.0f;
        if (iter_count[k] < max_iter) {
            float mag2 = zr[k] * zr[k] + zi[k] * zi[k];
            mag2 = mag2 < 16.0f ? 16.0f : mag2;
            smooth = ((float) iter_count[k] + 1.0f - fb_fast_log2(0.5f * fb_fast_log2(mag2))) * (1.0f / (float) max_iter);
        }
        mu[k] = smooth < 0.0f ? 0.0f : (smooth > 1.0f ? 1.0f : smooth);
        zr_out[k] = zr[k];
        zi_out[k] = zi[k];
    }
}

static inline float fb_lod_from_footprint(float base_step, float warp, float detail, float density)
{
    float footprint = base_step + warp * detail * 0.74f;
    float rho = footprint * density + warp * density * 0.12f;
    return fb_smooth01((rho - 0.09f) * 2.45f);
}

static inline void fb_render_organism_pixel(
    fractalorganism_t *t,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict mask_blur,
    int x,
    int y,
    float x0,
    float y0,
    float mu,
    float zr,
    float zi,
    float lod,
    const fo_render_consts_t *rc
) {
    const int w = t->w;
    const int h = t->h;
    const int i = y * w + x;
    
    float subject = (float) mask_blur[i] * 0.00392156862f; // 1.0f / 255.0f
    float freq = 1.0f - lod * 0.58f;
    float escape = fb_smooth01(mu);
    
    float tendril_phase = (x0 * 1.72f + y0 * 2.18f) * rc->freq_mult1 * freq + t->phase * (0.34f + rc->pulse * 0.42f);
    float tendril_phase2 = (x0 * -1.14f + y0 * 1.48f) * rc->freq_mult2 * freq + t->orbit * 0.41f + 1.37f;
    float cell_phase = (x0 - y0) * rc->freq_mult3 * freq + t->orbit * 0.30f;
    
    float tendril = 0.5f + 0.34f * fb_lut_sin(t, tendril_phase) + 0.16f * fb_lut_sin(t, tendril_phase2);
    float cell = 0.5f + 0.5f * fb_lut_cos(t, cell_phase);
    float coarse_phase = x0 * 0.86f + y0 * 0.67f + t->phase * 0.22f + t->orbit * 0.16f;
    float coarse = 0.5f + 0.5f * fb_lut_sin(t, coarse_phase);
    
    float fine_raw = tendril * (0.60f + cell * 0.22f) + escape * 0.08f;
    float coarse_raw = coarse * 0.72f + cell * 0.18f + escape * 0.08f;
    float fine_mesh = fb_smooth01(fb_clampf((fine_raw - 0.12f) * 1.12f, 0.0f, 1.0f));
    float coarse_mesh = fb_smooth01(fb_clampf((coarse_raw - 0.10f) * 1.16f, 0.0f, 1.0f));
    float mesh = fine_mesh + (coarse_mesh - fine_mesh) * lod;
    
    float silhouette = fb_smooth01(subject * rc->silhouette_scale);
    float colonize = fb_smooth01(silhouette * rc->colonize_scale);
    float local_mesh = mesh * colonize;
    float chew = fb_clampf(local_mesh * rc->chew_mesh_scale + silhouette * rc->chew_silh_scale, 0.0f, 1.0f);
    
    float zden = 1.0f + fb_absf(zr) + fb_absf(zi);
    float inv_zden = 1.0f / zden;
    float zx = zr * inv_zden;
    float zy = zi * inv_zden;
    float fine_w = 1.0f - lod;
    
    float flowx = fb_lut_sin(t, y0 * rc->density * 0.30f * freq + x0 * 0.42f + t->phase * 0.58f);
    float flowy = fb_lut_cos(t, x0 * rc->density * 0.28f * freq - y0 * 0.38f + t->orbit * 0.54f);
    float largex = fb_lut_sin(t, coarse_phase * 0.91f + 0.67f);
    float largey = fb_lut_cos(t, coarse_phase * 0.83f - 0.39f);
    
    float dx = (float) w * rc->warp * local_mesh * (fine_w * (flowx * 0.16f + zx * 0.08f) + (0.22f + 0.78f * lod) * largex * 0.22f);
    float dy = (float) h * rc->warp * local_mesh * (fine_w * (flowy * 0.16f + zy * 0.08f) + (0.22f + 0.78f * lod) * largey * 0.22f);
    float sx = (float) x + dx;
    float sy = (float) y + dy;
    
    int syv, suv, svv;
    fb_sample_bilinear_y(src_y, src_u, src_v, sx, sy, w, h, &syv, &suv, &svv);

    float target = 82.0f + rc->target_escape * escape;
    float fluid = (float) syv + (coarse_mesh - 0.5f) * rc->fluid_scale;
    target += (fluid - target) * lod * 0.82f;
    
    int outy = (int) ((float) syv * (1.0f - chew) + target * chew + 0.5f);
    outy += (int) ((local_mesh - 0.38f) * rc->outy_mesh_scale * (1.0f - lod * 0.48f));
    outy = fb_clampi(outy, 0, 255);

    int charge = (int) (local_mesh * rc->charge_base);
    float color_phase = coarse_phase * 0.68f + tendril_phase * 0.12f + (float) x * rc->color_phase_x_step + (float) y * rc->color_phase_y_step;
    float cu, cv;
    fb_spectral_uv(t, color_phase, &cu, &cv);
    
    int outu = fb_clampi(suv + (int) (cu * (float) charge * 0.58f), 0, 255);
    int outv = fb_clampi(svv + (int) (cv * (float) charge * 0.58f), 0, 255);

    Y[i] = (uint8_t) outy;
    U[i] = (uint8_t) outu;
    V[i] = (uint8_t) outv;
}

vj_effect *fractalorganism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = FRACTALORGANISM_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Fractal Organism Mesh";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_ZOOM]       = 52;
    ve->defaults[P_DENSITY]    = 46;
    ve->defaults[P_ITER]       = 72;
    ve->defaults[P_WARP]       = 52;
    ve->defaults[P_FACET]      = 42;
    ve->defaults[P_SILHOUETTE] = 72;
    ve->defaults[P_MIX]        = 64;
    ve->defaults[P_CHROMA]     = 60;
    ve->defaults[P_PULSE]      = 46;
    ve->defaults[P_SPEED]      = 180;

    ve->limits[0][P_ZOOM]       = 0;
    ve->limits[1][P_ZOOM]       = 100;
    ve->limits[0][P_DENSITY]    = 0;
    ve->limits[1][P_DENSITY]    = 100;
    ve->limits[0][P_ITER]       = 8;
    ve->limits[1][P_ITER]       = 192;
    ve->limits[0][P_WARP]       = 0;
    ve->limits[1][P_WARP]       = 100;
    ve->limits[0][P_FACET]      = 0;
    ve->limits[1][P_FACET]      = 100;
    ve->limits[0][P_SILHOUETTE] = 0;
    ve->limits[1][P_SILHOUETTE] = 100;
    ve->limits[0][P_MIX]        = 0;
    ve->limits[1][P_MIX]        = 100;
    ve->limits[0][P_CHROMA]     = 0;
    ve->limits[1][P_CHROMA]     = 100;
    ve->limits[0][P_PULSE]      = 0;
    ve->limits[1][P_PULSE]      = 100;
    ve->limits[0][P_SPEED]      = -1000;
    ve->limits[1][P_SPEED]      = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Fractal Zoom",
        "Mesh Density",
        "Escape Depth",
        "Biome Warp",
        "Organic Branching",
        "Growth Mask",
        "Biome Presence",
        "Chroma Charge",
        "Pulse",
        "Time Scale"
    );

    return ve;
}

void *fractalorganism_malloc(int w, int h)
{
    fractalorganism_t *t = (fractalorganism_t *) vj_calloc(sizeof(fractalorganism_t));
    if (!t) return NULL;

    size_t len = (size_t) w * (size_t) h;
    size_t total = len * 7;
    size_t off = 0;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->phase = 0.0f;
    t->orbit = 0.0f;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *base = (uint8_t *) t->region;
    t->src_y = base + off; off += len;
    t->src_u = base + off; off += len;
    t->src_v = base + off; off += len;
    t->blur  = base + off; off += len;
    t->mask   = base + off; off += len;
    t->coarse = base + off; off += len;
    t->tmp    = base + off;

    for (int i = 0; i < FR_TRIG_LUT_SIZE; i++) {
        float a = FR_TWO_PI * ((float) i / (float) FR_TRIG_LUT_SIZE);
        float h6 = (float) i * (6.0f / (float) FR_TRIG_LUT_SIZE);
        float r = fb_clampf(fb_absf(h6 - 3.0f) - 1.0f, 0.0f, 1.0f);
        float g = fb_clampf(2.0f - fb_absf(h6 - 2.0f), 0.0f, 1.0f);
        float b = fb_clampf(2.0f - fb_absf(h6 - 4.0f), 0.0f, 1.0f);

        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
        t->spectral_u[i] = (-0.168736f * r - 0.331264f * g + 0.500000f * b) * 2.0f;
        t->spectral_v[i] = ( 0.500000f * r - 0.418688f * g - 0.081312f * b) * 2.0f;
    }

    return (void *) t;
}

void fractalorganism_free(void *ptr)
{
    fractalorganism_t *t = (fractalorganism_t *) ptr;

    if (!t)
        return;

    if (t->region)
        free(t->region);

    free(t);
}

void fractalorganism_apply(void *ptr, VJFrame *frame, int *args)
{
    fractalorganism_t *t = (fractalorganism_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    uint8_t *restrict blur = t->blur;
    uint8_t *restrict mask = t->mask;
    uint8_t *restrict coarse = t->coarse;
    
    const int w = t->w;
    const int h = t->h;
    const int len = t->len;

#pragma omp single
    {
        veejay_memcpy(src_y, Y, (size_t)len);
        veejay_memcpy(src_u, U, (size_t)len);
        veejay_memcpy(src_v, V, (size_t)len);
    }

    int blur_r = 6 + args[P_DENSITY] / 9;
    fb_box_blur(t, src_y, coarse, blur_r);

#pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        int detail = (int) src_y[i] - (int) coarse[i];
        int bright = src_y[i] > 138 ? src_y[i] - 138 : 0;
        if (detail < 0) detail = -detail;
        int activity = detail * 5 + bright * 4;
        mask[i] = (uint8_t) fb_clampi(activity, 0, 255);
    }

    fb_box_blur(t, mask, blur, 18 + args[P_DENSITY] / 10);

#pragma omp single
    {
        float dt = fb_time_step(args[P_SPEED]);
        float pulse_f = (float) args[P_PULSE] * 0.01f;
        t->phase = fb_wrap_2pi(t->phase + dt * 0.82f);
        t->orbit = fb_wrap_2pi(t->orbit + dt * (0.35f + pulse_f * 0.30f));
    }
    
    fo_render_consts_t rc;
    
    float zoom = 2.9f - (float) args[P_ZOOM] * 0.022f;
    if (zoom < 0.38f) zoom = 0.38f;
    
    rc.density = 2.0f + (float) args[P_DENSITY] * 0.11f;
    float wt = (float) args[P_WARP] * 0.01f; 
    rc.warp = 0.12f * wt + 0.10f * wt * wt;
    rc.facet = (float) args[P_FACET] * 0.01f;
    float silhouette_gain = (float) args[P_SILHOUETTE] * 0.01f;
    float mix_gain = (float) args[P_MIX] * 0.01f;
    float chroma_gain = (float) args[P_CHROMA] * 0.01f;
    
    rc.pulse = (float) args[P_PULSE] * 0.01f;
    rc.pulse_wave = 0.5f + 0.5f * fb_lut_sin(t, t->phase * (0.8f + rc.pulse * 1.8f) + t->orbit * 0.5f);
    
    rc.freq_mult1 = 0.72f + 0.66f * rc.facet;
    rc.freq_mult2 = 0.86f + 0.38f * rc.facet;
    rc.freq_mult3 = 1.42f + 1.10f * rc.facet;
    rc.silhouette_scale = 0.65f + 2.20f * silhouette_gain;
    rc.colonize_scale = 0.55f + 0.75f * mix_gain;
    rc.chew_mesh_scale = 0.35f + 0.65f * mix_gain;
    rc.chew_silh_scale = 0.10f * mix_gain;
    rc.charge_base = chroma_gain * 76.0f * (0.35f + 0.65f * rc.pulse_wave);
    rc.fluid_scale = 72.0f * (0.30f + 0.70f * rc.pulse);
    rc.target_escape = 142.0f * (0.42f + 0.58f * rc.pulse_wave);
    rc.outy_mesh_scale = 76.0f * (0.22f + 0.78f * rc.pulse);
    rc.color_phase_x_step = FR_TWO_PI * 0.95f / (float) w;
    rc.color_phase_y_step = FR_TWO_PI * 0.65f / (float) h;

    float c_re = -0.78f + 0.12f * fb_lut_cos(t, t->orbit * 0.56f + rc.pulse_wave * 0.62f);
    float c_im =  0.16f + 0.14f * fb_lut_sin(t, t->phase * 0.66f + rc.pulse_wave * 0.92f);
    
    float min_dim = (float) (w < h ? w : h);
    float base_step = zoom / min_dim;
    const float invw = 1.0f / (float) w;
    const float invh = 1.0f / (float) h;
    int iter_i = args[P_ITER];

#pragma omp for schedule(static)
    for (int yy = 0; yy < h; yy++) {
        float ybase = (((float) yy * invh) - 0.5f) * zoom;
        int row = yy * w;
        int xx;

        for (xx = 0; xx <= w - FB_AUTO_BLOCK; xx += FB_AUTO_BLOCK) {
            float x0[FB_AUTO_BLOCK];
            float y0[FB_AUTO_BLOCK];
            float mu[FB_AUTO_BLOCK];
            float zr[FB_AUTO_BLOCK];
            float zi[FB_AUTO_BLOCK];
            float lodv[FB_AUTO_BLOCK];

            for (int k = 0; k < FB_AUTO_BLOCK; k++) {
                int i = row + xx + k;
                float xn = (((float) (xx + k) * invw) - 0.5f) * zoom;
                float srcn = ((float) src_y[i] - 128.0f) * 0.00392156862f; // 1.0f / 255.0f
                float blrn = ((float) coarse[i] - 128.0f) * 0.00392156862f;
                float detail = (float) mask[i] * 0.00392156862f;
                float lod = fb_lod_from_footprint(base_step, rc.warp, detail, rc.density);
                float source = srcn + (blrn - srcn) * (0.38f + 0.62f * lod);
                float subject = (float) blur[i] * 0.00392156862f;

                lodv[k] = lod;

                x0[k] = xn + source * rc.warp * (0.16f + subject * 0.60f)
                      + blrn * rc.warp * 0.12f
                      + 0.08f * fb_lut_cos(t, t->phase + ybase * (2.1f - lod * 0.9f));
                y0[k] = ybase + blrn * rc.warp * (0.12f + subject * 0.28f)
                      + source * rc.warp * 0.06f
                      + 0.08f * fb_lut_sin(t, t->orbit + xn * (2.1f - lod * 0.9f));
            }

            fb_fractal_auto(x0, y0, FB_AUTO_BLOCK, c_re, c_im, iter_i, mu, zr, zi);

            for (int k = 0; k < FB_AUTO_BLOCK; k++) {
                fb_render_organism_pixel(t, Y, U, V, src_y, src_u, src_v, blur,
                    xx + k, yy, x0[k], y0[k], mu[k], zr[k], zi[k], lodv[k], &rc);
            }
        }

        for (; xx < w; xx++) {
            int i = row + xx;
            float xn = (((float) xx * invw) - 0.5f) * zoom;
            float srcn = ((float) src_y[i] - 128.0f) * 0.00392156862f;
            float blrn = ((float) coarse[i] - 128.0f) * 0.00392156862f;
            float detail = (float) mask[i] * 0.00392156862f;
            float lod = fb_lod_from_footprint(base_step, rc.warp, detail, rc.density);
            float source = srcn + (blrn - srcn) * (0.38f + 0.62f * lod);
            float subject = (float) blur[i] * 0.00392156862f;
            
            float x0 = xn + source * rc.warp * (0.16f + subject * 0.60f)
                     + blrn * rc.warp * 0.12f
                     + 0.08f * fb_lut_cos(t, t->phase + ybase * (2.1f - lod * 0.9f));
            float y0 = ybase + blrn * rc.warp * (0.12f + subject * 0.28f)
                     + source * rc.warp * 0.06f
                     + 0.08f * fb_lut_sin(t, t->orbit + xn * (2.1f - lod * 0.9f));
            float mu, zr, zi;

            fb_fractal_scalar(x0, y0, c_re, c_im, iter_i, &mu, &zr, &zi);
            fb_render_organism_pixel(t, Y, U, V, src_y, src_u, src_v, blur,
                xx, yy, x0, y0, mu, zr, zi, lod, &rc);
        }
    }
}