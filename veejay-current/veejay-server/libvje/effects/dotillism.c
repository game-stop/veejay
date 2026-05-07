#include "common.h"
#include <veejaycore/vjmem.h>
#include "dotillism.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
    int grid_capacity;
    int w;
    int h;
    int initialized;
    int n_threads;
    float maturity;
    uint32_t frame_no;
} fluid_paint_t;

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

static inline void sample_canvas_444_fast(const uint8_t *restrict yplane, const uint8_t *restrict uplane, const uint8_t *restrict vplane, float x, float y, int w, int h, float *out_y, float *out_u, float *out_v)
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
    *out_y = (float)yplane[i00] * w00 + (float)yplane[i10] * w10 + (float)yplane[i01] * w01 + (float)yplane[i11] * w11;
    *out_u = (float)uplane[i00] * w00 + (float)uplane[i10] * w10 + (float)uplane[i01] * w01 + (float)uplane[i11] * w11;
    *out_v = (float)vplane[i00] * w00 + (float)vplane[i10] * w10 + (float)vplane[i01] * w01 + (float)vplane[i11] * w11;
}

static void build_flow_grid(fluid_paint_t *p, int w, int h, int cell)
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

vj_effect *dotillism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 11;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 100; ve->defaults[0] = 45;
    ve->limits[0][1] = 0; ve->limits[1][1] = 100; ve->defaults[1] = 24;
    ve->limits[0][2] = 0; ve->limits[1][2] = 100; ve->defaults[2] = 55;
    ve->limits[0][3] = 0; ve->limits[1][3] = 100; ve->defaults[3] = 55;
    ve->limits[0][4] = 0; ve->limits[1][4] = 100; ve->defaults[4] = 72;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100; ve->defaults[5] = 45;
    ve->limits[0][6] = 0; ve->limits[1][6] = 100; ve->defaults[6] = 70;
    ve->limits[0][7] = 0; ve->limits[1][7] = 100; ve->defaults[7] = 40;
    ve->limits[0][8] = 0; ve->limits[1][8] = 100; ve->defaults[8] = 55;
    ve->limits[0][9] = 0; ve->limits[1][9] = 100; ve->defaults[9] = 65;
    ve->limits[0][10] = 0; ve->limits[1][10] = 100; ve->defaults[10] = 50;

    ve->description = "Liquid Feedback";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Build Speed",
        "Source Feed",
        "Flow",
        "Swirl",
        "Color Bleed",
        "Detail",
        "Trail",
        "Turbulence",
        "Flow Scale",
        "Motion React",
        "Chroma Gain"
    );
    return ve;
}

void *dotillism_malloc(int w, int h)
{
    fluid_paint_t *p = (fluid_paint_t *)vj_calloc(sizeof(fluid_paint_t));
    const int len = w * h;
    const int min_cell = 8;
    const int max_gw = (w + min_cell - 1) / min_cell + 2;
    const int max_gh = (h + min_cell - 1) / min_cell + 2;

    p->w = w;
    p->h = h;
    p->canvas_y = (uint8_t *)vj_malloc(len);
    p->canvas_u = (uint8_t *)vj_malloc(len);
    p->canvas_v = (uint8_t *)vj_malloc(len);
    p->next_y = (uint8_t *)vj_malloc(len);
    p->next_u = (uint8_t *)vj_malloc(len);
    p->next_v = (uint8_t *)vj_malloc(len);
    p->prev_src_y = (uint8_t *)vj_malloc(len);
    p->grid_capacity = max_gw * max_gh;
    p->grid_x = (float *)vj_malloc(sizeof(float) * p->grid_capacity);
    p->grid_y = (float *)vj_malloc(sizeof(float) * p->grid_capacity);

    veejay_memset(p->canvas_y, 0, len);
    veejay_memset(p->canvas_u, 128, len);
    veejay_memset(p->canvas_v, 128, len);
    veejay_memset(p->next_y, 0, len);
    veejay_memset(p->next_u, 128, len);
    veejay_memset(p->next_v, 128, len);
    veejay_memset(p->prev_src_y, 0, len);
    veejay_memset(p->grid_x, 0, sizeof(float) * p->grid_capacity);
    veejay_memset(p->grid_y, 0, sizeof(float) * p->grid_capacity);

    p->initialized = 0;
    p->maturity = 0.0f;
    p->frame_no = 0;
    p->n_threads = vje_advise_num_threads(len);

    return (void *)p;
}

void dotillism_free(void *ptr)
{
    fluid_paint_t *p = (fluid_paint_t *)ptr;
    if (!p) return;
    if (p->canvas_y) free(p->canvas_y);
    if (p->canvas_u) free(p->canvas_u);
    if (p->canvas_v) free(p->canvas_v);
    if (p->next_y) free(p->next_y);
    if (p->next_u) free(p->next_u);
    if (p->next_v) free(p->next_v);
    if (p->prev_src_y) free(p->prev_src_y);
    if (p->grid_x) free(p->grid_x);
    if (p->grid_y) free(p->grid_y);
    free(p);
}

void dotillism_apply(void *ptr, VJFrame *frame, int *args)
{
    fluid_paint_t *p = (fluid_paint_t *)ptr;
    if (!p || !frame || !args) return;
    if (!frame->data[0] || !frame->data[1] || !frame->data[2]) return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = w * h;

    if (w <= 2 || h <= 2) return;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    if (p->w != w || p->h != h) return;

    if (!p->initialized) {
        veejay_memcpy(p->canvas_y, Y, len);
        veejay_memcpy(p->canvas_u, U, len);
        veejay_memcpy(p->canvas_v, V, len);
        veejay_memcpy(p->prev_src_y, Y, len);
        p->initialized = 1;
        p->maturity = 0.0f;
        p->frame_no = 0;
    }

    const float build_param = clampf_local((float)args[0] * 0.01f, 0.0f, 1.0f);
    const float refresh_param = clampf_local((float)args[1] * 0.01f, 0.0f, 1.0f);
    const float flow_param = clampf_local((float)args[2] * 0.01f, 0.0f, 1.0f);
    const float swirl_param = clampf_local((float)args[3] * 0.01f, 0.0f, 1.0f);
    const float bleed_param = clampf_local((float)args[4] * 0.01f, 0.0f, 1.0f);
    const float detail_param = clampf_local((float)args[5] * 0.01f, 0.0f, 1.0f);
    const float trail_param = clampf_local((float)args[6] * 0.01f, 0.0f, 1.0f);
    const float turbulence_param = clampf_local((float)args[7] * 0.01f, 0.0f, 1.0f);
    const float scale_param = clampf_local((float)args[8] * 0.01f, 0.0f, 1.0f);
    const float motion_param = clampf_local((float)args[9] * 0.01f, 0.0f, 1.0f);
    const float chroma_gain_param = clampf_local((float)args[10] * 0.01f, 0.0f, 1.0f);

    const float build_curve = build_param * build_param;
    const float refresh_curve = refresh_param * refresh_param;
    const float flow_curve = flow_param * flow_param;
    const float swirl_curve = swirl_param * swirl_param;
    const float bleed_curve = bleed_param * bleed_param;
    const float detail_curve = detail_param * detail_param;
    const float trail_curve = trail_param * trail_param;
    const float turbulence_curve = turbulence_param * turbulence_param;
    const float scale_curve = scale_param * scale_param;
    const float motion_curve = motion_param * motion_param;
    const float chroma_gain_curve = chroma_gain_param * chroma_gain_param;

    const float mature_rate = 0.0005f + build_curve * 0.0500f;
    p->maturity += (1.0f - p->maturity) * mature_rate;
    if (p->maturity > 1.0f) p->maturity = 1.0f;

    const float maturity = p->maturity;

    const float early_refresh = 0.008f + refresh_curve * 0.620f;
    const float mature_refresh = 0.010f + refresh_curve * 0.280f;

    float src_mix = lerpf_local(early_refresh, mature_refresh, maturity);
    src_mix *= (1.0f - trail_curve * 0.58f);
    if (src_mix < 0.006f) src_mix = 0.006f;
    if (src_mix > 0.78f) src_mix = 0.78f;

    const float hist_mix = 1.0f - src_mix;

    float chroma_src_mix = src_mix + 0.014f + refresh_curve * 0.120f + (1.0f - trail_curve) * 0.020f;
    if (chroma_src_mix < 0.018f) chroma_src_mix = 0.018f;
    if (chroma_src_mix > 0.82f) chroma_src_mix = 0.82f;

    const float chroma_hist_mix = 1.0f - chroma_src_mix;
    const float chroma_wet_gain = 0.92f + chroma_gain_curve * 0.38f + bleed_curve * 0.18f * (1.0f - chroma_src_mix);
    const float detail_gain = detail_curve * lerpf_local(1.10f, 0.55f, maturity);
    const float flow_pixels = (0.35f + maturity * 2.10f) * (0.5f + flow_curve * 40.0f);
    const float swirl_gain = swirl_curve * 7.0f;
    const float turbulence_gain = turbulence_curve * 18.0f * (0.25f + maturity * 0.75f);

    int cell = 10 + (int)(scale_curve * 54.0f);
    cell -= (int)(turbulence_curve * 18.0f);
    if (cell < 8) cell = 8;
    if (cell > 64) cell = 64;

    float lut[65];
    for (int i = 0; i <= cell; i++)
        lut[i] = smoothstepf_local((float)i / (float)cell);

    build_flow_grid(p, w, h, cell);

    const int gw = (w + cell - 1) / cell + 2;
    const int gh = (h + cell - 1) / cell + 2;

    const uint8_t *restrict old_y = p->canvas_y;
    const uint8_t *restrict old_u = p->canvas_u;
    const uint8_t *restrict old_v = p->canvas_v;

    uint8_t *restrict next_y = p->next_y;
    uint8_t *restrict next_u = p->next_u;
    uint8_t *restrict next_v = p->next_v;
    uint8_t *restrict prev_y = p->prev_src_y;

    const float inv_max_dim = 1.0f / ((float)((w > h) ? w : h) * 0.5f + 1.0f);
    const float amp = 1.0f + turbulence_gain * 0.18f;
    const float motion_scale = (0.35f + motion_curve * 5.25f) * (1.0f / 255.0f);

    #pragma omp parallel for collapse(2) schedule(static) num_threads(p->n_threads)
    for (int gy = 0; gy < gh - 1; gy++) {
        for (int gx = 0; gx < gw - 1; gx++) {
            const int y0 = gy * cell;
            const int y1 = y0 + cell;
            const int ye = (y1 < h) ? y1 : h;
            const int x0 = gx * cell;
            const int x1 = x0 + cell;
            const int xe = (x1 < w) ? x1 : w;

            const int gi00 = gy * gw + gx;
            const int gi10 = gy * gw + gx + 1;
            const int gi01 = (gy + 1) * gw + gx;
            const int gi11 = (gy + 1) * gw + gx + 1;

            const float vx00 = p->grid_x[gi00];
            const float vx10 = p->grid_x[gi10];
            const float vx01 = p->grid_x[gi01];
            const float vx11 = p->grid_x[gi11];

            const float vy00 = p->grid_y[gi00];
            const float vy10 = p->grid_y[gi10];
            const float vy01 = p->grid_y[gi01];
            const float vy11 = p->grid_y[gi11];

            for (int y = y0; y < ye; y++) {
                const int ly = y - y0;
                const float fy = lut[ly];

                const float ax = lerpf_local(vx00, vx01, fy);
                const float bx = lerpf_local(vx10, vx11, fy);
                const float ay = lerpf_local(vy00, vy01, fy);
                const float by = lerpf_local(vy10, vy11, fy);

                const int row = y * w;
                const float cy = (float)y - (float)h * 0.5f;

                for (int x = x0; x < xe; x++) {
                    const int idx = row + x;
                    const int lx = x - x0;
                    const float fx = lut[lx];

                    const uint8_t src_y = Y[idx];
                    const uint8_t src_u = U[idx];
                    const uint8_t src_v = V[idx];

                    int dm = (int)src_y - (int)prev_y[idx];
                    if (dm < 0) dm = -dm;

                    float vx = lerpf_local(ax, bx, fx);
                    float vy = lerpf_local(ay, by, fx);

                    if (swirl_gain > 0.0f) {
                        const float cx = (float)x - (float)w * 0.5f;
                        vx += (-cy * inv_max_dim) * swirl_gain;
                        vy += ( cx * inv_max_dim) * swirl_gain;
                    }

                    vx *= amp;
                    vy *= amp;

                    const float mag2 = vx * vx + vy * vy;
                    if (mag2 > 4.0f) {
                        const float s = 1.0f / (1.0f + (mag2 - 4.0f) * 0.125f);
                        vx *= s;
                        vy *= s;
                    }

                    const float motion_boost = 0.35f + (float)dm * motion_scale;
                    const float disp = flow_pixels * motion_boost;
                    const float sx = (float)x - vx * disp;
                    const float sy = (float)y - vy * disp;

                    float adv_y;
                    float adv_u;
                    float adv_v;

                    sample_canvas_444_fast(old_y, old_u, old_v, sx, sy, w, h, &adv_y, &adv_u, &adv_v);

                    float out_y = adv_y * hist_mix + (float)src_y * src_mix;
                    out_y += ((float)src_y - adv_y) * detail_gain * 0.45f;

                    const float adv_u_s = adv_u - 128.0f;
                    const float adv_v_s = adv_v - 128.0f;
                    const float src_u_s = (float)src_u - 128.0f;
                    const float src_v_s = (float)src_v - 128.0f;

                    const float out_u_s = (adv_u_s * chroma_hist_mix + src_u_s * chroma_src_mix) * chroma_wet_gain;
                    const float out_v_s = (adv_v_s * chroma_hist_mix + src_v_s * chroma_src_mix) * chroma_wet_gain;

                    const uint8_t oy = clamp_u8f(out_y);
                    const uint8_t ou = clamp_u8f(128.0f + out_u_s);
                    const uint8_t ov = clamp_u8f(128.0f + out_v_s);

                    next_y[idx] = oy;
                    next_u[idx] = ou;
                    next_v[idx] = ov;

                    Y[idx] = oy;
                    U[idx] = ou;
                    V[idx] = ov;

                    prev_y[idx] = src_y;
                }
            }
        }
    }

    {
        uint8_t *tmp;
        tmp = p->canvas_y; p->canvas_y = p->next_y; p->next_y = tmp;
        tmp = p->canvas_u; p->canvas_u = p->next_u; p->next_u = tmp;
        tmp = p->canvas_v; p->canvas_v = p->next_v; p->next_v = tmp;
    }

    p->frame_no++;
}