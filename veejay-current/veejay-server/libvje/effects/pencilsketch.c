/*
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "pencilsketch.h"

typedef uint8_t (*_pcf)(uint8_t a, uint8_t b, int t_max);
typedef uint8_t (*_pcbcr)(uint8_t a, uint8_t b);

vj_effect *pencilsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->defaults[1] = pixel_Y_lo_;
    ve->defaults[2] = pixel_Y_hi_;
    ve->defaults[3] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

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
        ve->limits[1][0],
        0,
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

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,   0,  0,    0,    0,   -1000, /* Sketch Mode */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 8,                  120,                6,  22, 1600, 3400, 700,  35,    /* Min Threshold */
        VJ_BEAT_DETAIL,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 135,                245,                6,  22, 1600, 3400, 700,  35,    /* Max Threshold */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,   0,  0,    0,    0,   -1000  /* Mask */
    );

    (void) w;
    (void) h;

    return ve;
}

static inline int ps_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t ps_u8(int v)
{
    return (uint8_t) ps_clampi(v, 0, 255);
}

static uint8_t _pcf_dneg(uint8_t a, uint8_t b, int t_max)
{
    uint8_t p =
        0xff - abs((0xff - abs((0xff - a) - a)) - (0xff - abs((0xff - b) - b)));

    (void) t_max;

    p = abs(abs(p - b) - b);

    return p;
}

static uint8_t _pcf_lghtn(uint8_t a, uint8_t b, int t_max)
{
    (void) t_max;
    return (a > b ? a : b);
}

static uint8_t _pcf_dneg2(uint8_t a, uint8_t b, int t_max)
{
    (void) t_max;
    return (uint8_t)(0xff - abs((0xff - a) - b));
}

static uint8_t _pcf_min(uint8_t a, uint8_t b, int t_max)
{
    uint8_t p = (b < a) ? b : a;

    (void) t_max;

    p = (uint8_t)(0xff - abs((0xff - p) - b));

    return p;
}

static uint8_t _pcf_max(uint8_t a, uint8_t b, int t_max)
{
    int p = (b > a) ? b : a;

    (void) t_max;

    p = CLAMP_Y(p);
    if(p == 0)
        p = 1;

    p = 0xff - (((0xff - b) * (0xff - b)) / p);

    return ps_u8(p);
}

static uint8_t _pcf_pq(uint8_t a, uint8_t b, int t_max)
{
    int p;
    int q;

    (void) t_max;

    a = CLAMP_Y(a);
    b = CLAMP_Y(b);

    if(a == 0)
        a = 1;
    if(b == 0)
        b = 1;

    p = 0xff - (((0xff - a) * (0xff - a)) / a);
    q = 0xff - (((0xff - b) * (0xff - b)) / b);

    if(q == 0)
        q = 1;

    p = 0xff - (((0xff - p) * (0xff - a)) / q);

    return ps_u8(p);
}

static uint8_t _pcf_color(uint8_t a, uint8_t b, int t_max)
{
    int p =
        0xff - abs((0xff - abs((0xff - a) - a)) - (0xff - abs((0xff - b) - b)));

    (void) t_max;

    p = abs(abs(p - b) - b);
    p = p + b - ((p * b) >> 8);

    return ps_u8(p);
}

static uint8_t _pcbcr_color(uint8_t a, uint8_t b)
{
    int p = (int)a - 128;
    int q = (int)b - 128;
    int r = p + q - ((p * q) >> 8);

    return ps_u8(r + 128);
}

static uint8_t _pcf_none(uint8_t a, uint8_t b, int t_max)
{
    (void) b;

    return (a >= pixel_Y_lo_ && a <= t_max) ? pixel_Y_lo_ : pixel_Y_hi_;
}

static _pcf _get_pcf(int type)
{
    switch(type) {
        case 0: return &_pcf_dneg;
        case 1: return &_pcf_min;
        case 2: return &_pcf_max;
        case 3: return &_pcf_lghtn;
        case 5: return &_pcf_pq;
        case 6: return &_pcf_dneg2;
        case 7: return &_pcf_color;
    }

    return &_pcf_none;
}

void pencilsketch_apply(void *ptr, VJFrame *frame, int *args)
{
    int type = args[0];
    int threshold_min = args[1];
    int threshold_max = args[2];
    int mode = args[3];

    const size_t len = (size_t) frame->len;
    const size_t uv_len = frame->ssm ? len : (size_t) frame->uv_len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    _pcf pff;
    int n_threads;

    (void) ptr;

    type = ps_clampi(type, 0, 8);
    mode = ps_clampi(mode, 0, 1);

    threshold_min = ps_clampi(threshold_min, 0, 255);
    threshold_max = ps_clampi(threshold_max, 0, 255);

    if(threshold_max < threshold_min) {
        int tmp = threshold_min;
        threshold_min = threshold_max;
        threshold_max = tmp;
    }

    pff = _get_pcf(type);
    n_threads = vje_advise_num_threads((int)len);

#pragma omp parallel num_threads(n_threads)
    {
        if(mode == 1) {
#pragma omp for schedule(static)
            for(size_t i = 0; i < len; i++) {
                const uint8_t y = Y[i];

                Y[i] = (y >= threshold_min && y <= threshold_max)
                    ? pff(y, (uint8_t)(0xff - y), threshold_max)
                    : pixel_Y_hi_;
            }
        } else {
            const int range = (threshold_max > threshold_min) ? (threshold_max - threshold_min) : 1;

#pragma omp for schedule(static)
            for(size_t i = 0; i < len; i++) {
                const uint8_t y_orig = Y[i];

                if(y_orig >= threshold_min && y_orig <= threshold_max) {
                    const int normalized = ((int)(y_orig - threshold_min) * 255) / range;
                    const uint8_t y_val = ps_u8(normalized);

                    Y[i] = pff(y_val, y_orig, threshold_max);
                } else {
                    Y[i] = pixel_Y_hi_;
                }
            }
        }

        if(type != 7) {
#pragma omp for schedule(static)
            for(size_t i = 0; i < uv_len; i++) {
                Cb[i] = 128;
                Cr[i] = 128;
            }
        } else {
#pragma omp for schedule(static)
            for(size_t i = 0; i < uv_len; i++) {
                Cb[i] = _pcbcr_color(128, Cb[i]);
                Cr[i] = _pcbcr_color(128, Cr[i]);
            }
        }
    }
}