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
#include "vbar.h"

#define VBAR_PARAMS       9

#define P_DIVIDER         0
#define P_TOP_Y           1
#define P_BOT_Y           2
#define P_TOP_X           3
#define P_BOT_X           4
#define P_BEAT_SLIDE      5
#define P_EDGE_GLOW       6
#define P_BEAT_PUSH       7
#define P_BEAT_SMOOTH     8

typedef struct {
    int bar_top_auto;
    int bar_bot_auto;
    int bar_top_vert;
    int bar_bot_vert;

    float beat_env;
    float beat_kick;
    float beat_prev;

    int n_threads;
} vbar_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t clamp_u8(int v)
{
    if((unsigned int)v > 255U)
        return (v < 0) ? 0 : 255;

    return (uint8_t)v;
}

static inline int wrapi(int v, int max)
{
    if(max <= 0)
        return 0;

    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline int wrap_add(int cur, int delta, int max)
{
    if(max <= 0)
        return 0;

    return wrapi(cur + delta, max);
}

static inline int vbar_beat_shape_q8(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    const int shaped = (beat_push * 30 + sq * 70 + 50) / 100;

    return (shaped * 255 + 500) / 1000;
}

static void vbar_update_beat(vbar_t *vbar, int beat_push, int beat_smooth)
{
    const int drive_q8 = vbar_beat_shape_q8(beat_push);
    const float target = (float)drive_q8 * (1.0f / 255.0f);
    const float smooth = (float)clampi(beat_smooth, 0, 1000) * 0.001f;

    const float attack = 0.52f - smooth * 0.34f;
    const float release = 0.16f - smooth * 0.12f;
    const float coef = (target > vbar->beat_env) ? attack : release;

    float delta = target - vbar->beat_prev;
    if(delta < 0.0f)
        delta = 0.0f;

    vbar->beat_env += (target - vbar->beat_env) * coef;

    if(vbar->beat_env < 0.0001f)
        vbar->beat_env = 0.0f;
    else if(vbar->beat_env > 1.0f)
        vbar->beat_env = 1.0f;

    if(delta > vbar->beat_kick)
        vbar->beat_kick = delta;
    else
        vbar->beat_kick *= 0.68f + smooth * 0.18f;

    if(vbar->beat_kick < 0.0001f)
        vbar->beat_kick = 0.0f;
    else if(vbar->beat_kick > 1.0f)
        vbar->beat_kick = 1.0f;

    vbar->beat_prev = target;
}

vj_effect *vbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = VBAR_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_w = width  > 0 ? width  : 1;
    int max_h = height > 0 ? height : 1;

    ve->defaults[P_DIVIDER]     = 4;
    ve->defaults[P_TOP_Y]       = 1;
    ve->defaults[P_BOT_Y]       = 3;
    ve->defaults[P_TOP_X]       = 0;
    ve->defaults[P_BOT_X]       = 0;
    ve->defaults[P_BEAT_SLIDE]  = 220;
    ve->defaults[P_EDGE_GLOW]   = 0;
    ve->defaults[P_BEAT_PUSH]   = 0;
    ve->defaults[P_BEAT_SMOOTH] = 420;

    ve->limits[0][P_DIVIDER]     = 1; ve->limits[1][P_DIVIDER]     = max_w;
    ve->limits[0][P_TOP_Y]       = 0; ve->limits[1][P_TOP_Y]       = max_h;
    ve->limits[0][P_BOT_Y]       = 0; ve->limits[1][P_BOT_Y]       = max_h;
    ve->limits[0][P_TOP_X]       = 0; ve->limits[1][P_TOP_X]       = max_w;
    ve->limits[0][P_BOT_X]       = 0; ve->limits[1][P_BOT_X]       = max_w;
    ve->limits[0][P_BEAT_SLIDE]  = 0; ve->limits[1][P_BEAT_SLIDE]  = 1000;
    ve->limits[0][P_EDGE_GLOW]   = 0; ve->limits[1][P_EDGE_GLOW]   = 1000;
    ve->limits[0][P_BEAT_PUSH]   = 0; ve->limits[1][P_BEAT_PUSH]   = 1000;
    ve->limits[0][P_BEAT_SMOOTH] = 0; ve->limits[1][P_BEAT_SMOOTH] = 1000;

    ve->description = "Vertical Sliding Bars";
    ve->sub_format = 1;
    ve->parallel = 0;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Divider",
        "Top Y",
        "Bot Y",
        "Top X",
        "Bot X",
        "Beat Slide",
        "Edge Glow",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  max_w > 32 ? 32 : max_w, 6,  22, 2200, 5200, 1800, 22,    /* Divider */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                       0,                  max_h > 72 ? 72 : max_h, 8,  30, 1000, 2600, 0,    42,    /* Top Y */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                       0,                  max_h > 72 ? 72 : max_h, 8,  30, 1000, 2600, 0,    42,    /* Bot Y */
        VJ_BEAT_DRIFT,     VJ_BEAT_F_CONTINUOUS,                       0,                  max_w > 72 ? 72 : max_w, 8,  30, 1000, 2600, 0,    34,    /* Top X */
        VJ_BEAT_DRIFT,     VJ_BEAT_F_CONTINUOUS,                       0,                  max_w > 72 ? 72 : max_w, 8,  30, 1000, 2600, 0,    34,    /* Bot X */
        VJ_BEAT_SPEED,     VJ_BEAT_F_REJECT,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,    0,  0,  0,    0,    0,    -1000, /* Beat Slide */
        VJ_BEAT_GLOW,      VJ_BEAT_F_CONTINUOUS,                       0,                  620,                8,  30, 1000, 2600, 0,    34,    /* Edge Glow */
        VJ_BEAT_KICK,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,   0,                  760,                16, 72, 80,   720,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY,                      220,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    return ve;
}

void *vbar_malloc(int w, int h)
{
    vbar_t *v = (vbar_t*) vj_calloc(sizeof(vbar_t));
    if(!v)
        return NULL;

    v->bar_top_auto = 0;
    v->bar_bot_auto = 0;
    v->bar_top_vert = 0;
    v->bar_bot_vert = 0;
    v->beat_env = 0.0f;
    v->beat_kick = 0.0f;
    v->beat_prev = 0.0f;

    v->n_threads = vje_advise_num_threads(w * h);
    if(v->n_threads < 1)
        v->n_threads = 1;

    return (void*) v;
}

void vbar_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

static void vbar_copy_region(VJFrame *frame,
                             VJFrame *frame2,
                             int x0,
                             int x1,
                             int y_off,
                             int x_off,
                             int n_threads)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    x0 = clampi(x0, 0, width);
    x1 = clampi(x1, 0, width);

    if(x1 <= x0)
        return;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        const int dst_row = y * width;
        const int src_y = wrapi(y + y_off, height);
        const int src_row = src_y * width;

        for(int x = x0; x < x1; x++) {
            const int src_x = wrapi(x + x_off, width);
            const int dst = dst_row + x;
            const int src = src_row + src_x;

            Y[dst]  = Y2[src];
            Cb[dst] = Cb2[src];
            Cr[dst] = Cr2[src];
        }
    }
}

static void vbar_apply_divider_glow(VJFrame *frame,
                                    int divider_x,
                                    int glow_width,
                                    int glow_strength,
                                    int n_threads)
{
    if(!frame || !frame->data[0] || glow_width <= 0 || glow_strength <= 0)
        return;

    const int width = frame->width;
    const int height = frame->height;

    if(width <= 0 || height <= 0 || divider_x <= 0 || divider_x >= width)
        return;

    const int x0 = clampi(divider_x - glow_width, 0, width);
    const int x1 = clampi(divider_x + glow_width + 1, 0, width);

    uint8_t *restrict Y = frame->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = x0; x < x1; x++) {
            int d = x - divider_x;
            if(d < 0)
                d = -d;

            int q = glow_width + 1 - d;
            if(q <= 0)
                continue;

            const int add = (glow_strength * q) / (glow_width + 1);
            const int idx = row + x;

            Y[idx] = clamp_u8((int)Y[idx] + add);
        }
    }
}

void vbar_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    vbar_t *vbar = (vbar_t*) ptr;

    if(!vbar || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int divider = clampi(args[P_DIVIDER], 1, width);
    const int top_y_delta = clampi(args[P_TOP_Y], 0, height);
    const int bot_y_delta = clampi(args[P_BOT_Y], 0, height);
    const int top_x_delta = clampi(args[P_TOP_X], 0, width);
    const int bot_x_delta = clampi(args[P_BOT_X], 0, width);
    const int beat_slide = clampi(args[P_BEAT_SLIDE], 0, 1000);
    const int edge_glow = clampi(args[P_EDGE_GLOW], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    vbar_update_beat(vbar, beat_push, beat_smooth);

    const float beat_drive = vbar->beat_env * vbar->beat_env;
    const float beat_kick = vbar->beat_kick;
    const int slide_limit_x = width > height ? width : height;
    const int beat_extra = (int)(((beat_drive * 0.080f) + (beat_kick * 0.150f)) * (float)beat_slide + 0.5f);
    const int max_extra = slide_limit_x >> 2;
    const int extra = beat_extra > max_extra ? max_extra : beat_extra;

    const int extra_y = height > 0 ? clampi(extra, 0, height) : 0;
    const int extra_x = width  > 0 ? clampi(extra, 0, width)  : 0;

    const int top_y_eff = top_y_delta + extra_y;
    const int bot_y_eff = bot_y_delta + (extra_y >> 1);
    const int top_x_eff = top_x_delta + extra_x;
    const int bot_x_eff = bot_x_delta - extra_x;

    const int left_width = width / divider;

    vbar->bar_top_auto = wrap_add(vbar->bar_top_auto, top_y_eff, height);
    vbar->bar_top_vert = wrap_add(vbar->bar_top_vert, top_x_eff, width);
    vbar->bar_bot_auto = wrap_add(vbar->bar_bot_auto, bot_y_eff, height);
    vbar->bar_bot_vert = wrap_add(vbar->bar_bot_vert, bot_x_eff, width);

    vbar_copy_region(
        frame,
        frame2,
        0,
        left_width,
        vbar->bar_top_auto,
        vbar->bar_top_vert,
        vbar->n_threads
    );

    vbar_copy_region(
        frame,
        frame2,
        left_width,
        width,
        vbar->bar_bot_auto,
        vbar->bar_bot_vert,
        vbar->n_threads
    );

    if(edge_glow > 0 || beat_push > 0) {
        const int glow_width = 1 + ((edge_glow * 7 + 500) / 1000) + (int)(beat_drive * 5.0f + beat_kick * 3.0f);
        int glow_strength = (edge_glow * 90 + 500) / 1000;

        glow_strength += (int)(beat_drive * 62.0f + beat_kick * 42.0f);
        glow_strength = clampi(glow_strength, 0, 180);

        vbar_apply_divider_glow(frame, left_width, glow_width, glow_strength, vbar->n_threads);
    }
}
