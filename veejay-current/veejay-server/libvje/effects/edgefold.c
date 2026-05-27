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
#include "edgefold.h"

#define FP_SHIFT 16
#define FP_ONE (1 << FP_SHIFT)

#define EDGEFOLD_PARAMS 9

#define P_EDGE_GATE    0
#define P_MEMORY       1
#define P_FOLD_FORCE   2
#define P_EXPANSION    3
#define P_DISP_SCALE   4
#define P_MAX_SPEED    5
#define P_CHROMA_SLIP  6
#define P_TURBULENCE   7
#define P_BEAT_PUSH    8

typedef struct {
    uint8_t *srcY, *srcU, *srcV;
    uint8_t *histY;

    float *vec_region;
    float *vecX, *vecY;
    float *nextX, *nextY;

    double time;
    int n_threads;
    int w;
    int h;
} liquid_fold_t;

static inline int edgefold_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t edgefold_u8(int v)
{
    return (uint8_t)edgefold_clampi(v, 0, 255);
}

static inline int edgefold_param1000_to_range(int v, int lo, int hi)
{
    v = edgefold_clampi(v, 0, 1000);
    return lo + (((hi - lo) * v + 500) / 1000);
}

static inline int edgefold_range_to_param1000(int v, int lo, int hi)
{
    v = edgefold_clampi(v, lo, hi);
    if(hi <= lo)
        return 0;
    return ((v - lo) * 1000 + ((hi - lo) >> 1)) / (hi - lo);
}

static inline int edgefold_percent_to_param1000(int v)
{
    v = edgefold_clampi(v, 0, 100);
    return (v * 1000 + 50) / 100;
}

static inline int edgefold_beat_shape_q8(int beat_push)
{
    int linear;
    int squared;
    int q;

    beat_push = edgefold_clampi(beat_push, 0, 1000);
    linear = (beat_push * 255 + 500) / 1000;
    squared = (linear * linear + 127) / 255;
    q = (linear * 104 + squared * 151 + 127) / 255;

    return edgefold_clampi(q, 0, 255);
}

static inline int edgefold_mix_i(int a, int b, int q8)
{
    q8 = edgefold_clampi(q8, 0, 255);
    return a + (((b - a) * q8 + (b >= a ? 127 : -127)) / 255);
}

static inline int edgefold_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint32_t edgefold_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t edgefold_hash3(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 374761393U;
    h ^= (uint32_t)y * 668265263U;
    h ^= (uint32_t)z * 2246822519U;
    return edgefold_hash_u32(h);
}

static inline float edgefold_hash_signed(uint32_t x)
{
    return ((float)(edgefold_hash_u32(x) & 0xffffU) * (1.0f / 65535.0f)) * 2.0f - 1.0f;
}

static inline int32_t edgefold_mirror_fp(float v)
{
    int32_t fp = (int32_t)(v * (float)FP_ONE);

    if(fp < 0)
        fp = -fp;

    if(fp > FP_ONE)
        fp = FP_ONE - (fp % FP_ONE);

    return fp;
}

vj_effect *edgefold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = EDGEFOLD_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_EDGE_GATE] = 0;
    ve->limits[1][P_EDGE_GATE] = 1000;
    ve->defaults[P_EDGE_GATE] = edgefold_range_to_param1000(30, 0, 255);

    ve->limits[0][P_MEMORY] = 0;
    ve->limits[1][P_MEMORY] = 1000;
    ve->defaults[P_MEMORY] = edgefold_percent_to_param1000(85);

    ve->limits[0][P_FOLD_FORCE] = 0;
    ve->limits[1][P_FOLD_FORCE] = 1000;
    ve->defaults[P_FOLD_FORCE] = edgefold_range_to_param1000(50, 0, 200);

    ve->limits[0][P_EXPANSION] = 0;
    ve->limits[1][P_EXPANSION] = 1000;
    ve->defaults[P_EXPANSION] = edgefold_percent_to_param1000(20);

    ve->limits[0][P_DISP_SCALE] = 0;
    ve->limits[1][P_DISP_SCALE] = 1000;
    ve->defaults[P_DISP_SCALE] = edgefold_range_to_param1000(100, 0, 400);

    ve->limits[0][P_MAX_SPEED] = 0;
    ve->limits[1][P_MAX_SPEED] = 1000;
    ve->defaults[P_MAX_SPEED] = edgefold_range_to_param1000(60, 10, 220);

    ve->limits[0][P_CHROMA_SLIP] = 0;
    ve->limits[1][P_CHROMA_SLIP] = 1000;
    ve->defaults[P_CHROMA_SLIP] = 160;

    ve->limits[0][P_TURBULENCE] = 0;
    ve->limits[1][P_TURBULENCE] = 1000;
    ve->defaults[P_TURBULENCE] = 80;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->description = "Liquid Edge Fold";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Edge Gate",
        "Flow Memory",
        "Fold Force",
        "Expansion",
        "Displacement",
        "Max Speed",
        "Chroma Slip",
        "Turbulence",
        "Beat Push"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,             VJ_BEAT_F_PHRASE_ONLY,                    60,                 420,                6,  22, 1800, 4200, 900,  -28,   /* Edge Gate */
        VJ_BEAT_INERTIA,            VJ_BEAT_F_PHRASE_ONLY,                    460,                960,                6,  24, 1800, 4200, 900,  34,    /* Flow Memory */
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS,                     80,                 860,                12, 48, 900,  2400, 0,    78,    /* Fold Force */
        VJ_BEAT_FLOW,               VJ_BEAT_F_CONTINUOUS,                     0,                  660,                10, 36, 1000, 2800, 0,    55,    /* Expansion */
        VJ_BEAT_WARP,               VJ_BEAT_F_CONTINUOUS,                     110,                920,                12, 48, 900,  2400, 0,    82,    /* Displacement */
        VJ_BEAT_SPEED,              VJ_BEAT_F_CONTINUOUS,                     90,                 850,                10, 38, 1000, 2800, 0,    58,    /* Max Speed */
        VJ_BEAT_COLOR_AMOUNT,       VJ_BEAT_F_CONTINUOUS,                     0,                  720,                8,  30, 1200, 3000, 0,    44,    /* Chroma Slip */
        VJ_BEAT_TURBULENCE,         VJ_BEAT_F_CLIMAX_ONLY,                    0,                  700,                6,  28, 1400, 3600, 600,  36,    /* Turbulence */
        VJ_BEAT_INTENSITY,          VJ_BEAT_F_CONTINUOUS,                     0,                  900,                22, 90, 60,   620,  0,    100    /* Beat Push */
    );

    (void)w;
    (void)h;

    return ve;
}

void *edgefold_malloc(int w, int h)
{
    liquid_fold_t *s;
    int size;

    if(w <= 0 || h <= 0)
        return NULL;

    s = (liquid_fold_t*) vj_calloc(sizeof(liquid_fold_t));
    if(!s)
        return NULL;

    size = w * h;

    s->srcY = (uint8_t*) vj_malloc((size_t)size * 4u);
    if(!s->srcY) {
        free(s);
        return NULL;
    }

    s->srcU = s->srcY + size;
    s->srcV = s->srcU + size;
    s->histY = s->srcV + size;

    s->vec_region = (float*) vj_calloc(sizeof(float) * (size_t)size * 4u);
    if(!s->vec_region) {
        free(s->srcY);
        free(s);
        return NULL;
    }

    s->vecX = s->vec_region;
    s->vecY = s->vecX + size;
    s->nextX = s->vecY + size;
    s->nextY = s->nextX + size;

    s->time = 0.0;
    s->w = w;
    s->h = h;

    s->n_threads = vje_advise_num_threads(size);
    if(s->n_threads <= 0)
        s->n_threads = 1;

    return (void*) s;
}

void edgefold_free(void *ptr)
{
    liquid_fold_t *s = (liquid_fold_t*) ptr;

    if(!s)
        return;

    if(s->srcY)
        free(s->srcY);

    if(s->vec_region)
        free(s->vec_region);

    free(s);
}

static inline int32_t sample_bilinear(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h)
{
    int32_t u = u_fp & (FP_ONE - 1);
    int32_t v = v_fp & (FP_ONE - 1);
    int32_t xf = (int64_t)u * (w - 1);
    int32_t yf = (int64_t)v * (h - 1);
    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;
    int32_t fx = xf & (FP_ONE - 1);
    int32_t fy = yf & (FP_ONE - 1);
    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;

    int64_t sum = (int64_t)(FP_ONE - fx) * (FP_ONE - fy) * buf[y * w + x] +
                  (int64_t)fx * (FP_ONE - fy) * buf[y * w + x1] +
                  (int64_t)(FP_ONE - fx) * fy * buf[y1 * w + x] +
                  (int64_t)fx * fy * buf[y1 * w + x1];

    return (int32_t)(sum >> (FP_SHIFT * 2));
}

static inline int32_t sample_bilinear_uv(const uint8_t *buf, int32_t u_fp, int32_t v_fp, int w, int h)
{
    int32_t u = u_fp & (FP_ONE - 1);
    int32_t v = v_fp & (FP_ONE - 1);
    int32_t xf = (int64_t)u * (w - 1);
    int32_t yf = (int64_t)v * (h - 1);
    int x = xf >> FP_SHIFT;
    int y = yf >> FP_SHIFT;
    int32_t fx = xf & (FP_ONE - 1);
    int32_t fy = yf & (FP_ONE - 1);
    int x1 = (x + 1 >= w) ? 0 : x + 1;
    int y1 = (y + 1 >= h) ? 0 : y + 1;

    int64_t sum = (int64_t)(FP_ONE - fx) * (FP_ONE - fy) * (buf[y * w + x] - 128) +
                  (int64_t)fx * (FP_ONE - fy) * (buf[y * w + x1] - 128) +
                  (int64_t)(FP_ONE - fx) * fy * (buf[y1 * w + x] - 128) +
                  (int64_t)fx * fy * (buf[y1 * w + x1] - 128);

    return (int32_t)(sum >> (FP_SHIFT * 2));
}

void edgefold_apply(void *ptr, VJFrame *frame, int *args)
{
    liquid_fold_t *s = (liquid_fold_t *)ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const size_t plane_size = (size_t)w * (size_t)h;

    if(w <= 2 || h <= 2 || len <= 0)
        return;

    if(w != s->w || h != s->h)
        return;

    int edge_gate = edgefold_param1000_to_range(args[P_EDGE_GATE], 0, 255);
    int memory_i = edgefold_clampi(args[P_MEMORY], 0, 1000);
    int force_i = edgefold_clampi(args[P_FOLD_FORCE], 0, 1000);
    int expansion_i = edgefold_clampi(args[P_EXPANSION], 0, 1000);
    int disp_i = edgefold_clampi(args[P_DISP_SCALE], 0, 1000);
    int max_speed_i = edgefold_clampi(args[P_MAX_SPEED], 0, 1000);
    int chroma_slip_i = edgefold_clampi(args[P_CHROMA_SLIP], 0, 1000);
    int turbulence_i = edgefold_clampi(args[P_TURBULENCE], 0, 1000);
    int beat_push = edgefold_clampi(args[P_BEAT_PUSH], 0, 1000);
    int beat_q8 = edgefold_beat_shape_q8(beat_push);

    if(beat_q8 > 0) {
        edge_gate = edgefold_mix_i(edge_gate, 10, (beat_q8 * 95 + 127) / 255);
        force_i = edgefold_mix_i(force_i, 900, (beat_q8 * 150 + 127) / 255);
        expansion_i = edgefold_mix_i(expansion_i, 520, (beat_q8 * 74 + 127) / 255);
        disp_i = edgefold_mix_i(disp_i, 820, (beat_q8 * 110 + 127) / 255);
        max_speed_i = edgefold_mix_i(max_speed_i, 860, (beat_q8 * 128 + 127) / 255);
        chroma_slip_i = edgefold_mix_i(chroma_slip_i, 760, (beat_q8 * 92 + 127) / 255);
        turbulence_i = edgefold_mix_i(turbulence_i, 680, (beat_q8 * 100 + 127) / 255);
    }

    const float edge_thresh_sq = (float)edge_gate * (float)edge_gate;
    const float momentum = 0.24f + ((float)memory_i * 0.00074f);
    const float fold_force = ((float)force_i * 86.0f) * 0.001f;
    const float expansion = ((float)expansion_i * 0.42f) * 0.001f;
    const float global_scale = 0.75f + ((float)disp_i * 9.0f) * 0.001f;
    const float max_speed = (float)edgefold_param1000_to_range(max_speed_i, 8, 340);
    const float max_speed_sq = max_speed * max_speed;
    const float chroma_slip = ((float)chroma_slip_i * 6.0f) * 0.001f;
    const float turbulence = ((float)turbulence_i * 6.0f) * 0.001f;
    const float beat_t = (float)beat_q8 * (1.0f / 255.0f);
    const float half_w = (float)w * 0.5f;
    const float half_h = (float)h * 0.5f;
    const float inv_max_dim = 1.0f / (((float)(w > h ? w : h) * 0.5f) + 1.0f);
    const float beat_shock_pixels = beat_t * (1.8f + (float)disp_i * 0.011f + (float)force_i * 0.006f);
    const float beat_swirl_pixels = beat_t * (0.8f + turbulence * 1.75f);
    const int beat_seed_keep = edgefold_clampi(14 + ((beat_q8 * (72 + turbulence_i / 12) + 127) / 255), 0, 190);
    const int beat_edge_floor = edgefold_clampi(edge_gate - ((edge_gate * beat_q8 + 255) / 510), 4, 180);
    const int beat_salt = (int)s->time;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    float *restrict VX = s->vecX;
    float *restrict VY = s->vecY;
    float *restrict NX = s->nextX;
    float *restrict NY = s->nextY;

    veejay_memcpy(s->srcY, srcY, plane_size);
    veejay_memcpy(s->srcU, srcU, plane_size);
    veejay_memcpy(s->srcV, srcV, plane_size);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < h - 1; y++) {
        for(int x = 1; x < w - 1; x++) {
            const int idx = y * w + x;

            const int gx_i =
                -1 * s->srcY[(y - 1) * w + (x - 1)] + 1 * s->srcY[(y - 1) * w + (x + 1)] +
                -2 * s->srcY[(y    ) * w + (x - 1)] + 2 * s->srcY[(y    ) * w + (x + 1)] +
                -1 * s->srcY[(y + 1) * w + (x - 1)] + 1 * s->srcY[(y + 1) * w + (x + 1)];

            const int gy_i =
                -1 * s->srcY[(y - 1) * w + (x - 1)] - 2 * s->srcY[(y - 1) * w + x] - 1 * s->srcY[(y - 1) * w + (x + 1)] +
                 1 * s->srcY[(y + 1) * w + (x - 1)] + 2 * s->srcY[(y + 1) * w + x] + 1 * s->srcY[(y + 1) * w + (x + 1)];

            const float gx = (float)gx_i;
            const float gy = (float)gy_i;
            const float mag_sq = gx * gx + gy * gy;
            const int edge_abs = edgefold_absi(gx_i) + edgefold_absi(gy_i);

            float fX = 0.0f;
            float fY = 0.0f;

            if(mag_sq > edge_thresh_sq) {
                fX = (gx * (1.0f / 255.0f)) * fold_force;
                fY = (gy * (1.0f / 255.0f)) * fold_force;
            }

            if(beat_q8 > 0) {
                const int cx = x >> 3;
                const int cy = y >> 3;
                const uint32_t hash = edgefold_hash3(cx, cy, beat_salt);
                const int accepted = (edge_abs >= beat_edge_floor && (int)((hash >> 8) & 255) <= beat_seed_keep);

                if(accepted) {
                    const float sign = (hash & 1U) ? 1.0f : -1.0f;
                    const float edge_t = edge_abs > 510 ? 1.0f : (float)edge_abs * (1.0f / 510.0f);
                    const float seed_force = fold_force * (0.62f + beat_t * 2.10f) * (0.55f + edge_t * 2.20f);

                    fX += (-gy * (1.0f / 255.0f)) * seed_force * sign;
                    fY += ( gx * (1.0f / 255.0f)) * seed_force * sign;
                }

                if(turbulence > 0.0f) {
                    const uint32_t h0 = edgefold_hash3(x >> 4, y >> 4, beat_salt ^ 0x51f15eU);
                    const float ng = turbulence * beat_t * (0.22f + ((float)edge_abs * (1.0f / 1020.0f)));
                    fX += edgefold_hash_signed(h0) * ng;
                    fY += edgefold_hash_signed(h0 ^ 0x9e3779b9U) * ng;
                }
            }

            {
                float vx = VX[idx];
                float vy = VY[idx];

                const float avgX = (VX[idx - 1] + VX[idx + 1] + VX[idx - w] + VX[idx + w]) * 0.25f;
                const float avgY = (VY[idx - 1] + VY[idx + 1] + VY[idx - w] + VY[idx + w]) * 0.25f;

                vx = vx * (1.0f - expansion) + avgX * expansion;
                vy = vy * (1.0f - expansion) + avgY * expansion;

                vx = (vx + fX) * momentum;
                vy = (vy + fY) * momentum;

                {
                    const float speed_sq = vx * vx + vy * vy;

                    if(speed_sq > max_speed_sq) {
                        const float inv_speed = max_speed / sqrtf(speed_sq);
                        vx *= inv_speed;
                        vy *= inv_speed;
                    }
                }

                NX[idx] = vx;
                NY[idx] = vy;

                {
                    float beat_dx = 0.0f;
                    float beat_dy = 0.0f;

                    if(beat_q8 > 0) {
                        const float cxn = ((float)x - half_w) * inv_max_dim;
                        const float cyn = ((float)y - half_h) * inv_max_dim;
                        const float r2 = cxn * cxn + cyn * cyn;
                        const float radial = 1.0f / (0.22f + r2 * 3.20f);
                        const uint32_t bh = edgefold_hash3(x >> 5, y >> 5, beat_salt ^ 0x7a35e9U);
                        const float bn0 = edgefold_hash_signed(bh);
                        const float bn1 = edgefold_hash_signed(bh ^ 0x9e3779b9U);
                        const float edge_t = edge_abs > 510 ? 1.0f : (float)edge_abs * (1.0f / 510.0f);
                        const float shock = beat_shock_pixels * radial * (0.22f + edge_t * 1.55f);
                        const float swirl = beat_swirl_pixels * radial * (0.35f + edge_t * 0.85f);
                        const float noise = turbulence * beat_t * (0.45f + edge_t * 0.90f);

                        beat_dx = cxn * shock - cyn * swirl + bn0 * noise;
                        beat_dy = cyn * shock + cxn * swirl + bn1 * noise;
                    }

                    const float base_x = (float)x - (vx * global_scale) - beat_dx;
                    const float base_y = (float)y - (vy * global_scale) - beat_dy;

                    const float nx = base_x / (float)w;
                    const float ny = base_y / (float)h;

                    const float slip_x = (-vy * chroma_slip * 0.035f) - beat_dy * chroma_slip * 0.08f;
                    const float slip_y = ( vx * chroma_slip * 0.035f) + beat_dx * chroma_slip * 0.08f;

                    const int32_t y_u_fp = edgefold_mirror_fp(nx);
                    const int32_t y_v_fp = edgefold_mirror_fp(ny);

                    const int32_t u_u_fp = edgefold_mirror_fp((base_x + slip_x) / (float)w);
                    const int32_t u_v_fp = edgefold_mirror_fp((base_y + slip_y) / (float)h);

                    const int32_t v_u_fp = edgefold_mirror_fp((base_x - slip_x) / (float)w);
                    const int32_t v_v_fp = edgefold_mirror_fp((base_y - slip_y) / (float)h);

                    int32_t y_val;
                    int32_t u_val;
                    int32_t v_val;

                    y_val = sample_bilinear(s->srcY, y_u_fp, y_v_fp, w, h);

                    if(beat_q8 > 0) {
                        const int crease = (edge_abs * beat_q8 + 4096) >> 13;
                        if(((x ^ y ^ beat_salt) & 1) != 0)
                            y_val += crease;
                        else
                            y_val -= crease >> 1;
                    }

                    u_val = sample_bilinear_uv(s->srcU, u_u_fp, u_v_fp, w, h) + 128;
                    v_val = sample_bilinear_uv(s->srcV, v_u_fp, v_v_fp, w, h) + 128;

                    frame->data[0][idx] = edgefold_u8(y_val);
                    frame->data[1][idx] = edgefold_u8(u_val);
                    frame->data[2][idx] = edgefold_u8(v_val);
                }
            }
        }
    }

    {
        float *tmp;
        tmp = s->vecX; s->vecX = s->nextX; s->nextX = tmp;
        tmp = s->vecY; s->vecY = s->nextY; s->nextY = tmp;
    }

    s->time += 1.0;
}
