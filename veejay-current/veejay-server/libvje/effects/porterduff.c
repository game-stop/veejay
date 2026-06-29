/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include "porterduff.h"

#define PORTERDUFF_PARAMS 1

#define P_OPERATOR 0

static inline uint8_t pd_div255(int x)
{
    const int v = x + 128;

    return (uint8_t)((v + (v >> 8)) >> 8);
}

static inline uint8_t pd_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *porterduff_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PORTERDUFF_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_OPERATOR] = 0;
    ve->limits[0][P_OPERATOR] = 0;
    ve->limits[1][P_OPERATOR] = 15;

    ve->param_description = vje_build_param_list(ve->num_params, "Operator");
    ve->has_user = 0;
    ve->description = "Porter Duff operations (Luma only)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 0;
    ve->rgba_only = 1;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_OPERATOR],
        P_OPERATOR,
        "Dest",
        "Dest Atop",
        "Dest In",
        "Dest Over",
        "Dest Out",
        "Src Over",
        "SrcAtop",
        "Src In",
        "Src Out",
        "Multiply",
        "Xor",
        "Add",
        "Subtract",
        "Divide",
        "Screen",
        "Overlay"
    );


    return ve;
}

static void porterduff_dst(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
    veejay_memcpy(A, B, n_pixels * 4);
}

static void porterduff_dst_atop(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int as = B[idx + 3];
        const int ad = A[idx + 3];
        const int inv_as = 255 - as;

        A[idx + 0] = pd_div255((int)B[idx + 0] * ad + (int)A[idx + 0] * inv_as);
        A[idx + 1] = pd_div255((int)B[idx + 1] * ad + (int)A[idx + 1] * inv_as);
        A[idx + 2] = pd_div255((int)B[idx + 2] * ad + (int)A[idx + 2] * inv_as);
        A[idx + 3] = (uint8_t)ad;
    }
}

static void porterduff_dst_in(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int as = B[idx + 3];

        A[idx + 0] = pd_div255((int)A[idx + 0] * as);
        A[idx + 1] = pd_div255((int)A[idx + 1] * as);
        A[idx + 2] = pd_div255((int)A[idx + 2] * as);
        A[idx + 3] = pd_div255((int)A[idx + 3] * as);
    }
}

static void porterduff_dst_out(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int inv_as = 255 - B[idx + 3];

        A[idx + 0] = pd_div255((int)A[idx + 0] * inv_as);
        A[idx + 1] = pd_div255((int)A[idx + 1] * inv_as);
        A[idx + 2] = pd_div255((int)A[idx + 2] * inv_as);
        A[idx + 3] = pd_div255((int)A[idx + 3] * inv_as);
    }
}

static void porterduff_dst_over(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int ad = A[idx + 3];
        const int inv_ad = 255 - ad;

        A[idx + 0] = pd_u8((int)A[idx + 0] + pd_div255((int)B[idx + 0] * inv_ad));
        A[idx + 1] = pd_u8((int)A[idx + 1] + pd_div255((int)B[idx + 1] * inv_ad));
        A[idx + 2] = pd_u8((int)A[idx + 2] + pd_div255((int)B[idx + 2] * inv_ad));
        A[idx + 3] = pd_u8(ad + pd_div255((int)B[idx + 3] * inv_ad));
    }
}

static void porterduff_src_over(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int as = B[idx + 3];
        const int inv_as = 255 - as;

        A[idx + 0] = pd_u8((int)B[idx + 0] + pd_div255((int)A[idx + 0] * inv_as));
        A[idx + 1] = pd_u8((int)B[idx + 1] + pd_div255((int)A[idx + 1] * inv_as));
        A[idx + 2] = pd_u8((int)B[idx + 2] + pd_div255((int)A[idx + 2] * inv_as));
        A[idx + 3] = pd_u8(as + pd_div255((int)A[idx + 3] * inv_as));
    }
}

static void porterduff_src_atop(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int as = B[idx + 3];
        const int ad = A[idx + 3];
        const int inv_as = 255 - as;

        A[idx + 0] = pd_div255((int)B[idx + 0] * ad + (int)A[idx + 0] * inv_as);
        A[idx + 1] = pd_div255((int)B[idx + 1] * ad + (int)A[idx + 1] * inv_as);
        A[idx + 2] = pd_div255((int)B[idx + 2] * ad + (int)A[idx + 2] * inv_as);
        A[idx + 3] = (uint8_t)as;
    }
}

static void porterduff_src_in(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int ad = A[idx + 3];

        A[idx + 0] = pd_div255((int)B[idx + 0] * ad);
        A[idx + 1] = pd_div255((int)B[idx + 1] * ad);
        A[idx + 2] = pd_div255((int)B[idx + 2] * ad);
        A[idx + 3] = pd_div255((int)B[idx + 3] * ad);
    }
}

static void porterduff_src_out(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int inv_ad = 255 - A[idx + 3];

        A[idx + 0] = pd_div255((int)B[idx + 0] * inv_ad);
        A[idx + 1] = pd_div255((int)B[idx + 1] * inv_ad);
        A[idx + 2] = pd_div255((int)B[idx + 2] * inv_ad);
        A[idx + 3] = pd_div255((int)B[idx + 3] * inv_ad);
    }
}

static void porterduff_multiply(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int sa = B[idx + 3];
        const int da = A[idx + 3];
        const int inv_sa = 255 - sa;
        const int inv_da = 255 - da;

        for(int j = 0; j < 3; j++) {
            const int s = B[idx + j];
            const int d = A[idx + j];
            const int v = pd_div255(s * d) + pd_div255(s * inv_da) + pd_div255(d * inv_sa);

            A[idx + j] = pd_u8(v);
        }

        A[idx + 3] = pd_u8(sa + pd_div255(da * inv_sa));
    }
}

static void porterduff_xor(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;
        const int sa = B[idx + 3];
        const int da = A[idx + 3];
        const int inv_sa = 255 - sa;
        const int inv_da = 255 - da;

        A[idx + 0] = pd_u8(pd_div255((int)B[idx + 0] * inv_da) + pd_div255((int)A[idx + 0] * inv_sa));
        A[idx + 1] = pd_u8(pd_div255((int)B[idx + 1] * inv_da) + pd_div255((int)A[idx + 1] * inv_sa));
        A[idx + 2] = pd_u8(pd_div255((int)B[idx + 2] * inv_da) + pd_div255((int)A[idx + 2] * inv_sa));
        A[idx + 3] = pd_div255(sa * inv_da + da * inv_sa);
    }
}

static void porterduff_add(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;

        A[idx + 0] = pd_u8((int)A[idx + 0] + (int)B[idx + 0]);
        A[idx + 1] = pd_u8((int)A[idx + 1] + (int)B[idx + 1]);
        A[idx + 2] = pd_u8((int)A[idx + 2] + (int)B[idx + 2]);
        A[idx + 3] = pd_u8((int)A[idx + 3] + (int)B[idx + 3]);
    }
}

static void porterduff_subtract(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;

        A[idx + 0] = pd_u8((int)A[idx + 0] - (int)B[idx + 0]);
        A[idx + 1] = pd_u8((int)A[idx + 1] - (int)B[idx + 1]);
        A[idx + 2] = pd_u8((int)A[idx + 2] - (int)B[idx + 2]);
        A[idx + 3] = pd_u8((int)A[idx + 3] - (int)B[idx + 3]);
    }
}

static void porterduff_divide(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;

        for(int j = 0; j < 3; j++) {
            const int b = B[idx + j];

            A[idx + j] = b == 0 ? 255 : pd_u8(((int)A[idx + j] * 255) / b);
        }
    }
}

static void porterduff_screen(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;

        A[idx + 0] = (uint8_t)(255 - pd_div255((255 - A[idx + 0]) * (255 - B[idx + 0])));
        A[idx + 1] = (uint8_t)(255 - pd_div255((255 - A[idx + 1]) * (255 - B[idx + 1])));
        A[idx + 2] = (uint8_t)(255 - pd_div255((255 - A[idx + 2]) * (255 - B[idx + 2])));
        A[idx + 3] = pd_u8((int)A[idx + 3] + pd_div255((int)B[idx + 3] * (255 - A[idx + 3])));
    }
}

static void porterduff_overlay(uint8_t *restrict A, const uint8_t *restrict B, int n_pixels)
{
#pragma omp for schedule(static)
    for(int i = 0; i < n_pixels; i++) {
        const int idx = i << 2;

        for(int j = 0; j < 3; j++) {
            const int d = A[idx + j];
            const int s = B[idx + j];

            A[idx + j] = d < 128
                ? pd_div255(2 * d * s)
                : (uint8_t)(255 - pd_div255(2 * (255 - d) * (255 - s)));
        }

        A[idx + 3] = pd_u8((int)A[idx + 3] + pd_div255((int)B[idx + 3] * (255 - A[idx + 3])));
    }
}

void porterduff_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void)ptr;

    const int mode = args[P_OPERATOR];
    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);
    uint8_t *restrict A = frame->data[0];
    const uint8_t *restrict B = frame2->data[0];

#pragma omp parallel num_threads(n_threads)
    {
        switch(mode) {
            case 0:
#pragma omp single
                porterduff_dst(A, B, len);
                break;
        case 1:
            porterduff_dst_atop(A, B, len);
            break;
        case 2:
            porterduff_dst_in(A, B, len);
            break;
        case 3:
            porterduff_dst_over(A, B, len);
            break;
        case 4:
            porterduff_dst_out(A, B, len);
            break;
        case 5:
            porterduff_src_over(A, B, len);
            break;
        case 6:
            porterduff_src_atop(A, B, len);
            break;
        case 7:
            porterduff_src_in(A, B, len);
            break;
        case 8:
            porterduff_src_out(A, B, len);
            break;
        case 9:
            porterduff_multiply(A, B, len);
            break;
        case 10:
            porterduff_xor(A, B, len);
            break;
        case 11:
            porterduff_add(A, B, len);
            break;
        case 12:
            porterduff_subtract(A, B, len);
            break;
        case 13:
            porterduff_divide(A, B, len);
            break;
        case 14:
            porterduff_screen(A, B, len);
            break;
            case 15:
                porterduff_overlay(A, B, len);
                break;
        }
    }
}
