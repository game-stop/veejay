/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "shutterdrag.h"

#define FIXED_BITS 16
#define FIXED_ONE  (1 << FIXED_BITS)
#define SHUTTER_HISTORY_LEN 3

#define SHUTTERDRAG_PARAMS 11

#define P_TRAIL_STRENGTH  0
#define P_DURATION        1
#define P_LOOP            2
#define P_Y_BOOST         3
#define P_SHARPEN         4
#define P_PROPAGATE       5
#define P_LUMA_CEILING    6
#define P_RESET           7
#define P_TRAIL_DRIVE     8
#define P_BOOST_DRIVE     9
#define P_PROPAGATE_DRIVE 10

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

    float sm_trail;
    float sm_duration;
    float sm_yboost;
    float sm_sharpen;
    float sm_propagate;
    float sm_ceiling;
    float sm_trail_drive;
    float sm_boost_drive;
    float sm_propagate_drive;

    int smooth_init;
    int history_len;
    int history_pos;
    int first_frame;
    int n_threads;
} shutterdrag_t;

static inline int shutter_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t shutter_u8(int v)
{
    return (uint8_t)shutter_clampi(v, 0, 255);
}

static inline int32_t shutter_pct_to_fp(int pct)
{
    return (int32_t)(((int64_t)pct * FIXED_ONE + 50) / 100);
}

static inline int32_t shutter_mix_fp(int64_t a, int64_t b, int32_t mix_b)
{
    return (int32_t)(((a * (FIXED_ONE - mix_b)) + (b * mix_b)) >> FIXED_BITS);
}


static inline float shutter_smooth_value(float cur, float target)
{
    const float k = target > cur ? 0.30f : 0.12f;

    return cur + (target - cur) * k;
}

static inline int shutter_round_clampi(float v, int lo, int hi)
{
    const int iv = (int)(v + (v >= 0.0f ? 0.5f : -0.5f));

    return shutter_clampi(iv, lo, hi);
}

vj_effect *shutterdrag_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SHUTTERDRAG_PARAMS;
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->limits[0] || !ve->limits[1] || !ve->defaults) {
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        if(ve->defaults)
            free(ve->defaults);
        free(ve);
        return NULL;
    }

    ve->defaults[P_TRAIL_STRENGTH] = 15;
    ve->defaults[P_DURATION] = 10;
    ve->defaults[P_LOOP] = 0;
    ve->defaults[P_Y_BOOST] = 5;
    ve->defaults[P_SHARPEN] = 140;
    ve->defaults[P_PROPAGATE] = 40;
    ve->defaults[P_LUMA_CEILING] = 255;
    ve->defaults[P_RESET] = 0;
    ve->defaults[P_TRAIL_DRIVE] = 0;
    ve->defaults[P_BOOST_DRIVE] = 0;
    ve->defaults[P_PROPAGATE_DRIVE] = 0;

    ve->limits[0][P_TRAIL_STRENGTH] = 0;    ve->limits[1][P_TRAIL_STRENGTH] = 100;
    ve->limits[0][P_DURATION] = 0;          ve->limits[1][P_DURATION] = 100;
    ve->limits[0][P_LOOP] = 0;              ve->limits[1][P_LOOP] = 1;
    ve->limits[0][P_Y_BOOST] = 0;           ve->limits[1][P_Y_BOOST] = 100;
    ve->limits[0][P_SHARPEN] = 0;           ve->limits[1][P_SHARPEN] = 255;
    ve->limits[0][P_PROPAGATE] = 0;         ve->limits[1][P_PROPAGATE] = 100;
    ve->limits[0][P_LUMA_CEILING] = 0;      ve->limits[1][P_LUMA_CEILING] = 511;
    ve->limits[0][P_RESET] = 0;             ve->limits[1][P_RESET] = 1;
    ve->limits[0][P_TRAIL_DRIVE] = 0;       ve->limits[1][P_TRAIL_DRIVE] = 1000;
    ve->limits[0][P_BOOST_DRIVE] = 0;       ve->limits[1][P_BOOST_DRIVE] = 1000;
    ve->limits[0][P_PROPAGATE_DRIVE] = 0;   ve->limits[1][P_PROPAGATE_DRIVE] = 1000;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trail Strength",
        "Duration",
        "Loop",
        "Y Boost",
        "Sharpen",
        "Propagate",
        "Luma Ceiling",
        "Reset",
        "Trail Drive",
        "Boost Drive",
        "Propagate Drive"
    );

    ve->description = "Shutter Drag";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_LOOP], P_LOOP, "Decay", "Loop");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_RESET], P_RESET, "Run", "Reset");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 8, 96, 64, 94, 220, 1600, 0, 1, 0, VJ_BEAT_COST_CHEAP, 80, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 6, 92, 78, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, 96, 80, 100, 4, 420, 20, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 80, 245, 68, 96, 100, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 10, 96, 86, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 220, 420, 62, 94, 220, 1400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 54, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 6, 420, 20, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 84, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void)w;
    (void)h;

    return ve;
}

void *shutterdrag_malloc(int width, int height)
{
    shutterdrag_t *sb = (shutterdrag_t*)vj_calloc(sizeof(shutterdrag_t));

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
    sb->smooth_init = 0;

    sb->feedback_block = (int32_t*)vj_malloc(sizeof(int32_t) * (size_t)pixels * 4u);

    if(!sb->feedback_block) {
        free(sb);
        return NULL;
    }

    sb->feedbackY = sb->feedback_block;
    sb->feedbackY_next = sb->feedbackY + pixels;
    sb->feedbackU = sb->feedbackY_next + pixels;
    sb->feedbackV = sb->feedbackU + pixels;

    sb->history_block = (uint8_t*)vj_malloc((size_t)pixels * (size_t)hlen * 3u);

    if(!sb->history_block) {
        free(sb->feedback_block);
        free(sb);
        return NULL;
    }

    sb->historyY = sb->history_block;
    sb->historyU = sb->historyY + (pixels * hlen);
    sb->historyV = sb->historyU + (pixels * hlen);
    sb->n_threads = vje_advise_num_threads(pixels);

    return (void*)sb;
}

void shutterdrag_free(void *ptr)
{
    shutterdrag_t *sb = (shutterdrag_t*)ptr;

    free(sb->feedback_block);
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

    int32_t *restrict fbY = sb->feedbackY;
    int32_t *restrict fbYn = sb->feedbackY_next;
    int32_t *restrict fbU = sb->feedbackU;
    int32_t *restrict fbV = sb->feedbackV;

    uint8_t *restrict hY = sb->historyY;
    uint8_t *restrict hU = sb->historyU;
    uint8_t *restrict hV = sb->historyV;

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

static void shutterdrag_update_controls(shutterdrag_t *sb,
                                        int *args,
                                        int *trail_strength,
                                        int *duration_mix,
                                        int *y_boost,
                                        int *sharpen,
                                        int *propagate,
                                        int *luma_ceiling)
{
    const int raw_trail = shutter_clampi(args[P_TRAIL_STRENGTH], 0, 100);
    const int raw_duration = shutter_clampi(args[P_DURATION], 0, 100);
    const int raw_yboost = shutter_clampi(args[P_Y_BOOST], 0, 100);
    const int raw_sharpen = shutter_clampi(args[P_SHARPEN], 0, 255);
    const int raw_propagate = shutter_clampi(args[P_PROPAGATE], 0, 100);
    const int raw_ceiling = shutter_clampi(args[P_LUMA_CEILING], 0, 511);
    const int raw_trail_drive = shutter_clampi(args[P_TRAIL_DRIVE], 0, 1000);
    const int raw_boost_drive = shutter_clampi(args[P_BOOST_DRIVE], 0, 1000);
    const int raw_propagate_drive = shutter_clampi(args[P_PROPAGATE_DRIVE], 0, 1000);

    if(!sb->smooth_init) {
        sb->sm_trail = (float)raw_trail;
        sb->sm_duration = (float)raw_duration;
        sb->sm_yboost = (float)raw_yboost;
        sb->sm_sharpen = (float)raw_sharpen;
        sb->sm_propagate = (float)raw_propagate;
        sb->sm_ceiling = (float)raw_ceiling;
        sb->sm_trail_drive = (float)raw_trail_drive;
        sb->sm_boost_drive = (float)raw_boost_drive;
        sb->sm_propagate_drive = (float)raw_propagate_drive;
        sb->smooth_init = 1;
    }
    else {
        sb->sm_trail = shutter_smooth_value(sb->sm_trail, (float)raw_trail);
        sb->sm_duration = shutter_smooth_value(sb->sm_duration, (float)raw_duration);
        sb->sm_yboost = shutter_smooth_value(sb->sm_yboost, (float)raw_yboost);
        sb->sm_sharpen = shutter_smooth_value(sb->sm_sharpen, (float)raw_sharpen);
        sb->sm_propagate = shutter_smooth_value(sb->sm_propagate, (float)raw_propagate);
        sb->sm_ceiling = shutter_smooth_value(sb->sm_ceiling, (float)raw_ceiling);
        sb->sm_trail_drive = shutter_smooth_value(sb->sm_trail_drive, (float)raw_trail_drive);
        sb->sm_boost_drive = shutter_smooth_value(sb->sm_boost_drive, (float)raw_boost_drive);
        sb->sm_propagate_drive = shutter_smooth_value(sb->sm_propagate_drive, (float)raw_propagate_drive);
    }

    const int trail_q = shutter_round_clampi(sb->sm_trail_drive, 0, 1000);
    const int boost_q = shutter_round_clampi(sb->sm_boost_drive, 0, 1000);
    const int prop_q = shutter_round_clampi(sb->sm_propagate_drive, 0, 1000);

    int tr = shutter_round_clampi(sb->sm_trail, 0, 100);
    int du = shutter_round_clampi(sb->sm_duration, 0, 100);
    int yb = shutter_round_clampi(sb->sm_yboost, 0, 100);
    int sh = shutter_round_clampi(sb->sm_sharpen, 0, 255);
    int pr = shutter_round_clampi(sb->sm_propagate, 0, 100);
    int lc = shutter_round_clampi(sb->sm_ceiling, 0, 511);

    tr = shutter_clampi(tr + (((100 - tr) * trail_q + 500) / 1000), 0, 100);
    du = shutter_clampi(du + (((100 - du) * trail_q + 500) / 1000), 0, 100);
    yb = shutter_clampi(yb + (((100 - yb) * boost_q + 500) / 1000), 0, 100);
    sh = shutter_clampi(sh + (((255 - sh) * boost_q + 500) / 1000), 0, 255);
    pr = shutter_clampi(pr + (((100 - pr) * prop_q + 500) / 1000), 0, 100);

    if(lc > 0) {
        const int ceiling_pull = ((lc * boost_q + 1000) / 2000);

        lc = shutter_clampi(lc - ceiling_pull, 16, 511);
    }

    *trail_strength = tr;
    *duration_mix = du;
    *y_boost = yb;
    *sharpen = sh;
    *propagate = pr;
    *luma_ceiling = lc;
}

void shutterdrag_apply(void *ptr, VJFrame *frame, int *args)
{
    shutterdrag_t *sb = (shutterdrag_t*)ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int pixels = frame->len;
    const int hlen = sb->history_len;

    int trail_strength = 0;
    int duration_mix = 0;
    int y_boost = 0;
    int sharpen = 0;
    int propagate = 0;
    int luma_ceiling = 0;

    const int loop = args[P_LOOP] ? 1 : 0;
    const int reset = args[P_RESET] ? 1 : 0;

    shutterdrag_update_controls(
        sb,
        args,
        &trail_strength,
        &duration_mix,
        &y_boost,
        &sharpen,
        &propagate,
        &luma_ceiling
    );

    if(sb->first_frame || reset)
        shutterdrag_seed_state(sb, frame);

    sb->history_pos = (sb->history_pos + 1) % hlen;
    const int pos = sb->history_pos;

    const int32_t alpha = shutter_pct_to_fp(trail_strength);
    const int32_t history_mix = shutter_pct_to_fp(duration_mix);
    const int32_t decay = loop ? FIXED_ONE : shutter_pct_to_fp(95);
    const int32_t boost_fp = shutter_pct_to_fp(y_boost);
    const int32_t prop_fp = shutter_pct_to_fp(propagate);
    const int64_t uv_limit = (int64_t)127 << FIXED_BITS;
    const int64_t y_limit = luma_ceiling > 0 ? ((int64_t)luma_ceiling << FIXED_BITS) : 0;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict hY = sb->historyY;
    uint8_t *restrict hU = sb->historyU;
    uint8_t *restrict hV = sb->historyV;

    int32_t *restrict fbY_old = sb->feedbackY;
    int32_t *restrict fbY_new = sb->feedbackY_next;
    int32_t *restrict fbU = sb->feedbackU;
    int32_t *restrict fbV = sb->feedbackV;

#pragma omp parallel num_threads(sb->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < pixels; i++) {
            const int i3 = i * hlen + pos;

            hY[i3] = Y[i];
            hU[i3] = U[i];
            hV[i3] = V[i];
        }

#pragma omp for schedule(static)
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
                }
                else if(fb < 0) {
                    fb = 0;
                }

                fbY_new[i] = (int32_t)fb;

                const int hsum = (int)hY[i3] + (int)hY[i3 + 1] + (int)hY[i3 + 2];
                const int64_t hist_fp = ((int64_t)hsum << FIXED_BITS) / hlen;
                const int64_t out_fp = shutter_mix_fp(fb, hist_fp, history_mix);

                Y[i] = shutter_u8((int)((out_fp * sharpen) >> (FIXED_BITS + 7)));
            }
        }

#pragma omp single
        {
            int32_t *tmpY = sb->feedbackY;
            sb->feedbackY = sb->feedbackY_next;
            sb->feedbackY_next = tmpY;
        }

#pragma omp for schedule(static)
        for(int i = 0; i < pixels; i++) {
            const int i3 = i * hlen;
            int64_t fu = shutter_mix_fp(fbU[i], ((int64_t)((int)hU[i3 + pos] - 128)) << FIXED_BITS, alpha);
            int64_t fv = shutter_mix_fp(fbV[i], ((int64_t)((int)hV[i3 + pos] - 128)) << FIXED_BITS, alpha);

            fu = (fu * decay) >> FIXED_BITS;
            fv = (fv * decay) >> FIXED_BITS;

            if(fu > uv_limit)
                fu = uv_limit;
            else if(fu < -uv_limit)
                fu = -uv_limit;

            if(fv > uv_limit)
                fv = uv_limit;
            else if(fv < -uv_limit)
                fv = -uv_limit;

            fbU[i] = (int32_t)fu;
            fbV[i] = (int32_t)fv;

            const int hu_sum = (int)hU[i3] + (int)hU[i3 + 1] + (int)hU[i3 + 2] - (128 * hlen);
            const int hv_sum = (int)hV[i3] + (int)hV[i3 + 1] + (int)hV[i3 + 2] - (128 * hlen);
            const int64_t hu_fp = ((int64_t)hu_sum << FIXED_BITS) / hlen;
            const int64_t hv_fp = ((int64_t)hv_sum << FIXED_BITS) / hlen;
            const int64_t out_u = shutter_mix_fp(fu, hu_fp, history_mix);
            const int64_t out_v = shutter_mix_fp(fv, hv_fp, history_mix);

            U[i] = shutter_u8((int)((out_u * sharpen) >> (FIXED_BITS + 7)) + 128);
            V[i] = shutter_u8((int)((out_v * sharpen) >> (FIXED_BITS + 7)) + 128);
        }
    }
}

