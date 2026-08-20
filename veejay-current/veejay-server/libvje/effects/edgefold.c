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

#define EDGEFOLD_PARAMS 8

#define P_EDGE_GATE    0
#define P_MEMORY       1
#define P_FOLD_FORCE   2
#define P_EXPANSION    3
#define P_DISP_SCALE   4
#define P_MAX_SPEED    5
#define P_CHROMA_SLIP  6
#define P_TURBULENCE   7

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













static inline int32_t edgefold_mirror_fp(float v)
{
    int64_t fp = (int64_t)(v * (float)FP_ONE);
    const int64_t period = (int64_t)FP_ONE << 1;

    fp %= period;

    if(fp < 0)
        fp += period;

    if(fp > FP_ONE)
        fp = period - fp;

    return (int32_t)fp;
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

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

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
        "Turbulence"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 360, 80, 100, 15, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LOG, 480, 980, 68, 96, 100, 1600, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 80, 1000, 90, 100, 0, 380, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_FLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 40, 680, 64, 94, 80, 1100, 0, 1, 0, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 80, 1000, 84, 100, 0, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 80, 1000, 78, 100, 0, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 76, 100, 0, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 0, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *edgefold_malloc(int w, int h)
{
    liquid_fold_t *s;
    int size;

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

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const size_t plane_size = (size_t)len;

    const int edge_gate = edgefold_param1000_to_range(args[P_EDGE_GATE], 0, 255);
    const int memory_i = args[P_MEMORY];
    const int force_i = args[P_FOLD_FORCE];
    const int expansion_i = args[P_EXPANSION];
    const int disp_i = args[P_DISP_SCALE];
    const int max_speed_i = args[P_MAX_SPEED];
    const int chroma_slip_i = args[P_CHROMA_SLIP];
    const int turbulence_i = args[P_TURBULENCE];

    const int memory_q = edgefold_clampi(memory_i, 0, 1000);
    const int force_q = edgefold_clampi(force_i, 0, 1000);
    const int expansion_q = edgefold_clampi(expansion_i, 0, 1000);
    const int disp_q = edgefold_clampi(disp_i, 0, 1000);
    const int max_speed_q = edgefold_clampi(max_speed_i, 0, 1000);
    const int chroma_slip_q = edgefold_clampi(chroma_slip_i, 0, 1000);
    const int turbulence_q = edgefold_clampi(turbulence_i, 0, 1000);

    const float edge_thresh_sq = (float)edge_gate * (float)edge_gate;
    const float momentum = 0.24f + ((float)memory_q * 0.00074f);
    const float fold_force = ((float)force_q * 86.0f) * 0.001f;
    const float expansion = ((float)expansion_q * 0.42f) * 0.001f;
    const float global_scale = 0.75f + ((float)disp_q * 9.0f) * 0.001f;
    const float max_speed = (float)edgefold_param1000_to_range(max_speed_q, 8, 340);
    const float max_speed_sq = max_speed * max_speed;
    const float chroma_slip = ((float)chroma_slip_q * 6.0f) * 0.001f;
    const float turbulence = ((float)turbulence_q * 6.0f) * 0.001f;

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

            float fX = 0.0f;
            float fY = 0.0f;

            if(mag_sq > edge_thresh_sq) {
                fX = (gx * (1.0f / 255.0f)) * fold_force;
                fY = (gy * (1.0f / 255.0f)) * fold_force;
            }

            if(turbulence > 0.0f) {
                const float wave_x = sinf(((float)y * 0.017f) + (float)s->time * 0.037f);
                const float wave_y = cosf(((float)x * 0.019f) - (float)s->time * 0.031f);
                fX += wave_x * turbulence * 0.22f;
                fY += wave_y * turbulence * 0.22f;
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
                    const float base_x = (float)x - (vx * global_scale);
                    const float base_y = (float)y - (vy * global_scale);

                    const float nx = base_x / (float)w;
                    const float ny = base_y / (float)h;

                    const float slip_x = -vy * chroma_slip * 0.035f;
                    const float slip_y =  vx * chroma_slip * 0.035f;

                    const int32_t y_u_fp = edgefold_mirror_fp(nx);
                    const int32_t y_v_fp = edgefold_mirror_fp(ny);

                    const int32_t u_u_fp = edgefold_mirror_fp((base_x + slip_x) / (float)w);
                    const int32_t u_v_fp = edgefold_mirror_fp((base_y + slip_y) / (float)h);

                    const int32_t v_u_fp = edgefold_mirror_fp((base_x - slip_x) / (float)w);
                    const int32_t v_v_fp = edgefold_mirror_fp((base_y - slip_y) / (float)h);

                    const int32_t y_val = sample_bilinear(s->srcY, y_u_fp, y_v_fp, w, h);
                    const int32_t u_val = sample_bilinear_uv(s->srcU, u_u_fp, u_v_fp, w, h) + 128;
                    const int32_t v_val = sample_bilinear_uv(s->srcV, v_u_fp, v_v_fp, w, h) + 128;

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
