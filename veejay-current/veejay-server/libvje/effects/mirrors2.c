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
#include "mirrors2.h"
vj_effect *mirrors2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 11;
    ve->sub_format = 1;
    ve->description = "Mirror";
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Symmetry Mode");

    ve->hints = vje_init_value_hint_list( ve->num_params );
    
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0,
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
    
    return ve;
}

typedef struct {
	int n_threads;
} mirror2_t;


void *mirror2_malloc(int w, int h) {
	mirror2_t *m = (mirror2_t*) vj_malloc(sizeof(mirror2_t));
	if(!m) return NULL;
	m->n_threads = vje_advise_num_threads(w*h);
	return (void*) m;
}

void mirror2_free(void *ptr) {
	mirror2_t *m = (mirror2_t*) ptr;
	if(m) {
		free(m);
	}
}

#define GET_YUV_PTRS \
    uint8_t * restrict py = yuv[0]; \
    uint8_t * restrict pu = yuv[1]; \
    uint8_t * restrict pv = yuv[2];

static void mirror_quadrant(uint8_t * yuv[3], int width, int height, int mode, int n_threads) {
    GET_YUV_PTRS
    const int half_h = height / 2;
    const int half_w = width / 2;

    const int start_y = (mode == 0 || mode == 1) ? half_h : 0;
    const int end_y   = (mode == 0 || mode == 1) ? height : half_h;
    const int start_x = (mode == 0 || mode == 2) ? half_w : 0;
    const int end_x   = (mode == 0 || mode == 2) ? width : half_w;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = start_y; y < end_y; y++) {
        int yi1 = y * width;
        int yi2 = (height - y - 1) * width;
        for (int x = start_x; x < end_x; x++) {
            int opp_x = width - x - 1;
            int src = yi1 + x;

            uint8_t val_y = py[src], val_u = pu[src], val_v = pv[src];

            py[yi1+opp_x] = py[yi2+x] = py[yi2+opp_x] = val_y;
            pu[yi1+opp_x] = pu[yi2+x] = pu[yi2+opp_x] = val_u;
            pv[yi1+opp_x] = pv[yi2+x] = pv[yi2+opp_x] = val_v;
        }
    }
}

static void mirror_vertical(uint8_t * yuv[3], int width, int height, int copy_top, int n_threads) {
    GET_YUV_PTRS
    const int half_h = height / 2;

    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < half_h; y++) {
        const int src_y = copy_top ? y : (height - 1 - y);
        const int dst_y = copy_top ? (height - 1 - y) : y;

        const uint8_t *src_row_y = py + (src_y * width);
        const uint8_t *src_row_u = pu + (src_y * width);
        const uint8_t *src_row_v = pv + (src_y * width);

        uint8_t *dst_row_y = py + (dst_y * width);
        uint8_t *dst_row_u = pu + (dst_y * width);
        uint8_t *dst_row_v = pv + (dst_y * width);

        for (int x = 0; x < width; x++) {
            dst_row_y[x] = src_row_y[x];
            dst_row_u[x] = src_row_u[x];
            dst_row_v[x] = src_row_v[x];
        }
    }
}

static void mirror_horizontal(uint8_t * yuv[3], int width, int height, int copy_left, int n_threads) {
    GET_YUV_PTRS
    const int half_w = width / 2;
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int y = 0; y < height; y++) {
        const int row = y * width;
        for (int x = 0; x < half_w; x++) {
            int src_x = copy_left ? x : (width - 1 - x);
            int dst_x = copy_left ? (width - 1 - x) : x;
            py[row + dst_x] = py[row + src_x];
            pu[row + dst_x] = pu[row + src_x];
            pv[row + dst_x] = pv[row + src_x];
        }
    }
}

static void mirror_diag_tl_br(uint8_t *yuv[3], int width, int height, int inverse, int n_threads) {
    GET_YUV_PTRS
    const int32_t rw = (width << 16) / height;
    const int32_t rh = (height << 16) / width;

    if (inverse) {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for (int y = 0; y < height; y++) {
            const int ty = y * width;
            int sx = (y * rw) >> 16;
            if (sx >= width) sx = width - 1;

            for (int x = 0; x < width; x++) {
                if (x * height < y * width) {
                    int sy = (x * rh) >> 16;
                    if (sy >= height) sy = height - 1;
                    const int src = sy * width + sx;
                    const int dst = ty + x;
                    py[dst] = py[src]; pu[dst] = pu[src]; pv[dst] = pv[src];
                }
            }
        }
    } else {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for (int y = 0; y < height; y++) {
            const int ty = y * width;
            int sx = (y * rw) >> 16;
            if (sx >= width) sx = width - 1;

            for (int x = 0; x < width; x++) {
                if (x * height > y * width) {
                    int sy = (x * rh) >> 16;
                    if (sy >= height) sy = height - 1;
                    const int src = sy * width + sx;
                    const int dst = ty + x;
                    py[dst] = py[src]; pu[dst] = pu[src]; pv[dst] = pv[src];
                }
            }
        }
    }
}

static void mirror_diag_tr_bl(uint8_t *yuv[3], int width, int height, int inverse, int n_threads) {
    GET_YUV_PTRS
    const int32_t rw = (width << 16) / height;
    const int32_t rh = (height << 16) / width;
    const int32_t total_area = width * height;

    if (inverse) {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for (int y = 0; y < height; y++) {
            const int ty = y * width;
            const int sx_base = (y * rw) >> 16;
            const int y_w = y * width;

            for (int x = 0; x < width; x++) {
                if ((x * height + y_w) > total_area) {
                    int sy = height - 1 - ((x * rh) >> 16);
                    int sx = width - 1 - sx_base;
                    if (sy < 0) 
			    sy = 0; 
		    if (sx < 0) 
			    sx = 0;
                    
                    const int src = sy * width + sx;
                    const int dst = ty + x;
                    py[dst] = py[src]; pu[dst] = pu[src]; pv[dst] = pv[src];
                }
            }
        }
    } else {
        #pragma omp parallel for num_threads(n_threads) schedule(static)
        for (int y = 0; y < height; y++) {
            const int ty = y * width;
            const int sx_base = (y * rw) >> 16;
            const int y_w = y * width;

            for (int x = 0; x < width; x++) {
                if ((x * height + y_w) < total_area) {
                    int sy = height - 1 - ((x * rh) >> 16);
                    int sx = width - 1 - sx_base;
                    if (sy < 0) 
			    sy = 0; 
		    if (sx < 0) 
			    sx = 0;

                    const int src = sy * width + sx;
                    const int dst = ty + x;
                    py[dst] = py[src]; pu[dst] = pu[src]; pv[dst] = pv[src];
                }
            }
        }
    }
}


void mirrors2_apply(void *ptr, VJFrame *frame, int *args ) {
    mirror2_t *m = (mirror2_t *) ptr;
    int type = args[0];
    int nt = m->n_threads;

    switch (type) {
        case 0: case 1: case 2: case 3:
            mirror_quadrant(frame->data, frame->width, frame->height, type, nt); break;
        case 4: mirror_vertical(frame->data, frame->width, frame->height, 1, nt); break;
        case 5: mirror_vertical(frame->data, frame->width, frame->height, 0, nt); break;
        case 6: mirror_horizontal(frame->data, frame->width, frame->height, 1, nt); break;
        case 7: mirror_horizontal(frame->data, frame->width, frame->height, 0, nt); break;
        case 8: mirror_diag_tl_br(frame->data, frame->width, frame->height, 0, nt); break;
        case 9: mirror_diag_tl_br(frame->data, frame->width, frame->height, 1, nt); break;
        case 10: mirror_diag_tr_bl(frame->data, frame->width, frame->height, 0, nt); break;
        case 11: mirror_diag_tr_bl(frame->data, frame->width, frame->height, 1, nt); break;
    }
}
