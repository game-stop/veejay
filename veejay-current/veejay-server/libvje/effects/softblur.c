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
#include "softblur.h"
#include <stdlib.h>
#include <omp.h>

extern int vje_get_quality(void);

typedef struct {
    uint8_t *bufY;
    uint8_t *bufU;
    uint8_t *bufV;
    int buf_size;
    int n_threads;
} softblur_t;

vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2; 
    ve->description = "Soft Blur";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Kernel Size");
    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "1x3", "3x3","5x5");

    return ve;
}

void *softblur_malloc(int w, int h) {
    softblur_t *sb = (softblur_t*) vj_calloc(sizeof(softblur_t));
    if(!sb)
        return NULL;
    sb->bufY = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3);
    sb->bufU = sb->bufY + (w*h);
    sb->bufV = sb->bufU + (w*h);


	sb->n_threads = vje_advise_num_threads(w*h);

    if(!sb->bufY) {
        free(sb);
        return NULL;
    }
    return (void*) sb;
}

void softblur_free(void *ptr) {
    softblur_t *sb = (softblur_t*) ptr;
    if(sb) {
        if(sb->bufY) {
            free(sb->bufY);
        }
        free(sb);
    }
}

static void softblur1_core(const uint8_t *src, uint8_t *dst, int w, int h, int n_threads)
{
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++)
    {
        const uint8_t *row = src + y*w;
        uint8_t *out = dst + y*w;

        if (w == 1) {
            out[0] = row[0];
            continue;
        }

        // left edge
        out[0] = (row[0]*2 + row[1]) / 3;

        // middle pixels
        for(int x = 1; x < w-1; x++) {
            out[x] = (row[x-1] + row[x] + row[x+1]) / 3;
        }

        // right edge
        out[w-1] = (row[w-2] + row[w-1]*2) / 3;
    }
}

static void softblur3_core(const uint8_t *src, uint8_t *tmp_buf, uint8_t *dst, int w, int h, int n_threads)
{
    // horizontal pass -> tmp_buf
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const uint8_t *row = src + y*w;
        uint8_t *trow = tmp_buf + y*w;

        if (w == 1) {
            trow[0] = row[0];
            continue;
        }

        trow[0] = (row[0]*2 + row[1]) / 3;
        for(int x = 1; x < w-1; x++) {
            trow[x] = (row[x-1] + row[x] + row[x+1]) / 3;
        }
        trow[w-1] = (row[w-2] + row[w-1]*2) / 3;
    }

    // vertical pass -> dst
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        uint8_t *drow = dst + y*w;

        int ym = (y > 0) ? y-1 : 0;
        int yp = (y < h-1) ? y+1 : h-1;

        const uint8_t *r0 = tmp_buf + ym*w;
        const uint8_t *r1 = tmp_buf + y*w;
        const uint8_t *r2 = tmp_buf + yp*w;

        for(int x = 0; x < w; x++) {
            drow[x] = (r0[x] + r1[x] + r2[x]) / 3;
        }
    }
}

static void softblur_inplace_1x3(uint8_t *plane, int width, int height, int n_threads)
{
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int y=0; y<height; y++) {
        uint8_t *row = plane + y*width;
        uint8_t prev = row[0];
        uint8_t curr = row[0];
        for(int x=0; x<width; x++) {
            uint8_t next = (x < width-1) ? row[x+1] : row[x];
            row[x] = (prev + curr + next)/3;
            prev = curr;
            curr = next;
        }
    }
}

static void softblur_inplace_3x3(uint8_t *plane, int width, int height, int n_threads)
{
    #pragma omp parallel num_threads(n_threads)
    {
        uint8_t buf_row[width];

        #pragma omp for schedule(static)
        for(int y=0; y<height; y++) {
            int ym = (y>0) ? y-1 : 0;
            int yp = (y<height-1) ? y+1 : height-1;

            uint8_t *row_m = plane + ym*width;
            uint8_t *row   = plane + y*width;
            uint8_t *row_p = plane + yp*width;

            if(width == 1) {
                buf_row[0] = row[0];
            } else {
                buf_row[0] = (row[0]*2 + row[1])/3;
                for(int x=1; x<width-1; x++)
                    buf_row[x] = (row[x-1] + row[x] + row[x+1])/3;
                buf_row[width-1] = (row[width-2] + row[width-1]*2)/3;
            }

            for(int x=0; x<width; x++) {
                int xm = (x>0) ? x-1 : 0;
                int xp = (x<width-1) ? x+1 : width-1;

                int sum =
                    row_m[xm] + row_m[x] + row_m[xp] +
                    buf_row[xm] + buf_row[x] + buf_row[xp] +
                    row_p[xm] + row_p[x] + row_p[xp];

                row[x] = sum / 9;
            }
        }
    }
}

static void softblur_inplace(uint8_t *plane, int width, int height, int type, int n_threads)
{
    switch(type) {
        case 0:
            softblur_inplace_1x3(plane, width, height, n_threads);
            break;

        case 1:
            softblur_inplace_3x3(plane, width, height, n_threads);
            break;

        case 2:
            softblur_inplace_3x3(plane, width, height, n_threads);
            softblur_inplace_3x3(plane, width, height, n_threads);
            break;
    }
}


void softblur_apply_internal(VJFrame *frame)
{
    int type = vje_get_quality();

    softblur_inplace(frame->data[0], frame->width, frame->height, type, vje_advise_num_threads(frame->len));
}


void softblur_apply(void *ptr, VJFrame *frame, int *args)
{
    softblur_t *blur = (softblur_t*) ptr;
    const int width  = frame->width;
    const int height = frame->height;
    const int uv_width = frame->uv_width;
    const int uv_height = frame->uv_height;
    const int len    = frame->len;
    const int uv_len = frame->uv_len;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *outY, *outU, *outV;

    // Setup local output buffers if available
    if (vje_setup_local_bufs(1, frame, &outY, &outU, &outV, NULL) == 0) {
        veejay_memcpy(blur->bufY, srcY, len);
        veejay_memcpy(blur->bufU, srcU, uv_len);
        veejay_memcpy(blur->bufV, srcV, uv_len);
        srcY = blur->bufY;
        srcU = blur->bufU;
        srcV = blur->bufV;
    }

    int type = args[0];

    switch(type)
    {
        case 0: // 1x3 blur
            softblur1_core(srcY, outY, width, height, blur->n_threads);
            softblur1_core(srcU, outU, uv_width, uv_height, blur->n_threads);
            softblur1_core(srcV, outV, uv_width, uv_height, blur->n_threads);
            break;

        case 1: // 3x3 blur
            softblur3_core(srcY, blur->bufY, outY, width, height, blur->n_threads);
            softblur3_core(srcU, blur->bufU, outU, uv_width, uv_height, blur->n_threads);
            softblur3_core(srcV, blur->bufV, outV, uv_width, uv_height, blur->n_threads);
            break;

        case 2: // 5x5 blur (double 3x3)
            softblur3_core(srcY, blur->bufY, outY, width, height, blur->n_threads);
            softblur3_core(outY, blur->bufY, outY, width, height, blur->n_threads);

            softblur3_core(srcU, blur->bufU, outU, uv_width, uv_height, blur->n_threads);
            softblur3_core(outU, blur->bufU, outU, uv_width, uv_height, blur->n_threads);

            softblur3_core(srcV, blur->bufV, outV, uv_width, uv_height, blur->n_threads);
            softblur3_core(outV, blur->bufV, outV, uv_width, uv_height, blur->n_threads);
            break;
    }
}