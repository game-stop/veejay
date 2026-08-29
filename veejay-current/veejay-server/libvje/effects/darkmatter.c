/* 
 * veejay  
 *
 * Copyright (C) 2000-2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include "common.h"
#include "darkmatter.h"

#define DARKMATTER_PARAMS 10
#define DM_MAX_HALOS      12
#define DM_MAX_FILAMENTS  4
#define DM_SUBS_PER_HALO  2
#define DM_MAX_SUBHALOS   (DM_MAX_HALOS * DM_SUBS_PER_HALO)
#define DM_GRID_SHIFT     3
#define DM_GRID_STEP      (1 << DM_GRID_SHIFT)
#define DM_GRID_MASK      (DM_GRID_STEP - 1)
#define DM_TWO_PI         6.28318530718f

#define P_MASS            0
#define P_HALOS           1
#define P_SCALE           2
#define P_SHEAR           3
#define P_FILAMENT        4
#define P_SUBSTRUCTURE    5
#define P_TIMESCALE       6
#define P_DYNAMICS        7
#define P_COUPLING        8
#define P_CRITICALITY     9

typedef struct {
    int w;
    int h;
    int len;
    int gw;
    int gh;
    int grid_len;
    int n_threads;
    int frame;

    void *region;

    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;

    float *grid_x;
    float *grid_y;
    float *grid_gain;

    float halo_x[DM_MAX_HALOS];
    float halo_y[DM_MAX_HALOS];
    float halo_vx[DM_MAX_HALOS];
    float halo_vy[DM_MAX_HALOS];
    float halo_weight[DM_MAX_HALOS];
    float halo_soft[DM_MAX_HALOS];
    float sub_phase[DM_MAX_HALOS];

    float time;
    float couple_x;
    float couple_y;
} darkmatter_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float dm_clampf(float v, float lo, float hi)
{
    if (v != v) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float dm_smooth01(float t)
{
    t = dm_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline size_t dm_align_size(size_t off, size_t align)
{
    return (off + align - 1) & ~(align - 1);
}

static inline float dm_time_step(int speed)
{
    if (speed == 0)
        return 0.0f;

    float u = dm_clampf(fabsf((float) speed) * 0.001f, 0.0f, 1.0f);
    float mag = 0.000050f * (exp2f(12.0f * u) - 1.0f);
    return speed < 0 ? -mag : mag;
}

static inline uint32_t dm_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float dm_rand01(uint32_t x)
{
    return (float) (dm_hash32(x) & 0x00ffffffU) * 5.9604644775390625e-8f; // 1.0f / 16777215.0f
}

static inline void dm_sample_bilinear_yuv(
    const uint8_t *restrict Y,
    const uint8_t *restrict U,
    const uint8_t *restrict V,
    float fx,
    float fy,
    int w,
    int h,
    uint8_t *oy,
    uint8_t *ou,
    uint8_t *ov
) {
    fx = dm_clampf(fx, 0.0f, (float) (w - 1));
    fy = dm_clampf(fy, 0.0f, (float) (h - 1));

    int x0 = (int) fx;
    int y0 = (int) fy;
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    int wx = (int) ((fx - (float) x0) * 256.0f);
    int wy = (int) ((fy - (float) y0) * 256.0f);
    int iw = 256 - wx;

    int p00 = y0 * w + x0;
    int p10 = y0 * w + x1;
    int p01 = y1 * w + x0;
    int p11 = y1 * w + x1;

    int a = Y[p00] * iw + Y[p10] * wx;
    int b = Y[p01] * iw + Y[p11] * wx;
    *oy = (uint8_t) (((a * (256 - wy) + b * wy) + 32768) >> 16);

    if (U && V) {
        a = U[p00] * iw + U[p10] * wx;
        b = U[p01] * iw + U[p11] * wx;
        *ou = (uint8_t) (((a * (256 - wy) + b * wy) + 32768) >> 16);

        a = V[p00] * iw + V[p10] * wx;
        b = V[p01] * iw + V[p11] * wx;
        *ov = (uint8_t) (((a * (256 - wy) + b * wy) + 32768) >> 16);
    }
}

static void dm_update_halos(
    darkmatter_t *t,
    int halos,
    float dt,
    float dynamics,
    float mass_t,
    float scale_t,
    float coupling
) {
    float ax[DM_MAX_HALOS];
    float ay[DM_MAX_HALOS];
    float half_min = 0.5f * (float) (t->w < t->h ? t->w : t->h);
    float x_bound = ((float) t->w * 0.5f) / half_min * 0.92f;
    float y_bound = ((float) t->h * 0.5f) / half_min * 0.92f;
    float dyn2 = dynamics * dynamics;
    float g = (0.010f + 0.122f * dyn2) * (0.35f + 0.65f * mass_t);
    float soft = (0.024f + 0.085f * scale_t) * (1.0f - 0.52f * dynamics) + 0.010f;
    float soft2 = soft * soft;
    float adt = fabsf(dt);
    int substeps = 1 + (adt > 0.020f) + (adt > 0.060f) + (adt > 0.120f);
    float step = dt * (0.12f + 0.88f * dynamics) / (float) substeps;

    if (dynamics <= 0.0001f || dt == 0.0f)
        return;

    for (int sub = 0; sub < substeps; sub++) {
        for (int i = 0; i < halos; i++) {
            float px = t->halo_x[i];
            float py = t->halo_y[i];
            float confinement = 0.022f - 0.016f * dynamics;
            float aix = -px * confinement;
            float aiy = -py * confinement;

            for (int j = 0; j < halos; j++) {
                if (j == i) continue;

                float dx = t->halo_x[j] - px;
                float dy = t->halo_y[j] - py;
                float r2 = dx * dx + dy * dy + soft2;
                float invr = 1.0f / sqrtf(r2);
                float invr3 = invr * invr * invr;
                float q = g * t->halo_weight[j] * invr3;
                aix += dx * q;
                aiy += dy * q;
            }

            if (coupling > 0.0001f) {
                float q = coupling * (0.006f + 0.024f * dynamics);
                aix += (t->couple_x - px) * q;
                aiy += (t->couple_y - py) * q;
            }

            ax[i] = aix;
            ay[i] = aiy;
        }

        for (int i = 0; i < halos; i++) {
            float damping = 1.0f - 0.0015f * fabsf(step);

            t->halo_vx[i] = (t->halo_vx[i] + ax[i] * step) * damping;
            t->halo_vy[i] = (t->halo_vy[i] + ay[i] * step) * damping;
            t->halo_x[i] += t->halo_vx[i] * step;
            t->halo_y[i] += t->halo_vy[i] * step;

            if (t->halo_x[i] < -x_bound) {
                t->halo_x[i] = -x_bound;
                t->halo_vx[i] = fabsf(t->halo_vx[i]) * 0.82f;
            } else if (t->halo_x[i] > x_bound) {
                t->halo_x[i] = x_bound;
                t->halo_vx[i] = -fabsf(t->halo_vx[i]) * 0.82f;
            }

            if (t->halo_y[i] < -y_bound) {
                t->halo_y[i] = -y_bound;
                t->halo_vy[i] = fabsf(t->halo_vy[i]) * 0.82f;
            } else if (t->halo_y[i] > y_bound) {
                t->halo_y[i] = y_bound;
                t->halo_vy[i] = -fabsf(t->halo_vy[i]) * 0.82f;
            }
        }
    }
}

static void dm_update_coupling(darkmatter_t *t, const uint8_t *src_y, int coupling)
{
    long long sx = 0;
    long long sy = 0;
    long long sw = 0;
    const int step = 24;
    const float cx = (float) t->w * 0.5f;
    const float cy = (float) t->h * 0.5f;
    const float half_min = 0.5f * (float) (t->w < t->h ? t->w : t->h);

    if (coupling <= 0) {
        t->couple_x *= 0.97f;
        t->couple_y *= 0.97f;
        return;
    }

    for (int y = 0; y < t->h; y += step) {
        int row = y * t->w;
        for (int x = 0; x < t->w; x += step) {
            int v = src_y[row + x];
            int q = v - 24;

            if (q > 0) {
                int weight = q * q;
                sx += (long long) x * (long long) weight;
                sy += (long long) y * (long long) weight;
                sw += weight;
            }
        }
    }

    if (sw > 0) {
        float tx = (((float) sx / (float) sw) - cx) / half_min;
        float ty = (((float) sy / (float) sw) - cy) / half_min;
        float k = 0.018f + 0.030f * ((float) coupling * 0.01f);

        t->couple_x += (tx - t->couple_x) * k;
        t->couple_y += (ty - t->couple_y) * k;
    }
}

vj_effect *darkmatter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    (void) w;
    (void) h;

    if (!ve) return NULL;

    ve->num_params = DARKMATTER_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->description = "Dark Matter (Gravitational Lensing)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->defaults[P_MASS]         = 58;
    ve->defaults[P_HALOS]        = 6;
    ve->defaults[P_SCALE]        = 46;
    ve->defaults[P_SHEAR]        = 22;
    ve->defaults[P_FILAMENT]     = 38;
    ve->defaults[P_SUBSTRUCTURE] = 24;
    ve->defaults[P_TIMESCALE]    = 360;
    ve->defaults[P_DYNAMICS]     = 42;
    ve->defaults[P_COUPLING]     = 0;
    ve->defaults[P_CRITICALITY]   = 64;

    ve->limits[0][P_MASS]         = 0;
    ve->limits[1][P_MASS]         = 100;
    ve->limits[0][P_HALOS]        = 1;
    ve->limits[1][P_HALOS]        = DM_MAX_HALOS;
    ve->limits[0][P_SCALE]        = 1;
    ve->limits[1][P_SCALE]        = 100;
    ve->limits[0][P_SHEAR]        = -100;
    ve->limits[1][P_SHEAR]        = 100;
    ve->limits[0][P_FILAMENT]     = 0;
    ve->limits[1][P_FILAMENT]     = 100;
    ve->limits[0][P_SUBSTRUCTURE] = 0;
    ve->limits[1][P_SUBSTRUCTURE] = 100;
    ve->limits[0][P_TIMESCALE]    = -1000;
    ve->limits[1][P_TIMESCALE]    = 1000;
    ve->limits[0][P_DYNAMICS]     = 0;
    ve->limits[1][P_DYNAMICS]     = 100;
    ve->limits[0][P_COUPLING]     = 0;
    ve->limits[1][P_COUPLING]     = 100;
    ve->limits[0][P_CRITICALITY]   = 0;
    ve->limits[1][P_CRITICALITY]   = 100;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Dark Mass",
        "Halo Count",
        "Halo Scale",
        "Tidal Shear",
        "Filament Mass",
        "Substructure",
        "Time Scale",
        "Halo Dynamics",
        "Matter Coupling",
        "Criticality"
    );

    return ve;
}

void *darkmatter_malloc(int w, int h)
{
    darkmatter_t *t = (darkmatter_t *) vj_calloc(sizeof(darkmatter_t));
    if (!t) return NULL;

    size_t len = (size_t) w * (size_t) h;
    size_t bytes = len;
    size_t off = 0;

    t->w = w;
    t->h = h;
    t->len = (int) len;
    t->gw = ((w + DM_GRID_STEP - 1) >> DM_GRID_SHIFT) + 1;
    t->gh = ((h + DM_GRID_STEP - 1) >> DM_GRID_SHIFT) + 1;
    t->grid_len = t->gw * t->gh;
    t->n_threads = vje_advise_num_threads(w * h);

    size_t total = bytes * 3 + 64 + sizeof(float) * (size_t) t->grid_len * 3;
    t->region = vj_malloc(total);
    if (!t->region) {
        free(t);
        return NULL;
    }

    unsigned char *base = (unsigned char *) t->region;

    t->src_y = base + off; off += bytes;
    t->src_u = base + off; off += bytes;
    t->src_v = base + off; off += bytes;

    off = dm_align_size(off, sizeof(float));
    t->grid_x = (float *) (base + off); off += sizeof(float) * (size_t) t->grid_len;
    t->grid_y = (float *) (base + off); off += sizeof(float) * (size_t) t->grid_len;
    t->grid_gain = (float *) (base + off);

    float half_min = 0.5f * (float) (w < h ? w : h);
    float x_extent = ((float) w * 0.5f) / half_min;

    for (int i = 0; i < DM_MAX_HALOS; i++) {
        uint32_t s = 0x9e3779b9U * (uint32_t) (i + 1);
        float rx = dm_rand01(s ^ 0x13a5ba1dU);
        float ry = dm_rand01(s ^ 0x6c8e9cf5U);
        float rv = dm_rand01(s ^ 0xa511e9b3U);
        float rw = dm_rand01(s ^ 0x1b56c4e9U);
        float rs = dm_rand01(s ^ 0xe4bb3f21U);
        float sign = (dm_rand01(s ^ 0x91e10da5U) < 0.5f) ? -1.0f : 1.0f;

        t->halo_x[i] = (rx * 2.0f - 1.0f) * x_extent * 0.66f;
        t->halo_y[i] = (ry * 2.0f - 1.0f) * 0.66f;
        float px = t->halo_x[i];
        float py = t->halo_y[i];
        float rinv = 1.0f / sqrtf(px * px + py * py + 0.035f);
        float v = sign * (0.035f + 0.080f * rv);
        
        t->halo_vx[i] = -py * rinv * v;
        t->halo_vy[i] = px * rinv * v;
        t->halo_weight[i] = 0.68f + rw * 0.68f;
        t->halo_soft[i] = 0.78f + rs * 0.48f;
        t->sub_phase[i] = dm_rand01(s ^ 0xd34c71b7U) * DM_TWO_PI;
    }

    t->time = 0.0f;
    t->couple_x = 0.0f;
    t->couple_y = 0.0f;

    return t;
}

void darkmatter_free(void *ptr)
{
    darkmatter_t *t = (darkmatter_t *) ptr;
    if (!t) return;
    if (t->region) free(t->region);
    free(t);
}

void darkmatter_apply(void *ptr, VJFrame *frame, int *args)
{
    darkmatter_t *t = (darkmatter_t *) ptr;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict src_y = t->src_y;
    uint8_t *restrict src_u = t->src_u;
    uint8_t *restrict src_v = t->src_v;
    float *restrict grid_x = t->grid_x;
    float *restrict grid_y = t->grid_y;
    float *restrict grid_gain = t->grid_gain;

    const int w = t->w;
    const int h = t->h;
    const int len = t->len;
    
    const int halos = clampi(args[P_HALOS], 1, DM_MAX_HALOS);
    const int coupling_i = args[P_COUPLING];

    const float half_min = 0.5f * (float) (w < h ? w : h);
    const float cx = (float) w * 0.5f;
    const float cy = (float) h * 0.5f;
    const float inv_half_min = 1.0f / half_min;

    const float mass_t = (float) args[P_MASS] * 0.01f;
    const float scale_t = (float) args[P_SCALE] * 0.01f;
    const float shear_t = (float) args[P_SHEAR] * 0.01f;
    const float filament_t = (float) args[P_FILAMENT] * 0.01f;
    const float sub_t = (float) args[P_SUBSTRUCTURE] * 0.01f;
    const float dynamics_t = (float) args[P_DYNAMICS] * 0.01f;
    const float coupling_t = (float) coupling_i * 0.01f;
    const float critical_t = dm_clampf((float) args[P_CRITICALITY] * 0.01f, 0.0f, 1.0f);

    int do_processing = (args[P_MASS] > 0 && args[P_CRITICALITY] > 0);

    #pragma omp single
    {
        veejay_memcpy(src_y, Y, len);
        veejay_memcpy(src_u, U, len);
        veejay_memcpy(src_v, V, len);

        if (do_processing) {
            if ((t->frame & 7) == 0)
                dm_update_coupling(t, src_y, coupling_i);

            float speed_step = dm_time_step(args[P_TIMESCALE]);
            dm_update_halos(t, halos, speed_step, dynamics_t, mass_t, scale_t, coupling_t);
            t->time += speed_step;
            if (t->time > DM_TWO_PI || t->time < -DM_TWO_PI)
                t->time = fmodf(t->time, DM_TWO_PI);
        }
        t->frame++;
    }

    if (!do_processing)
        return;

    float hx[DM_MAX_HALOS];
    float hy[DM_MAX_HALOS];
    float hm[DM_MAX_HALOS];
    float hs2[DM_MAX_HALOS];
    float ho2[DM_MAX_HALOS];
    float subx[DM_MAX_SUBHALOS];
    float suby[DM_MAX_SUBHALOS];
    float subm[DM_MAX_SUBHALOS];
    float subs2[DM_MAX_SUBHALOS];

    float count_norm = 1.0f / sqrtf(1.0f + 0.08f * (float) (halos - 1));
    float scale_gain = 0.70f + 1.65f * scale_t * scale_t;
    float critical_gain = critical_t * critical_t;
    critical_gain *= 0.35f + 4.65f * critical_gain;
    float total_mass = (0.0040f + 0.0400f * dm_smooth01(mass_t)) * count_norm * scale_gain * critical_gain;
    float base_soft = 0.030f + 0.260f * dm_smooth01(scale_t);

    float coupling_shift_x = t->couple_x * coupling_t * 0.08f;
    float coupling_shift_y = t->couple_y * coupling_t * 0.08f;

    for (int gi = 0; gi < halos; gi++) {
        float sr = base_soft * (0.72f + 0.18f * t->halo_soft[gi]);

        hx[gi] = t->halo_x[gi] + coupling_shift_x;
        hy[gi] = t->halo_y[gi] + coupling_shift_y;
        hm[gi] = total_mass * t->halo_weight[gi];
        hs2[gi] = sr * sr;
        ho2[gi] = (0.85f + 1.80f * scale_t);
        ho2[gi] *= ho2[gi];

        for (int si = 0; si < DM_SUBS_PER_HALO; si++) {
            int sj = gi * DM_SUBS_PER_HALO + si;
            float sign = si == 0 ? -1.0f : 1.0f;
            float phase = t->sub_phase[gi] + sign * t->time * (1.25f + 0.17f * (float) gi + 0.38f * (float) si) + (float) si * 2.39996323f;
            float radius = sr * (0.38f + 0.46f * sub_t + 0.34f * (float) si);
            float compact = 0.016f + 0.040f * (1.0f - sub_t) + 0.012f * (float) si;

            subx[sj] = hx[gi] + cosf(phase) * radius;
            suby[sj] = hy[gi] + sinf(phase) * radius;
            subm[sj] = hm[gi] * sub_t * sub_t * (si == 0 ? 0.135f : 0.085f);
            subs2[sj] = hs2[gi] * compact;
        }
    }

    float filament_mass = total_mass * filament_t * (0.34f + 0.42f * filament_t);
    float filament_soft2 = base_soft * (0.30f + 0.30f * filament_t);
    filament_soft2 *= filament_soft2;
    float filament_outer2 = 0.32f + 1.10f * scale_t;
    filament_outer2 *= filament_outer2;
    
    int filament_links = halos > 1 ? halos - 1 : 0;
    if (filament_links > DM_MAX_HALOS)
        filament_links = DM_MAX_HALOS;

    float fil_x0[DM_MAX_HALOS];
    float fil_y0[DM_MAX_HALOS];
    float fil_vx[DM_MAX_HALOS];
    float fil_vy[DM_MAX_HALOS];
    float fil_inv_l2[DM_MAX_HALOS];
    float fil_nx[DM_MAX_HALOS];
    float fil_ny[DM_MAX_HALOS];
    float fil_bend[DM_MAX_HALOS];

    for (int gi = 0; gi < filament_links; gi++) {
        int a = (gi * halos) / (filament_links + 1);
        int b = (a + 1 + halos / 2) % halos;
        if (b == a)
            b = (a + 1) % halos;

        fil_x0[gi] = hx[a];
        fil_y0[gi] = hy[a];
        float vx = hx[b] - hx[a];
        float vy = hy[b] - hy[a];
        float l2 = vx * vx + vy * vy + 1.0e-6f;
        float invl = 1.0f / sqrtf(l2);
        fil_vx[gi] = vx;
        fil_vy[gi] = vy;
        fil_inv_l2[gi] = 1.0f / l2;
        fil_nx[gi] = -vy * invl;
        fil_ny[gi] = vx * invl;
        float wave = sinf(t->time * (0.21f + 0.07f * (float) gi) + (float) gi * 1.731f);
        fil_bend[gi] = wave * (0.025f + 0.165f * filament_t * (0.25f + 0.75f * dynamics_t));
    }

    float shear_angle = t->time * 0.23f + (shear_t < 0.0f ? 1.570796327f : 0.0f);
    float s2a = sinf(shear_angle * 2.0f);
    float c2a = cosf(shear_angle * 2.0f);
    float shear_gamma = dm_clampf(fabsf(shear_t), 0.0f, 1.0f) * (0.030f + 0.145f * critical_t * critical_t);
    float gamma1 = shear_gamma * c2a;
    float gamma2 = shear_gamma * s2a;

    float max_disp = half_min * (0.025f + 1.050f * critical_t * critical_t);
    float max_disp2 = max_disp * max_disp;

    #pragma omp for schedule(static)
    for (int gi = 0; gi < t->grid_len; gi++) {
        int gy = gi / t->gw;
        int gx = gi - gy * t->gw;
        float px = (float) (gx << DM_GRID_SHIFT);
        float py = (float) (gy << DM_GRID_SHIFT);
        float nx = (px - cx) * inv_half_min;
        float ny = (py - cy) * inv_half_min;
        float ax = gamma1 * nx + gamma2 * ny;
        float ay = gamma2 * nx - gamma1 * ny;

        for (int k = 0; k < halos; k++) {
            float dx = nx - hx[k];
            float dy = ny - hy[k];
            float r2 = dx * dx + dy * dy;
            float invr = 1.0f / sqrtf(r2 + hs2[k]);
            float outer = 1.0f / (1.0f + r2 / ho2[k]);
            float q = hm[k] * invr * outer;

            ax += q * dx;
            ay += q * dy;

            if (sub_t > 0.0001f) {
                for (int si = 0; si < DM_SUBS_PER_HALO; si++) {
                    int sj = k * DM_SUBS_PER_HALO + si;
                    float sdx = nx - subx[sj];
                    float sdy = ny - suby[sj];
                    float sr2 = sdx * sdx + sdy * sdy;
                    float sinvr = 1.0f / sqrtf(sr2 + subs2[sj]);
                    float sq = subm[sj] * sinvr / (1.0f + sr2 / (0.18f + 0.32f * scale_t));

                    ax += sq * sdx;
                    ay += sq * sdy;
                }
            }
        }

        if (filament_mass > 0.0000001f) {
            for (int k = 0; k < filament_links; k++) {
                float u = ((nx - fil_x0[k]) * fil_vx[k] + (ny - fil_y0[k]) * fil_vy[k]) * fil_inv_l2[k];
                u = dm_clampf(u, 0.0f, 1.0f);
                float taper = 4.0f * u * (1.0f - u);
                float curve = fil_bend[k] * taper;
                float qx = fil_x0[k] + fil_vx[k] * u + fil_nx[k] * curve;
                float qy = fil_y0[k] + fil_vy[k] * u + fil_ny[k] * curve;
                float dx = nx - qx;
                float dy = ny - qy;
                float d2 = dx * dx + dy * dy;
                float invr = 1.0f / sqrtf(d2 + filament_soft2);
                float outer = 1.0f / (1.0f + d2 / filament_outer2);
                float fq = filament_mass * taper * invr * outer;

                ax += fq * dx;
                ay += fq * dy;
            }
        }

        grid_x[gi] = -ax * half_min;
        grid_y[gi] = -ay * half_min;
    }

    #pragma omp for schedule(static)
    for (int gi = 0; gi < t->grid_len; gi++) {
        int gy = gi / t->gw;
        int gx = gi - gy * t->gw;
        float gain = 1.0f;

        if (critical_t > 0.32f && gx > 0 && gx + 1 < t->gw && gy > 0 && gy + 1 < t->gh) {
            float inv2s = 0.5f / (float) DM_GRID_STEP;
            float dxx = (grid_x[gi + 1] - grid_x[gi - 1]) * inv2s;
            float dxy = (grid_x[gi + t->gw] - grid_x[gi - t->gw]) * inv2s;
            float dyx = (grid_y[gi + 1] - grid_y[gi - 1]) * inv2s;
            float dyy = (grid_y[gi + t->gw] - grid_y[gi - t->gw]) * inv2s;
            float det = (1.0f + dxx) * (1.0f + dyy) - dxy * dyx;
            float window = 0.14f + 0.46f * critical_t;
            float near = 1.0f - dm_clampf(fabsf(det) / window, 0.0f, 1.0f);

            near = dm_smooth01(near);
            gain += near * near * (0.18f + 2.85f * critical_t * critical_t);
            if (det < 0.0f)
                gain += near * critical_t * 0.65f;
        }

        grid_gain[gi] = gain;
    }

    #pragma omp for schedule(static)
    for (int gi = 0; gi < t->grid_len; gi++) {
        float ax = grid_x[gi] * grid_gain[gi];
        float ay = grid_y[gi] * grid_gain[gi];
        float d2 = ax * ax + ay * ay;

        if (d2 > max_disp2) {
            float q = max_disp / sqrtf(d2);
            ax *= q;
            ay *= q;
        }

        grid_x[gi] = ax;
        grid_y[gi] = ay;
    }

    #pragma omp for schedule(static)
    for (int y = 0; y < h; y++) {
        int gy = y >> DM_GRID_SHIFT;
        float fy = (float) (y & DM_GRID_MASK) * (1.0f / (float) DM_GRID_STEP);
        int g0 = gy * t->gw;
        int g1 = g0 + t->gw;

        for (int x = 0; x < w; x++) {
            int gx = x >> DM_GRID_SHIFT;
            float fx = (float) (x & DM_GRID_MASK) * (1.0f / (float) DM_GRID_STEP);
            int i00 = g0 + gx;
            int i10 = i00 + 1;
            int i01 = g1 + gx;
            int i11 = i01 + 1;
            float dx0 = grid_x[i00] + (grid_x[i10] - grid_x[i00]) * fx;
            float dx1 = grid_x[i01] + (grid_x[i11] - grid_x[i01]) * fx;
            float dy0 = grid_y[i00] + (grid_y[i10] - grid_y[i00]) * fx;
            float dy1 = grid_y[i01] + (grid_y[i11] - grid_y[i01]) * fx;
            float sx = (float) x + dx0 + (dx1 - dx0) * fy;
            float sy = (float) y + dy0 + (dy1 - dy0) * fy;
            int i = y * w + x;
            uint8_t oy, ou, ov;

            dm_sample_bilinear_yuv(src_y, src_u, src_v, sx, sy, w, h, &oy, &ou, &ov);

            Y[i] = oy;
            if (U && V) {
                U[i] = ou;
                V[i] = ov;
            }
        }
    }
}