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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "sobel.h"

#define SOBEL_PARAMS 5

#define P_THRESHOLD    0
#define P_MODE         1
#define P_MIX          2
#define P_EDGE_GAIN    3
#define P_CHROMA_EDGE  4

typedef struct {
    uint8_t *buf[3];
    int max_len;
    int n_threads;

    float eff_threshold;
    float eff_mix;
    float eff_gain;
    float eff_chroma;
    int eff_initialized;
} sobel_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int sobel_abs_i(int v)
{
    const int m = v >> 31;
    return (v ^ m) - m;
}

static inline uint8_t sobel_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint8_t sobel_mix_y(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t sobel_mix_uv(uint8_t neutral, uint8_t src, int q8)
{
    q8 = clampi(q8, 0, 256);

    const int ac = (int)neutral - 128;
    const int bc = (int)src - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline uint8_t sobel_norm_grad(int gx, int gy)
{
    int grad = sobel_abs_i(gx) + sobel_abs_i(gy);
    int norm = (grad * 255 + 510) / 1020;

    return (uint8_t)clampi(norm, 0, 255);
}


static inline int sobel_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

vj_effect *sobel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SOBEL_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_THRESHOLD] = 0;    ve->limits[1][P_THRESHOLD] = 255;  ve->defaults[P_THRESHOLD] = 0;
    ve->limits[0][P_MODE] = 0;         ve->limits[1][P_MODE] = 2;         ve->defaults[P_MODE] = 1;
    ve->limits[0][P_MIX] = 0;          ve->limits[1][P_MIX] = 1000;       ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_EDGE_GAIN] = 0;    ve->limits[1][P_EDGE_GAIN] = 2000; ve->defaults[P_EDGE_GAIN] = 1000;
    ve->limits[0][P_CHROMA_EDGE] = 0;  ve->limits[1][P_CHROMA_EDGE] = 1000; ve->defaults[P_CHROMA_EDGE] = 0;

    ve->description = "Sobel";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Mode",
        "Opacity",
        "Edge Gain",
        "Chroma Edge"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Binary",
        "Gradient",
        "Wide Gradient"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_REJECT,                               VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_INTENSITY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 520,                2000,               16, 64,  500, 2200, 0,    96,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_REJECT,                               VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    (void) w;
    (void) h;

    return ve;
}

void *sobel_malloc(int w, int h)
{
    sobel_t *s = (sobel_t*) vj_calloc(sizeof(sobel_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;
    s->max_len = len;

    s->n_threads = vje_advise_num_threads(len);

    s->eff_initialized = 0;

    return (void*) s;
}

void sobel_free(void *ptr)
{
    sobel_t *s = (sobel_t*) ptr;

    free(s->buf[0]);
    free(s);
}

static void sobel_binary_3x3(sobel_t *s, VJFrame *frame, int threshold)
{
    const int width = frame->width;
    const int height = frame->height;
    const int t2 = threshold * threshold;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf[0];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++) {
            const int i = row + x;

            const int nw = B[i - width - 1];
            const int n  = B[i - width];
            const int ne = B[i - width + 1];
            const int w  = B[i - 1];
            const int e  = B[i + 1];
            const int sw = B[i + width - 1];
            const int s0 = B[i + width];
            const int se = B[i + width + 1];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const int mag2 = gx * gx + gy * gy;

            Y[i] = (mag2 > t2) ? pixel_Y_hi_ : pixel_Y_lo_;
        }
    }
}

static void sobel_gradient_3x3(sobel_t *s, VJFrame *frame, int threshold, int gain)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf[0];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 1; x < width - 1; x++) {
            const int i = row + x;

            const int nw = B[i - width - 1];
            const int n  = B[i - width];
            const int ne = B[i - width + 1];
            const int w  = B[i - 1];
            const int e  = B[i + 1];
            const int sw = B[i + width - 1];
            const int s0 = B[i + width];
            const int se = B[i + width + 1];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const int norm = sobel_norm_grad(gx, gy);

            Y[i] = (norm > threshold) ? sobel_u8((norm * gain + 500) / 1000) : pixel_Y_lo_;
        }
    }
}

static void sobel_gradient_wide(sobel_t *s, VJFrame *frame, int threshold, int gain)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict B = s->buf[0];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 2; y < height - 2; y++) {
        const int row = y * width;

        for(int x = 2; x < width - 2; x++) {
            const int i = row + x;

            const int nw = B[i - (2 * width) - 2];
            const int n  = B[i - (2 * width)];
            const int ne = B[i - (2 * width) + 2];
            const int w  = B[i - 2];
            const int e  = B[i + 2];
            const int sw = B[i + (2 * width) - 2];
            const int s0 = B[i + (2 * width)];
            const int se = B[i + (2 * width) + 2];

            const int gx = -nw + ne - (w << 1) + (e << 1) - sw + se;
            const int gy = -nw - (n << 1) - ne + sw + (s0 << 1) + se;
            const int norm = sobel_norm_grad(gx, gy);

            Y[i] = (norm > threshold) ? sobel_u8((norm * gain + 500) / 1000) : pixel_Y_lo_;
        }
    }
}

static void sobel_postprocess(sobel_t *s,
                              VJFrame *frame,
                              int mix,
                              int chroma_edge)
{
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int mix_q8 = clampi((mix * 256 + 500) / 1000, 0, 256);
    const int chroma_q8 = clampi((chroma_edge * 256 + 500) / 1000, 0, 256);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict srcY  = s->buf[0];
    const uint8_t *restrict srcCb = s->buf[1];
    const uint8_t *restrict srcCr = s->buf[2];

#pragma omp parallel num_threads(s->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            if(mix_q8 < 256)
                Y[i] = sobel_mix_y(srcY[i], Y[i], mix_q8);
        }

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            const int e = Y[i];
            const int q = (e * chroma_q8 + 127) / 255;

            if(q <= 0) {
                Cb[i] = 128;
                Cr[i] = 128;
            } else {
                Cb[i] = sobel_mix_uv(128, srcCb[i], q);
                Cr[i] = sobel_mix_uv(128, srcCr[i], q);
            }
        }
    }
}

void sobel_apply(void *ptr, VJFrame *frame, int *args)
{
    sobel_t *s = (sobel_t*) ptr;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    int threshold = args[P_THRESHOLD];
    const int mode = args[P_MODE];
    int mix = args[P_MIX];
    int gain = args[P_EDGE_GAIN];
    int chroma = args[P_CHROMA_EDGE];

    const float param_fast = 0.30f;
    const float param_slow = 0.085f;

    if(!s->eff_initialized) {
        s->eff_threshold = (float)threshold;
        s->eff_mix = (float)mix;
        s->eff_gain = (float)gain;
        s->eff_chroma = (float)chroma;
        s->eff_initialized = 1;
    } else {
        threshold    = sobel_smooth_i(&s->eff_threshold, threshold,    param_fast, param_slow);
        mix          = sobel_smooth_i(&s->eff_mix,       mix,          param_fast * 0.88f, param_slow);
        gain         = sobel_smooth_i(&s->eff_gain,      gain,         param_fast * 0.88f, param_slow);
        chroma       = sobel_smooth_i(&s->eff_chroma,    chroma,       param_fast * 0.80f, param_slow);
    }

    threshold = clampi(threshold, 0, 255);
    mix = clampi(mix, 0, 1000);
    gain = clampi(gain, 0, 2000);
    chroma = clampi(chroma, 0, 1000);

    uint8_t *restrict Y = frame->data[0];

    veejay_memcpy(s->buf[0], Y, len);

    veejay_memcpy(s->buf[1], frame->data[1], uv_len);
    veejay_memcpy(s->buf[2], frame->data[2], uv_len);

    veejay_memset(Y, pixel_Y_lo_, len);

    switch(mode) {
        case 0:
            sobel_binary_3x3(s, frame, threshold);
            break;
        case 1:
            sobel_gradient_3x3(s, frame, threshold, gain);
            break;
        case 2:
            if(width >= 5 && height >= 5)
                sobel_gradient_wide(s, frame, threshold, gain);
            else
                sobel_gradient_3x3(s, frame, threshold, gain);
            break;
        default:
            break;
    }

    sobel_postprocess(s, frame, mix, chroma);
}
