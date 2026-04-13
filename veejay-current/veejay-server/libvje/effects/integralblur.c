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
#include "integralblur.h"

vj_effect *integralblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = MIN(w, h) / 4;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 6;
    
    ve->defaults[0] = 3;
    ve->defaults[1] = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Iterations");
   
    ve->description = "Integral Blur";
    ve->sub_format = 1;

    return ve;
}

typedef struct {
    uint8_t  *mask;
    uint8_t  *tmp;
    uint32_t *integral;
    int width;
    int stride;
    int height;
    int n_threads;
} integralblur_t;

void *integralblur_malloc(int width, int height)
{
    integralblur_t *f = (integralblur_t*) vj_calloc(sizeof(integralblur_t));
    if(!f) return NULL;

    f->width = width;
    f->height = height;
    f->stride = width + 1;

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

void integralblur_free(void *ptr)
{
    integralblur_t *f = (integralblur_t*) ptr;
    free(f->mask);
    free(f->tmp);
    free(f->integral);
    free(f);
}

static void build_integral(integralblur_t *f, uint8_t *src)
{
    int w = f->width;
    int h = f->height;
    int stride = f->stride;

    uint32_t *I = f->integral;

    veejay_memset(I, 0, sizeof(uint32_t) * (w + 1));

    for (int y = 1; y <= h; y++)
    {
        uint32_t sum = 0;

        uint8_t  *src_row = src + (y - 1) * w;
        uint32_t *cur      = I + y * stride;
        uint32_t *prev     = I + (y - 1) * stride;

        cur[0] = 0;

        for (int x = 1; x <= w; x++)
        {
            sum += src_row[x - 1];
            cur[x] = sum + prev[x];
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

static void box_blur(integralblur_t *f,
                     uint8_t *src,
                     uint8_t *dst,
                     int radius)
{
    int w = f->width;
    int h = f->height;
    int stride = f->stride;

    uint32_t *I = f->integral;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; y++)
    {
        int y0 = y - radius; if (y0 < 0) y0 = 0;
        int y1 = y + radius; if (y1 >= h) y1 = h - 1;

        int iy0 = y0;
        int iy1 = y1 + 1;

        uint32_t *row_i0 = I + iy0 * stride;
        uint32_t *row_i1 = I + iy1 * stride;

        uint8_t *out = dst + y * w;

        for (int x = 0; x < w; x++)
        {
            int x0 = x - radius; if (x0 < 0) x0 = 0;
            int x1 = x + radius; if (x1 >= w) x1 = w - 1;

            int ix0 = x0;
            int ix1 = x1 + 1;

            uint32_t sum =
                row_i1[ix1]
              - row_i0[ix1]
              - row_i1[ix0]
              + row_i0[ix0];

            int area = (x1 - x0 + 1) * (y1 - y0 + 1);

            out[x] = (uint8_t)(sum / area);
        }
    }
}

void integralblur_apply(void *ptr, VJFrame *frame, int *args)
{
    integralblur_t *f = (integralblur_t*) ptr;

    int radius = args[0];
    int iter   = args[1];

    int len = frame->len;

    uint8_t *restrict src = f->mask;
    uint8_t *restrict dst = f->tmp;

    for (int p = 0; p < 3; p++)
    {
        veejay_memcpy(src, frame->data[p], len);

        if (iter < 2)
        {
            for (int i = 0; i < iter; i++)
            {
                build_integral(f, src);
                box_blur(f, src, dst, radius);

                uint8_t *swap = src;
                src = dst;
                dst = swap;
            }
        }
        else
        {
            int n1 = iter / 2;
            int n2 = iter - n1;

            float r1f = radius * sqrtf((float)n1);
            float r2f = radius * sqrtf((float)n2);

            int r1 = (int)(r1f + 0.5f);
            int r2 = (int)(r2f + 0.5f);

            if (r1 <= 0) r1 = radius;
            if (r2 <= 0) r2 = radius;

            build_integral(f, src);
            box_blur(f, src, dst, r1);

            build_integral(f, dst);
            box_blur(f, dst, src, r2);
        }

        veejay_memcpy(frame->data[p], src, len);
    }
}
