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
#include "transline.h"

typedef struct {
    int wipe_pos;
    int direction;
    int n_threads;
} wipe_t;

static inline int transline_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *transline_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_speed = (width > height) ? width : height;
    if(max_speed < 1)
        max_speed = 1;

    ve->defaults[0] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = max_speed;

    ve->defaults[1] = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Transition Wipe Cross";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Bounce"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Loop",
        "Bounce"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,                         0,                  max_speed > 240 ? 240 : max_speed, 8, 30, 1200, 3000, 0,   50,    /* Speed */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000  /* Bounce */
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

    wipe->n_threads = vje_advise_num_threads(w * h);

    return wipe;
}

void transline_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

static void transline_step(wipe_t *wipe, int speed, int bounce, int max_pos)
{
    if(max_pos <= 0) {
        wipe->wipe_pos = 0;
        wipe->direction = 1;
        return;
    }

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

void transline_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    wipe_t *wipe = (wipe_t*) ptr;

    if(!wipe || !frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int max_speed = (width > height) ? width : height;
    const int speed = transline_clampi(args[0], 0, max_speed);
    const int bounce = args[1] ? 1 : 0;

    const int max_pos = width > 1 ? width : 1;

    transline_step(wipe, speed, bounce, max_pos);

    const int center_x = width >> 1;
    const int center_y = height >> 1;

    const int cross_w = (width  * wipe->wipe_pos + (max_pos >> 1)) / max_pos;
    const int cross_h = (height * wipe->wipe_pos + (max_pos >> 1)) / max_pos;

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
}