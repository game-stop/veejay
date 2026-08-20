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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "noiseadd.h"

#define NOISEADD_PARAMS 2

#define P_MODE 0
#define P_AMP  1

#define NOISEADD_MODE_1X3     0
#define NOISEADD_MODE_3X3     1
#define NOISEADD_MODE_NEG_3X3 2

typedef struct {
    uint8_t *Yb_frame;
    int n_threads;
} noiseadd_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int noiseadd_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t noiseadd_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t noiseadd_absdiff_scaled(int a, int b, int coeff, int denom)
{
    const int d = noiseadd_absi(a - b);
    return noiseadd_u8((d * coeff + (denom >> 1)) / denom);
}

vj_effect *noiseadd_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = NOISEADD_PARAMS;
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

    ve->defaults[P_MODE] = NOISEADD_MODE_1X3;
    ve->defaults[P_AMP] = 1000;

    ve->limits[0][P_MODE] = 0; ve->limits[1][P_MODE] = 2;
    ve->limits[0][P_AMP] = 1;  ve->limits[1][P_AMP] = 5000;

    ve->description = "Amplify low noise";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Amplification");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "1x3 Mask", "3x3 Mask", "3x3 Inverted Mask");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 100, 5000, 92, 100, 0, 460, 0, 10, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void noiseadd_free(void *ptr)
{
    noiseadd_t *n = (noiseadd_t*) ptr;

    free(n->Yb_frame);
    free(n);
}

void *noiseadd_malloc(int width, int height)
{
    noiseadd_t *n = (noiseadd_t*) vj_calloc(sizeof(noiseadd_t));

    if(!n)
        return NULL;

    const int len = width * height;

    n->Yb_frame = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len);

    if(!n->Yb_frame) {
        free(n);
        return NULL;
    }

    n->n_threads = vje_advise_num_threads(len);

    return (void*) n;
}

static void noiseadd_apply_mask(noiseadd_t *n, VJFrame *frame, int mode, int coeff)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int denom = mode == NOISEADD_MODE_1X3 ? 100 : 1000;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = n->Yb_frame;

#pragma omp parallel num_threads(n->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            B[i] = Y[i];

        if(mode == NOISEADD_MODE_1X3) {
#pragma omp for schedule(static)
            for(int y = 0; y < height; y++) {
                const int row = y * width;

                for(int x = 1; x < width - 1; x++) {
                    const int idx = row + x;

                    B[idx] = (uint8_t)((Y[idx - 1] + Y[idx] + Y[idx + 1]) / 3);
                }
            }
        }
        else if(mode == NOISEADD_MODE_3X3) {
#pragma omp for schedule(static)
            for(int y = 1; y < height - 1; y++) {
                const int row = y * width;
                const int up = row - width;
                const int dn = row + width;

                for(int x = 1; x < width - 1; x++) {
                    const int idx = row + x;

                    B[idx] = (uint8_t)(
                        (Y[up + x - 1] + Y[up + x] + Y[up + x + 1] +
                         Y[row + x - 1] + Y[idx] + Y[row + x + 1] +
                         Y[dn + x - 1] + Y[dn + x] + Y[dn + x + 1]) / 9
                    );
                }
            }
        }
        else {
#pragma omp for schedule(static)
            for(int y = 1; y < height - 1; y++) {
                const int row = y * width;
                const int up = row - width;
                const int dn = row + width;

                for(int x = 1; x < width - 1; x++) {
                    const int idx = row + x;

                    B[idx] = (uint8_t)(255 - (
                        (Y[up + x - 1] + Y[up + x] + Y[up + x + 1] +
                         Y[row + x - 1] + Y[idx] + Y[row + x + 1] +
                         Y[dn + x - 1] + Y[dn + x] + Y[dn + x + 1]) / 9
                    ));
                }
            }
        }

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y[i] = noiseadd_absdiff_scaled((int)B[i], (int)Y[i], coeff, denom);
    }
}

void noiseadd_apply(void *ptr, VJFrame *frame, int *args)
{
    noiseadd_t *n = (noiseadd_t*) ptr;

    const int mode = args[P_MODE];
    const int coeff = args[P_AMP];

    noiseadd_apply_mask(n, frame, mode, coeff);
}
