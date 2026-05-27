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

typedef struct {
    int bar_top_auto;
    int bar_bot_auto;
    int bar_top_vert;
    int bar_bot_vert;
    int n_threads;
} vbar_t;

static inline int vbar_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int vbar_wrap_add(int cur, int delta, int max)
{
    if(max <= 0)
        return 0;

    cur += delta;
    cur %= max;

    if(cur < 0)
        cur += max;

    return cur;
}

vj_effect *vbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_w = width  > 0 ? width  : 1;
    int max_h = height > 0 ? height : 1;

    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = max_w;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = max_h;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = max_h;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = max_w;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = max_w;

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
        "Bot X"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1, max_w > 32 ? 32 : max_w, 6, 22, 2200, 5200, 1800, 25, /* Divider */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                        0, max_h > 96 ? 96 : max_h, 8, 30, 1200, 3000, 0,    50, /* Top Y */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                        0, max_h > 96 ? 96 : max_h, 8, 30, 1200, 3000, 0,    50, /* Bot Y */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                        0, max_w > 96 ? 96 : max_w, 8, 30, 1200, 3000, 0,    50, /* Top X */
        VJ_BEAT_SPEED,     VJ_BEAT_F_CONTINUOUS,                        0, max_w > 96 ? 96 : max_w, 8, 30, 1200, 3000, 0,    50  /* Bot X */
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

    v->n_threads = vje_advise_num_threads(w * h);


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

    x0 = vbar_clampi(x0, 0, width);
    x1 = vbar_clampi(x1, 0, width);

    if(x1 <= x0)
        return;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        const int dst_row = y * width;
        const int src_y = (y + y_off) % height;
        const int src_row = src_y * width;

        for(int x = x0; x < x1; x++) {
            const int src_x = (x + x_off) % width;
            const int dst = dst_row + x;
            const int src = src_row + src_x;

            Y[dst]  = Y2[src];
            Cb[dst] = Cb2[src];
            Cr[dst] = Cr2[src];
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

    const int divider = vbar_clampi(args[0], 1, width);
    const int top_y_delta = vbar_clampi(args[1], 0, height);
    const int bot_y_delta = vbar_clampi(args[2], 0, height);
    const int top_x_delta = vbar_clampi(args[3], 0, width);
    const int bot_x_delta = vbar_clampi(args[4], 0, width);

    const int left_width = width / divider;

    vbar->bar_top_auto = vbar_wrap_add(vbar->bar_top_auto, top_y_delta, height);
    vbar->bar_top_vert = vbar_wrap_add(vbar->bar_top_vert, top_x_delta, width);
    vbar->bar_bot_auto = vbar_wrap_add(vbar->bar_bot_auto, bot_y_delta, height);
    vbar->bar_bot_vert = vbar_wrap_add(vbar->bar_bot_vert, bot_x_delta, width);

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
}