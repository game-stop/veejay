/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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

/*
 * Edge flow, inspired by Salsaman's Edgeflow Frei0r plugin  (salsaman@gmail.com)
 *
 */

#include "common.h"
#include "edgeglow.h"

typedef struct 
{
    uint8_t *buf;
    uint8_t *blurmask;
    int n_threads;
} edgeglow_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *edgeglow_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;   ve->limits[1][0] = 255; ve->defaults[0] = 15;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255; ve->defaults[1] = 255;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255; ve->defaults[2] = 255;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 255; ve->defaults[3] = 0;
    ve->limits[0][4] = 1;   ve->limits[1][4] = 100; ve->defaults[4] = 20;

    ve->description = "Edge Glow";
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Red", "Green", "Blue", "Scaling Factor");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 2, 112, 76, 100, 15, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 100, 86, 100, 0, 460, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *edgeglow_malloc(int w, int h)
{
    edgeglow_t *s = (edgeglow_t*) vj_calloc(sizeof(edgeglow_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 2);

    if(!s->buf) {
        free(s);
        return NULL;
    }

    s->blurmask = s->buf + len;
    s->n_threads = vje_advise_num_threads(len);

    return s;
}

void edgeglow_free(void *ptr)
{
    edgeglow_t *s = (edgeglow_t*) ptr;

    free(s->buf);
    free(s);
}

void edgeglow_apply(void *ptr, VJFrame *frame, int *args)
{
    edgeglow_t *s = (edgeglow_t*) ptr;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    const int threshold = args[0];
    const int red = args[1];
    const int green = args[2];
    const int blue = args[3];
    const int scale_q8 = args[4] * 26;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict B = s->buf;
    uint8_t *restrict C = s->blurmask;

    int nY = 0;
    int nU = 128;
    int nV = 128;

    _rgb2yuv(red, green, blue, nY, nU, nV);

    const int L2 = (nY * 100) >> 8;
    const int a2 = ((nU - 128) * 127) >> 8;
    const int b2 = ((nV - 128) * 127) >> 8;

    veejay_memset(B, 0, len);
    veejay_memset(C, 0, len);

#pragma omp parallel num_threads(s->n_threads)
    {
#pragma omp for schedule(static)
        for(int y = 1; y < height - 1; y++)
        {
            const int row = y * width;

#pragma omp simd
            for(int x = 1; x < width - 1; x++)
            {
                const int idx = row + x;
                const int gx =
                    Y[idx - width - 1] - Y[idx - width + 1] +
                    ((Y[idx - 1] - Y[idx + 1]) << 1) +
                    Y[idx + width - 1] - Y[idx + width + 1];
                const int gy =
                    Y[idx - width - 1] + (Y[idx - width] << 1) + Y[idx - width + 1] -
                    Y[idx + width - 1] - (Y[idx + width] << 1) - Y[idx + width + 1];
                const int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
                const int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
                const int grad = abs_gx + abs_gy;
                const int norm = (grad * 255) / 1020;
                const int mask = -(norm > threshold);

                B[idx] = (uint8_t)(clampi(grad, 0, 255) & mask);
            }
        }

#pragma omp for schedule(static)
        for(int y = 1; y < height - 1; y++)
        {
            const int row = y * width;

#pragma omp simd
            for(int x = 1; x < width - 1; x++)
            {
                const int idx = row + x;
                const int sum =
                    B[idx - width - 1] + B[idx - width] + B[idx - width + 1] +
                    B[idx - 1]         + B[idx]         + B[idx + 1] +
                    B[idx + width - 1] + B[idx + width] + B[idx + width + 1];

                C[idx] = (uint8_t)((sum * 29) >> 8);
            }
        }

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            int edgeIntensity = (C[i] * scale_q8) >> 8;

            if(edgeIntensity > 255)
                edgeIntensity = 255;

            const int mask = -(edgeIntensity > 0);

            int L1 = (Y[i] * 100) >> 8;
            int a1 = ((Cb[i] - 128) * 127) >> 8;
            int b1 = ((Cr[i] - 128) * 127) >> 8;

            L1 += ((L2 - L1) * edgeIntensity) / 255;
            a1 += ((a2 - a1) * edgeIntensity) / 255;
            b1 += ((b2 - b1) * edgeIntensity) / 255;

            const int L_out = clampi((L1 * 255) / 100, 0, 255);
            const int a_out = clampi(a1 + 128, 0, 255);
            const int b_out = clampi(b1 + 128, 0, 255);

            Y[i] = (uint8_t)((L_out & mask) | (Y[i] & ~mask));
            Cb[i] = (uint8_t)((a_out & mask) | (Cb[i] & ~mask));
            Cr[i] = (uint8_t)((b_out & mask) | (Cr[i] & ~mask));
        }
    }
}
