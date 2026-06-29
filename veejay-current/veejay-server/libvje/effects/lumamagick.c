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

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_KICK,     VJ_BEAT_F_CONTINUOUS,                   35,                 165,                10,46,160,  1050, 0,    66,
        VJ_BEAT_SNARE,    VJ_BEAT_F_CONTINUOUS,                   35,                 165,                8, 38,200,  1250, 0,    58
    );

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

static void lumamagick_lumaflow(VJFrame *frame, VJFrame *frame2, int op_a, int op_b, int n_threads)
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

static void lumamagick_process(VJFrame *frame, VJFrame *frame2, int mode, int op_a, int op_b, int n_threads)
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

        int oy = sy;
        int ou = su;
        int ov = sv;

        switch(mode) {
            case VJ_EFFECT_BLEND_ADDDISTORT:
                oy = lm_u8(lm_yq((uint8_t)lm_u8(a + b), qa) + b);
                ou = lm_c8(lm_smul_q8(ua + ub, qa) + ub);
                ov = lm_c8(lm_smul_q8(va + vb, qa) + vb);
                break;

            case VJ_EFFECT_BLEND_SUBDISTORT:
                oy = lm_u8(a - b);
                ou = lm_c8(ua - ub);
                ov = lm_c8(va - vb);
                break;

            case VJ_EFFECT_BLEND_MULTIPLY:
                oy = lm_u8((a * b + 128) >> 8);
                ou = lm_c8((ua * ub + 64) >> 7);
                ov = lm_c8((va * vb + 64) >> 7);
                break;

            case VJ_EFFECT_BLEND_DIVIDE: {
                const int denom = 255 - b;
                if(denom > pixel_Y_lo_)
                    oy = lm_u8(lm_safe_div(a * a, denom));
                break;
            }

            case VJ_EFFECT_BLEND_ADDITIVE:
                oy = lm_u8(a + b);
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;

            case VJ_EFFECT_BLEND_SUBSTRACTIVE:
                oy = lm_u8(a + b - 255);
                break;

            case VJ_EFFECT_BLEND_SOFTBURN:
                if(a + b < 255)
                    oy = (a > pixel_Y_hi_) ? lm_u8(a) : lm_u8(lm_safe_div(b << 7, 255 - a));
                else
                    oy = (b <= pixel_Y_lo_) ? 255 : lm_u8(255 - lm_safe_div((255 - a) << 7, b));
                break;

            case VJ_EFFECT_BLEND_INVERSEBURN:
                oy = (a <= pixel_Y_lo_) ? pixel_Y_lo_ : lm_u8(255 - lm_safe_div((255 - b) << 8, a));
                break;

            case VJ_EFFECT_BLEND_COLORDODGE: {
                int denom = 255 - b;
                denom |= (denom == 0);
                oy = lm_u8(a + lm_safe_div(a * b, denom));

                int denom_u = 127 - ub;
                int denom_v = 127 - vb;
                denom_u = denom_u ? denom_u : 1;
                denom_v = denom_v ? denom_v : 1;
                ou = lm_c8(ua + lm_safe_div(ua * ub, denom_u));
                ov = lm_c8(va + lm_safe_div(va * vb, denom_v));
                break;
            }

            case VJ_EFFECT_BLEND_MULSUB: {
                const int denom = (pixel_Y_hi_ - ty) | 1;
                oy = lm_u8(lm_safe_div(sy * qa, denom));
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;
            }

            case VJ_EFFECT_BLEND_LIGHTEN:
                oy = lm_u8(a > b ? a : b);
                break;

            case VJ_EFFECT_BLEND_DIFFERENCE:
                oy = lm_u8(lm_absi(a - b));
                break;

            case VJ_EFFECT_BLEND_DIFFNEGATE:
                oy = lm_u8(255 - lm_absi(lm_yq((uint8_t)(255 - sy), qa) - b));
                ou = lm_c8(-lm_absi(ua - ub));
                ov = lm_c8(-lm_absi(va - vb));
                break;

            case VJ_EFFECT_BLEND_EXCLUSIVE:
                oy = lm_u8(a + b - ((a * b + 64) >> 7));
                ou = lm_c8(ua + ub - ((ua * ub + 64) >> 7));
                ov = lm_c8(va + vb - ((va * vb + 64) >> 7));
                break;

            case VJ_EFFECT_BLEND_BASECOLOR: {
                const int mult = (a * b + 64) >> 7;
                const int scr = lm_screen(a, b);
                oy = lm_u8(mult + ((a * (scr - mult) + 128) >> 8));
                break;
            }

            case VJ_EFFECT_BLEND_HARDLIGHT:
                oy = lm_u8(lm_overlay(a, b));
                break;

            case VJ_EFFECT_BLEND_RELADD:
                oy = lm_u8((a + b) >> 1);
                ou = (uint8_t)(((int)su + (int)tu) >> 1);
                ov = (uint8_t)(((int)sv + (int)tv) >> 1);
                break;

            case VJ_EFFECT_BLEND_RELSUB:
                oy = lm_u8((a - b + 255) >> 1);
                ou = lm_u8((su - tu + 255) >> 1);
                ov = lm_u8((sv - tv + 255) >> 1);
                break;

            case VJ_EFFECT_BLEND_MAXSEL:
                if(b > a) {
                    oy = lm_u8(b);
                    ou = tu;
                    ov = tv;
                }
                break;

            case VJ_EFFECT_BLEND_MINSEL:
                if(b < a) {
                    oy = lm_u8(b);
                    ou = tu;
                    ov = tv;
                }
                break;

            case VJ_EFFECT_BLEND_RELADDLUM:
                oy = lm_u8((a + b) >> 1);
                break;

            case VJ_EFFECT_BLEND_RELSUBLUM:
                oy = lm_u8(a - b + 128);
                ou = lm_c8(ua - ub);
                ov = lm_c8(va - vb);
                break;

            case VJ_EFFECT_BLEND_MINSUBSEL:
                if(b < a)
                    oy = lm_u8((b - a + 255) >> 1);
                else
                    oy = lm_u8((a - b + 255) >> 1);
                break;

            case VJ_EFFECT_BLEND_MAXSUBSEL:
                if(b > a)
                    oy = lm_u8((b - a + 255) >> 1);
                else
                    oy = lm_u8((a - b + 255) >> 1);
                break;

            case VJ_EFFECT_BLEND_ADDSUBSEL:
                if(b < a)
                    oy = lm_u8((a + b) >> 1);
                break;

            case VJ_EFFECT_BLEND_ADDAVG:
                oy = lm_u8(a + (b << 1) - 255);
                break;

            case VJ_EFFECT_BLEND_ADDTEST2:
                oy = lm_u8(a + (b << 1) - 255);
                ou = lm_u8(su + (tu << 1) - 255);
                ov = lm_u8(sv + (tv << 1) - 255);
                break;

            case VJ_EFFECT_BLEND_ADDTEST3: {
                int denom = b - 255;
                if(denom <= pixel_Y_lo_)
                    denom = 255;
                oy = lm_u8(lm_safe_div(a * a, denom));
                break;
            }

            case VJ_EFFECT_BLEND_ADDTEST4: {
                int denom = lm_yq((uint8_t)(255 - ty), qb);
                if(denom <= pixel_Y_lo_)
                    denom = 1;
                oy = lm_u8(lm_safe_div(a * a, denom));
                ou = (uint8_t)((su + (255 - tu)) >> 1);
                ov = (uint8_t)((sv + (255 - tv)) >> 1);
                break;
            }

            case VJ_EFFECT_BLEND_ADDTEST6:
                oy = lm_u8(a + b);
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;

            case VJ_EFFECT_BLEND_FREEZE:
                oy = lm_u8(ty + (((255 - ty) * sy + 128) >> 8));
                if(oy < pixel_Y_lo_)
                    oy = pixel_Y_lo_;
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;

            case VJ_EFFECT_BLEND_UNFREEZE:
                oy = lm_u8(255 - lm_safe_div((255 - ty) * (255 - ty), sy | 1));
                if(oy < pixel_Y_lo_)
                    oy = pixel_Y_lo_;
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;

            case VJ_EFFECT_BLEND_ADDLUM: {
                int denom = 255 - b;
                denom |= (denom == 0);
                oy = lm_u8(lm_safe_div(a * a, denom));
                ou = lm_c8(ua + ub);
                ov = lm_c8(va + vb);
                break;
            }

            case VJ_EFFECT_BLEND_NEGDIV:
                oy = lm_u8(255 - lm_absi(a - b));
                ou = lm_c8(ua + ub - ((ua * ub + 64) >> 7));
                ov = lm_c8(va + vb - ((va * vb + 64) >> 7));
                break;

            case VJ_EFFECT_BLEND_SCREEN:
                oy = lm_u8(((lm_screen(sy, ty) * qa_mix) + (sy * (256 - qa_mix)) + 128) >> 8);
                ou = lm_u8(((su * (256 - qa_mix)) + (tu * qa_mix) + 128) >> 8);
                ov = lm_u8(((sv * (256 - qa_mix)) + (tv * qa_mix) + 128) >> 8);
                break;

            case VJ_EFFECT_BLEND_SUBSTRACTIVE2:
                oy = lm_u8(a - b);
                ou = lm_c8(ua - ub);
                ov = lm_c8(va - vb);
                break;

            case VJ_EFFECT_BLEND_SWAP:
                oy = ty;
                ou = tu;
                ov = tv;
                break;

            default:
                oy = lm_u8(((sy * qa_mix) + (ty * qb_mix) + 128) >> 8);
                ou = lm_u8(((su * qa_mix) + (tu * qb_mix) + 128) >> 8);
                ov = lm_u8(((sv * qa_mix) + (tv * qb_mix) + 128) >> 8);
                break;
        }

        Y[i] = (uint8_t)oy;
        U[i] = (uint8_t)ou;
        V[i] = (uint8_t)ov;
    }
}

void lumamagick_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    lumamagick_t *m = (lumamagick_t*) ptr;

    const int mode = args[P_MODE];
    const int op_a = args[P_OPACITY_A];
    const int op_b = args[P_OPACITY_B];

#pragma omp parallel num_threads(m->n_threads)
    {
        if(mode == VJ_EFFECT_BLEND_ADDTEST7)
            lumamagick_lumaflow(frame, frame2, op_a, op_b, m->n_threads);
        else
            lumamagick_process(frame, frame2, mode, op_a, op_b, m->n_threads);
    }
}
