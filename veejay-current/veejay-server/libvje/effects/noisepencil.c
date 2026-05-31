/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "noisepencil.h"

typedef struct {
    uint8_t *Yb_frame;
    uint8_t *mask;
    int n_threads;
} noisepencil_t;

vj_effect *noisepencil_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->defaults[1] = 1000;
    ve->defaults[2] = 68;
    ve->defaults[3] = 110;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 4;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 10000;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

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
        ve->limits[1][0],
        0,
        "1x3 NonZero",
        "3x3 NonZero",
        "3x3 Invert",
        "3x3 Add",
        "1x3 All"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,   0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS,                       250,                4200,               10, 38, 1000, 2600, 0,    60,    /* Amplification */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 32,                 120,                6,  22, 1600, 3400, 700,  35,    /* Min Threshold */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 128,                235,                6,  22, 1600, 3400, 700,  35     /* Max Threshold */
    );

    (void) width;
    (void) height;

    return ve;
}

void *noisepencil_malloc(int width, int height)
{
    noisepencil_t *n = (noisepencil_t*) vj_calloc(sizeof(noisepencil_t));
    if(!n)
        return NULL;

    const int len = width * height;

    n->Yb_frame = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 2);
    if(!n->Yb_frame) {
        free(n);
        return NULL;
    }

    n->mask = n->Yb_frame + len;

    n->n_threads = vje_advise_num_threads(len);
    if(n->n_threads < 1)
        n->n_threads = 1;

    return (void*) n;
}

void noisepencil_free(void *ptr)
{
    noisepencil_t *n = (noisepencil_t*) ptr;
    if(!n)
        return;

    if(n->Yb_frame)
        free(n->Yb_frame);

    free(n);
}

static inline int noisepencil_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t noisepencil_u8(int v)
{
    return (uint8_t) noisepencil_clampi(v, 0, 255);
}

static inline int noisepencil_in_range(int v, int lo, int hi)
{
    return (v >= lo && v <= hi);
}

static inline uint8_t noisepencil_scale_signed(int diff, int coeff, int denom)
{
    int v;

    if(diff <= 0)
        return 0;

    v = (diff * coeff + (denom >> 1)) / denom;

    return noisepencil_u8(v);
}

static void noisepencil_blur_1x3(
    noisepencil_t *n,
    uint8_t *restrict Y,
    int width,
    int height,
    int min_t,
    int max_t,
    int thresholded
) {
    const int len = width * height;
    uint8_t *restrict B = n->Yb_frame;
    uint8_t *restrict M = n->mask;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int xl = x > 0 ? x - 1 : x;
            const int xr = x < width - 1 ? x + 1 : x;
            const int idx = row + x;

            const int tmp = (
                Y[row + xl] +
                Y[idx] +
                Y[row + xr]
            ) / 3;

            B[idx] = (uint8_t) tmp;
            M[idx] = (uint8_t) (!thresholded || noisepencil_in_range(tmp, min_t, max_t));
        }
    }

    (void) len;
}

static void noisepencil_blur_3x3(
    noisepencil_t *n,
    uint8_t *restrict Y,
    int width,
    int height,
    int min_t,
    int max_t,
    int thresholded
) {
    uint8_t *restrict B = n->Yb_frame;
    uint8_t *restrict M = n->mask;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
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

            const int tmp = (
                Y[up  + xl] + Y[up  + x] + Y[up  + xr] +
                Y[row + xl] + Y[row + x] + Y[row + xr] +
                Y[dn  + xl] + Y[dn  + x] + Y[dn  + xr]
            ) / 9;

            B[idx] = (uint8_t) tmp;
            M[idx] = (uint8_t) (!thresholded || noisepencil_in_range(tmp, min_t, max_t));
        }
    }
}

static void noisepencil_apply_residual(
    noisepencil_t *n,
    uint8_t *restrict Y,
    int len,
    int coeff,
    int denom,
    int invert,
    int gated,
    int add_mode
) {
    uint8_t *restrict B = n->Yb_frame;
    uint8_t *restrict M = n->mask;

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        if(!M[i]) {
            if(gated)
                Y[i] = 0;
            continue;
        }

        const int diff = invert ? ((int)Y[i] - (int)B[i]) : ((int)B[i] - (int)Y[i]);
        const uint8_t edge = noisepencil_scale_signed(diff, coeff, denom);

        if(add_mode) {
            Y[i] = noisepencil_u8((int)Y[i] + (int)edge);
        } else {
            Y[i] = edge;
        }
    }
}

void noisepencil_apply(void *ptr, VJFrame *frame, int *args)
{
    noisepencil_t *n = (noisepencil_t*) ptr;
    if(!n || !frame || !args)
        return;

    int type = noisepencil_clampi(args[0], 0, 4);
    int coeff = noisepencil_clampi(args[1], 1, 10000);
    int min_t = noisepencil_clampi(args[2], 0, 255);
    int max_t = noisepencil_clampi(args[3], 0, 255);

    if(max_t < min_t) {
        int tmp = min_t;
        min_t = max_t;
        max_t = tmp;
    }

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    switch(type) {
        case 0:
            noisepencil_blur_1x3(n, Y, width, height, min_t, max_t, 1);
            noisepencil_apply_residual(n, Y, len, coeff, 100, 0, 1, 0);
            break;

        case 1:
            noisepencil_blur_3x3(n, Y, width, height, min_t, max_t, 1);
            noisepencil_apply_residual(n, Y, len, coeff, 1000, 0, 1, 0);
            break;

        case 2:
            noisepencil_blur_3x3(n, Y, width, height, min_t, max_t, 1);
            noisepencil_apply_residual(n, Y, len, coeff, 1000, 1, 1, 0);
            break;

        case 3:
            noisepencil_blur_3x3(n, Y, width, height, min_t, max_t, 1);
            noisepencil_apply_residual(n, Y, len, coeff, 1000, 1, 0, 1);
            break;

        case 4:
            noisepencil_blur_1x3(n, Y, width, height, min_t, max_t, 0);
            noisepencil_apply_residual(n, Y, len, coeff, 1000, 1, 0, 0);
            break;
    }
}