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
#include "transop.h"

static inline int transop_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *transop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int def_w = width  > 0 ? width  / 3 : 0;
    int def_h = height > 0 ? height / 3 : 0;

    if(def_w < 1 && width > 0)
        def_w = 1;
    if(def_h < 1 && height > 0)
        def_h = 1;

    ve->defaults[0] = def_w;
    ve->defaults[1] = def_h;
    ve->defaults[2] = height > 0 ? height / 4 : 0;
    ve->defaults[3] = width  > 0 ? width  / 4 : 0;
    ve->defaults[4] = height > 0 ? height / 3 : 0;
    ve->defaults[5] = width  > 0 ? width  / 3 : 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = width;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = height;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = width;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = height;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = width;

    ve->description = "Frame Translate";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Width",
        "Height",
        "Source Y",
        "Source X",
        "Dest Y",
        "Dest X"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS, 8, width  > 0 ? width  : 1, 8,  30, 1200, 3000, 0, 45, /* Width */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS, 8, height > 0 ? height : 1, 8,  30, 1200, 3000, 0, 45, /* Height */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS, 0, height > 0 ? height : 1, 8,  30, 1200, 3000, 0, 35, /* Source Y */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS, 0, width  > 0 ? width  : 1, 8,  30, 1200, 3000, 0, 35, /* Source X */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS, 0, height > 0 ? height : 1, 8,  30, 1200, 3000, 0, 35, /* Dest Y */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS, 0, width  > 0 ? width  : 1, 8,  30, 1200, 3000, 0, 35  /* Dest X */
    );

    return ve;
}

void transop_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    (void) ptr;

    if(!frame || !frame2 || !args ||
       !frame->data[0] || !frame->data[1] || !frame->data[2] ||
       !frame2->data[0] || !frame2->data[1] || !frame2->data[2])
        return;

    const int width  = frame->width;
    const int height = frame->height;
    const int len    = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int rect_w = transop_clampi(args[0], 0, width);
    int rect_h = transop_clampi(args[1], 0, height);

    int sy = transop_clampi(args[2], 0, height);
    int sx = transop_clampi(args[3], 0, width);
    int dy = transop_clampi(args[4], 0, height);
    int dx = transop_clampi(args[5], 0, width);

    if(rect_w <= 0 || rect_h <= 0)
        return;

    if(sx >= width || sy >= height || dx >= width || dy >= height)
        return;

    if(sx + rect_w > width)
        rect_w = width - sx;
    if(dx + rect_w > width)
        rect_w = width - dx;

    if(sy + rect_h > height)
        rect_h = height - sy;
    if(dy + rect_h > height)
        rect_h = height - dy;

    if(rect_w <= 0 || rect_h <= 0)
        return;

    uint8_t *restrict dY  = frame->data[0];
    uint8_t *restrict dCb = frame->data[1];
    uint8_t *restrict dCr = frame->data[2];

    const uint8_t *restrict sY  = frame2->data[0];
    const uint8_t *restrict sCb = frame2->data[1];
    const uint8_t *restrict sCr = frame2->data[2];

    int n_threads = vje_advise_num_threads(rect_w * rect_h);
    if(n_threads < 1)
        n_threads = 1;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < rect_h; y++) {
        const int src = (sy + y) * width + sx;
        const int dst = (dy + y) * width + dx;

        veejay_memcpy(dY  + dst, sY  + src, rect_w);
        veejay_memcpy(dCb + dst, sCb + src, rect_w);
        veejay_memcpy(dCr + dst, sCr + src, rect_w);
    }
}