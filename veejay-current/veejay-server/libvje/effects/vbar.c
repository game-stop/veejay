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
#include <veejaycore/vjmem.h>
#include "vbar.h"

#define VBAR_PARAMS       7

#define P_DIVIDER         0
#define P_TOP_Y           1
#define P_BOT_Y           2
#define P_TOP_X           3
#define P_BOT_X           4
#define P_SLIDE_DRIVE     5
#define P_EDGE_GLOW       6

typedef struct {
    int bar_top_auto;
    int bar_bot_auto;
    int bar_top_vert;
    int bar_bot_vert;

    float top_y_env;
    float bot_y_env;
    float top_x_env;
    float bot_x_env;
    float slide_env;
    float glow_env;

    float slide_phase;

    int initialized;
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
    v %= max;

    if(v < 0)
        v += max;

    return v;
}

static inline int wrap_add(int cur, int delta, int max)
{
    return wrapi(cur + delta, max);
}

static inline float vbar_smooth(float oldv, float target, float attack, float release)
{
    const float c = target > oldv ? attack : release;
    return oldv + (target - oldv) * c;
}

static inline int vbar_tri_signed_q8(int phase)
{
    int p = phase & 1023;
    int tri = p < 512 ? p : 1023 - p;

    return tri - 256;
}





vj_effect *vbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = VBAR_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int max_w = width;
    int max_h = height;

    ve->defaults[P_DIVIDER]     = 4;
    ve->defaults[P_TOP_Y]       = 1;
    ve->defaults[P_BOT_Y]       = 3;
    ve->defaults[P_TOP_X]       = 0;
    ve->defaults[P_BOT_X]       = 0;
    ve->defaults[P_SLIDE_DRIVE] = 0;
    ve->defaults[P_EDGE_GLOW]   = 0;

    ve->limits[0][P_DIVIDER]     = 1; ve->limits[1][P_DIVIDER]     = max_w;
    ve->limits[0][P_TOP_Y]       = 0; ve->limits[1][P_TOP_Y]       = max_h;
    ve->limits[0][P_BOT_Y]       = 0; ve->limits[1][P_BOT_Y]       = max_h;
    ve->limits[0][P_TOP_X]       = 0; ve->limits[1][P_TOP_X]       = max_w;
    ve->limits[0][P_BOT_X]       = 0; ve->limits[1][P_BOT_X]       = max_w;
    ve->limits[0][P_SLIDE_DRIVE] = 0; ve->limits[1][P_SLIDE_DRIVE] = 1000;
    ve->limits[0][P_EDGE_GLOW]   = 0; ve->limits[1][P_EDGE_GLOW]   = 1000;

    ve->description = "Vertical Sliding Bars";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Divider",
        "Top Y",
        "Bot Y",
        "Top X",
        "Bot X",
        "Slide Drive",
        "Edge Glow"
    );
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, (max_h < 64 ? max_h : 64), 70, 96, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, (max_h < 64 ? max_h : 64), 68, 96, 100, 820, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, (max_w < 64 ? max_w : 64), 84, 100, 8, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, (max_w < 64 ? max_w : 64), 64, 92, 60, 620, 0, 1, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 96, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 4, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

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
    v->top_y_env = 0.0f;
    v->bot_y_env = 0.0f;
    v->top_x_env = 0.0f;
    v->bot_x_env = 0.0f;
    v->slide_env = 0.0f;
    v->glow_env = 0.0f;
    v->slide_phase = 0.0f;
    v->initialized = 0;

    v->n_threads = vje_advise_num_threads(w * h);

    return (void*) v;
}

void vbar_free(void *ptr)
{
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
    const int width = frame->width;
    const int height = frame->height;

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

    const int width = frame->width;
    const int height = frame->height;

    const int divider = args[P_DIVIDER];
    const int top_y_delta = args[P_TOP_Y];
    const int bot_y_delta = args[P_BOT_Y];
    const int top_x_delta = args[P_TOP_X];
    const int bot_x_delta = args[P_BOT_X];
    const int slide_drive = args[P_SLIDE_DRIVE];
    const int edge_glow = args[P_EDGE_GLOW];

    const float fast = 0.245f;
    const float slow = 0.112f;

    if(!vbar->initialized) {
        vbar->top_y_env = (float)top_y_delta;
        vbar->bot_y_env = (float)bot_y_delta;
        vbar->top_x_env = (float)top_x_delta;
        vbar->bot_x_env = (float)bot_x_delta;
        vbar->slide_env = (float)slide_drive;
        vbar->glow_env = (float)edge_glow;
        vbar->initialized = 1;
    } else {
        vbar->top_y_env = vbar_smooth(vbar->top_y_env, (float)top_y_delta, fast, slow);
        vbar->bot_y_env = vbar_smooth(vbar->bot_y_env, (float)bot_y_delta, fast, slow);
        vbar->top_x_env = vbar_smooth(vbar->top_x_env, (float)top_x_delta, fast * 0.90f, slow);
        vbar->bot_x_env = vbar_smooth(vbar->bot_x_env, (float)bot_x_delta, fast * 0.90f, slow);
        vbar->slide_env = vbar_smooth(vbar->slide_env, (float)slide_drive, fast * 1.18f, slow);
        vbar->glow_env = vbar_smooth(vbar->glow_env, (float)edge_glow, fast, slow);
    }

    const int top_y_base = clampi((int)(vbar->top_y_env + 0.5f), 0, height);
    const int bot_y_base = clampi((int)(vbar->bot_y_env + 0.5f), 0, height);
    const int top_x_base = clampi((int)(vbar->top_x_env + 0.5f), 0, width);
    const int bot_x_base = clampi((int)(vbar->bot_x_env + 0.5f), 0, width);
    const int slide_q = clampi((int)(vbar->slide_env + 0.5f), 0, 1000);
    const int glow_q = clampi((int)(vbar->glow_env + 0.5f), 0, 1000);

    const int slide_limit = width > height ? width : height;
    const int slide_depth_q = slide_q;

    if(slide_depth_q > 0) {
        vbar->slide_phase += 2.0f + (float)slide_depth_q * 0.072f;
        if(vbar->slide_phase > 8192.0f)
            vbar->slide_phase -= 8192.0f;
    }

    const int phase_i = (int)vbar->slide_phase;
    const int wave_x = vbar_tri_signed_q8(phase_i);
    const int wave_y = vbar_tri_signed_q8(phase_i + 256);

    const int slide_amp = clampi((slide_limit * slide_depth_q + 9000) / 18000, 0, slide_limit >> 2);

    const int lfo_x = (slide_amp * wave_x) >> 8;
    const int lfo_y = (slide_amp * wave_y) >> 8;

    const int top_y_eff = top_y_base + lfo_y;
    const int bot_y_eff = bot_y_base - (lfo_y >> 1);
    const int top_x_eff = top_x_base + lfo_x;
    const int bot_x_eff = bot_x_base - lfo_x;

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

    if(glow_q > 0) {
        const int glow_width = 1 + ((glow_q * 13 + 500) / 1000);
        int glow_strength = (glow_q * 150 + 500) / 1000;

        glow_strength = clampi(glow_strength, 0, 240);

        vbar_apply_divider_glow(frame, left_width, glow_width, glow_strength, vbar->n_threads);
    }
}
