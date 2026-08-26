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
#include "transblend.h"
#include <libvje/internal.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TRANSBLEND_PARAMS 4

#define P_SPEED        0
#define P_BOUNCE       1
#define P_EXPAND_DRIVE 2
#define P_EDGE_GLOW    3

typedef struct {
    uint16_t *angle_lut;
    int progress_q16;
    int direction;
    int w;
    int h;

    float speed_env;
    float expand_env;
    float glow_env;
} wipe_t;

static inline int transblend_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline uint8_t transblend_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : (v > 255 ? 255 : v));
}

static inline uint8_t transblend_blend_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = transblend_clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline float transblend_smooth(float oldv, float target, float amount)
{
    return oldv + (target - oldv) * amount;
}

vj_effect *transblend_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRANSBLEND_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_speed = (width > height) ? width : height;

    ve->defaults[P_SPEED]        = 1;
    ve->defaults[P_BOUNCE]       = 1;
    ve->defaults[P_EXPAND_DRIVE] = 0;
    ve->defaults[P_EDGE_GLOW]    = 0;

    ve->limits[0][P_SPEED]        = 0; ve->limits[1][P_SPEED]        = max_speed;
    ve->limits[0][P_BOUNCE]       = 0; ve->limits[1][P_BOUNCE]       = 1;
    ve->limits[0][P_EXPAND_DRIVE] = 0; ve->limits[1][P_EXPAND_DRIVE] = 1000;
    ve->limits[0][P_EDGE_GLOW]    = 0; ve->limits[1][P_EDGE_GLOW]    = 1000;

    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->description = "Transition Wipe Clockwise";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Bounce",
        "Expand Drive",
        "Edge Glow"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_BOUNCE],
        P_BOUNCE,
        "Loop",
        "Bounce"
    );

    int speed_hi = max_speed;
    if(speed_hi > 180)
        speed_hi = 180;
    if(speed_hi < 1)
        speed_hi = 1;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, speed_hi, 96, 100, 8, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 94, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 94, 100, 4, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void transblend_build_angle_lut(wipe_t *wipe, int w, int h)
{
    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;
    const float scale = 65535.0f / (float)(2.0 * M_PI);

    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const float dy = (float)y - cy;

        for(int x = 0; x < w; x++) {
            const float dx = (float)x - cx;

            float a = atan2f(dx, -dy);
            if(a < 0.0f)
                a += (float)(2.0 * M_PI);

            wipe->angle_lut[row + x] = (uint16_t)(a * scale + 0.5f);
        }
    }

    wipe->w = w;
    wipe->h = h;
}

void *transblend_malloc(int w, int h)
{
    wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!wipe)
        return NULL;

    const int len = w * h;

    wipe->angle_lut = (uint16_t*) vj_malloc(sizeof(uint16_t) * (size_t)len);
    if(!wipe->angle_lut) {
        free(wipe);
        return NULL;
    }

    wipe->progress_q16 = 0;
    wipe->direction = 1;
    wipe->speed_env = 1.0f;
    wipe->expand_env = 0.0f;
    wipe->glow_env = 0.0f;

    transblend_build_angle_lut(wipe, w, h);

    return wipe;
}

void transblend_free(void *ptr)
{
    wipe_t *wipe = (wipe_t*) ptr;
    if(!wipe)
        return;

    if(wipe->angle_lut)
        free(wipe->angle_lut);
    free(wipe);
}

static void transblend_step(wipe_t *wipe, int speed, int bounce, int w, int h)
{
    const int max_speed = transblend_clampi((w > h) ? w : h, 1, 65535);
    int step = (speed * 65535) / max_speed;

    if(speed > 0 && step < 1)
        step = 1;

    if(bounce) {
        wipe->progress_q16 += step * wipe->direction;

        if(wipe->progress_q16 >= 65535) {
            wipe->progress_q16 = 65535;
            wipe->direction = -1;
        } else if(wipe->progress_q16 <= 0) {
            wipe->progress_q16 = 0;
            wipe->direction = 1;
        }
    } else {
        wipe->progress_q16 += step;

        while(wipe->progress_q16 > 65535)
            wipe->progress_q16 -= 65536;

        wipe->direction = 1;
    }
}

void transblend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int max_speed = (width > height) ? width : height;
    const int speed_arg = transblend_clampi(args[P_SPEED], 0, max_speed);
    const int bounce = args[P_BOUNCE] ? 1 : 0;
    const int expand_drive_arg = args[P_EXPAND_DRIVE];
    const int edge_glow_arg = args[P_EDGE_GLOW];

    int speed_eff = 0, progress_eff = 0, glow_width = 0, glow_strength = 0;
    uint8_t *Y = NULL, *U = NULL, *V = NULL;
    const uint8_t *Y2 = NULL, *U2 = NULL, *V2 = NULL;
    const uint16_t *angle = NULL;
    uint16_t progress = 0;

    #pragma omp single copyprivate(speed_eff, progress_eff, glow_width, glow_strength, Y, U, V, Y2, U2, V2, angle, progress)
    {
        const float fast = 0.24f;

        wipe->speed_env = transblend_smooth(wipe->speed_env, (float)speed_arg, fast);
        wipe->expand_env = transblend_smooth(wipe->expand_env, (float)expand_drive_arg, fast * 0.82f);
        wipe->glow_env = transblend_smooth(wipe->glow_env, (float)edge_glow_arg, fast * 0.88f);

        const float expand_t = wipe->expand_env * 0.001f;

        int s_eff = (int)(wipe->speed_env + 0.5f);
        s_eff += (int)((float)max_speed * expand_t * 0.045f + 0.5f);
        speed_eff = transblend_clampi(s_eff, 0, max_speed);

        transblend_step(wipe, speed_eff, bounce, width, height);

        const int expand_q16 = transblend_clampi((int)(wipe->expand_env * 42.0f + 0.5f), 0, 32768);

        int p_eff = wipe->progress_q16 + expand_q16;
        if(p_eff > 65535)
            p_eff = 65535;
        progress_eff = p_eff;

        if(wipe->glow_env > 0.5f) {
            int gw = 96 + (int)(wipe->glow_env * 9.0f + expand_t * 1800.0f + 0.5f);
            glow_width = transblend_clampi(gw, 1, 8192);

            int gs = (int)(wipe->glow_env * 0.135f + expand_t * 36.0f + 0.5f);
            glow_strength = transblend_clampi(gs, 0, 180);
        }

        Y  = frame->data[0];
        U  = frame->data[1];
        V  = frame->data[2];

        Y2 = frame2->data[0];
        U2 = frame2->data[1];
        V2 = frame2->data[2];

        angle = wipe->angle_lut;
        progress = (uint16_t)progress_eff;
    }

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        const int a = (int)angle[i];

        if(a <= progress) {
            Y[i] = Y2[i];
            U[i] = U2[i];
            V[i] = V2[i];
        }

        if(glow_width > 0 && glow_strength > 0) {
            int d = a - (int)progress;
            if(d < 0)
                d = -d;

            if(d < glow_width) {
                const int q = ((glow_width - d) * 256 + (glow_width >> 1)) / glow_width;
                const int add = (glow_strength * q + 128) >> 8;
                const int mix = q >> 2;

                Y[i] = transblend_u8((int)Y[i] + add);
                U[i] = transblend_blend_u8(U[i], U2[i], mix);
                V[i] = transblend_blend_u8(V[i], V2[i], mix);
            }
        }
    }
}