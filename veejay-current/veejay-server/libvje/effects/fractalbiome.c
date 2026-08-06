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
#include "fractalbiome.h"

#define FRACTALBIOME_PARAMS 10
#define FB_AUTO_BLOCK 32
#define FB_TRIG_LUT_SIZE 1024
#define FB_TRIG_LUT_MASK 1023
#define FB_TWO_PI 6.28318530718f
#define FB_INV_TWO_PI 0.15915494309f

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
    int n_threads;
    void *region;
    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;
    uint8_t *blur;
    uint8_t *tmp;
    float sin_lut[FB_TRIG_LUT_SIZE];
    float cos_lut[FB_TRIG_LUT_SIZE];
    float phase;
    float orbit;
} fractalbiome_t;

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

static inline float fb_smooth01(float x)
{
    x = fb_clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

static inline float fb_wrap_2pi(float v)
{
    if (v >= FB_TWO_PI || v < 0.0f) {
        int k = (int) (v * FB_INV_TWO_PI);
        v -= (float) k * FB_TWO_PI;
        if (v < 0.0f)
            v += FB_TWO_PI;
        else if (v >= FB_TWO_PI)
            v -= FB_TWO_PI;
    }
    return v;
}

static inline float fb_lut_sin(const fractalbiome_t *t, float phase)
{
    float fidx = phase * ((float) FB_TRIG_LUT_SIZE * FB_INV_TWO_PI);
    int idx0 = (int) fidx;
    float frac;
    int i0;
    int i1;

    if (fidx < 0.0f && (float) idx0 != fidx)
        idx0--;

    frac = fidx - (float) idx0;
    i0 = idx0 & FB_TRIG_LUT_MASK;
    i1 = (i0 + 1) & FB_TRIG_LUT_MASK;
    return t->sin_lut[i0] + (t->sin_lut[i1] - t->sin_lut[i0]) * frac;
}

static inline float fb_lut_cos(const fractalbiome_t *t, float phase)
{
    float fidx = phase * ((float) FB_TRIG_LUT_SIZE * FB_INV_TWO_PI);
    int idx0 = (int) fidx;
    float frac;
    int i0;
    int i1;

    if (fidx < 0.0f && (float) idx0 != fidx)
        idx0--;

    frac = fidx - (float) idx0;
    i0 = idx0 & FB_TRIG_LUT_MASK;
    i1 = (i0 + 1) & FB_TRIG_LUT_MASK;
    return t->cos_lut[i0] + (t->cos_lut[i1] - t->cos_lut[i0]) * frac;
}

static inline void fb_spectral_uv(float phase, float *u, float *v)
{
    float h = fb_wrap_2pi(phase) * FB_INV_TWO_PI;
    float h6 = h * 6.0f;
    float r = fb_clampf(fb_absf(h6 - 3.0f) - 1.0f, 0.0f, 1.0f);
    float g = fb_clampf(2.0f - fb_absf(h6 - 2.0f), 0.0f, 1.0f);
    float b = fb_clampf(2.0f - fb_absf(h6 - 4.0f), 0.0f, 1.0f);

    *u = (-0.168736f * r - 0.331264f * g + 0.500000f * b) * 2.0f;
    *v = ( 0.500000f * r - 0.418688f * g - 0.081312f * b) * 2.0f;
}

static inline float fb_time_step(int speed)
{
    float u;
    float mag;

    if (speed == 0)
        return 0.0f;

    u = fb_clampf(fb_absf((float) speed) * 0.001f, 0.0f, 1.0f);
    mag = 0.00018f * (exp2f(9.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static void fb_box_blur(
    fractalbiome_t *t,
    const uint8_t *restrict src,
    uint8_t *restrict dst,
    int radius
) {
    const int w = t->w;
    const int h = t->h;
    const int window = radius * 2 + 1;
    const int recip = (65536 + window / 2) / window;
    uint8_t *restrict tmp = t->tmp;
    int y;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + y * w;
        uint8_t *out = tmp + y * w;
        int sum = row[0] * (radius + 1);
        int x;

        for (x = 1; x <= radius; x++)
            sum += row[x < w ? x : w - 1];

        for (x = 0; x < w; x++) {
            int subx = x - radius;
            int addx = x + radius + 1;

            out[x] = (uint8_t) ((sum * recip + 32768) >> 16);
            if (subx < 0)
                subx = 0;
            if (addx >= w)
                addx = w - 1;
            sum += row[addx] - row[subx];
        }
    }

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (int x = 0; x < w; x++) {
        int sum = tmp[x] * (radius + 1);
        int yy;

        for (yy = 1; yy <= radius; yy++) {
            int sy = yy < h ? yy : h - 1;
            sum += tmp[sy * w + x];
        }

        for (yy = 0; yy < h; yy++) {
            int suby = yy - radius;
            int addy = yy + radius + 1;

            dst[yy * w + x] = (uint8_t) ((sum * recip + 32768) >> 16);
            if (suby < 0)
                suby = 0;
            if (addy >= h)
                addy = h - 1;
            sum += tmp[addy * w + x] - tmp[suby * w + x];
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
    int x0;
    int y0;
    int x1;
    int y1;
    int wx;
    int wy;
    int a;
    int b;
    int p00;
    int p10;
    int p01;
    int p11;
    int ni;

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
    if (fx < 0.0f && (float) x0 != fx)
        x0--;
    if (fy < 0.0f && (float) y0 != fy)
        y0--;

    x1 = x0 + 1;
    y1 = y0 + 1;

    if (x0 < 0)
        x0 = 0;
    else if (x0 >= w)
        x0 = w - 1;
    if (y0 < 0)
        y0 = 0;
    else if (y0 >= h)
        y0 = h - 1;
    if (x1 < 0)
        x1 = 0;
    else if (x1 >= w)
        x1 = w - 1;
    if (y1 < 0)
        y1 = 0;
    else if (y1 >= h)
        y1 = h - 1;

    wx = (int) ((fx - (float) x0) * 256.0f);
    wy = (int) ((fy - (float) y0) * 256.0f);

    p00 = Y[y0 * w + x0];
    p10 = Y[y0 * w + x1];
    p01 = Y[y1 * w + x0];
    p11 = Y[y1 * w + x1];
    a = p00 * (256 - wx) + p10 * wx;
    b = p01 * (256 - wx) + p11 * wx;
    *oy = (a * (256 - wy) + b * wy + 32768) >> 16;

    ni = y0 * w + x0;
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
        mu = ((float) iter + 1.0f - log2f(0.5f * log2f(mag2))) / (float) max_iter;
    }
    else {
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
    float iterf[FB_AUTO_BLOCK];
    float active[FB_AUTO_BLOCK];
    int k;
    int iter;

#pragma omp simd
    for (k = 0; k < n; k++) {
        zr[k] = x0[k];
        zi[k] = y0[k];
        iterf[k] = 0.0f;
        active[k] = 1.0f;
    }

    for (iter = 0; iter < max_iter; iter++) {
        int any = 0;

#pragma omp simd reduction(+:any)
        for (k = 0; k < n; k++) {
            float zrk = zr[k];
            float zik = zi[k];
            float zr2 = zrk * zrk;
            float zi2 = zik * zik;
            float mag2 = zr2 + zi2;
            int step = (active[k] > 0.5f) && (mag2 <= 16.0f);
            float prod = zrk * zik;
            float nzr = zr2 - zi2 + cr;
            float nzi = prod + prod + ci;

            if (step) {
                zr[k] = nzr;
                zi[k] = nzi;
                iterf[k] += 1.0f;
            }
            active[k] = step ? 1.0f : 0.0f;
            any += step;
        }

        if (any == 0)
            break;
    }

    for (k = 0; k < n; k++) {
        float smooth;

        if (iterf[k] >= (float) max_iter - 0.5f) {
            smooth = 1.0f;
        }
        else {
            float mag2 = zr[k] * zr[k] + zi[k] * zi[k];
            if (mag2 < 16.0f)
                mag2 = 16.0f;
            smooth = (iterf[k] + 1.0f - log2f(0.5f * log2f(mag2))) * (1.0f / (float) max_iter);
        }

        mu[k] = fb_clampf(smooth, 0.0f, 1.0f);
        zr_out[k] = zr[k];
        zi_out[k] = zi[k];
    }
}

static inline float fb_lod_from_footprint(float base_step, float warp, float detail, float density)
{
    float footprint = base_step + warp * detail * 0.82f;
    float rho = footprint * density + warp * density * 0.12f;
    return fb_smooth01((rho - 0.10f) * 2.35f);
}

static inline void fb_render_pixel(
    fractalbiome_t *t,
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    const uint8_t *restrict src_y,
    const uint8_t *restrict src_u,
    const uint8_t *restrict src_v,
    const uint8_t *restrict blur,
    int x,
    int y,
    float x0,
    float y0,
    float mu,
    float zr,
    float zi,
    float density,
    float warp,
    float facet,
    float silhouette_gain,
    float mix_gain,
    float chroma_gain,
    float pulse,
    float pulse_wave,
    float base_step
) {
    const int w = t->w;
    const int h = t->h;
    const int i = y * w + x;
    float detail = fb_absf((float) src_y[i] - (float) blur[i]) * (1.0f / 255.0f);
    float footprint_lod = fb_lod_from_footprint(base_step, warp, detail, density);
    float lod = footprint_lod;
    float zden = 1.0f + fb_absf(zr) + fb_absf(zi);
    float zx = zr / zden;
    float zy = zi / zden;
    float fractal_lod = fb_smooth01((mu - 0.48f) * 1.65f);
    float alias_lod = fb_smooth01((footprint_lod - 0.24f) * 1.3157895f);
    float fine_density;
    float gx;
    float gy;
    float fx;
    float fy;

    if (fractal_lod > lod)
        lod = fractal_lod;
    fine_density = density * (1.0f - 0.58f * lod);
    gx = x0 * fine_density + zx * 1.15f * (1.0f - lod);
    gy = y0 * fine_density + zy * 1.15f * (1.0f - lod);
    fx = gx - floorf(gx);
    fy = gy - floorf(gy);
    float edge0 = fx < (1.0f - fx) ? fx : (1.0f - fx);
    float edge1 = fy < (1.0f - fy) ? fy : (1.0f - fy);
    float edge = edge0 < edge1 ? edge0 : edge1;
    float aa = 0.065f + lod * 0.34f;
    float line = 1.0f - fb_smooth01(edge / aa);
    float crystal = fb_smooth01(mu) * (0.55f + 0.45f * pulse_wave);
    float coarse_phase = x0 * (1.45f + facet * 0.85f)
                       + y0 * (1.10f + facet * 0.62f)
                       + mu * 1.65f + t->phase * 0.52f + t->orbit * 0.31f;
    float coarse = 0.5f + 0.5f * fb_lut_sin(t, coarse_phase);
    float fine_mesh = fb_clampf(line * (0.40f + facet * 0.90f) + crystal * (0.55f + facet * 0.55f), 0.0f, 1.0f);
    float coarse_mesh = fb_smooth01(fb_clampf(coarse * (0.72f + facet * 0.20f) + crystal * 0.42f - 0.16f, 0.0f, 1.0f));
    float mesh = fine_mesh + (coarse_mesh - fine_mesh) * lod;
    float render_crystal = crystal;
    float render_phase = coarse_phase;
    float safe_mesh = coarse_mesh;

    if (alias_lod > 0.0001f) {
        float safe_coarse;
        float safe_crystal;

        render_phase -= mu * 1.65f * alias_lod;
        safe_coarse = 0.5f + 0.5f * fb_lut_sin(t, render_phase);
        safe_crystal = fb_smooth01(fb_clampf(safe_coarse * 0.90f + 0.05f, 0.0f, 1.0f))
                     * (0.62f + 0.38f * pulse_wave);
        safe_mesh = fb_smooth01(fb_clampf(safe_coarse * (0.80f + facet * 0.16f)
                  + safe_crystal * 0.34f - 0.12f, 0.0f, 1.0f));
        render_crystal += (safe_crystal - render_crystal) * alias_lod;
        mesh += (safe_mesh - mesh) * alias_lod;
    }

    float source_edge = fb_clampf(detail * (255.0f / 38.0f), 0.0f, 1.0f);
    float silhouette = fb_smooth01(source_edge * (0.50f + 1.50f * silhouette_gain));
    float chew = fb_clampf(mesh * (0.25f + 0.75f * mix_gain) + silhouette * (0.30f + 0.70f * mix_gain), 0.0f, 1.0f);
    float fine_w = (1.0f - lod) * (1.0f - alias_lod);
    float coarse_x = fb_lut_sin(t, render_phase * 0.83f + 0.71f);
    float coarse_y = fb_lut_cos(t, render_phase * 0.77f - 0.43f);
    float dx = (float) w * warp * (
        fine_w * (zx * 0.18f + (fx - 0.5f) * 0.28f) * (0.28f + 0.72f * mesh)
        + (0.20f + 0.80f * lod) * coarse_x * 0.20f * (0.38f + 0.62f * mesh));
    float dy = (float) h * warp * (
        fine_w * (zy * 0.18f + (fy - 0.5f) * 0.28f) * (0.28f + 0.72f * mesh)
        + (0.20f + 0.80f * lod) * coarse_y * 0.20f * (0.38f + 0.62f * mesh));
    float sx = (float) x + dx;
    float sy = (float) y + dy;
    float color_phase = render_phase * 0.72f
                      + (float) x * (FB_TWO_PI * 1.10f / (float) w)
                      + (float) y * (FB_TWO_PI * 0.55f / (float) h)
                      + (fx - fy) * 1.70f * (1.0f - alias_lod);
    float cu;
    float cv;
    int syv;
    int suv;
    int svv;
    int outy;
    int outu;
    int outv;
    int charge;

    fb_sample_bilinear_y(src_y, src_u, src_v, sx, sy, w, h, &syv, &suv, &svv);

    {
        float target = 96.0f + 159.0f * render_crystal;
        float fluid = (float) syv + (safe_mesh - 0.5f) * 86.0f * (0.35f + 0.65f * pulse);
        float fluid_mix = lod * 0.74f;
        fluid_mix += (0.94f - fluid_mix) * alias_lod;
        target += (fluid - target) * fluid_mix;
        outy = (int) ((float) syv * (1.0f - chew) + target * chew + 0.5f);
    }
    outy += (int) ((mesh - 0.5f) * 72.0f * (0.25f + 0.75f * pulse)
                  * (1.0f - lod * 0.42f) * (1.0f - alias_lod * 0.72f));
    outy = fb_clampi(outy, 0, 255);

    charge = (int) (mesh * chroma_gain * 74.0f * (0.35f + 0.65f * pulse_wave));
    fb_spectral_uv(color_phase, &cu, &cv);
    outu = fb_clampi(suv + (int) (cu * (float) charge * 0.58f), 0, 255);
    outv = fb_clampi(svv + (int) (cv * (float) charge * 0.58f), 0, 255);

    Y[i] = (uint8_t) outy;
    U[i] = (uint8_t) outu;
    V[i] = (uint8_t) outv;
}


vj_effect *fractalbiome_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve)
        return NULL;

    ve->num_params = FRACTALBIOME_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Fractal Biome Mesh";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_ZOOM]       = 44;
    ve->defaults[P_DENSITY]    = 52;
    ve->defaults[P_ITER]       = 64;
    ve->defaults[P_WARP]       = 58;
    ve->defaults[P_FACET]      = 62;
    ve->defaults[P_SILHOUETTE] = 64;
    ve->defaults[P_MIX]        = 68;
    ve->defaults[P_CHROMA]     = 56;
    ve->defaults[P_PULSE]      = 48;
    ve->defaults[P_SPEED]      = 220;

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
        "Crystal Facet",
        "Silhouette Pull",
        "Fractal Mix",
        "Chroma Charge",
        "Pulse",
        "Time Scale"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_KICK_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 44, 80, 28, 46, 20, 260, 0, 1, 0, VJ_BEAT_COST_CHEAP, 150, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_HAT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HAT_PULSE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 44, 86, 18, 34, 16, 130, 0, 1, 0, VJ_BEAT_COST_CHEAP, 115, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BEAT_GATE, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 34, 80, 34, 54, 20, 200, 0, 1, 0, VJ_BEAT_COST_CHEAP, 180, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *fractalbiome_malloc(int w, int h)
{
    fractalbiome_t *t = NULL;
    unsigned char *base = NULL;
    size_t len = (size_t) w * (size_t) h;
    size_t total = len * 5;
    size_t off = 0;
    size_t i;

    if (w <= 0 || h <= 0 || len == 0)
        return NULL;

    t = (fractalbiome_t *) vj_calloc(sizeof(fractalbiome_t));
    if (!t)
        return NULL;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->n_threads = vje_advise_num_threads(w * h);
    t->phase = 0.0f;
    t->orbit = 0.0f;

    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    base = (unsigned char *) t->region;
    t->src_y = (uint8_t *) (base + off); off += len;
    t->src_u = (uint8_t *) (base + off); off += len;
    t->src_v = (uint8_t *) (base + off); off += len;
    t->blur  = (uint8_t *) (base + off); off += len;
    t->tmp   = (uint8_t *) (base + off);

    for (i = 0; i < FB_TRIG_LUT_SIZE; i++) {
        float a = FB_TWO_PI * ((float) i / (float) FB_TRIG_LUT_SIZE);
        t->sin_lut[i] = sinf(a);
        t->cos_lut[i] = cosf(a);
    }

    return (void *) t;
}

void fractalbiome_free(void *ptr)
{
    fractalbiome_t *t = (fractalbiome_t *) ptr;

    if (!t)
        return;

    if (t->region)
        free(t->region);

    free(t);
}

void fractalbiome_apply(void *ptr, VJFrame *frame, int *args)
{
    fractalbiome_t *t = (fractalbiome_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    uint8_t *restrict blur = t->blur;
    const int w = t->w;
    const int h = t->h;
    const float invw = 1.0f / (float) w;
    const float invh = 1.0f / (float) h;
    const float min_dim = (float) (w < h ? w : h);
    int len = frame->len;
    int zoom_i = args[P_ZOOM];
    int density_i = args[P_DENSITY];
    int iter_i = args[P_ITER];
    int warp_i = args[P_WARP];
    int facet_i = args[P_FACET];
    int silhouette_i = args[P_SILHOUETTE];
    int mix_i = args[P_MIX];
    int chroma_i = args[P_CHROMA];
    int pulse_i = args[P_PULSE];
    int speed_i = args[P_SPEED];
    float zoom;
    float density;
    float warp;
    float facet;
    float silhouette_gain;
    float mix_gain;
    float chroma_gain;
    float pulse;
    float pulse_wave;
    float c_re;
    float c_im;
    float base_step;
    int blur_r;
    int yy;

    if (len <= 0 || len > t->len)
        len = t->len;

    veejay_memcpy(src_y, Y, len);
    veejay_memcpy(src_u, U, len);
    veejay_memcpy(src_v, V, len);

    blur_r = 6 + density_i / 9;
    fb_box_blur(t, src_y, blur, blur_r);

    pulse = (float) pulse_i * 0.01f;
    {
        float dt = fb_time_step(speed_i);
        t->phase = fb_wrap_2pi(t->phase + dt * 1.10f);
        t->orbit = fb_wrap_2pi(t->orbit + dt * (0.45f + pulse * 0.35f));
    }

    zoom = 2.9f - (float) zoom_i * 0.022f;
    if (zoom < 0.38f)
        zoom = 0.38f;
    density = 2.0f + (float) density_i * 0.11f;
    warp = (float) warp_i * 0.0019f;
    facet = (float) facet_i * 0.01f;
    silhouette_gain = (float) silhouette_i * 0.01f;
    mix_gain = (float) mix_i * 0.01f;
    chroma_gain = (float) chroma_i * 0.01f;
    pulse_wave = 0.5f + 0.5f * fb_lut_sin(t, t->phase * (0.8f + pulse * 1.8f) + t->orbit * 0.5f);
    c_re = -0.78f + 0.14f * fb_lut_cos(t, t->orbit * 0.62f + pulse_wave * 0.72f);
    c_im =  0.16f + 0.16f * fb_lut_sin(t, t->phase * 0.72f + pulse_wave * 1.05f);
    base_step = zoom / min_dim;

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for (yy = 0; yy < h; yy++) {
        float ybase = (((float) yy * invh) - 0.5f) * zoom;
        int row = yy * w;
        int xx;

        for (xx = 0; xx <= w - FB_AUTO_BLOCK; xx += FB_AUTO_BLOCK) {
            float x0[FB_AUTO_BLOCK];
            float y0[FB_AUTO_BLOCK];
            float mu[FB_AUTO_BLOCK];
            float zr[FB_AUTO_BLOCK];
            float zi[FB_AUTO_BLOCK];
            int k;

            for (k = 0; k < FB_AUTO_BLOCK; k++) {
                int i = row + xx + k;
                float xn = (((float) (xx + k) * invw) - 0.5f) * zoom;
                float srcn = ((float) src_y[i] - 128.0f) * (1.0f / 255.0f);
                float blrn = ((float) blur[i] - 128.0f) * (1.0f / 255.0f);
                float detail = fb_absf((float) src_y[i] - (float) blur[i]) * (1.0f / 255.0f);
                float lod = fb_lod_from_footprint(base_step, warp, detail, density);
                float source = srcn + (blrn - srcn) * lod;

                x0[k] = xn + source * warp * 0.72f + blrn * warp * 0.18f
                      + 0.11f * fb_lut_cos(t, t->phase + ybase * (2.2f - lod * 0.9f));
                y0[k] = ybase + blrn * warp * 0.70f + source * warp * 0.10f
                      + 0.11f * fb_lut_sin(t, t->orbit + xn * (2.2f - lod * 0.9f));
            }

            fb_fractal_auto(x0, y0, FB_AUTO_BLOCK, c_re, c_im, iter_i, mu, zr, zi);

            for (k = 0; k < FB_AUTO_BLOCK; k++) {
                fb_render_pixel(t, Y, U, V, src_y, src_u, src_v, blur,
                    xx + k, yy, x0[k], y0[k], mu[k], zr[k], zi[k],
                    density, warp, facet, silhouette_gain, mix_gain,
                    chroma_gain, pulse, pulse_wave, base_step);
            }
        }

        for (; xx < w; xx++) {
            int i = row + xx;
            float xn = (((float) xx * invw) - 0.5f) * zoom;
            float srcn = ((float) src_y[i] - 128.0f) * (1.0f / 255.0f);
            float blrn = ((float) blur[i] - 128.0f) * (1.0f / 255.0f);
            float detail = fb_absf((float) src_y[i] - (float) blur[i]) * (1.0f / 255.0f);
            float lod = fb_lod_from_footprint(base_step, warp, detail, density);
            float source = srcn + (blrn - srcn) * lod;
            float x0 = xn + source * warp * 0.72f + blrn * warp * 0.18f
                     + 0.11f * fb_lut_cos(t, t->phase + ybase * (2.2f - lod * 0.9f));
            float y0 = ybase + blrn * warp * 0.70f + source * warp * 0.10f
                     + 0.11f * fb_lut_sin(t, t->orbit + xn * (2.2f - lod * 0.9f));
            float mu;
            float zr;
            float zi;

            fb_fractal_scalar(x0, y0, c_re, c_im, iter_i, &mu, &zr, &zi);
            fb_render_pixel(t, Y, U, V, src_y, src_u, src_v, blur,
                xx, yy, x0, y0, mu, zr, zi, density, warp, facet,
                silhouette_gain, mix_gain, chroma_gain, pulse, pulse_wave, base_step);
        }
    }
}

