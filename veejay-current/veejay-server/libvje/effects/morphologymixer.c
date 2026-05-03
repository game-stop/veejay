/*
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/vjmem.h>
#include "common.h"

#define FLOW_SHIFT 4                   
#define FLOW_SIZE (1 << FLOW_SHIFT)    
#define FLOW_MASK (FLOW_SIZE - 1)      

#define CLAMP(v, min, max) ((v) < (min) ? (min) : ((v) > (max) ? (max) : (v)))

typedef struct {
    uint8_t *tmpY;
    uint8_t *tmpCb;
    uint8_t *tmpCr;
    
    int grid_w;
    int grid_h;
    int *flow_x1; 
    int *flow_y1;
    int *flow_x2; 
    int *flow_y2;

    int n_threads;
} morph_t;

vj_effect *morphologymixer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 5;
    ve->defaults   = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0]  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1]  = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0]  = 128;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1]  = 64;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 2;
    ve->defaults[2]  = 1;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3]  = 160;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
    ve->defaults[4]  = 180;

    ve->sub_format   = 1;
    ve->extra_frame  = 1;

    ve->description =
        "Displacement Morphology";

    ve->param_description =
        vje_build_param_list(
            ve->num_params,
            "Mix Progress",
            "Warp Intensity",
            "Mode",
            "Response",
            "Stability"
        );

    return ve;
}

void morphologymixer_free(void *ptr) 
{
    morph_t *m = (morph_t*) ptr;
    if(m) {
        if(m->tmpY) free(m->tmpY);
        if(m->flow_x1) free(m->flow_x1);
        if(m->flow_x2) free(m->flow_x2);
        free(m);
    }
}

void *morphologymixer_malloc(int w, int h)
{
    morph_t *m = (morph_t*) vj_calloc(sizeof(morph_t));
    if(!m)
        return NULL;

    const int size = w * h;

    m->tmpY  = (uint8_t*) vj_calloc(size * 3);
    m->tmpCb = m->tmpY + size;
    m->tmpCr = m->tmpCb + size;

    m->grid_w = (w >> FLOW_SHIFT) + 2;
    m->grid_h = (h >> FLOW_SHIFT) + 2;

    const int gsize = m->grid_w * m->grid_h * sizeof(int);

    m->flow_x2 = (int*) vj_calloc(gsize * 2);
    m->flow_y2 = m->flow_x2 + gsize;

    m->flow_x1 = (int*) vj_calloc(size * sizeof(int) * 2);
    m->flow_y1 = m->flow_x1 + size;

    m->n_threads = vje_advise_num_threads(size);

    return (void*) m;
}

void morphologymixer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int progress  = args[0];
    const int warp_amt  = args[1];
    const int mode      = args[2];
    const int response  = args[3];
    const int stability = args[4];

    const int w    = frame->width;
    const int h    = frame->height;
    const int size = w * h;

    uint8_t *Y   = frame->data[0];
    uint8_t *Cb  = frame->data[1];
    uint8_t *Cr  = frame->data[2];

    uint8_t *Y2  = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    morph_t *m = (morph_t*) ptr;

    if(progress <= 0)
        return;

    if(progress >= 255)
    {
        veejay_memcpy(Y,  Y2,  size);
        veejay_memcpy(Cb, Cb2, size);
        veejay_memcpy(Cr, Cr2, size);
        return;
    }

    veejay_memcpy(m->tmpY,  Y,  size);
    veejay_memcpy(m->tmpCb, Cb, size);
    veejay_memcpy(m->tmpCr, Cr, size);

    uint8_t *sY  = m->tmpY;
    uint8_t *sCb = m->tmpCb;
    uint8_t *sCr = m->tmpCr;

    const int inv_progress = 255 - progress;

    if(warp_amt <= 0)
    {
        #pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int i = 0; i < size; i++)
        {
            Y[i]  = (sY[i]  * inv_progress + Y2[i]  * progress) >> 8;
            Cb[i] = (sCb[i] * inv_progress + Cb2[i] * progress) >> 8;
            Cr[i] = (sCr[i] * inv_progress + Cr2[i] * progress) >> 8;
        }
        return;
    }

    if(mode == 0)
    {
        const int gain = response + 32;

        #pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 0; y < h; y++)
        {
            const int row = y * w;
            const int up  = (y > 0)     ? row - w : row;
            const int dn  = (y < h - 1) ? row + w : row;

            for(int x = 0; x < w; x++)
            {
                const int idx = row + x;
                const int lx  = (x > 0)     ? x - 1 : x;
                const int rx  = (x < w - 1) ? x + 1 : x;

                int dx1 = Y2[row + rx] - Y2[row + lx];
                int dy1 = Y2[dn + x]   - Y2[up + x];

                int dx2 = sY[row + rx] - sY[row + lx];
                int dy2 = sY[dn + x]   - sY[up + x];

                int sx1 = CLAMP(x + ((dx1 * warp_amt * gain * progress) >> 24),0,w-1);
                int sy1 = CLAMP(y + ((dy1 * warp_amt * gain * progress) >> 24),0,h-1);
                int sx2 = CLAMP(x - ((dx2 * warp_amt * gain * inv_progress)>>24),0,w-1);
                int sy2 = CLAMP(y - ((dy2 * warp_amt * gain * inv_progress)>>24),0,h-1);

                int a = sy1 * w + sx1;
                int b = sy2 * w + sx2;

                Y[idx]  = (sY[a]  * inv_progress + Y2[b]  * progress) >> 8;
                Cb[idx] = (sCb[a] * inv_progress + Cb2[b] * progress) >> 8;
                Cr[idx] = (sCr[a] * inv_progress + Cr2[b] * progress) >> 8;
            }
        }
        return;
    }


    if(mode == 1)
    {
        const int envelope = (progress * inv_progress) >> 6;

        const int gain =
            warp_amt + ((warp_amt * response) >> 8);

        const int soften = 256 - (stability >> 1);

        #pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int gy = 0; gy < m->grid_h; gy++)
        {
            int y = gy << FLOW_SHIFT;
            if(y >= h) y = h - 1;

            int up = (y >= FLOW_SIZE) ? y - FLOW_SIZE : 0;
            int dn = (y < h - FLOW_SIZE) ? y + FLOW_SIZE : h - 1;

            for(int gx = 0; gx < m->grid_w; gx++)
            {
                int x = gx << FLOW_SHIFT;
                if(x >= w) x = w - 1;

                int lx = (x >= FLOW_SIZE) ? x - FLOW_SIZE : 0;
                int rx = (x < w - FLOW_SIZE) ? x + FLOW_SIZE : w - 1;

                int gi = gy * m->grid_w + gx;

                int dx1 = Y2[y*w+rx] - Y2[y*w+lx];
                int dy1 = Y2[dn*w+x] - Y2[up*w+x];

                int dx2 = sY[y*w+rx] - sY[y*w+lx];
                int dy2 = sY[dn*w+x] - sY[up*w+x];

                m->flow_x1[gi] = (dx1 * gain * envelope) >> 16;
                m->flow_y1[gi] = (dy1 * gain * envelope) >> 16;
                m->flow_x2[gi] = (dx2 * gain * envelope) >> 16;
                m->flow_y2[gi] = (dy2 * gain * envelope) >> 16;
            }
        }

        #pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 0; y < h; y++)
        {
            int gy  = y >> FLOW_SHIFT;
            int yf  = y & FLOW_MASK;
            int row = y * w;

            for(int x = 0; x < w; x++)
            {
                int gx = x >> FLOW_SHIFT;
                int xf = x & FLOW_MASK;
                int gi = gy * m->grid_w + gx;

                int topx1 = m->flow_x1[gi] +
                    (((m->flow_x1[gi+1] - m->flow_x1[gi]) * xf) >> FLOW_SHIFT);
                int botx1 = m->flow_x1[gi+m->grid_w] +
                    (((m->flow_x1[gi+m->grid_w+1] - m->flow_x1[gi+m->grid_w]) * xf) >> FLOW_SHIFT);

                int topy1 = m->flow_y1[gi] +
                    (((m->flow_y1[gi+1] - m->flow_y1[gi]) * xf) >> FLOW_SHIFT);
                int boty1 = m->flow_y1[gi+m->grid_w] +
                    (((m->flow_y1[gi+m->grid_w+1] - m->flow_y1[gi+m->grid_w]) * xf) >> FLOW_SHIFT);

                int topx2 = m->flow_x2[gi] +
                    (((m->flow_x2[gi+1] - m->flow_x2[gi]) * xf) >> FLOW_SHIFT);
                int botx2 = m->flow_x2[gi+m->grid_w] +
                    (((m->flow_x2[gi+m->grid_w+1] - m->flow_x2[gi+m->grid_w]) * xf) >> FLOW_SHIFT);

                int topy2 = m->flow_y2[gi] +
                    (((m->flow_y2[gi+1] - m->flow_y2[gi]) * xf) >> FLOW_SHIFT);
                int boty2 = m->flow_y2[gi+m->grid_w] +
                    (((m->flow_y2[gi+m->grid_w+1] - m->flow_y2[gi+m->grid_w]) * xf) >> FLOW_SHIFT);

                int wx1 = topx1 + (((botx1 - topx1) * yf) >> FLOW_SHIFT);
                int wy1 = topy1 + (((boty1 - topy1) * yf) >> FLOW_SHIFT);
                int wx2 = topx2 + (((botx2 - topx2) * yf) >> FLOW_SHIFT);
                int wy2 = topy2 + (((boty2 - topy2) * yf) >> FLOW_SHIFT);

                wx1 = (wx1 * soften) >> 8;
                wy1 = (wy1 * soften) >> 8;
                wx2 = (wx2 * soften) >> 8;
                wy2 = (wy2 * soften) >> 8;

                int sx1 = CLAMP(x + wx1,0,w-1);
                int sy1 = CLAMP(y + wy1,0,h-1);
                int sx2 = CLAMP(x - wx2,0,w-1);
                int sy2 = CLAMP(y - wy2,0,h-1);

                int idx = row + x;
                int a = sy1*w + sx1;
                int b = sy2*w + sx2;

                Y[idx]  = (sY[a]  * inv_progress + Y2[b] * progress) >> 8;
                Cb[idx] = (sCb[a] * inv_progress + Cb2[b] * progress) >> 8;
                Cr[idx] = (sCr[a] * inv_progress + Cr2[b] * progress) >> 8;
            }
        }
        return;
    }


    {
        const int env = (progress * inv_progress) >> 7;
        const int impact = ((warp_amt * (response + 32)) >> 8);
        const int persistence = 96 + ((stability * 156) >> 8);

        #pragma omp parallel for schedule(static) num_threads(m->n_threads)
        for(int y = 1; y < h - 1; y++)
        {
            int row = y * w;

            for(int x = 1; x < w - 1; x++)
            {
                int idx = row + x;
                int fx =
                    ((sY[idx+1] - sY[idx-1]) +
                    (Y2[idx+1] - Y2[idx-1])) << 4;

                int fy =
                    ((sY[idx+w] - sY[idx-w]) +
                    (Y2[idx+w] - Y2[idx-w])) << 4;

                int vx =
                    (m->flow_x1[idx] * persistence +
                    fx * impact) >> 8;

                int vy =
                    (m->flow_y1[idx] * persistence +
                    fy * impact) >> 8;

                vx = CLAMP(vx, -32000, 32000);
                vy = CLAMP(vy, -32000, 32000);

                m->flow_x1[idx] = vx;
                m->flow_y1[idx] = vy;

                int shift_x = (vx * env) >> 9;
                int shift_y = (vy * env) >> 9;

                int ax = CLAMP(x + shift_x, 0, w - 1);
                int ay = CLAMP(y + shift_y, 0, h - 1);
                int bx = CLAMP(x - shift_x, 0, w - 1);
                int by = CLAMP(y - shift_y, 0, h - 1);

                int a = ay * w + ax;
                int b = by * w + bx;

                Y[idx]  = (sY[a]  * inv_progress + Y2[b] * progress) >> 8;

                Cb[idx] = (sCb[idx] * inv_progress + Cb2[b] * progress) >> 8;
                Cr[idx] = (sCr[idx] * inv_progress + Cr2[b] * progress) >> 8;
            }
        }
    }
}