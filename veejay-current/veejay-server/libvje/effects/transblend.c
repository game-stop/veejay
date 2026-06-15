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

#include <libvje/effects/common.h>
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
    int n_threads;
    int w;
    int h;

    float speed_env;
    float expand_env;
    float glow_env;
} wipe_t;

static inline int transblend_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
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

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

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

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  max_speed,          12, 46,  600, 2400, 0,    82,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,   0,    0,    -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               16, 62,  500, 2200, 0,    94,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               14, 54,  500, 2200, 0,    86
    );

    return ve;
}

static void transblend_build_angle_lut(wipe_t *wipe, int w, int h)
{
    const float cx = ((float)w - 1.0f) * 0.5f;
    const float cy = ((float)h - 1.0f) * 0.5f;
    const float scale = 65535.0f / (float)(2.0 * M_PI);

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
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

    wipe->n_threads = vje_advise_num_threads(len);

    transblend_build_angle_lut(wipe, w, h);

    return wipe;
}

void transblend_free(void *ptr)
{
    wipe_t *wipe = (wipe_t*) ptr;

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

    const float fast = 0.24f;

    wipe->speed_env = transblend_smooth(wipe->speed_env, (float)speed_arg, fast);
    wipe->expand_env = transblend_smooth(wipe->expand_env, (float)expand_drive_arg, fast * 0.82f);
    wipe->glow_env = transblend_smooth(wipe->glow_env, (float)edge_glow_arg, fast * 0.88f);

    const float expand_t = wipe->expand_env * 0.001f;

    int speed_eff = (int)(wipe->speed_env + 0.5f);
    speed_eff += (int)((float)max_speed * expand_t * 0.045f + 0.5f);
    speed_eff = transblend_clampi(speed_eff, 0, max_speed);

    transblend_step(wipe, speed_eff, bounce, width, height);

    int expand_q16 = (int)(wipe->expand_env * 42.0f + 0.5f);
    expand_q16 = transblend_clampi(expand_q16, 0, 32768);

    int progress_eff = wipe->progress_q16 + expand_q16;
    if(progress_eff > 65535)
        progress_eff = 65535;

    int glow_width = 0;
    int glow_strength = 0;

    if(wipe->glow_env > 0.5f) {
        glow_width = 96 + (int)(wipe->glow_env * 9.0f + expand_t * 1800.0f + 0.5f);
        glow_width = transblend_clampi(glow_width, 1, 8192);

        glow_strength = (int)(wipe->glow_env * 0.135f + expand_t * 36.0f + 0.5f);
        glow_strength = transblend_clampi(glow_strength, 0, 180);
    }

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict U  = frame->data[1];
    uint8_t *restrict V  = frame->data[2];

    const uint8_t *restrict Y2 = frame2->data[0];
    const uint8_t *restrict U2 = frame2->data[1];
    const uint8_t *restrict V2 = frame2->data[2];

    const uint16_t *restrict angle = wipe->angle_lut;
    const uint16_t progress = (uint16_t)progress_eff;

    if(glow_width <= 0 || glow_strength <= 0) {
#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
        for(int i = 0; i < len; i++) {
            if(angle[i] <= progress) {
                Y[i] = Y2[i];
                U[i] = U2[i];
                V[i] = V2[i];
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
        for(int i = 0; i < len; i++) {
            const int a = (int)angle[i];

            if(a <= progress) {
                Y[i] = Y2[i];
                U[i] = U2[i];
                V[i] = V2[i];
            }

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
