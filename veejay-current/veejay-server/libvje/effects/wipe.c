/* veejay - Linux VeeJay
 *       (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *     
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "common.h"
#include "wipe.h"

typedef struct {
    int wipe_position;
    int last_restart;
    int n_threads;
} wipe_t;

static inline int wipe_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *wipe_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_speed = (w > h ? w : h);
    if(max_speed < 1)
        max_speed = 1;

    ve->defaults[0] = 0;
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = max_speed;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Transition Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Restart"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Run",
        "Restart"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,                         0,                  max_speed > 240 ? 240 : max_speed, 8, 30, 1200, 3000, 0,   50,    /* Speed */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000  /* Restart */
    );

    ve->is_transition_ready_func = wipe_ready;

    return ve;
}

int wipe_ready(void *ptr, int width, int height)
{
    wipe_t *w = (wipe_t*) ptr;

    (void) height;

    if(!w || width <= 0)
        return TRANSITION_COMPLETED;

    return (w->wipe_position >= width)
        ? TRANSITION_COMPLETED
        : TRANSITION_RUNNING;
}

void *wipe_malloc(int w, int h)
{
    wipe_t *prv = (wipe_t*) vj_calloc(sizeof(wipe_t));
    if(!prv)
        return NULL;

    prv->wipe_position = 0;
    prv->last_restart = 1;

    prv->n_threads = vje_advise_num_threads(w * h);

    return prv;
}

void wipe_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

void wipe_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
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

    const int speed = wipe_clampi(args[0], 0, width);
    const int restart = args[1] ? 1 : 0;

    if(restart && !wipe->last_restart)
        wipe->wipe_position = 0;

    wipe->last_restart = restart;

    wipe->wipe_position += speed;

    if(wipe->wipe_position > width)
        wipe->wipe_position = width;

    const int copy_w = wipe_clampi(wipe->wipe_position, 0, width);

    if(copy_w <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        veejay_memcpy(frame->data[0] + row, frame2->data[0] + row, copy_w);
        veejay_memcpy(frame->data[1] + row, frame2->data[1] + row, copy_w);
        veejay_memcpy(frame->data[2] + row, frame2->data[2] + row, copy_w);
    }
}