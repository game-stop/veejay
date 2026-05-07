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
#include "plasmafeedback.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
    uint8_t *energy;
    uint8_t *next_energy;
    uint8_t *prev_src_y;
    float *grid_x;
    float *grid_y;
    int grid_capacity;
    uint8_t lut_y[256];
    uint8_t lut_u[256];
    uint8_t lut_v[256];
    uint16_t excite_y_lut[256];
    uint16_t excite_motion_lut[256];
    int w;
    int h;
    int ew;
    int eh;
    int initialized;
    int n_threads;
    int last_phase_i;
    int last_false_i;
    int last_charge_i;
    int last_motion_i;
    uint32_t frame_no;
} plasmafeedback_t;

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

static inline float sample_energy_nearest(const uint8_t *restrict plane, float x, float y, int w, int h)
{
    int ix = (int)(x + 0.5f);
    int iy = (int)(y + 0.5f);

    if (ix < 0) ix = 0;
    else if (ix >= w) ix = w - 1;

    if (iy < 0) iy = 0;
    else if (iy >= h) iy = h - 1;

    return (float)plane[iy * w + ix];
}

static inline int sample_energy_half_to_full(const uint8_t *restrict plane, int x, int y, int ew, int eh)
{
    const int hx = x >> 1;
    const int hy = y >> 1;
    const int hx1 = (hx + 1 < ew) ? hx + 1 : hx;
    const int hy1 = (hy + 1 < eh) ? hy + 1 : hy;
    const int fx = x & 1;
    const int fy = y & 1;

    const int i00 = hy * ew + hx;
    const int i10 = hy * ew + hx1;
    const int i01 = hy1 * ew + hx;
    const int i11 = hy1 * ew + hx1;

    if (!fx && !fy)
        return plane[i00];

    if (fx && !fy)
        return ((int)plane[i00] + (int)plane[i10] + 1) >> 1;

    if (!fx && fy)
        return ((int)plane[i00] + (int)plane[i01] + 1) >> 1;

    return ((int)plane[i00] + (int)plane[i10] + (int)plane[i01] + (int)plane[i11] + 2) >> 2;
}

static int plasmafeedback_grid_required(int w, int h, int cell)
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

static int plasmafeedback_ensure_grid(plasmafeedback_t *p, int w, int h, int cell)
{
    if (!p)
        return 0;

    const int required = plasmafeedback_grid_required(w, h, cell);

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

static void build_flow_grid(plasmafeedback_t *p, int w, int h, int cell)
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

static inline void yuv_from_rgb(float r, float g, float b, float *y, float *u, float *v)
{
    *y = 0.299f * r + 0.587f * g + 0.114f * b;
    *u = 128.0f - 0.168736f * r - 0.331264f * g + 0.500000f * b;
    *v = 128.0f + 0.500000f * r - 0.418688f * g - 0.081312f * b;
}

static inline void rgb_lerp(float ar, float ag, float ab, float br, float bg, float bb, float t, float *r, float *g, float *b)
{
    *r = ar + (br - ar) * t;
    *g = ag + (bg - ag) * t;
    *b = ab + (bb - ab) * t;
}

static inline void plasma_palette(float e, float phase, float false_color, float *r, float *g, float *b)
{
    const float p = false_color;
    const float pi2 = 6.28318530717958647692f;
    const float wave = phase + e * 3.5f;
    const float s0 = sinf(wave * pi2);
    const float s1 = sinf((wave + 0.333333f) * pi2);
    const float s2 = sinf((wave + 0.666666f) * pi2);

    if (p < 0.25f) {
        const float t = p * 4.0f;
        const float a0r = 4.0f + e * 70.0f;
        const float a0g = 12.0f + e * 120.0f;
        const float a0b = 45.0f + e * 230.0f;
        const float a1r = 18.0f + e * 150.0f + s0 * 25.0f;
        const float a1g = 20.0f + e * 210.0f + s1 * 35.0f;
        const float a1b = 80.0f + e * 220.0f + s2 * 30.0f;
        rgb_lerp(a0r, a0g, a0b, a1r, a1g, a1b, t, r, g, b);
    } else if (p < 0.50f) {
        const float t = (p - 0.25f) * 4.0f;
        const float a0r = 18.0f + e * 150.0f + s0 * 25.0f;
        const float a0g = 20.0f + e * 210.0f + s1 * 35.0f;
        const float a0b = 80.0f + e * 220.0f + s2 * 30.0f;
        const float a1r = 70.0f + e * 230.0f;
        const float a1g = 14.0f + e * 120.0f;
        const float a1b = 4.0f + e * 30.0f;
        rgb_lerp(a0r, a0g, a0b, a1r, a1g, a1b, t, r, g, b);
    } else if (p < 0.75f) {
        const float t = (p - 0.50f) * 4.0f;
        const float a0r = 70.0f + e * 230.0f;
        const float a0g = 14.0f + e * 120.0f;
        const float a0b = 4.0f + e * 30.0f;
        const float a1r = 5.0f + e * 70.0f;
        const float a1g = 45.0f + e * 230.0f;
        const float a1b = 20.0f + e * 120.0f;
        rgb_lerp(a0r, a0g, a0b, a1r, a1g, a1b, t, r, g, b);
    } else {
        const float t = (p - 0.75f) * 4.0f;
        const float a0r = 5.0f + e * 70.0f;
        const float a0g = 45.0f + e * 230.0f;
        const float a0b = 20.0f + e * 120.0f;
        const float a1r = 128.0f + s0 * 120.0f;
        const float a1g = 128.0f + s1 * 120.0f;
        const float a1b = 128.0f + s2 * 120.0f;
        rgb_lerp(a0r, a0g, a0b, a1r, a1g, a1b, t, r, g, b);
    }

    const float core = e * e * e;
    *r = *r + (255.0f - *r) * core * 0.80f;
    *g = *g + (255.0f - *g) * core * 0.80f;
    *b = *b + (255.0f - *b) * core * 0.80f;
}

static void build_plasma_lut(plasmafeedback_t *p, float phase, float false_color)
{
    for (int i = 0; i < 256; i++) {
        const float e = (float)i * (1.0f / 255.0f);
        float r, g, b;
        float y, u, v;

        plasma_palette(e, phase, false_color, &r, &g, &b);
        yuv_from_rgb(r, g, b, &y, &u, &v);

        p->lut_y[i] = clamp_u8f(y);
        p->lut_u[i] = clamp_u8f(u);
        p->lut_v[i] = clamp_u8f(v);
    }
}

static void build_excitation_luts(plasmafeedback_t *p, float charge_gain, float brightness_gain, float motion_gain)
{
    for (int i = 0; i < 256; i++) {
        const float v = (float)i * (1.0f / 255.0f);
        const float b = v * v * brightness_gain * charge_gain;
        const float m = v * motion_gain * charge_gain;

        p->excite_y_lut[i] = (uint16_t)clamp_u8f(b);
        p->excite_motion_lut[i] = (uint16_t)clamp_u8f(m);
    }
}

vj_effect *plasmafeedback_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    ve->num_params = 11;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 100; ve->defaults[0] = 78;
    ve->limits[0][1] = 0; ve->limits[1][1] = 100; ve->defaults[1] = 86;
    ve->limits[0][2] = 0; ve->limits[1][2] = 100; ve->defaults[2] = 35;
    ve->limits[0][3] = 0; ve->limits[1][3] = 100; ve->defaults[3] = 62;
    ve->limits[0][4] = 0; ve->limits[1][4] = 100; ve->defaults[4] = 78;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100; ve->defaults[5] = 82;
    ve->limits[0][6] = 0; ve->limits[1][6] = 100; ve->defaults[6] = 55;
    ve->limits[0][7] = 0; ve->limits[1][7] = 100; ve->defaults[7] = 20;
    ve->limits[0][8] = 0; ve->limits[1][8] = 100; ve->defaults[8] = 45;
    ve->limits[0][9] = 0; ve->limits[1][9] = 100; ve->defaults[9] = 95;
    ve->limits[0][10] = 0; ve->limits[1][10] = 100; ve->defaults[10] = 35;

    ve->description = "Plasma Feedback";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Charge",
        "Decay",
        "Discharge",
        "Flow",
        "Filaments",
        "Turbulence",
        "Glow",
        "Source Feed",
        "Color Phase",
        "Motion React",
        "False Color"
    );

    return ve;
}

void plasmafeedback_free(void *ptr)
{
    plasmafeedback_t *p = (plasmafeedback_t *)ptr;

    if (!p)
        return;

    if (p->energy) free(p->energy);
    if (p->next_energy) free(p->next_energy);
    if (p->prev_src_y) free(p->prev_src_y);
    if (p->grid_x) free(p->grid_x);
    if (p->grid_y) free(p->grid_y);

    free(p);
}

void *plasmafeedback_malloc(int w, int h)
{
    if (w <= 2 || h <= 2)
        return NULL;

    const size_t full_len = (size_t)w * (size_t)h;

    if (full_len == 0 || full_len > (size_t)INT_MAX)
        return NULL;

    const int ew = (w + 1) >> 1;
    const int eh = (h + 1) >> 1;
    const size_t energy_len = (size_t)ew * (size_t)eh;

    if (energy_len == 0 || energy_len > (size_t)INT_MAX)
        return NULL;

    plasmafeedback_t *p = (plasmafeedback_t *)vj_calloc(sizeof(plasmafeedback_t));

    if (!p)
        return NULL;

    const int min_cell = 4;
    const int capacity = plasmafeedback_grid_required(ew, eh, min_cell);

    if (capacity <= 0) {
        free(p);
        return NULL;
    }

    p->w = w;
    p->h = h;
    p->ew = ew;
    p->eh = eh;
    p->grid_capacity = capacity;

    p->energy = (uint8_t *)vj_malloc(energy_len);
    p->next_energy = (uint8_t *)vj_malloc(energy_len);
    p->prev_src_y = (uint8_t *)vj_malloc(full_len);
    p->grid_x = (float *)vj_malloc(sizeof(float) * (size_t)p->grid_capacity);
    p->grid_y = (float *)vj_malloc(sizeof(float) * (size_t)p->grid_capacity);

    if (!p->energy || !p->next_energy || !p->prev_src_y || !p->grid_x || !p->grid_y) {
        plasmafeedback_free(p);
        return NULL;
    }

    veejay_memset(p->energy, 0, energy_len);
    veejay_memset(p->next_energy, 0, energy_len);
    veejay_memset(p->prev_src_y, 0, full_len);
    veejay_memset(p->grid_x, 0, sizeof(float) * (size_t)p->grid_capacity);
    veejay_memset(p->grid_y, 0, sizeof(float) * (size_t)p->grid_capacity);

    p->initialized = 0;
    p->frame_no = 0;
    p->n_threads = vje_advise_num_threads((int)full_len);

    if (p->n_threads <= 0)
        p->n_threads = 1;

    p->last_phase_i = -1;
    p->last_false_i = -1;
    p->last_charge_i = -1;
    p->last_motion_i = -1;

    build_plasma_lut(p, 0.0f, 0.35f);
    build_excitation_luts(p, 1.0f, 1.0f, 1.0f);

    return (void *)p;
}

void plasmafeedback_apply(void *ptr, VJFrame *frame, int *args)
{
    plasmafeedback_t *p = (plasmafeedback_t *)ptr;

    if (!p || !frame || !args)
        return;

    if (!frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;

    if (w <= 2 || h <= 2)
        return;

    const size_t full_len_sz = (size_t)w * (size_t)h;

    if (full_len_sz == 0 || full_len_sz > (size_t)INT_MAX)
        return;

    const int full_len = (int)full_len_sz;

    if (p->w != w || p->h != h)
        return;

    const int ew = p->ew;
    const int eh = p->eh;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    if (frame->uv_len > 0 && frame->uv_len < full_len)
        return;

    if (!p->initialized) {
        veejay_memcpy(p->prev_src_y, Y, full_len_sz);
        veejay_memset(p->energy, 0, (size_t)ew * (size_t)eh);
        veejay_memset(p->next_energy, 0, (size_t)ew * (size_t)eh);
        p->initialized = 1;
        p->frame_no = 0;
    }

    const float charge_param = clampf_local((float)args[0] * 0.01f, 0.0f, 1.0f);
    const float decay_param = clampf_local((float)args[1] * 0.01f, 0.0f, 1.0f);
    const float discharge_param = clampf_local((float)args[2] * 0.01f, 0.0f, 1.0f);
    const float flow_param = clampf_local((float)args[3] * 0.01f, 0.0f, 1.0f);
    const float filament_param = clampf_local((float)args[4] * 0.01f, 0.0f, 1.0f);
    const float turbulence_param = clampf_local((float)args[5] * 0.01f, 0.0f, 1.0f);
    const float glow_param = clampf_local((float)args[6] * 0.01f, 0.0f, 1.0f);
    const float source_param = clampf_local((float)args[7] * 0.01f, 0.0f, 1.0f);
    const float color_phase_param = clampf_local((float)args[8] * 0.01f, 0.0f, 1.0f);
    const float motion_param = clampf_local((float)args[9] * 0.01f, 0.0f, 1.0f);
    const float false_color_param = clampf_local((float)args[10] * 0.01f, 0.0f, 1.0f);

    const float charge_curve = charge_param * charge_param;
    const float decay_curve = decay_param * decay_param;
    const float discharge_curve = discharge_param * discharge_param;
    const float flow_curve = flow_param * flow_param;
    const float filament_curve = filament_param * filament_param;
    const float turbulence_curve = turbulence_param * turbulence_param;
    const float glow_curve = glow_param * glow_param;
    const float source_curve = source_param * source_param;
    const float motion_curve = motion_param * motion_param;

    int cell = 22 - (int)(turbulence_curve * 15.0f);
    if (cell < 4) cell = 4;
    if (cell > 22) cell = 22;

    if (!plasmafeedback_ensure_grid(p, ew, eh, cell))
        return;

    float lut[23];

    for (int i = 0; i <= cell; i++)
        lut[i] = smoothstepf_local((float)i / (float)cell);

    build_flow_grid(p, ew, eh, cell);

    const int gw = (ew + cell - 1) / cell + 2;
    const int gh = (eh + cell - 1) / cell + 2;

    if (gw <= 1 || gh <= 1)
        return;

    if (plasmafeedback_grid_required(ew, eh, cell) > p->grid_capacity)
        return;

    float color_phase = (float)p->frame_no * (0.0015f + color_phase_param * 0.018f);
    color_phase -= floorf(color_phase);

    const int phase_i = (int)(color_phase * 1023.0f);
    const int false_i = args[10];

    if (phase_i != p->last_phase_i || false_i != p->last_false_i) {
        build_plasma_lut(p, color_phase, false_color_param);
        p->last_phase_i = phase_i;
        p->last_false_i = false_i;
    }

    const float charge_gain = 28.0f + charge_curve * 230.0f;
    const float decay_keep = 0.780f + decay_curve * 0.214f;
    const float discharge = discharge_curve * 0.018f;
    const float flow_pixels = (0.15f + flow_curve * 13.0f) * (0.65f + decay_curve * 0.65f);
    const float filament_gain = 1.0f + filament_curve * 5.5f;
    const float pressure_gain = 2.20f + flow_curve * 3.80f;
    const float turbulence_gain = turbulence_curve * 2.5f;
    const float arc_gain = 0.50f + turbulence_curve * 2.50f;
    const float motion_gain = 0.20f + motion_curve * 2.80f;
    const float brightness_gain = 0.08f + charge_curve * 0.52f;

    const int charge_i = args[0];
    const int motion_i = args[9];

    if (charge_i != p->last_charge_i || motion_i != p->last_motion_i) {
        build_excitation_luts(p, charge_gain, brightness_gain, motion_gain);
        p->last_charge_i = charge_i;
        p->last_motion_i = motion_i;
    }

    const uint8_t *restrict old_e = p->energy;
    uint8_t *restrict next_e = p->next_energy;
    const uint8_t *restrict prev_y = p->prev_src_y;

    #pragma omp parallel for collapse(2) schedule(static) num_threads(p->n_threads)
    for (int gy = 0; gy < gh - 1; gy++) {
        for (int gx = 0; gx < gw - 1; gx++) {
            const int y0 = gy * cell;
            const int y1 = y0 + cell;
            const int ye = (y1 < eh) ? y1 : eh;
            const int x0 = gx * cell;
            const int x1 = x0 + cell;
            const int xe = (x1 < ew) ? x1 : ew;

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

            for (int ey = y0; ey < ye; ey++) {
                const int ly = ey - y0;
                const float fy = lut[ly];
                const float ax = lerpf_local(vx00, vx01, fy);
                const float bx = lerpf_local(vx10, vx11, fy);
                const float ay = lerpf_local(vy00, vy01, fy);
                const float by = lerpf_local(vy10, vy11, fy);
                const int erow = ey * ew;
                const int eym = (ey > 0) ? ey - 1 : ey;
                const int eyp = (ey < eh - 1) ? ey + 1 : ey;

                const int sy0 = ey << 1;
                const int sy1 = (sy0 + 1 < h) ? sy0 + 1 : sy0;
                const int srow0 = sy0 * w;
                const int srow1 = sy1 * w;

                for (int ex = x0; ex < xe; ex++) {
                    const int eidx = erow + ex;
                    const int lx = ex - x0;
                    const float fx = lut[lx];

                    const int exm = (ex > 0) ? ex - 1 : ex;
                    const int exp = (ex < ew - 1) ? ex + 1 : ex;

                    const int idx_l = erow + exm;
                    const int idx_r = erow + exp;
                    const int idx_u = eym * ew + ex;
                    const int idx_d = eyp * ew + ex;

                    const float e_l = (float)old_e[idx_l] * (1.0f / 255.0f);
                    const float e_r = (float)old_e[idx_r] * (1.0f / 255.0f);
                    const float e_u = (float)old_e[idx_u] * (1.0f / 255.0f);
                    const float e_d = (float)old_e[idx_d] * (1.0f / 255.0f);
                    const float e_c = (float)old_e[eidx] * (1.0f / 255.0f);

                    const float egx = e_r - e_l;
                    const float egy = e_d - e_u;
                    const float lap = (e_l + e_r + e_u + e_d) - e_c * 4.0f;

                    const float noise_x = lerpf_local(ax, bx, fx);
                    const float noise_y = lerpf_local(ay, by, fx);

                    float vx = egx * pressure_gain + (-egy) * filament_gain + noise_x * turbulence_gain * arc_gain + lap * noise_x * 2.25f;
                    float vy = egy * pressure_gain + ( egx) * filament_gain + noise_y * turbulence_gain * arc_gain + lap * noise_y * 2.25f;

                    const float hot_drive = 0.35f + e_c * 1.65f;
                    vx *= hot_drive;
                    vy *= hot_drive;

                    const float mag2 = vx * vx + vy * vy;

                    if (mag2 > 6.25f) {
                        const float s = 1.0f / (1.0f + (mag2 - 6.25f) * 0.080f);
                        vx *= s;
                        vy *= s;
                    }

                    const float sx = (float)ex - vx * flow_pixels;
                    const float sy = (float)ey - vy * flow_pixels;
                    const float adv = sample_energy_nearest(old_e, sx, sy, ew, eh);

                    const int sx0 = ex << 1;
                    const int sx1 = (sx0 + 1 < w) ? sx0 + 1 : sx0;

                    const int si00 = srow0 + sx0;
                    const int si10 = srow0 + sx1;
                    const int si01 = srow1 + sx0;
                    const int si11 = srow1 + sx1;

                    const int src_y_avg = ((int)Y[si00] + (int)Y[si10] + (int)Y[si01] + (int)Y[si11] + 2) >> 2;

                    int dm0 = (int)Y[si00] - (int)prev_y[si00]; if (dm0 < 0) dm0 = -dm0;
                    int dm1 = (int)Y[si10] - (int)prev_y[si10]; if (dm1 < 0) dm1 = -dm1;
                    int dm2 = (int)Y[si01] - (int)prev_y[si01]; if (dm2 < 0) dm2 = -dm2;
                    int dm3 = (int)Y[si11] - (int)prev_y[si11]; if (dm3 < 0) dm3 = -dm3;

                    const int motion_avg = (dm0 + dm1 + dm2 + dm3 + 2) >> 2;

                    float e = adv * decay_keep + (float)p->excite_y_lut[src_y_avg] + (float)p->excite_motion_lut[motion_avg];

                    if (discharge > 0.0f) {
                        const float hot = clampf_local((e - 168.0f) * (1.0f / 87.0f), 0.0f, 1.0f);
                        e -= hot * hot * discharge * e * 255.0f;
                    }

                    next_e[eidx] = clamp_u8f(e);
                }
            }
        }
    }

    const uint8_t *restrict plasma = p->next_energy;
    uint8_t *restrict prev_y_write = p->prev_src_y;
    const float glow_amount = glow_curve * 0.85f;
    const float source_feed = source_curve * 0.78f;
    const float plasma_mix = 1.0f - source_feed;
    const float motion_render_gain = motion_curve * 42.0f;

    #pragma omp parallel for schedule(static) num_threads(p->n_threads)
    for (int y = 0; y < h; y++) {
        const int row = y * w;

        for (int x = 0; x < w; x++) {
            const int idx = row + x;
            const int hx = x >> 1;
            const int hy = y >> 1;
            const int hxm = (hx > 0) ? hx - 1 : hx;
            const int hxp = (hx < ew - 1) ? hx + 1 : hx;
            const int hym = (hy > 0) ? hy - 1 : hy;
            const int hyp = (hy < eh - 1) ? hy + 1 : hy;

            const int e0 = sample_energy_half_to_full(plasma, x, y, ew, eh);
            const int hrow = hy * ew;
            const int glow = (e0 * 4 + plasma[hrow + hxm] + plasma[hrow + hxp] + plasma[hym * ew + hx] + plasma[hyp * ew + hx]) >> 3;

            const uint8_t src_y = Y[idx];
            const uint8_t src_u = U[idx];
            const uint8_t src_v = V[idx];

            int dm = (int)src_y - (int)prev_y_write[idx];
            if (dm < 0) dm = -dm;

            int ei = e0 + (int)((float)glow * glow_amount) + (int)((float)dm * motion_render_gain * (1.0f / 255.0f));

            if (ei > 255)
                ei = 255;

            const uint8_t py = p->lut_y[ei];
            const uint8_t pu = p->lut_u[ei];
            const uint8_t pv = p->lut_v[ei];

            Y[idx] = clamp_u8f((float)py * plasma_mix + (float)src_y * source_feed);
            U[idx] = clamp_u8f((float)pu * plasma_mix + (float)src_u * source_feed);
            V[idx] = clamp_u8f((float)pv * plasma_mix + (float)src_v * source_feed);

            prev_y_write[idx] = src_y;
        }
    }

    {
        uint8_t *tmp = p->energy;
        p->energy = p->next_energy;
        p->next_energy = tmp;
    }

    p->frame_no++;
}