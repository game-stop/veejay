/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include <libvje/internal.h>
#include "magicoverlays.h"

#define OVERLAYMAGIC_PARAMS 2

#define P_MODE 0
#define P_CLEAR_CHROMA 1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int overlaymagic_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t overlaymagic_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int overlaymagic_div(int a, int b)
{
    return a / (b ? b : 1);
}

static inline int overlaymagic_screen(int a, int b)
{
    return 255 - ((((255 - a) * (255 - b)) + 128) >> 8);
}

vj_effect *overlaymagic_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = OVERLAYMAGIC_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_MODE] = 0;
    ve->defaults[P_CLEAR_CHROMA] = 0;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = VJ_EFFECT_BLEND_COUNT;
    ve->limits[0][P_CLEAR_CHROMA] = 0;
    ve->limits[1][P_CLEAR_CHROMA] = 1;

    ve->description = "Overlay Magic";
    ve->extra_frame = 1;
    ve->sub_format = -1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Keep or clear color");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, VJ_EFFECT_BLEND_STRINGS);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_CLEAR_CHROMA], P_CLEAR_CHROMA, "Keep Color", "Clear Chroma");

    return ve;
}

void overlaymagic_adddistorted(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] + (int)Y2[i]);
}

void overlaymagic_add_distorted(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y2[i] - (int)Y[i]);
}

void overlaymagic_subdistorted(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] - (int)Y2[i]);
}

void overlaymagic_sub_distorted(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y2[i] - (int)Y[i]);
}

void overlaymagic_multiply(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = (uint8_t)(((int)Y[i] * (int)Y2[i] + 128) >> 8);
}

void overlaymagic_simpledivide(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int b = Y2[i];
        if(b > pixel_Y_lo_)
            Y[i] = overlaymagic_u8(((int)Y[i] << 8) / b);
    }
}

void overlaymagic_divide(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        int denom = 255 - (int)Y2[i];

        if(denom <= 0)
            denom = 1;

        Y[i] = overlaymagic_u8((a * a) / denom);
    }
}

void overlaymagic_additive(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] + (((int)Y2[i] << 1) - 255));
}

void overlaymagic_substractive(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] - (int)Y2[i]);
}

void overlaymagic_softburn(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        int c;

        if(a + b <= 255) {
            c = (a >= 255) ? a : overlaymagic_div(b << 7, 256 - a);
        }
        else {
            c = 255 - overlaymagic_div((255 - a) << 7, b);
        }

        Y[i] = overlaymagic_u8(c);
    }
}

void overlaymagic_inverseburn(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        int c = pixel_Y_lo_;

        if(a > pixel_Y_lo_)
            c = 255 - overlaymagic_div((255 - b) << 8, a);

        Y[i] = overlaymagic_u8(c);
    }
}

void overlaymagic_colordodge(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        const int denom = 255 - b;
        int c = pixel_Y_hi_;

        if(denom > 0)
            c = a + overlaymagic_div(a * b, denom);

        Y[i] = overlaymagic_u8(c);
    }
}

void overlaymagic_mulsub(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int denom = 255 - (int)Y2[i];

        if(denom > pixel_Y_lo_)
            Y[i] = overlaymagic_u8(((int)Y[i] << 8) / denom);
    }
}

void overlaymagic_lighten(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = Y[i] > Y2[i] ? Y[i] : Y2[i];
}

void overlaymagic_difference(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = (uint8_t)overlaymagic_absi((int)Y[i] - (int)Y2[i]);
}

void overlaymagic_diffnegate(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = (uint8_t)(255 - overlaymagic_absi((255 - (int)Y[i]) - (int)Y2[i]));
}

void overlaymagic_exclusive(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        Y[i] = overlaymagic_u8(a + ((b << 1) - 255) - ((a * b + 128) >> 8));
    }
}

void overlaymagic_basecolor(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        const int mult = (a * b + 128) >> 8;
        const int scr = overlaymagic_screen(a, b);
        const int c = mult + ((a * (scr - mult) + 128) >> 8);

        Y[i] = overlaymagic_u8(c);
    }
}

void overlaymagic_freeze(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        if(b > pixel_Y_lo_)
            Y[i] = overlaymagic_u8(255 - overlaymagic_div((255 - a) * (255 - a), b));
    }
}

void overlaymagic_unfreeze(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        if(a > pixel_Y_lo_)
            Y[i] = overlaymagic_u8(255 - overlaymagic_div((255 - b) * (255 - b), a));
    }
}

void overlaymagic_hardlight(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        if(b < 128)
            Y[i] = overlaymagic_u8((a * b + 64) >> 7);
        else
            Y[i] = overlaymagic_u8(255 - (((255 - b) * (255 - a) + 64) >> 7));
    }
}

void overlaymagic_relativeaddlum(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = (uint8_t)(((int)Y[i] + (int)Y2[i]) >> 1);
}

void overlaymagic_relativesublum(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = (uint8_t)(((int)Y[i] - (int)Y2[i] + 255) >> 1);
}

void overlaymagic_relativeadd(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    overlaymagic_relativeaddlum(frame, frame2, n_threads);
}

void overlaymagic_relativesub(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    overlaymagic_relativesublum(frame, frame2, n_threads);
}

void overlaymagic_minsubselect(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        Y[i] = (uint8_t)((b < a ? b - a + 255 : a - b + 255) >> 1);
    }
}

void overlaymagic_maxsubselect(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        Y[i] = (uint8_t)((b > a ? b - a + 255 : a - b + 255) >> 1);
    }
}

void overlaymagic_addsubselect(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];

        if(b < a)
            Y[i] = (uint8_t)((a + b) >> 1);
    }
}

void overlaymagic_maxselect(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    overlaymagic_lighten(frame, frame2, n_threads);
}

void overlaymagic_minselect(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = Y2[i] < Y[i] ? Y2[i] : Y[i];
}

void overlaymagic_addtest(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] + ((((int)Y2[i] << 1) - 255) >> 1));
}

void overlaymagic_addtest2(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    overlaymagic_additive(frame, frame2, n_threads);
}

void overlaymagic_additive_luma(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] + (int)Y2[i]);
}

void overlaymagic_screen_blend(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8(overlaymagic_screen((int)Y[i], (int)Y2[i]));
}

void overlaymagic_addtest6(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        Y[i] = overlaymagic_u8(a + overlaymagic_screen(a, b) - 128);
    }
}

void overlaymagic_addtest7(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = Y2[i];
        Y[i] = overlaymagic_u8(a + (((b - 128) * (255 - overlaymagic_absi(a - 128))) >> 7));
    }
}

void overlaymagic_subtractive_clamped(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = overlaymagic_u8((int)Y[i] - (((int)Y2[i] << 1) - 255));
}

void overlaymagic_swap(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        Y[i] = Y2[i];
}

void overlaymagic_addtest4(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        const int a = Y[i];
        const int b = (int)Y2[i] - 255;

        if(b > pixel_Y_lo_)
            Y[i] = overlaymagic_u8((a * a) / b);
    }
}

void overlaymagic_try(VJFrame *frame, VJFrame *frame2, int n_threads)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        int a = Y[i];
        int b = Y[i];
        int p;
        int q;

        if(b <= pixel_Y_lo_)
            p = pixel_Y_lo_;
        else
            p = 255 - overlaymagic_div((256 - a) * (256 - a), b);

        if(p <= pixel_Y_lo_)
            p = pixel_Y_lo_;

        a = Y2[i];
        b = Y2[i];

        if(b <= pixel_Y_lo_)
            q = pixel_Y_lo_;
        else
            q = 255 - overlaymagic_div((256 - a) * (256 - a), b);

        if(q <= pixel_Y_lo_)
            q = pixel_Y_lo_;
        else
            q = 255 - overlaymagic_div((256 - p) * (256 - a), q);

        Y[i] = overlaymagic_u8(q);
    }
}

void overlaymagic_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
	const int n_threads = vje_advise_num_threads(frame->len);
    const int mode = args[P_MODE];
    const int clearchroma = args[P_CLEAR_CHROMA];

    switch(mode) {
        case VJ_EFFECT_BLEND_ADDITIVE:
            overlaymagic_additive(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SUBSTRACTIVE:
            overlaymagic_substractive(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MULTIPLY:
            overlaymagic_multiply(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_DIVIDE:
            overlaymagic_simpledivide(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_LIGHTEN:
            overlaymagic_lighten(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_DIFFERENCE:
            overlaymagic_difference(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_DIFFNEGATE:
            overlaymagic_diffnegate(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_EXCLUSIVE:
            overlaymagic_exclusive(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_BASECOLOR:
            overlaymagic_basecolor(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_FREEZE:
            overlaymagic_freeze(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_UNFREEZE:
            overlaymagic_unfreeze(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_RELADD:
            overlaymagic_relativeadd(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_RELSUB:
            overlaymagic_relativesub(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_RELADDLUM:
            overlaymagic_relativeaddlum(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_RELSUBLUM:
            overlaymagic_relativesublum(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MAXSEL:
            overlaymagic_maxselect(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MINSEL:
            overlaymagic_minselect(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MINSUBSEL:
            overlaymagic_minsubselect(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MAXSUBSEL:
            overlaymagic_maxsubselect(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDSUBSEL:
            overlaymagic_addsubselect(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDAVG:
            overlaymagic_add_distorted(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST2:
            overlaymagic_addtest(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST3:
            overlaymagic_addtest2(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST4:
            overlaymagic_addtest4(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_MULSUB:
            overlaymagic_mulsub(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SOFTBURN:
            overlaymagic_softburn(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_INVERSEBURN:
            overlaymagic_inverseburn(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_COLORDODGE:
            overlaymagic_colordodge(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDDISTORT:
            overlaymagic_adddistorted(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SUBDISTORT:
            overlaymagic_subdistorted(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST5:
            overlaymagic_try(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_NEGDIV:
            overlaymagic_divide(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDLUM:
            overlaymagic_additive_luma(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SCREEN:
            overlaymagic_screen_blend(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST6:
            overlaymagic_addtest6(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_ADDTEST7:
            overlaymagic_addtest7(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SUBSTRACTIVE2:
            overlaymagic_subtractive_clamped(frame, frame2, n_threads);
            break;
        case VJ_EFFECT_BLEND_SWAP:
            overlaymagic_swap(frame, frame2, n_threads);
            break;
    }

    if(clearchroma) {
        const int uv_len = frame->ssm ? frame->len : frame->uv_len;

        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }
}
