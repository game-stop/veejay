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
#include "transcarot.h"

typedef struct {
    int diagonal_pos;

    int box_x;
    int box_y;
    int box_dir_x;
    int box_dir_y;

    int n_threads;
} wipe_t;

static inline int transcarot_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *transcarot_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 2;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;

    ve->defaults[1] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Transition Wipe Diagonal";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Speed",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Diagonal",
        "Bouncy Box"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS,                         0,                  72,                 8, 30, 1200, 3000, 0,   50,    /* Speed */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) width;
    (void) height;

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

    wipe->n_threads = vje_advise_num_threads(w * h);
    if(wipe->n_threads < 1)
        wipe->n_threads = 1;

    return wipe;
}

void transcarot_free(void *ptr)
{
    if(ptr)
        free(ptr);
}

static void transcarot_copy_prefix_rows(VJFrame *frame,
                                        VJFrame *frame2,
                                        int rows,
                                        int cols,
                                        int n_threads)
{
    const int width = frame->width;

    if(rows <= 0 || cols <= 0)
        return;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < rows; y++) {
        const int off = y * width;

        veejay_memcpy(frame->data[0] + off, frame2->data[0] + off, cols);
        veejay_memcpy(frame->data[1] + off, frame2->data[1] + off, cols);
        veejay_memcpy(frame->data[2] + off, frame2->data[2] + off, cols);
    }
}

static void transcarot_apply_bouncybox(wipe_t *wipe, VJFrame *frame, VJFrame *frame2, int speed)
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

    const int cur_x = transcarot_clampi(wipe->box_x, 0, width);
    const int cur_y = transcarot_clampi(wipe->box_y, 0, height);

    transcarot_copy_prefix_rows(frame, frame2, cur_y, cur_x, wipe->n_threads);
}

void transcarot_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
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

    const int speed = transcarot_clampi(args[0], 0, 100);
    const int mode = args[1] ? 1 : 0;

    if(mode == 1) {
        transcarot_apply_bouncybox(wipe, frame, frame2, speed);
        return;
    }

    const int total_span = width + height;

    wipe->diagonal_pos += speed;

    if(total_span > 0) {
        while(wipe->diagonal_pos >= total_span)
            wipe->diagonal_pos -= total_span;
    } else {
        wipe->diagonal_pos = 0;
    }

    const int progress = wipe->diagonal_pos;

#pragma omp parallel for schedule(static) num_threads(wipe->n_threads)
    for(int y = 0; y < height; y++) {
        int limit = progress - y;

        if(limit <= 0)
            continue;

        if(limit > width)
            limit = width;

        const int off = y * width;

        veejay_memcpy(frame->data[0] + off, frame2->data[0] + off, limit);
        veejay_memcpy(frame->data[1] + off, frame2->data[1] + off, limit);
        veejay_memcpy(frame->data[2] + off, frame2->data[2] + off, limit);
    }
}