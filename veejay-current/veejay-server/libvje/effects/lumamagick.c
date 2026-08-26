/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "internal.h"
#include "lumamagick.h"

#define LUMAMAGICK_PARAMS 3

#define P_MODE      0
#define P_OPACITY_A 1
#define P_OPACITY_B 2

typedef struct {
    int n_threads;
} lumamagick_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int lm_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t lm_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t lm_c8(int v)
{
    return (uint8_t)(128 + clampi(v, -128, 127));
}

static inline int lm_smul_q8(int v, int q)
{
    return (v >= 0) ? ((v * q + 128) >> 8) : -(((-v) * q + 128) >> 8);
}

static inline int lm_yq(uint8_t y, int q)
{
    return ((int)y * q + 128) >> 8;
}

static inline int lm_cq(uint8_t c, int q)
{
    return lm_smul_q8((int)c - 128, q);
}

static inline int lm_q_from_percent(int v)
{
    return (clampi(v, 0, 200) * 256 + 50) / 100;
}

static inline int lm_safe_div(int a, int b)
{
    return a / (b ? b : 1);
}

static inline int lm_screen(int a, int b)
{
    return 255 - (((255 - a) * (255 - b) + 128) >> 8);
}

static inline int lm_overlay(int a, int b)
{
    return (b < 128) ? ((a * b + 64) >> 7) : 255 - ((((255 - b) * (255 - a)) + 64) >> 7);
}

vj_effect *lumamagick_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LUMAMAGICK_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[P_MODE] = 0;
    ve->defaults[P_OPACITY_A] = 100;
    ve->defaults[P_OPACITY_B] = 100;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = VJ_EFFECT_BLEND_COUNT;
    ve->limits[0][P_OPACITY_A] = 0;
    ve->limits[1][P_OPACITY_A] = 200;
    ve->limits[0][P_OPACITY_B] = 0;
    ve->limits[1][P_OPACITY_B] = 200;

    ve->description = "Luma Magick";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Opacity A", "Opacity B");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, VJ_EFFECT_BLEND_STRINGS);

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 20, 190, 82, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_MID_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 20, 190, 82, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *lumamagick_malloc(int w, int h)
{
    lumamagick_t *m = (lumamagick_t*) vj_malloc(sizeof(lumamagick_t));

    if(!m)
        return NULL;

    m->n_threads = vje_advise_num_threads(w * h);

    return (void*) m;
}

void lumamagick_free(void *ptr)
{
    free(ptr);
}

static void lumamagick_lumaflow(VJFrame *frame, VJFrame *frame2, int op_a, int op_b)
{
    const int len = frame->len;
    const int flow_intensity = op_a * 5;
    const int quant_levels = (op_b / 10) + 2;
    const int step = 255 / quant_levels;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    const uint8_t *restrict Y2 = frame2->data[0];

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        const int delta = lm_absi((int)Y[i] - (int)Y2[i]);
        const int offset = ((delta * flow_intensity) / 100) & 15;
        const int j = (i + offset < len) ? i + offset : i;

        Y[i] = (uint8_t)((Y[i] / step) * step);
        U[i] = U[j];
        V[i] = V[j];
    }
}

static void lumamagick_process(VJFrame *frame, VJFrame *frame2, int mode, int op_a, int op_b)
{
    const int len = frame->len;
    const int qa = lm_q_from_percent(op_a);
    const int qb = lm_q_from_percent(op_b);
    const int qa_mix = qa > 256 ? 256 : qa;
    const int qb_mix = qb > 256 ? 256 : qb;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    switch(mode) {
        case VJ_EFFECT_BLEND_ADDDISTORT:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(lm_yq((uint8_t)lm_u8(a + b), qa) + b);
                U[i] = lm_c8(lm_smul_q8(ua + ub, qa) + ub);
                V[i] = lm_c8(lm_smul_q8(va + vb, qa) + vb);
            }
            break;

        case VJ_EFFECT_BLEND_SUBDISTORT:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a - b);
                U[i] = lm_c8(ua - ub);
                V[i] = lm_c8(va - vb);
            }
            break;

        case VJ_EFFECT_BLEND_MULTIPLY:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8((a * b + 128) >> 8);
                U[i] = lm_c8((ua * ub + 64) >> 7);
                V[i] = lm_c8((va * vb + 64) >> 7);
            }
            break;

        case VJ_EFFECT_BLEND_DIVIDE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int denom = 255 - b;
                if(denom > pixel_Y_lo_)
                    Y[i] = lm_u8(lm_safe_div(a * a, denom));
            }
            break;

        case VJ_EFFECT_BLEND_ADDITIVE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a + b);
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_SUBSTRACTIVE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(a + b - 255);
            }
            break;

        case VJ_EFFECT_BLEND_SOFTBURN:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(a + b < 255)
                    Y[i] = (a > pixel_Y_hi_) ? lm_u8(a) : lm_u8(lm_safe_div(b << 7, 255 - a));
                else
                    Y[i] = (b <= pixel_Y_lo_) ? 255 : lm_u8(255 - lm_safe_div((255 - a) << 7, b));
            }
            break;

        case VJ_EFFECT_BLEND_INVERSEBURN:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = (a <= pixel_Y_lo_) ? pixel_Y_lo_ : lm_u8(255 - lm_safe_div((255 - b) << 8, a));
            }
            break;

        case VJ_EFFECT_BLEND_COLORDODGE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);

                int denom = 255 - b;
                denom |= (denom == 0);
                Y[i] = lm_u8(a + lm_safe_div(a * b, denom));

                int denom_u = 127 - ub;
                int denom_v = 127 - vb;
                denom_u = denom_u ? denom_u : 1;
                denom_v = denom_v ? denom_v : 1;
                U[i] = lm_c8(ua + lm_safe_div(ua * ub, denom_u));
                V[i] = lm_c8(va + lm_safe_div(va * vb, denom_v));
            }
            break;

        case VJ_EFFECT_BLEND_MULSUB:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                const int denom = (pixel_Y_hi_ - ty) | 1;
                Y[i] = lm_u8(lm_safe_div(sy * qa, denom));
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_LIGHTEN:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(a > b ? a : b);
            }
            break;

        case VJ_EFFECT_BLEND_DIFFERENCE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(lm_absi(a - b));
            }
            break;

        case VJ_EFFECT_BLEND_DIFFNEGATE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int su = U[i];
                const int sv = V[i];
                const int ty = Y2[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(255 - lm_absi(lm_yq((uint8_t)(255 - sy), qa) - b));
                U[i] = lm_c8(-lm_absi(ua - ub));
                V[i] = lm_c8(-lm_absi(va - vb));
            }
            break;

        case VJ_EFFECT_BLEND_EXCLUSIVE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a + b - ((a * b + 64) >> 7));
                U[i] = lm_c8(ua + ub - ((ua * ub + 64) >> 7));
                V[i] = lm_c8(va + vb - ((va * vb + 64) >> 7));
            }
            break;

        case VJ_EFFECT_BLEND_BASECOLOR:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int mult = (a * b + 64) >> 7;
                const int scr = lm_screen(a, b);
                Y[i] = lm_u8(mult + ((a * (scr - mult) + 128) >> 8));
            }
            break;

        case VJ_EFFECT_BLEND_HARDLIGHT:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(lm_overlay(a, b));
            }
            break;

        case VJ_EFFECT_BLEND_RELADD:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8((a + b) >> 1);
                U[i] = (uint8_t)(((int)su + (int)tu) >> 1);
                V[i] = (uint8_t)(((int)sv + (int)tv) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_RELSUB:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8((a - b + 255) >> 1);
                U[i] = lm_u8((su - tu + 255) >> 1);
                V[i] = lm_u8((sv - tv + 255) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_MAXSEL:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(b > a) {
                    Y[i] = lm_u8(b);
                    U[i] = tu;
                    V[i] = tv;
                }
            }
            break;

        case VJ_EFFECT_BLEND_MINSEL:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(b < a) {
                    Y[i] = lm_u8(b);
                    U[i] = tu;
                    V[i] = tv;
                }
            }
            break;

        case VJ_EFFECT_BLEND_RELADDLUM:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8((a + b) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_RELSUBLUM:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a - b + 128);
                U[i] = lm_c8(ua - ub);
                V[i] = lm_c8(va - vb);
            }
            break;

        case VJ_EFFECT_BLEND_MINSUBSEL:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(b < a)
                    Y[i] = lm_u8((b - a + 255) >> 1);
                else
                    Y[i] = lm_u8((a - b + 255) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_MAXSUBSEL:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(b > a)
                    Y[i] = lm_u8((b - a + 255) >> 1);
                else
                    Y[i] = lm_u8((a - b + 255) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_ADDSUBSEL:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                if(b < a)
                    Y[i] = lm_u8((a + b) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_ADDAVG:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(a + (b << 1) - 255);
            }
            break;

        case VJ_EFFECT_BLEND_ADDTEST2:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                Y[i] = lm_u8(a + (b << 1) - 255);
                U[i] = lm_u8(su + (tu << 1) - 255);
                V[i] = lm_u8(sv + (tv << 1) - 255);
            }
            break;

        case VJ_EFFECT_BLEND_ADDTEST3:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                int denom = b - 255;
                if(denom <= pixel_Y_lo_)
                    denom = 255;
                Y[i] = lm_u8(lm_safe_div(a * a, denom));
            }
            break;

        case VJ_EFFECT_BLEND_ADDTEST4:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                int denom = lm_yq((uint8_t)(255 - ty), qb);
                if(denom <= pixel_Y_lo_)
                    denom = 1;
                Y[i] = lm_u8(lm_safe_div(a * a, denom));
                U[i] = (uint8_t)((su + (255 - tu)) >> 1);
                V[i] = (uint8_t)((sv + (255 - tv)) >> 1);
            }
            break;

        case VJ_EFFECT_BLEND_ADDTEST6:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a + b);
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_FREEZE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                int oy = lm_u8(ty + (((255 - ty) * sy + 128) >> 8));
                if(oy < pixel_Y_lo_)
                    oy = pixel_Y_lo_;
                Y[i] = (uint8_t)oy;
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_UNFREEZE:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                int oy = lm_u8(255 - lm_safe_div((255 - ty) * (255 - ty), sy | 1));
                if(oy < pixel_Y_lo_)
                    oy = pixel_Y_lo_;
                Y[i] = (uint8_t)oy;
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_ADDLUM:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                int denom = 255 - b;
                denom |= (denom == 0);
                Y[i] = lm_u8(lm_safe_div(a * a, denom));
                U[i] = lm_c8(ua + ub);
                V[i] = lm_c8(va + vb);
            }
            break;

        case VJ_EFFECT_BLEND_NEGDIV:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(255 - lm_absi(a - b));
                U[i] = lm_c8(ua + ub - ((ua * ub + 64) >> 7));
                V[i] = lm_c8(va + vb - ((va * vb + 64) >> 7));
            }
            break;

        case VJ_EFFECT_BLEND_SCREEN:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                Y[i] = lm_u8(((lm_screen(sy, ty) * qa_mix) + (sy * (256 - qa_mix)) + 128) >> 8);
                U[i] = lm_u8(((su * (256 - qa_mix)) + (tu * qa_mix) + 128) >> 8);
                V[i] = lm_u8(((sv * (256 - qa_mix)) + (tv * qa_mix) + 128) >> 8);
            }
            break;

        case VJ_EFFECT_BLEND_SUBSTRACTIVE2:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                const int a = lm_yq((uint8_t)sy, qa);
                const int b = lm_yq((uint8_t)ty, qb);
                const int ua = lm_cq((uint8_t)su, qa);
                const int ub = lm_cq((uint8_t)tu, qb);
                const int va = lm_cq((uint8_t)sv, qa);
                const int vb = lm_cq((uint8_t)tv, qb);
                Y[i] = lm_u8(a - b);
                U[i] = lm_c8(ua - ub);
                V[i] = lm_c8(va - vb);
            }
            break;

        case VJ_EFFECT_BLEND_SWAP:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                Y[i] = Y2[i];
                U[i] = U2[i];
                V[i] = V2[i];
            }
            break;

        default:
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int su = U[i];
                const int sv = V[i];
                const int tu = U2[i];
                const int tv = V2[i];
                const int sy = Y[i];
                const int ty = Y2[i];
                Y[i] = lm_u8(((sy * qa_mix) + (ty * qb_mix) + 128) >> 8);
                U[i] = lm_u8(((su * qa_mix) + (tu * qb_mix) + 128) >> 8);
                V[i] = lm_u8(((sv * qa_mix) + (tv * qb_mix) + 128) >> 8);
            }
            break;
    }
}

void lumamagick_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    lumamagick_t *m = (lumamagick_t*) ptr;

    const int mode = args[P_MODE];
    const int op_a = args[P_OPACITY_A];
    const int op_b = args[P_OPACITY_B];
    if(mode == VJ_EFFECT_BLEND_ADDTEST7)
        lumamagick_lumaflow(frame, frame2, op_a, op_b);
    else
        lumamagick_process(frame, frame2, mode, op_a, op_b);
}