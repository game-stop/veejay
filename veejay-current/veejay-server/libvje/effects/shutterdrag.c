/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "shutterdrag.h"

#define FIXED_BITS 16
#define FIXED_ONE  (1 << FIXED_BITS)
#define SHUTTER_HISTORY_LEN 3

typedef struct {
    int width;
    int height;
    int pixels;

    int32_t *feedback_block;
    int32_t *feedbackY;
    int32_t *feedbackY_next;
    int32_t *feedbackU;
    int32_t *feedbackV;

    uint8_t *history_block;
    uint8_t *historyY;
    uint8_t *historyU;
    uint8_t *historyV;

    int history_len;
    int history_pos;
    int first_frame;
    int n_threads;
} shutterdrag_t;

static inline int shutter_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t shutter_u8(int v)
{
    return (uint8_t) shutter_clampi(v, 0, 255);
}

static inline int32_t shutter_pct_to_fp(int pct)
{
    return (int32_t)(((int64_t)pct * FIXED_ONE + 50) / 100);
}

static inline int32_t shutter_mix_fp(int64_t a, int64_t b, int32_t mix_b)
{
    return (int32_t)(((a * (FIXED_ONE - mix_b)) + (b * mix_b)) >> FIXED_BITS);
}

vj_effect *shutterdrag_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 15;
    ve->defaults[1] = 10;
    ve->defaults[2] = 0;
    ve->defaults[3] = 5;
    ve->defaults[4] = 140;
    ve->defaults[5] = 40;
    ve->defaults[6] = 255;
    ve->defaults[7] = 0;

    ve->limits[0][0] = 0;   ve->limits[1][0] = 100;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 100;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 100;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 511;
    ve->limits[0][7] = 0;   ve->limits[1][7] = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trail Strength",
        "Duration",
        "Loop",
        "Y Boost",
        "Sharpen",
        "Propagate",
        "Luma Ceiling",
        "Reset"
    );

    ve->description = "Shutter Drag";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS,                              4,                  86,                 10, 38, 1000, 2600, 0,   62,    /* Trail Strength */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_CONTINUOUS,                              0,                  78,                 8,  32, 1200, 3200, 0,   55,    /* Duration */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Loop */
        VJ_BEAT_GLOW,      VJ_BEAT_F_CONTINUOUS,                              0,                  42,                 10, 38, 1000, 2600, 0,   58,    /* Y Boost */
        VJ_BEAT_CONTRAST,  VJ_BEAT_F_CONTINUOUS,                              96,                 190,                8,  30, 1200, 3000, 0,   45,    /* Sharpen */
        VJ_BEAT_WARP,      VJ_BEAT_F_CONTINUOUS,                              0,                  78,                 10, 38, 1000, 2600, 0,   60,    /* Propagate */
        VJ_BEAT_CONTRAST,  VJ_BEAT_F_PHRASE_ONLY,                             180,                320,                6,  22, 1600, 3400, 700, 35,    /* Luma Ceiling */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000  /* Reset */
    );

    (void) w;
    (void) h;

    return ve;
}

void *shutterdrag_malloc(int width, int height)
{
    shutterdrag_t *sb = (shutterdrag_t*) vj_calloc(sizeof(shutterdrag_t));
    if(!sb)
        return NULL;

    const int pixels = width * height;
    const int hlen = SHUTTER_HISTORY_LEN;

    sb->width = width;
    sb->height = height;
    sb->pixels = pixels;
    sb->history_len = hlen;
    sb->history_pos = 0;
    sb->first_frame = 1;

    sb->feedback_block = (int32_t*) vj_malloc(sizeof(int32_t) * (size_t)pixels * 4u);
    if(!sb->feedback_block) {
        free(sb);
        return NULL;
    }

    sb->feedbackY      = sb->feedback_block;
    sb->feedbackY_next = sb->feedbackY + pixels;
    sb->feedbackU      = sb->feedbackY_next + pixels;
    sb->feedbackV      = sb->feedbackU + pixels;

    sb->history_block = (uint8_t*) vj_malloc((size_t)pixels * (size_t)hlen * 3u);
    if(!sb->history_block) {
        free(sb->feedback_block);
        free(sb);
        return NULL;
    }

    sb->historyY = sb->history_block;
    sb->historyU = sb->historyY + (pixels * hlen);
    sb->historyV = sb->historyU + (pixels * hlen);

    sb->n_threads = vje_advise_num_threads(pixels);
    if(sb->n_threads < 1)
        sb->n_threads = 1;

    return (void*) sb;
}

void shutterdrag_free(void *ptr)
{
    shutterdrag_t *sb = (shutterdrag_t*) ptr;
    if(!sb)
        return;

    if(sb->feedback_block)
        free(sb->feedback_block);

    if(sb->history_block)
        free(sb->history_block);

    free(sb);
}

static void shutterdrag_seed_state(shutterdrag_t *sb, VJFrame *frame)
{
    const int pixels = sb->pixels;
    const int hlen = sb->history_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int32_t *restrict fbY  = sb->feedbackY;
    int32_t *restrict fbYn = sb->feedbackY_next;
    int32_t *restrict fbU  = sb->feedbackU;
    int32_t *restrict fbV  = sb->feedbackV;

    uint8_t *restrict hY = sb->historyY;
    uint8_t *restrict hU = sb->historyU;
    uint8_t *restrict hV = sb->historyV;

#pragma omp parallel for schedule(static) num_threads(sb->n_threads)
    for(int i = 0; i < pixels; i++) {
        const int i3 = i * hlen;

        const int32_t yfp = (int32_t)Y[i] << FIXED_BITS;
        const int32_t ufp = ((int32_t)U[i] - 128) << FIXED_BITS;
        const int32_t vfp = ((int32_t)V[i] - 128) << FIXED_BITS;

        fbY[i] = yfp;
        fbYn[i] = yfp;
        fbU[i] = ufp;
        fbV[i] = vfp;

        for(int k = 0; k < hlen; k++) {
            hY[i3 + k] = Y[i];
            hU[i3 + k] = U[i];
            hV[i3 + k] = V[i];
        }
    }

    sb->history_pos = 0;
    sb->first_frame = 0;
}

void shutterdrag_apply(void *ptr, VJFrame *frame, int *args)
{
    shutterdrag_t *sb = (shutterdrag_t*) ptr;

    if(!sb || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int pixels = frame->len;
    const int hlen = sb->history_len;

    if(w <= 0 || h <= 0 || pixels <= 0)
        return;

    int trail_strength = shutter_clampi(args[0], 0, 100);
    int duration_mix   = shutter_clampi(args[1], 0, 100);
    int loop           = args[2] ? 1 : 0;
    int y_boost        = shutter_clampi(args[3], 0, 100);
    int sharpen        = shutter_clampi(args[4], 0, 255);
    int propagate      = shutter_clampi(args[5], 0, 100);
    int luma_ceiling   = shutter_clampi(args[6], 0, 511);
    int reset          = args[7] ? 1 : 0;

    if(sb->first_frame || reset)
        shutterdrag_seed_state(sb, frame);

    sb->history_pos = (sb->history_pos + 1) % hlen;
    const int pos = sb->history_pos;

    const int32_t alpha       = shutter_pct_to_fp(trail_strength);
    const int32_t history_mix = shutter_pct_to_fp(duration_mix);
    const int32_t decay       = loop ? FIXED_ONE : shutter_pct_to_fp(95);
    const int32_t boost_fp    = shutter_pct_to_fp(y_boost);
    const int32_t prop_fp     = shutter_pct_to_fp(propagate);
    const int64_t uv_limit    = (int64_t)127 << FIXED_BITS;
    const int64_t y_limit     = luma_ceiling > 0 ? ((int64_t)luma_ceiling << FIXED_BITS) : 0;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict hY = sb->historyY;
    uint8_t *restrict hU = sb->historyU;
    uint8_t *restrict hV = sb->historyV;

#pragma omp parallel for schedule(static) num_threads(sb->n_threads)
    for(int i = 0; i < pixels; i++) {
        const int i3 = i * hlen + pos;

        hY[i3] = Y[i];
        hU[i3] = U[i];
        hV[i3] = V[i];
    }

    int32_t *restrict fbY_old = sb->feedbackY;
    int32_t *restrict fbY_new = sb->feedbackY_next;

#pragma omp parallel for schedule(static) num_threads(sb->n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int i = row + x;
            const int i3 = i * hlen;

            const uint8_t cur = hY[i3 + pos];

            int64_t fb = shutter_mix_fp(fbY_old[i], (int64_t)cur << FIXED_BITS, alpha);

            fb += (fb * boost_fp) >> FIXED_BITS;
            fb = (fb * decay) >> FIXED_BITS;

            if(propagate > 0 && x > 0 && x < w - 1 && y > 0 && y < h - 1) {
                const int gx = (int)hY[(i + 1) * hlen + pos] - (int)hY[(i - 1) * hlen + pos];
                const int gy = (int)hY[(i + w) * hlen + pos] - (int)hY[(i - w) * hlen + pos];

                const int agx = gx < 0 ? -gx : gx;
                const int agy = gy < 0 ? -gy : gy;

                const int nidx = (agx > agy)
                    ? i + (gx < 0 ? -1 : 1)
                    : i + (gy < 0 ? -w : w);

                fb = shutter_mix_fp(fb, fbY_old[nidx], prop_fp);
            }

            if(y_limit > 0) {
                if(fb > y_limit)
                    fb = y_limit;
                else if(fb < 0)
                    fb = 0;
            } else if(fb < 0) {
                fb = 0;
            }

            fbY_new[i] = (int32_t)fb;

            const int hsum = (int)hY[i3] + (int)hY[i3 + 1] + (int)hY[i3 + 2];
            const int64_t hist_fp = ((int64_t)hsum << FIXED_BITS) / hlen;
            const int64_t out_fp = shutter_mix_fp(fb, hist_fp, history_mix);

            Y[i] = shutter_u8((int)((out_fp * sharpen) >> (FIXED_BITS + 7)));
        }
    }

    int32_t *tmpY = sb->feedbackY;
    sb->feedbackY = sb->feedbackY_next;
    sb->feedbackY_next = tmpY;

    int32_t *restrict fbU = sb->feedbackU;
    int32_t *restrict fbV = sb->feedbackV;

#pragma omp parallel for schedule(static) num_threads(sb->n_threads)
    for(int i = 0; i < pixels; i++) {
        const int i3 = i * hlen;

        int64_t fu = shutter_mix_fp(fbU[i], ((int64_t)((int)hU[i3 + pos] - 128)) << FIXED_BITS, alpha);
        int64_t fv = shutter_mix_fp(fbV[i], ((int64_t)((int)hV[i3 + pos] - 128)) << FIXED_BITS, alpha);

        fu = (fu * decay) >> FIXED_BITS;
        fv = (fv * decay) >> FIXED_BITS;

        if(fu > uv_limit) fu = uv_limit;
        else if(fu < -uv_limit) fu = -uv_limit;

        if(fv > uv_limit) fv = uv_limit;
        else if(fv < -uv_limit) fv = -uv_limit;

        fbU[i] = (int32_t)fu;
        fbV[i] = (int32_t)fv;

        const int hu_sum =
            (int)hU[i3] +
            (int)hU[i3 + 1] +
            (int)hU[i3 + 2] -
            (128 * hlen);

        const int hv_sum =
            (int)hV[i3] +
            (int)hV[i3 + 1] +
            (int)hV[i3 + 2] -
            (128 * hlen);

        const int64_t hu_fp = ((int64_t)hu_sum << FIXED_BITS) / hlen;
        const int64_t hv_fp = ((int64_t)hv_sum << FIXED_BITS) / hlen;

        const int64_t out_u = shutter_mix_fp(fu, hu_fp, history_mix);
        const int64_t out_v = shutter_mix_fp(fv, hv_fp, history_mix);

        U[i] = shutter_u8((int)((out_u * sharpen) >> (FIXED_BITS + 7)) + 128);
        V[i] = shutter_u8((int)((out_v * sharpen) >> (FIXED_BITS + 7)) + 128);
    }
}