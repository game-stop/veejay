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
#include "split.h"

typedef struct {
    uint8_t *tmp[3];
    int n_threads;
} split_t;

static inline int split_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *split_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 8;
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 8;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->description = "Splitted Screens";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Switch"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Right Half",
        "Right Mirror",
        "Left Mirror",
        "Upper Left",
        "Upper Right",
        "Lower Right",
        "Lower Left",
        "Dual Squeeze",
        "Upper Half"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Direct",
        "Fit Source"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000, /* Mode */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000  /* Switch */
    );

    (void) width;
    (void) height;

    return ve;
}

void *split_malloc(int width, int height)
{
    split_t *s = (split_t*) vj_calloc(sizeof(split_t));
    if(!s)
        return NULL;

    const int len = width * height;

    s->tmp[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->tmp[0]) {
        free(s);
        return NULL;
    }

    s->tmp[1] = s->tmp[0] + len;
    s->tmp[2] = s->tmp[1] + len;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void split_free(void *ptr)
{
    split_t *s = (split_t*) ptr;
    if(!s)
        return;

    if(s->tmp[0])
        free(s->tmp[0]);

    free(s);
}

static void split_copy_region_plane(uint8_t *restrict dst,
                                    const uint8_t *restrict src,
                                    int w,
                                    int h,
                                    int x0,
                                    int y0,
                                    int x1,
                                    int y1,
                                    int mirror_x,
                                    int fit_source,
                                    int n_threads)
{
    x0 = split_clampi(x0, 0, w);
    x1 = split_clampi(x1, 0, w);
    y0 = split_clampi(y0, 0, h);
    y1 = split_clampi(y1, 0, h);

    if(x1 <= x0 || y1 <= y0)
        return;

    const int rw = x1 - x0;
    const int rh = y1 - y0;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = y0; y < y1; y++) {
        const int dst_row = y * w;

        for(int x = x0; x < x1; x++) {
            int sx;
            int sy;

            if(fit_source) {
                sx = ((x - x0) * w) / rw;
                sy = ((y - y0) * h) / rh;
            } else {
                sx = x;
                sy = y;
            }

            if(mirror_x)
                sx = (w - 1) - sx;

            sx = split_clampi(sx, 0, w - 1);
            sy = split_clampi(sy, 0, h - 1);

            dst[dst_row + x] = src[sy * w + sx];
        }
    }
}

static void split_copy_region(VJFrame *dst_frame,
                              VJFrame *src_frame,
                              int x0_num,
                              int x0_den,
                              int y0_num,
                              int y0_den,
                              int x1_num,
                              int x1_den,
                              int y1_num,
                              int y1_den,
                              int mirror_x,
                              int fit_source,
                              int n_threads)
{
    const int w = dst_frame->width;
    const int h = dst_frame->height;

    const int x0 = (w * x0_num) / x0_den;
    const int y0 = (h * y0_num) / y0_den;
    const int x1 = (w * x1_num) / x1_den;
    const int y1 = (h * y1_num) / y1_den;

    split_copy_region_plane(
        dst_frame->data[0],
        src_frame->data[0],
        w,
        h,
        x0,
        y0,
        x1,
        y1,
        mirror_x,
        fit_source,
        n_threads
    );

    if(dst_frame->data[1] && dst_frame->data[2] &&
       src_frame->data[1] && src_frame->data[2])
    {
        const int uw = dst_frame->ssm ? w : dst_frame->uv_width;
        const int uh = dst_frame->ssm ? h : dst_frame->uv_height;

        const int ux0 = (uw * x0_num) / x0_den;
        const int uy0 = (uh * y0_num) / y0_den;
        const int ux1 = (uw * x1_num) / x1_den;
        const int uy1 = (uh * y1_num) / y1_den;

        split_copy_region_plane(
            dst_frame->data[1],
            src_frame->data[1],
            uw,
            uh,
            ux0,
            uy0,
            ux1,
            uy1,
            mirror_x,
            fit_source,
            n_threads
        );

        split_copy_region_plane(
            dst_frame->data[2],
            src_frame->data[2],
            uw,
            uh,
            ux0,
            uy0,
            ux1,
            uy1,
            mirror_x,
            fit_source,
            n_threads
        );
    }
}

static void split_snapshot(split_t *s, VJFrame *frame)
{
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    veejay_memcpy(s->tmp[0], frame->data[0], len);

    if(frame->data[1] && frame->data[2] && uv_len > 0) {
        veejay_memcpy(s->tmp[1], frame->data[1], uv_len);
        veejay_memcpy(s->tmp[2], frame->data[2], uv_len);
    }
}

static void split_squeeze_plane(uint8_t *restrict dst,
                                const uint8_t *restrict src,
                                int w,
                                int h,
                                int x0,
                                int x1,
                                int n_threads)
{
    x0 = split_clampi(x0, 0, w);
    x1 = split_clampi(x1, 0, w);

    if(x1 <= x0)
        return;

    const int rw = x1 - x0;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const int row = y * w;

        for(int x = x0; x < x1; x++) {
            const int sx = ((x - x0) * w) / rw;
            dst[row + x] = src[row + sx];
        }
    }
}

static void split_dual_squeeze(split_t *s, VJFrame *frame, VJFrame *frame2)
{
    const int w = frame->width;
    const int h = frame->height;
    const int half = w >> 1;

    split_snapshot(s, frame);

    split_squeeze_plane(frame->data[0], frame2->data[0], w, h, 0, half, s->n_threads);
    split_squeeze_plane(frame->data[0], s->tmp[0],       w, h, half, w, s->n_threads);

    if(frame->data[1] && frame->data[2] &&
       frame2->data[1] && frame2->data[2])
    {
        const int uw = frame->ssm ? w : frame->uv_width;
        const int uh = frame->ssm ? h : frame->uv_height;
        const int uhalf = uw >> 1;

        split_squeeze_plane(frame->data[1], frame2->data[1], uw, uh, 0, uhalf, s->n_threads);
        split_squeeze_plane(frame->data[2], frame2->data[2], uw, uh, 0, uhalf, s->n_threads);

        split_squeeze_plane(frame->data[1], s->tmp[1], uw, uh, uhalf, uw, s->n_threads);
        split_squeeze_plane(frame->data[2], s->tmp[2], uw, uh, uhalf, uw, s->n_threads);
    }
}

void split_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    split_t *s = (split_t*) ptr;

    if(!s || !frame || !frame2 || !args ||
       !frame->data[0] || !frame2->data[0])
        return;

    const int mode = split_clampi(args[0], 0, 8);
    const int fit_source = args[1] ? 1 : 0;

    switch(mode) {
        case 0:
            split_copy_region(frame, frame2, 1, 2, 0, 1, 1, 1, 1, 1, 0, fit_source, s->n_threads);
            break;

        case 1:
            split_copy_region(frame, frame2, 1, 2, 0, 1, 1, 1, 1, 1, 1, fit_source, s->n_threads);
            break;

        case 2:
            split_copy_region(frame, frame2, 0, 1, 0, 1, 1, 2, 1, 1, 1, fit_source, s->n_threads);
            break;

        case 3:
            split_copy_region(frame, frame2, 0, 1, 0, 1, 1, 2, 1, 2, 0, fit_source, s->n_threads);
            break;

        case 4:
            split_copy_region(frame, frame2, 1, 2, 0, 1, 1, 1, 1, 2, 0, fit_source, s->n_threads);
            break;

        case 5:
            split_copy_region(frame, frame2, 1, 2, 1, 2, 1, 1, 1, 1, 0, fit_source, s->n_threads);
            break;

        case 6:
            split_copy_region(frame, frame2, 0, 1, 1, 2, 1, 2, 1, 1, 0, fit_source, s->n_threads);
            break;

        case 7:
            split_dual_squeeze(s, frame, frame2);
            break;

        case 8:
            split_copy_region(frame, frame2, 0, 1, 0, 1, 1, 1, 1, 2, 0, fit_source, s->n_threads);
            break;

        default:
            break;
    }
}