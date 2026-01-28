/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>
#include "smartblur.h"

#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

typedef struct {
    uint8_t *tmp;
    int w;
    int h;
    int ds_w;
    int ds_h;
} smartblur_t;

vj_effect *smartblur_init(int w, int h)
{
    vj_effect *ve = vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = vj_calloc(sizeof(int)*3);
    ve->limits[0] = vj_calloc(sizeof(int)*3);
    ve->limits[1] = vj_calloc(sizeof(int)*3);

    ve->limits[0][0]=1;   ve->limits[1][0]=64;
    ve->limits[0][1]=0;   ve->limits[1][1]=255;
    
    ve->defaults[0]=10;
    ve->defaults[1]=20;
    
    ve->description="Smart Blur";
    ve->param_description=vje_build_param_list(3,"Radius","Threshold");

    ve->sub_format=-1;
    return ve;
}


void *smartblur_malloc(int w, int h)
{
    smartblur_t *s = vj_malloc(sizeof(*s));
    s->w = w;
    s->h = h;

    // Downsample size: half resolution
    s->ds_w = (w + 1) / 2;
    s->ds_h = (h + 1) / 2;

    // tmp buffer must fit the full image + downsampled
    s->tmp = vj_malloc(w * h); 
    
     // TODO:  allocate buffer for down/up sampling + s->ds_w * s->ds_h);
    return s;
}


void smartblur_free(void *ptr)
{
    smartblur_t *s = ptr;
    if(s){ free(s->tmp); free(s); }
}

static inline int iabs(int v)
{
    int m = v >> 31;
    return (v ^ m) - m;
} 


static void horizontal_blur_Y(uint8_t *dst, const uint8_t *src, uint8_t *tmp, int w, int h, int radius, int threshold)
{
    int y;
    #pragma omp parallel for schedule(static) default(none) shared(src,tmp,w,h,radius,threshold) private(y)
    for (y = 0; y < h; y++) {
        const uint8_t *restrict in = src + y * w;
        uint8_t *restrict out = tmp + y * w;

        for (int x = 0; x < w; x++) {
            int sum = 0, cnt = 0;
            int center = in[x];

            int start = x - radius;
            int end   = x + radius;

            if (start < 0) start = 0;
            if (end >= w) end = w - 1;

            for (int k = start; k <= end; k++) {
                int diff = iabs(in[k] - center);
                int include = (diff <= threshold) ? 1 : 0;
                sum += in[k] * include;
                cnt += include;
            }

            out[x] = cnt ? sum / cnt : center;
        }
    }
}

static void vertical_blur_Y(uint8_t *dst, uint8_t *tmp, int w, int h, int radius, int threshold)
{
    int x,y;
    #pragma omp parallel for schedule(static) default(none) shared(dst,tmp,w,h,radius,threshold) private(y)    
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            int sum = 0, cnt = 0;
            int center = tmp[y * w + x];

            int start = y - radius;
            int end   = y + radius;

            if (start < 0) start = 0;
            if (end >= h) end = h - 1;

            for (int k = start; k <= end; k++) {
                int diff = iabs(tmp[k * w + x] - center);
                int include = (diff <= threshold) ? 1 : 0;
                sum += tmp[k * w + x] * include;
                cnt += include;
            }

            dst[y * w + x] = cnt ? sum / cnt : center;
        }
    }
}


static void horizontal_blur_plane(uint8_t *dst, const uint8_t *src, uint8_t *tmp, int w, int h, int radius)
{
    int y;
    #pragma omp parallel for schedule(static) default(none) shared(src,tmp,w,h,radius) private(y)

    for (y = 0; y < h; y++) {

        const uint8_t *restrict in  = src + y * w;
        uint8_t *restrict out = tmp + y * w;

        int sum = 0;
        int x = 0;

        int hi = (radius < w) ? radius : (w - 1);
        for (int k = 0; k <= hi; k++)
            sum += in[k];

        out[0] = sum / (hi + 1);

        for (x = 1; x <= radius && x < w; x++) {
            int add = x + radius;
            if (add < w)
                sum += in[add];

            out[x] = sum / (x + radius + 1);
        }

        for (; x + radius < w; x++) {
            sum += in[x + radius];
            sum -= in[x - radius - 1];
            out[x] = sum / (2 * radius + 1);
        }

        for (; x < w; x++) {
            sum -= in[x - radius - 1];
            out[x] = sum / (w - (x - radius - 1));
        }
    }
}


// This is still slow because of cache misses & non continous pixels as we are skipping <width> pixels each iteration
static void vertical_blur_plane1(uint8_t *dst, uint8_t *tmp, int w, int h, int radius)
{
    int x,y;
    #pragma omp parallel for schedule(static) default(none) shared(dst,tmp,w,h,radius) private(x,y)

    for (x = 0; x < w; x++) {

        int sum = 0;
        y = 0;
        int hi = (radius < h) ? radius : (h - 1);
        for (int k = 0; k <= hi; k++)
            sum += tmp[k * w + x];

        dst[x] = sum / (hi + 1);

        for (y = 1; y <= radius && y < h; y++) {
            sum += tmp[(y + radius < h ? y + radius : h - 1) * w + x];
            dst[y * w + x] = sum / (y + radius + 1);
        }

        for (; y + radius < h; y++) {
            sum += tmp[(y + radius) * w + x];
            sum -= tmp[(y - radius - 1) * w + x];
            dst[y * w + x] = sum / (2 * radius + 1);
        }

        for (; y < h; y++) {
            sum -= tmp[(y - radius - 1) * w + x];
            dst[y * w + x] = sum / (h - (y - radius - 1));
        }
    }
}

// This is faster, because memory is now continuous
static void vertical_blur_plane(uint8_t *dst, uint8_t *tmp, int w, int h, int radius)
{
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; y++) {
        int y_w = y * w;
        for (int x = 0; x < w; x++) {
            tmp[x*h + y] = dst[y_w + x];
        }
    }

    horizontal_blur_plane(tmp, tmp, dst, h, w, radius);

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < w; y++) {
        int y_h = y * h;
        for (int x = 0; x < h; x++) {
            dst[x*w + y] = tmp[y_h + x];
        }
    }
}



void smartblur_apply(void *ptr, VJFrame *frame, int *args)
{
    smartblur_t *s = ptr;
    if(!s || !frame) return;

    const int w = s->w;
    const int h = s->h;
    const int radius = args[0];
    const int threshold = args[1];

    uint8_t *tmp_full = s->tmp;
    uint8_t *tmp_ds = s->tmp + (s->w * s->h);

    int use_ds = 0; // (radius > 16);
    int ds_radius = radius / 2;

    if(use_ds) {
        int ds_w = s->ds_w;
        int ds_h = s->ds_h;
        uint8_t *tmp_ds = s->tmp + w * h;

        // TODO: downsample/upsample or scale to half resolution

    }
    else {
        // fallback: full-resolution blur
        horizontal_blur_Y(tmp_full, frame->data[0], tmp_full, w, h, radius, threshold);
        vertical_blur_Y(frame->data[0], tmp_full, w, h, radius, threshold);

        horizontal_blur_plane(tmp_full, frame->data[1], tmp_full, w, h, radius);
        vertical_blur_plane(frame->data[1], tmp_full, w, h, radius);

        horizontal_blur_plane(tmp_full, frame->data[2], tmp_full, w, h, radius);
        vertical_blur_plane(frame->data[2], tmp_full, w, h, radius);
    }
}

