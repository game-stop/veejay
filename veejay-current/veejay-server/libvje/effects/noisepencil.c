/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "noisepencil.h"

#define NOISEPENCIL_PARAMS 4

#define P_MODE  0
#define P_AMP   1
#define P_MIN_T 2
#define P_MAX_T 3

#define NP_MODE_1X3_NONZERO 0
#define NP_MODE_3X3_NONZERO 1
#define NP_MODE_3X3_INVERT  2
#define NP_MODE_3X3_ADD     3
#define NP_MODE_1X3_ALL     4

typedef struct {
    uint8_t *Yb_frame;
    uint8_t *mask;
    int n_threads;
} noisepencil_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t noisepencil_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t noisepencil_scale_pos(int diff, int coeff, int denom)
{
    if(diff <= 0)
        return 0;

    return noisepencil_u8((diff * coeff + (denom >> 1)) / denom);
}

static inline int noisepencil_in_range(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

vj_effect *noisepencil_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NOISEPENCIL_PARAMS;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_MODE] = NP_MODE_1X3_NONZERO;
    ve->defaults[P_AMP] = 1000;
    ve->defaults[P_MIN_T] = 68;
    ve->defaults[P_MAX_T] = 110;

    ve->limits[0][P_MODE] = 0;  ve->limits[1][P_MODE] = 4;
    ve->limits[0][P_AMP] = 1;   ve->limits[1][P_AMP] = 10000;
    ve->limits[0][P_MIN_T] = 0; ve->limits[1][P_MIN_T] = 255;
    ve->limits[0][P_MAX_T] = 0; ve->limits[1][P_MAX_T] = 255;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Amplification",
        "Min Threshold",
        "Max Threshold"
    );

    ve->description = "Noise Pencil";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "1x3 NonZero",
        "3x3 NonZero",
        "3x3 Invert",
        "3x3 Add",
        "1x3 All"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 320, 3200, 94, 100, 10, 520, 0, 20, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 16, 104, 84, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 82, 1, 0, VJ_BEAT_GROUP_ASCENDING, 16),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 128, 235, 84, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 1, 1, VJ_BEAT_GROUP_ASCENDING, 16)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void noisepencil_free(void *ptr)
{
    noisepencil_t *n = (noisepencil_t*) ptr;

    free(n->Yb_frame);
    free(n);
}

void *noisepencil_malloc(int width, int height)
{
    noisepencil_t *n = (noisepencil_t*) vj_calloc(sizeof(noisepencil_t));

    if(!n)
        return NULL;

    const int len = width * height;

    n->Yb_frame = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 2u);

    if(!n->Yb_frame) {
        free(n);
        return NULL;
    }

    n->mask = n->Yb_frame + len;
    n->n_threads = vje_advise_num_threads(len);

    return (void*) n;
}

static void noisepencil_apply_mode(noisepencil_t *n,
                                   VJFrame *frame,
                                   int mode,
                                   int coeff,
                                   int min_t,
                                   int max_t)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int use_1x3 = mode == NP_MODE_1X3_NONZERO || mode == NP_MODE_1X3_ALL;
    const int thresholded = mode != NP_MODE_1X3_ALL;
    const int invert = mode == NP_MODE_3X3_INVERT || mode == NP_MODE_3X3_ADD || mode == NP_MODE_1X3_ALL;
    const int add_mode = mode == NP_MODE_3X3_ADD;
    const int gated = mode != NP_MODE_3X3_ADD;
    const int denom = mode == NP_MODE_1X3_NONZERO ? 100 : 1000;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = n->Yb_frame;
    uint8_t *restrict M = n->mask;

#pragma omp parallel num_threads(n->n_threads)
    {
        if(use_1x3) {
#pragma omp for schedule(static)
            for(int y = 0; y < height; y++) {
                const int row = y * width;

                for(int x = 0; x < width; x++) {
                    const int xl = x > 0 ? x - 1 : x;
                    const int xr = x < width - 1 ? x + 1 : x;
                    const int idx = row + x;
                    const int avg = (Y[row + xl] + Y[idx] + Y[row + xr]) / 3;

                    B[idx] = (uint8_t)avg;
                    M[idx] = (uint8_t)(!thresholded || noisepencil_in_range(avg, min_t, max_t));
                }
            }
        }
        else {
#pragma omp for schedule(static)
            for(int y = 0; y < height; y++) {
                const int ym = y > 0 ? y - 1 : y;
                const int yp = y < height - 1 ? y + 1 : y;
                const int row = y * width;
                const int up = ym * width;
                const int dn = yp * width;

                for(int x = 0; x < width; x++) {
                    const int xl = x > 0 ? x - 1 : x;
                    const int xr = x < width - 1 ? x + 1 : x;
                    const int idx = row + x;
                    const int avg = (
                        Y[up + xl] + Y[up + x] + Y[up + xr] +
                        Y[row + xl] + Y[idx] + Y[row + xr] +
                        Y[dn + xl] + Y[dn + x] + Y[dn + xr]
                    ) / 9;

                    B[idx] = (uint8_t)avg;
                    M[idx] = (uint8_t)(noisepencil_in_range(avg, min_t, max_t));
                }
            }
        }

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            if(!M[i]) {
                if(gated)
                    Y[i] = 0;
                continue;
            }

            const int diff = invert ? ((int)Y[i] - (int)B[i]) : ((int)B[i] - (int)Y[i]);
            const uint8_t edge = noisepencil_scale_pos(diff, coeff, denom);

            if(add_mode)
                Y[i] = noisepencil_u8((int)Y[i] + (int)edge);
            else
                Y[i] = edge;
        }
    }
}

void noisepencil_apply(void *ptr, VJFrame *frame, int *args)
{
    noisepencil_t *n = (noisepencil_t*) ptr;

    int min_t = args[P_MIN_T];
    int max_t = args[P_MAX_T];

    if(max_t < min_t) {
        const int tmp = min_t;
        min_t = max_t;
        max_t = tmp;
    }

    noisepencil_apply_mode(
        n,
        frame,
        args[P_MODE],
        args[P_AMP],
        min_t,
        max_t
    );
}
