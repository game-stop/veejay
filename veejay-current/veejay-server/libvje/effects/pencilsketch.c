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
#include "pencilsketch.h"

#define PENCILSKETCH_PARAMS 4

#define P_MODE  0
#define P_MIN_T 1
#define P_MAX_T 2
#define P_MASK  3

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int ps_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t ps_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t ps_eval_none(uint8_t a, int t_max)
{
    return (a >= pixel_Y_lo_ && a <= t_max) ? (uint8_t)pixel_Y_lo_ : (uint8_t)pixel_Y_hi_;
}

static inline uint8_t ps_eval(int type, uint8_t a, uint8_t b, int t_max)
{
    int p;
    int q;

    switch(type) {
        case 0:
            p = 255 - ps_absi((255 - ps_absi((255 - a) - a)) - (255 - ps_absi((255 - b) - b)));
            p = ps_absi(ps_absi(p - b) - b);
            return (uint8_t)p;

        case 1:
            p = b < a ? b : a;
            p = 255 - ps_absi((255 - p) - b);
            return (uint8_t)p;

        case 2:
            p = b > a ? b : a;
            p = CLAMP_Y(p);

            if(p == 0)
                p = 1;

            return ps_u8(255 - (((255 - b) * (255 - b)) / p));

        case 3:
            return a > b ? a : b;

        case 5:
            a = CLAMP_Y(a);
            b = CLAMP_Y(b);

            if(a == 0)
                a = 1;
            if(b == 0)
                b = 1;

            p = 255 - (((255 - a) * (255 - a)) / a);
            q = 255 - (((255 - b) * (255 - b)) / b);

            if(q == 0)
                q = 1;

            return ps_u8(255 - (((255 - p) * (255 - a)) / q));

        case 6:
            return (uint8_t)(255 - ps_absi((255 - a) - b));

        case 7:
            p = 255 - ps_absi((255 - ps_absi((255 - a) - a)) - (255 - ps_absi((255 - b) - b)));
            p = ps_absi(ps_absi(p - b) - b);
            p = p + b - ((p * b) >> 8);
            return ps_u8(p);

        default:
            return ps_eval_none(a, t_max);
    }
}

static inline uint8_t ps_chroma_color(uint8_t a, uint8_t b)
{
    const int p = (int)a - 128;
    const int q = (int)b - 128;
    const int r = p + q - ((p * q) >> 8);

    return ps_u8(r + 128);
}

vj_effect *pencilsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PENCILSKETCH_PARAMS;
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

    ve->limits[0][P_MODE] = 0;  ve->limits[1][P_MODE] = 8;   ve->defaults[P_MODE] = 0;
    ve->limits[0][P_MIN_T] = 0; ve->limits[1][P_MIN_T] = 255; ve->defaults[P_MIN_T] = pixel_Y_lo_;
    ve->limits[0][P_MAX_T] = 0; ve->limits[1][P_MAX_T] = 255; ve->defaults[P_MAX_T] = pixel_Y_hi_;
    ve->limits[0][P_MASK] = 0;  ve->limits[1][P_MASK] = 1;    ve->defaults[P_MASK] = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Sketch Mode",
        "Min Threshold",
        "Max Threshold",
        "Mask"
    );

    ve->description = "Pencil Sketch";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Absolute",
        "Minimum",
        "Maximum",
        "Light",
        "Nothing",
        "Division",
        "Dianegative",
        "Chromatic",
        "Fallthrough"
    );
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MASK], P_MASK, "Normalize", "Mask");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 8,                  118,                12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    132,                248,                12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                            VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void pencilsketch_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int type = args[P_MODE];
    int threshold_min = args[P_MIN_T];
    int threshold_max = args[P_MAX_T];
    const int mask_mode = args[P_MASK];
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int n_threads = vje_advise_num_threads(len);

    if(threshold_max < threshold_min) {
        const int tmp = threshold_min;
        threshold_min = threshold_max;
        threshold_max = tmp;
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel num_threads(n_threads)
    {
        if(mask_mode) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t y = Y[i];

                Y[i] = (y >= threshold_min && y <= threshold_max)
                    ? ps_eval(type, y, (uint8_t)(255 - y), threshold_max)
                    : (uint8_t)pixel_Y_hi_;
            }
        }
        else {
            const int range = threshold_max > threshold_min ? threshold_max - threshold_min : 1;

#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const uint8_t y = Y[i];

                if(y >= threshold_min && y <= threshold_max) {
                    const uint8_t yn = ps_u8(((int)(y - threshold_min) * 255) / range);

                    Y[i] = ps_eval(type, yn, y, threshold_max);
                }
                else {
                    Y[i] = (uint8_t)pixel_Y_hi_;
                }
            }
        }

        if(type == 7) {
#pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++) {
                Cb[i] = ps_chroma_color(128, Cb[i]);
                Cr[i] = ps_chroma_color(128, Cr[i]);
            }
        }
        else {
#pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++) {
                Cb[i] = 128;
                Cr[i] = 128;
            }
        }
    }
}
