/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "mirrors2.h"
#include <stdint.h>

#define MIRRORS2_PARAMS 1

#define P_SYMMETRY_MODE 0

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *mirrors2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MIRRORS2_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_SYMMETRY_MODE] = 0;
    ve->limits[0][P_SYMMETRY_MODE] = 0;
    ve->limits[1][P_SYMMETRY_MODE] = 11;

    ve->sub_format = 1;
    ve->description = "Mirror";
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Symmetry Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_SYMMETRY_MODE], P_SYMMETRY_MODE,
        "Bottom-Right",
        "Bottom-Left",
        "Top-Right",
        "Top-Left",
        "Top to Bottom",
        "Bottom to Top",
        "Left to Right",
        "Right to Left",
        "Top Left to Bottom Right",
        "Top Left to Bottom Left",
        "Top Right to Bottom Right",
        "Top Right to Bottom Left"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

    return ve;
}

static void mirrors2_quadrant(uint8_t *yuv[3], int width, int height, int mode, int n_threads)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];

    const int half_h = height >> 1;
    const int half_w = width >> 1;

    const int start_y = (mode == 0 || mode == 1) ? half_h : 0;
    const int end_y = (mode == 0 || mode == 1) ? height : half_h;
    const int start_x = (mode == 0 || mode == 2) ? half_w : 0;
    const int end_x = (mode == 0 || mode == 2) ? width : half_w;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = start_y; y < end_y; y++) {
        const int row_a = y * width;
        const int row_b = (height - y - 1) * width;

        for(int x = start_x; x < end_x; x++) {
            const int ox = width - x - 1;
            const int src = row_a + x;
            const int dst_x = row_a + ox;
            const int dst_y = row_b + x;
            const int dst_xy = row_b + ox;

            const uint8_t yy = py[src];
            const uint8_t uu = pu[src];
            const uint8_t vv = pv[src];

            py[dst_x] = yy;
            py[dst_y] = yy;
            py[dst_xy] = yy;

            pu[dst_x] = uu;
            pu[dst_y] = uu;
            pu[dst_xy] = uu;

            pv[dst_x] = vv;
            pv[dst_y] = vv;
            pv[dst_xy] = vv;
        }
    }
}

static void mirrors2_vertical(uint8_t *yuv[3], int width, int height, int copy_top, int n_threads)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];

    const int half_h = height >> 1;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < half_h; y++) {
        const int src_y = copy_top ? y : (height - 1 - y);
        const int dst_y = copy_top ? (height - 1 - y) : y;

        const uint8_t *restrict src_row_y = py + src_y * width;
        const uint8_t *restrict src_row_u = pu + src_y * width;
        const uint8_t *restrict src_row_v = pv + src_y * width;

        uint8_t *restrict dst_row_y = py + dst_y * width;
        uint8_t *restrict dst_row_u = pu + dst_y * width;
        uint8_t *restrict dst_row_v = pv + dst_y * width;

        veejay_memcpy(dst_row_y, src_row_y, width);
        veejay_memcpy(dst_row_u, src_row_u, width);
        veejay_memcpy(dst_row_v, src_row_v, width);
    }
}

static void mirrors2_horizontal(uint8_t *yuv[3], int width, int height, int copy_left, int n_threads)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];

    const int half_w = width >> 1;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < half_w; x++) {
            const int src_x = copy_left ? x : (width - 1 - x);
            const int dst_x = copy_left ? (width - 1 - x) : x;
            const int src = row + src_x;
            const int dst = row + dst_x;

            py[dst] = py[src];
            pu[dst] = pu[src];
            pv[dst] = pv[src];
        }
    }
}

static void mirrors2_diag_tl_br(uint8_t *yuv[3], int width, int height, int inverse, int n_threads)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];

    const int32_t rw = (width << 16) / height;
    const int32_t rh = (height << 16) / width;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        int sx = (y * rw) >> 16;

        if(sx >= width)
            sx = width - 1;

        for(int x = 0; x < width; x++) {
            const int64_t side = (int64_t)x * (int64_t)height - (int64_t)y * (int64_t)width;

            if((inverse && side < 0) || (!inverse && side > 0)) {
                int sy = (x * rh) >> 16;

                if(sy >= height)
                    sy = height - 1;

                const int src = sy * width + sx;
                const int dst = row + x;

                py[dst] = py[src];
                pu[dst] = pu[src];
                pv[dst] = pv[src];
            }
        }
    }
}

static void mirrors2_diag_tr_bl(uint8_t *yuv[3], int width, int height, int inverse, int n_threads)
{
    uint8_t *restrict py = yuv[0];
    uint8_t *restrict pu = yuv[1];
    uint8_t *restrict pv = yuv[2];

    const int32_t rw = (width << 16) / height;
    const int32_t rh = (height << 16) / width;
    const int64_t total_area = (int64_t)width * (int64_t)height;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int sx_base = (y * rw) >> 16;
        const int sx = width - 1 - sx_base;
        const int64_t y_w = (int64_t)y * (int64_t)width;

        for(int x = 0; x < width; x++) {
            const int64_t side = (int64_t)x * (int64_t)height + y_w - total_area;

            if((inverse && side > 0) || (!inverse && side < 0)) {
                int sy = height - 1 - ((x * rh) >> 16);
                const int src = sy * width + sx;
                const int dst = row + x;

                py[dst] = py[src];
                pu[dst] = pu[src];
                pv[dst] = pv[src];
            }
        }
    }
}

void mirrors2_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int type = args[P_SYMMETRY_MODE];
    const int n_threads = vje_advise_num_threads(frame->len);

    switch(type) {
        case 0:
        case 1:
        case 2:
        case 3:
            mirrors2_quadrant(frame->data, frame->width, frame->height, type, n_threads);
            break;
        case 4:
            mirrors2_vertical(frame->data, frame->width, frame->height, 1, n_threads);
            break;
        case 5:
            mirrors2_vertical(frame->data, frame->width, frame->height, 0, n_threads);
            break;
        case 6:
            mirrors2_horizontal(frame->data, frame->width, frame->height, 1, n_threads);
            break;
        case 7:
            mirrors2_horizontal(frame->data, frame->width, frame->height, 0, n_threads);
            break;
        case 8:
            mirrors2_diag_tl_br(frame->data, frame->width, frame->height, 0, n_threads);
            break;
        case 9:
            mirrors2_diag_tl_br(frame->data, frame->width, frame->height, 1, n_threads);
            break;
        case 10:
            mirrors2_diag_tr_bl(frame->data, frame->width, frame->height, 0, n_threads);
            break;
        case 11:
            mirrors2_diag_tr_bl(frame->data, frame->width, frame->height, 1, n_threads);
            break;
    }
}
