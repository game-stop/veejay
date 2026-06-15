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
#include "transline.h"

#define TRANSLINE_PARAMS 4

#define P_SPEED        0
#define P_BOUNCE       1
#define P_EXPAND_DRIVE 2
#define P_EDGE_GLOW    3

typedef struct {
    int wipe_pos;
    int direction;
    int n_threads;

    float speed_state;
    float expand_state;
    float glow_state;

    int state_ready;
} wipe_t;

static inline int transline_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float transline_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t transline_u8_add(uint8_t v, int add)
{
    const int r = (int)v + add;
    return (uint8_t)((r < 0) ? 0 : (r > 255 ? 255 : r));
}





vj_effect *transline_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRANSLINE_PARAMS;

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

    ve->description = "Transition Wipe Cross";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

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
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  max_speed,          12, 46,  700, 2600, 0,    82,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,   0,    0,    -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               16, 62,  600, 2400, 0,    94,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120,                1000,               14, 54,  600, 2400, 0,    86
    );

    return ve;
}

void *transline_malloc(int w, int h)
{
    wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!wipe)
        return NULL;

    wipe->wipe_pos = 0;
    wipe->direction = 1;
    wipe->speed_state = 1.0f;
    wipe->expand_state = 0.0f;
    wipe->glow_state = 0.0f;
    wipe->state_ready = 0;

    wipe->n_threads = vje_advise_num_threads(w * h);

    return wipe;
}

void transline_free(void *ptr)
{
    free(ptr);
}

static void transline_step(wipe_t *wipe, int speed, int bounce, int max_pos)
{
    if(bounce) {
        wipe->wipe_pos += speed * wipe->direction;

        if(wipe->wipe_pos >= max_pos) {
            wipe->wipe_pos = max_pos;
            wipe->direction = -1;
        } else if(wipe->wipe_pos <= 0) {
            wipe->wipe_pos = 0;
            wipe->direction = 1;
        }
    } else {
        wipe->wipe_pos += speed;

        while(wipe->wipe_pos > max_pos)
            wipe->wipe_pos -= (max_pos + 1);

        wipe->direction = 1;
    }
}

static void transline_apply_cross_glow(VJFrame *frame,
                                       int x0,
                                       int x1,
                                       int y0,
                                       int y1,
                                       int glow_width,
                                       int glow_strength,
                                       int n_threads)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int e = 0; e < 2; e++) {
            const int edge_x = e ? x1 : x0;

            if(edge_x < 0 || edge_x >= width)
                continue;

            for(int dx = -glow_width; dx <= glow_width; dx++) {
                const int x = edge_x + dx;

                if(x < 0 || x >= width)
                    continue;

                int d = dx < 0 ? -dx : dx;
                const int q = glow_width + 1 - d;
                const int add = (glow_strength * q) / (glow_width + 1);

                Y[row + x] = transline_u8_add(Y[row + x], add);
            }
        }

        int hq = 0;

        if(y0 >= 0 && y0 < height) {
            int d = y - y0;
            if(d < 0)
                d = -d;
            if(d <= glow_width)
                hq += glow_width + 1 - d;
        }

        if(y1 >= 0 && y1 < height) {
            int d = y - y1;
            if(d < 0)
                d = -d;
            if(d <= glow_width)
                hq += glow_width + 1 - d;
        }

        if(hq > 0) {
            int add = (glow_strength * hq) / (glow_width + 1);
            if(add > glow_strength)
                add = glow_strength;

            for(int x = 0; x < width; x++)
                Y[row + x] = transline_u8_add(Y[row + x], add);
        }
    }
}

void transline_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;

    const int max_speed = (width > height) ? width : height;
    const int speed_arg = transline_clampi(args[P_SPEED], 0, max_speed);
    const int bounce = args[P_BOUNCE] ? 1 : 0;
    const int expand_drive = args[P_EXPAND_DRIVE];
    const int edge_glow = args[P_EDGE_GLOW];

    const float fast = 0.28f;

    if(!wipe->state_ready) {
        wipe->speed_state = (float)speed_arg;
        wipe->expand_state = (float)expand_drive;
        wipe->glow_state = (float)edge_glow;
        wipe->state_ready = 1;
    } else {
        wipe->speed_state += ((float)speed_arg - wipe->speed_state) * fast;
        wipe->expand_state += ((float)expand_drive - wipe->expand_state) * (fast * 0.62f);
        wipe->glow_state += ((float)edge_glow - wipe->glow_state) * (fast * 0.72f);
    }

    const float expand_t = wipe->expand_state * 0.001f;

    int speed = transline_clampi((int)(wipe->speed_state + 0.5f) + (int)(expand_t * (float)(max_speed / 8) + 0.5f), 0, max_speed);

    const int max_pos = width;
    transline_step(wipe, speed, bounce, max_pos);

    const int center_x = width >> 1;
    const int center_y = height >> 1;

    int cross_w = (width  * wipe->wipe_pos + (max_pos >> 1)) / max_pos;
    int cross_h = (height * wipe->wipe_pos + (max_pos >> 1)) / max_pos;

    const int direct_extra_w = (int)((float)width  * expand_t * 0.48f + 0.5f);
    const int direct_extra_h = (int)((float)height * expand_t * 0.48f + 0.5f);

    cross_w = transline_clampi(cross_w + direct_extra_w, 0, width);
    cross_h = transline_clampi(cross_h + direct_extra_h, 0, height);

    const int half_w = cross_w >> 1;
    const int half_h = cross_h >> 1;

    int x0 = center_x - half_w;
    int x1 = center_x + half_w;
    int y0 = center_y - half_h;
    int y1 = center_y + half_h;

    x0 = transline_clampi(x0, 0, width - 1);
    x1 = transline_clampi(x1, 0, width - 1);
    y0 = transline_clampi(y0, 0, height - 1);
    y1 = transline_clampi(y1, 0, height - 1);

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        if(y >= y0 && y <= y1) {
            veejay_memcpy(frame->data[0] + row, frame2->data[0] + row, width);
            veejay_memcpy(frame->data[1] + row, frame2->data[1] + row, width);
            veejay_memcpy(frame->data[2] + row, frame2->data[2] + row, width);
        } else if(x0 <= x1) {
            const int n = x1 - x0 + 1;
            const int off = row + x0;

            veejay_memcpy(frame->data[0] + off, frame2->data[0] + off, n);
            veejay_memcpy(frame->data[1] + off, frame2->data[1] + off, n);
            veejay_memcpy(frame->data[2] + off, frame2->data[2] + off, n);
        }
    }

    if(wipe->glow_state > 0.5f || expand_drive > 0) {
        const int glow_width = 1 + (int)(wipe->glow_state * 0.012f + expand_t * 7.0f + 0.5f);
        int glow_strength = (int)(wipe->glow_state * 0.150f + expand_t * 42.0f + 0.5f);

        glow_strength = transline_clampi(glow_strength, 0, 210);
        transline_apply_cross_glow(frame, x0, x1, y0, y1, glow_width, glow_strength, wipe->n_threads);
    }
}
