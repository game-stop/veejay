/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "slicer.h"

typedef struct {
    int *slice_xshift;
    int *slice_yshift;
    int *prev_slice_xshift;
    int *prev_slice_yshift;
    int last_period;
    int current_period;
    uint32_t seed;
} slicer_t;


vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = h;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 128;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 500;
	ve->limits[0][4] = 0;
	ve->limits[1][4] = 1;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 100;
    ve->defaults[0] = 16;
    ve->defaults[1] = 16;
	ve->defaults[2] = 8;
	ve->defaults[3] = 0;
	ve->defaults[4] = 0;
    ve->defaults[5] = 0;
    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Width", "Height", "Shatter", "Period", "Mode", "Smoothness"); 
 
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][4], 4, "No bounds", "With bounds" );

 
	return ve;
}

static inline uint32_t fast_rand(uint32_t *state) {
    *state = (*state * 1103515245 + 12345) & 0x7fffffff;
    return *state;
}

static void recalc(slicer_t *s, int w, int h, uint8_t *Yinp, int v1, int v2, const int shatter, uint32_t seed) 
{
    int x, y, dx, dy, r;
    uint32_t state = seed;

    int half_v1 = (v1 >> 1) + 1;
    int mask_v1 = (v1 >> 1) - 1;
    if (mask_v1 < 0) mask_v1 = 0;
    int half_v2 = (v2 >> 1) + 1;
    int mask_v2 = (v2 >> 1) - 1;
    if (mask_v2 < 0) mask_v2 = 0;

    for (x = dx = 0; x < w; x++) {
        if (dx <= 0) {
            r = (fast_rand(&state) % v1) - half_v1;
            dx = shatter + (Yinp[x] & mask_v1);
        } else {
            dx--;
        }
        s->slice_yshift[x] = r;
    }

    for (y = dy = 0; y < h; y++) {
        if (dy <= 0) {
            uint8_t sample = Yinp[y * w];
            r = (fast_rand(&state) % v2) - half_v2;
            dy = shatter + (sample & mask_v2);
        } else {
            dy--;
        }
        s->slice_xshift[y] = r;
    }
}

void *slicer_malloc(int width, int height)
{
    slicer_t *s = (slicer_t*) vj_calloc(sizeof(slicer_t));
    if (!s) return NULL;

    size_t total_ints = height*2 + width*2;
    int *all_shifts = (int*) vj_malloc(sizeof(int) * total_ints);
    if (!all_shifts) {
        free(s);
        return NULL;
    }

    s->slice_xshift      = all_shifts;
    s->slice_yshift      = all_shifts + height;
    s->prev_slice_xshift = all_shifts + height + width;
    s->prev_slice_yshift = all_shifts + height + width + height;

    s->last_period = -1;
    s->current_period = 1;

    return s;
}

void slicer_free(void *ptr)
{
    slicer_t *s = (slicer_t*) ptr;
    if(s) {
        free(s->slice_xshift);
        free(s);
    }
}

void slicer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    slicer_t *s = (slicer_t*) ptr;
    const int width = frame->width;
    const int height = frame->height;

    int val1 = args[0], val2 = args[1], shatter = args[2], period = args[3], mode = args[4];
    int smoothness = args[5];

    if (s->current_period <= 0) {
        veejay_memcpy(s->prev_slice_xshift, s->slice_xshift, sizeof(int)*height);
        veejay_memcpy(s->prev_slice_yshift, s->slice_yshift, sizeof(int)*width);

        uint32_t frame_seed = (uint32_t)(frame->timecode * 1000.0) ^ (val1 * 0x12345678);
        recalc(s, width, height, frame2->data[0], val1, val2, shatter, frame_seed);

        for (int y = 0; y < height; y++)
            s->slice_xshift[y] = (s->slice_xshift[y] * (100 - smoothness) + s->prev_slice_xshift[y] * smoothness) / 100;
        for (int x = 0; x < width; x++)
            s->slice_yshift[x] = (s->slice_yshift[x] * (100 - smoothness) + s->prev_slice_yshift[x] * smoothness) / 100;

        s->current_period = (period > 0) ? period : 1;
    }
    s->current_period--;

    uint8_t *srcY_base  = frame2->data[0];
    uint8_t *srcCb_base = frame2->data[1];
    uint8_t *srcCr_base = frame2->data[2];
    uint8_t *srcA_base  = frame2->data[3];

    for (int y = 0; y < height; y++) {
        const int x_shift = s->slice_xshift[y];
        const int row_offset = y * width;

        uint8_t *destY  = &frame->data[0][row_offset];
        uint8_t *destCb = &frame->data[1][row_offset];
        uint8_t *destCr = &frame->data[2][row_offset];
        uint8_t *destA  = &frame->data[3][row_offset];

        for (int x = 0; x < width; x++) {
            int src_x = x + x_shift;
            int src_y = y + s->slice_yshift[x];

            int out = (src_x < 0) | (src_x >= width) | (src_y < 0) | (src_y >= height);
            int mask = -(out == 0);
            int p = ((src_y * width) + src_x) & mask;

            *destY  = (srcY_base[p]  & mask) | (16  & ~mask);
            *destCb = (srcCb_base[p] & mask) | (128 & ~mask);
            *destCr = (srcCr_base[p] & mask) | (128 & ~mask);
            *destA  = (srcA_base[p]  & mask) | (0   & ~mask);

            destY++; destCb++; destCr++; destA++;
        }
    }
}