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
#include "feathermask.h"

vj_effect *feathermask_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 32;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 4;
    
    ve->defaults[0] = 3;
    ve->defaults[1] = 1;
    ve->param_description = vje_build_param_list(
        ve->num_params, "Radius", "Iterations");
   
    ve->description = "Alpha: Feather Mask";
    ve->sub_format = 1;
	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;
    return ve;
}

typedef struct {
    uint8_t  *mask;
    uint8_t  *tmp;
    uint32_t *integral;
    int width;
    int height;
    int n_threads;
} feathermask_t;

void *feathermask_malloc(int width, int height)
{
    feathermask_t *f = (feathermask_t*) vj_calloc(sizeof(feathermask_t));
    if(!f) return NULL;

    f->width = width;
    f->height = height;

    size_t len = (size_t)width * (size_t)height;

    f->mask = (uint8_t*) vj_malloc(len);
    f->tmp  = (uint8_t*) vj_malloc(len);

    f->integral = (uint32_t*) vj_malloc(sizeof(uint32_t) * (width + 1) * (height + 1));

    if(!f->mask || !f->tmp || !f->integral) {
        free(f->mask);
        free(f->tmp);
        free(f->integral);
        free(f);
        return NULL;
    }
    
    f->n_threads = vje_advise_num_threads(width*height);

    return f;
}

void feathermask_free(void *ptr)
{
    feathermask_t *f = (feathermask_t*) ptr;
    free(f->mask);
    free(f->tmp);
    free(f->integral);
    free(f);
}
static void build_integral(feathermask_t *f, uint8_t *src)
{
    int w = f->width;
    int h = f->height;

    uint32_t *I = f->integral;

    veejay_memset(I, 0, sizeof(uint32_t) * (w + 1));

    for (int y = 1; y <= h; y++) {
        uint32_t row_sum = 0;

        int row_off  = y * (w + 1);
        int prev_off = (y - 1) * (w + 1);

        I[row_off] = 0;

        uint8_t *src_row = &src[(y - 1) * w];

        for (int x = 1; x <= w; x++) {
            row_sum += src_row[x - 1];
            I[row_off + x] = I[prev_off + x] + row_sum;
        }
    }
}

static inline uint32_t box_sum(uint32_t *I, int stride,
                               int x0, int y0, int x1, int y1)
{
    return I[y1 * stride + x1]
         - I[y0 * stride + x1]
         - I[y1 * stride + x0]
         + I[y0 * stride + x0];
}

static void box_blur(feathermask_t *f,
                     uint8_t *src,
                     uint8_t *dst,
                     int radius)
{
    int w = f->width;
    int h = f->height;
    uint32_t *I = f->integral;
    int stride = w + 1;

    build_integral(f, src);

    #pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for(int y = 0; y < h; y++) {

        int y0 = y - radius; if(y0 < 0) y0 = 0;
        int y1 = y + radius; if(y1 >= h) y1 = h - 1;

        int iy0 = y0;
        int iy1 = y1 + 1;

        for(int x = 0; x < w; x++) {

            int x0 = x - radius; if(x0 < 0) x0 = 0;
            int x1 = x + radius; if(x1 >= w) x1 = w - 1;

            int ix0 = x0;
            int ix1 = x1 + 1;

            uint32_t sum = box_sum(I, stride, ix0, iy0, ix1, iy1);

            int area = (x1 - x0 + 1) * (y1 - y0 + 1);

            dst[y * w + x] = (uint8_t)(sum / area);
        }
    }
}

void feathermask_apply(void *ptr, VJFrame *frame, int *args)
{
    feathermask_t *f = (feathermask_t*) ptr;

    int radius = args[0];
    int iter   = args[1];

    int len = frame->len;

    veejay_memcpy(f->mask,frame->data[0], len);

    uint8_t *restrict src = f->mask;
    uint8_t *restrict dst = f->tmp;

    for(int i = 0; i < iter; i++) {

        box_blur(f, src, dst, radius);

        uint8_t *swap = src;
        src = dst;
        dst = swap;
    }

    veejay_memcpy(frame->data[3], src, len);
}