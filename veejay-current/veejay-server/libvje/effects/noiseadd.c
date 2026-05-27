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
#include "noiseadd.h"

typedef struct {
    uint8_t *Yb_frame;
    int n_threads;
} noiseadd_t;

vj_effect *noiseadd_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->defaults[1] = 1000;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 5000;

    ve->description = "Amplify low noise";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Amplification"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "1x3 Mask",
        "3x3 Mask",
        "3x3 Inverted Mask"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS,                    150,                2600,               10, 38, 1000, 2600, 0,   60     /* Amplification */
    );

    (void) width;
    (void) height;

    return ve;
}

void *noiseadd_malloc(int width, int height)
{
    noiseadd_t *n = (noiseadd_t*) vj_calloc(sizeof(noiseadd_t));
    if(!n)
        return NULL;

    const int len = width * height;

    n->Yb_frame = (uint8_t*) vj_malloc(sizeof(uint8_t) * len);
    if(!n->Yb_frame) {
        free(n);
        return NULL;
    }

    n->n_threads = vje_advise_num_threads(len);
    if(n->n_threads < 1)
        n->n_threads = 1;

    return (void*) n;
}

void noiseadd_free(void *ptr)
{
    noiseadd_t *n = (noiseadd_t*) ptr;
    if(!n)
        return;

    if(n->Yb_frame)
        free(n->Yb_frame);

    free(n);
}

static inline uint8_t noiseadd_clamp_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint8_t noiseadd_absdiff_scaled(int a, int b, int coeff, int denom)
{
    int d = a - b;
    d = (d < 0) ? -d : d;
    d = (d * coeff + (denom >> 1)) / denom;
    return noiseadd_clamp_u8(d);
}

static void noiseblur1x3_maskapply(noiseadd_t *n, VJFrame *frame, int coeff)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = n->Yb_frame;

    if(width < 3 || height < 1)
        return;

    veejay_memcpy(B, Y, len);

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++) {
            const int idx = row + x;
            B[idx] = (uint8_t)((Y[idx - 1] + Y[idx] + Y[idx + 1]) / 3);
        }
    }

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        Y[i] = noiseadd_absdiff_scaled((int)B[i], (int)Y[i], coeff, 100);
    }
}

static void noiseblur3x3_maskapply(noiseadd_t *n, VJFrame *frame, int coeff)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = n->Yb_frame;

    if(width < 3 || height < 3)
        return;

    veejay_memcpy(B, Y, len);

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int up = row - width;
        const int dn = row + width;

        for(int x = 1; x < width - 1; x++) {
            const int idx = row + x;

            B[idx] = (uint8_t)(
                (
                    Y[up + x - 1] + Y[up + x] + Y[up + x + 1] +
                    Y[row + x - 1] + Y[row + x] + Y[row + x + 1] +
                    Y[dn + x - 1] + Y[dn + x] + Y[dn + x + 1]
                ) / 9
            );
        }
    }

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        Y[i] = noiseadd_absdiff_scaled((int)B[i], (int)Y[i], coeff, 1000);
    }
}

static void noiseneg3x3_maskapply(noiseadd_t *n, VJFrame *frame, int coeff)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict B = n->Yb_frame;

    if(width < 3 || height < 3)
        return;

    veejay_memcpy(B, Y, len);

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int up = row - width;
        const int dn = row + width;

        for(int x = 1; x < width - 1; x++) {
            const int idx = row + x;

            B[idx] = (uint8_t)(
                255 - (
                    (
                        Y[up + x - 1] + Y[up + x] + Y[up + x + 1] +
                        Y[row + x - 1] + Y[row + x] + Y[row + x + 1] +
                        Y[dn + x - 1] + Y[dn + x] + Y[dn + x + 1]
                    ) / 9
                )
            );
        }
    }

#pragma omp parallel for schedule(static) num_threads(n->n_threads)
    for(int i = 0; i < len; i++) {
        Y[i] = noiseadd_absdiff_scaled((int)Y[i], (int)B[i], coeff, 1000);
    }
}

void noiseadd_apply(void *ptr, VJFrame *frame, int *args)
{
    noiseadd_t *n = (noiseadd_t*) ptr;
    if(!n || !frame || !args)
        return;

    int type = args[0];
    int coeff = args[1];

    if(coeff < 1)
        coeff = 1;
    else if(coeff > 5000)
        coeff = 5000;

    switch(type) {
        case 0:
            noiseblur1x3_maskapply(n, frame, coeff);
            break;
        case 1:
            noiseblur3x3_maskapply(n, frame, coeff);
            break;
        case 2:
            noiseneg3x3_maskapply(n, frame, coeff);
            break;
    }
}