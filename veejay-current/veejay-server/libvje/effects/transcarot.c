/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include "transcarot.h"

#define TRANSCAROT_PARAMS 4

#define P_SPEED        0
#define P_MODE         1
#define P_EXPAND_DRIVE 2
#define P_EDGE_GLOW    3

typedef struct {
    int diagonal_pos;

    int box_x;
    int box_y;
    int box_dir_x;
    int box_dir_y;

    float speed_env;
    float expand_env;
    float glow_env;

    int n_threads;
} wipe_t;

static inline int transcarot_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t transcarot_u8_add(uint8_t v, int add)
{
    const int r = (int)v + add;
    return (uint8_t)((r < 0) ? 0 : (r > 255 ? 255 : r));
}



static inline float transcarot_smoothf(float oldv, float target, float coef)
{
    return oldv + (target - oldv) * coef;
}



vj_effect *transcarot_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRANSCAROT_PARAMS;

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

    ve->defaults[P_SPEED]        = 2;
    ve->defaults[P_MODE]         = 0;
    ve->defaults[P_EXPAND_DRIVE] = 0;
    ve->defaults[P_EDGE_GLOW]    = 0;

    ve->limits[0][P_SPEED]        = 0;    ve->limits[1][P_SPEED]        = 100;
    ve->limits[0][P_MODE]         = 0;    ve->limits[1][P_MODE]         = 1;
    ve->limits[0][P_EXPAND_DRIVE] = 0;    ve->limits[1][P_EXPAND_DRIVE] = 1000;
    ve->limits[0][P_EDGE_GLOW]    = 0;    ve->limits[1][P_EDGE_GLOW]    = 1000;

    ve->description = "Transition Wipe Diagonal";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Mode",
        "Expand Drive",
        "Edge Glow"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Diagonal",
        "Bouncy Box"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,  1,                  100,                12, 46,  700, 2600, 0,    82,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,  120,                1000,               16, 62,  600, 2400, 0,    94,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,  120,                1000,               14, 54,  600, 2400, 0,    86
    );

    return ve;
}

void *transcarot_malloc(int w, int h)
{
    wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!wipe)
        return NULL;

    wipe->diagonal_pos = 0;

    wipe->box_x = 0;
    wipe->box_y = 0;
    wipe->box_dir_x = 1;
    wipe->box_dir_y = 1;

    wipe->speed_env = 2.0f;
    wipe->expand_env = 0.0f;
    wipe->glow_env = 0.0f;

    wipe->n_threads = vje_advise_num_threads(w * h);

    return wipe;
}

void transcarot_free(void *ptr)
{
    free(ptr);
}

static void transcarot_apply_diagonal(wipe_t *wipe,
                                      VJFrame *frame,
                                      VJFrame *frame2,
                                      int progress,
                                      int expand_px,
                                      int glow_width,
                                      int glow_strength)
{
    const int width = frame->width;
    const int height = frame->height;

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        int limit = progress + expand_px - y;

        if(limit > width)
            limit = width;

        if(limit > 0) {
            const int off = y * width;

            veejay_memcpy(frame->data[0] + off, frame2->data[0] + off, limit);
            veejay_memcpy(frame->data[1] + off, frame2->data[1] + off, limit);
            veejay_memcpy(frame->data[2] + off, frame2->data[2] + off, limit);
        }

        if(glow_width > 0 && glow_strength > 0) {
            const int edge_x = progress + expand_px - y;
            int x0 = edge_x - glow_width;
            int x1 = edge_x + glow_width + 1;

            x0 = transcarot_clampi(x0, 0, width);
            x1 = transcarot_clampi(x1, 0, width);

            if(x1 > x0) {
                const int row = y * width;

                for(int x = x0; x < x1; x++) {
                    int d = x - edge_x;
                    if(d < 0)
                        d = -d;

                    int q = glow_width + 1 - d;
                    if(q > 0) {
                        const int add = (glow_strength * q) / (glow_width + 1);
                        frame->data[0][row + x] = transcarot_u8_add(frame->data[0][row + x], add);
                    }
                }
            }
        }
    }
}

static void transcarot_apply_bouncybox(wipe_t *wipe,
                                       VJFrame *frame,
                                       VJFrame *frame2,
                                       int speed,
                                       int expand_px,
                                       int glow_width,
                                       int glow_strength)
{
    const int width = frame->width;
    const int height = frame->height;

    wipe->box_x += speed * wipe->box_dir_x;
    wipe->box_y += speed * wipe->box_dir_y;

    if(wipe->box_x >= width) {
        wipe->box_x = width;
        wipe->box_dir_x = -1;
    } else if(wipe->box_x <= 0) {
        wipe->box_x = 0;
        wipe->box_dir_x = 1;
    }

    if(wipe->box_y >= height) {
        wipe->box_y = height;
        wipe->box_dir_y = -1;
    } else if(wipe->box_y <= 0) {
        wipe->box_y = 0;
        wipe->box_dir_y = 1;
    }

    const int cur_x = transcarot_clampi(wipe->box_x + expand_px, 0, width);
    const int cur_y = transcarot_clampi(wipe->box_y + expand_px, 0, height);

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        if(y < cur_y && cur_x > 0) {
            const int off = y * width;

            veejay_memcpy(frame->data[0] + off, frame2->data[0] + off, cur_x);
            veejay_memcpy(frame->data[1] + off, frame2->data[1] + off, cur_x);
            veejay_memcpy(frame->data[2] + off, frame2->data[2] + off, cur_x);
        }

        if(glow_width > 0 && glow_strength > 0) {
            const int row = y * width;

            if(y < cur_y) {
                int x0 = transcarot_clampi(cur_x - glow_width, 0, width);
                int x1 = transcarot_clampi(cur_x + glow_width + 1, 0, width);

                for(int x = x0; x < x1; x++) {
                    int d = x - cur_x;
                    if(d < 0)
                        d = -d;

                    int q = glow_width + 1 - d;
                    if(q > 0) {
                        const int add = (glow_strength * q) / (glow_width + 1);
                        frame->data[0][row + x] = transcarot_u8_add(frame->data[0][row + x], add);
                    }
                }
            }

            if(cur_x > 0) {
                int d = y - cur_y;
                if(d < 0)
                    d = -d;

                if(d <= glow_width) {
                    int q = glow_width + 1 - d;
                    int add = (glow_strength * q) / (glow_width + 1);
                    int x1 = transcarot_clampi(cur_x, 0, width);

                    for(int x = 0; x < x1; x++)
                        frame->data[0][row + x] = transcarot_u8_add(frame->data[0][row + x], add);
                }
            }
        }
    }
}

void transcarot_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int speed_arg = transcarot_clampi(args[P_SPEED], 0, 100);
    const int mode = args[P_MODE] ? 1 : 0;
    const int expand_drive = args[P_EXPAND_DRIVE];
    const int edge_glow = args[P_EDGE_GLOW];

    const float fast = 0.28f;

    wipe->speed_env = transcarot_smoothf(wipe->speed_env, (float)speed_arg, fast);
    wipe->expand_env = transcarot_smoothf(wipe->expand_env, (float)expand_drive, fast * 0.66f);
    wipe->glow_env = transcarot_smoothf(wipe->glow_env, (float)edge_glow, fast * 0.74f);

    const int max_dim = width > height ? width : height;
    const float expand_t = wipe->expand_env * 0.001f;

    int effective_speed = (int)(wipe->speed_env + 0.5f);
    effective_speed += (int)(expand_t * (float)(1 + max_dim / 32) + 0.5f);
    effective_speed = transcarot_clampi(effective_speed, 0, max_dim);

    int expand_px = (int)(((float)max_dim * 0.42f) * expand_t + 0.5f);
    expand_px = transcarot_clampi(expand_px, 0, max_dim);

    int glow_width = 0;
    int glow_strength = 0;

    if(edge_glow > 0 || expand_drive > 0) {
        glow_width = 1 + (int)((wipe->glow_env * 10.0f) * 0.001f + expand_t * 4.0f + 0.5f);
        glow_width = transcarot_clampi(glow_width, 1, 18);

        glow_strength = (int)((wipe->glow_env * 128.0f) * 0.001f + expand_t * 42.0f + 0.5f);
        glow_strength = transcarot_clampi(glow_strength, 0, 220);
    }

    if(mode == 1) {
        transcarot_apply_bouncybox(
            wipe,
            frame,
            frame2,
            effective_speed,
            expand_px,
            glow_width,
            glow_strength
        );
        return;
    }

    const int total_span = width + height;

    wipe->diagonal_pos += effective_speed;

    while(wipe->diagonal_pos >= total_span)
        wipe->diagonal_pos -= total_span;

    transcarot_apply_diagonal(
        wipe,
        frame,
        frame2,
        wipe->diagonal_pos,
        expand_px,
        glow_width,
        glow_strength
    );
}
