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

#include <config.h>
#include <omp.h>
#include <stdint.h>
#include <sys/types.h>
#include <math.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libvje/internal.h>
#include <veejaycore/vjmem.h>
#include "hist.h"

typedef struct
{
    uint32_t hY[256];
    uint32_t hR[256];
    uint32_t hG[256];
    uint32_t hB[256];
} histogram_t;

static inline int vj_hist_clamp255(int v)
{
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

static inline uint8_t vj_hist_div255_u8(uint32_t v)
{
    v += 128;
    return (uint8_t)((v + (v >> 8)) >> 8);
}

void *veejay_histogram_new(void)
{
    histogram_t *h = (histogram_t*) vj_calloc(sizeof(histogram_t));
    return h;
}

void veejay_histogram_del(void *his)
{
    histogram_t *h = (histogram_t*) his;
    if (h)
        free(h);
}

static void build_histogram(histogram_t *h, VJFrame *f)
{
    uint32_t *restrict H = h->hY;
    const uint8_t *restrict p = f->data[0];
    const int len = f->len;

    if (!p || len <= 0)
        return;

    #pragma omp single
    veejay_memset(H, 0, sizeof(uint32_t) * 256);

    uint32_t LH[256] = {0};

    #pragma omp for schedule(static) nowait
    for (int i = 0; i < len; i++) {
        LH[p[i]]++;
    }

    #pragma omp critical
    {
        for (int i = 0; i < 256; i++) {
            H[i] += LH[i];
        }
    }
    #pragma omp barrier
}

static void build_histogram_rgb(uint8_t *rgb, histogram_t *h, VJFrame *f)
{
    uint32_t *restrict Hr = h->hR;
    uint32_t *restrict Hg = h->hG;
    uint32_t *restrict Hb = h->hB;

    const int pixels = f->width * f->height;

    if (!rgb || pixels <= 0)
        return;

    #pragma omp single
    {
        veejay_memset(Hr, 0, sizeof(uint32_t) * 256);
        veejay_memset(Hg, 0, sizeof(uint32_t) * 256);
        veejay_memset(Hb, 0, sizeof(uint32_t) * 256);
    }

    uint32_t LR[256] = {0};
    uint32_t LG[256] = {0};
    uint32_t LB[256] = {0};

    #pragma omp for schedule(static) nowait
    for (int i = 0; i < pixels; i++)
    {
        const int k = i * 3;
        LR[rgb[k + 0]]++;
        LG[rgb[k + 1]]++;
        LB[rgb[k + 2]]++;
    }

    #pragma omp critical
    {
        for (int i = 0; i < 256; i++)
        {
            Hr[i] += LR[i];
            Hg[i] += LG[i];
            Hb[i] += LB[i];
        }
    }
    #pragma omp barrier
}

static inline void veejay_lut_calc(
    const uint32_t *restrict h,
    uint8_t *restrict lut,
    int intensity,
    int strength,
    int len)
{
    intensity = vj_hist_clamp255(intensity);
    strength  = vj_hist_clamp255(strength);

    if (len <= 0 || intensity <= 0 || strength <= 0)
    {
        for (int i = 0; i < 256; i++)
            lut[i] = (uint8_t) i;
        return;
    }

    uint32_t cdf_min = 0;
    uint32_t acc_probe = 0;

    for (int i = 0; i < 256; i++)
    {
        acc_probe += h[i];

        if (acc_probe != 0)
        {
            cdf_min = acc_probe;
            break;
        }
    }

    const uint32_t denom = (uint32_t) len - cdf_min;

    if (denom == 0)
    {
        for (int i = 0; i < 256; i++)
            lut[i] = (uint8_t) i;
        return;
    }

    const uint64_t scale = ((uint64_t)255 << 32) / denom;
    uint32_t acc = 0;

    for (int i = 0; i < 256; i++)
    {
        acc += h[i];

        uint32_t eq = 0;

        if (acc > cdf_min)
            eq = (uint32_t)((((uint64_t)(acc - cdf_min) * scale) + (1ULL << 31)) >> 32);

        eq = (eq > 255) ? 255 : eq;

        const uint8_t target = vj_hist_div255_u8(
            ((uint32_t)i * (uint32_t)(255 - intensity)) +
            (eq * (uint32_t)intensity)
        );

        lut[i] = vj_hist_div255_u8(
            ((uint32_t)i * (uint32_t)(255 - strength)) +
            ((uint32_t)target * (uint32_t)strength)
        );
    }
}


static inline void veejay_histogram_make_bars(
    uint8_t *restrict bars,
    const uint32_t *restrict hist,
    int height)
{
    uint32_t maxv = 0;

    for (int i = 0; i < 256; i++)
        maxv = (hist[i] > maxv) ? hist[i] : maxv;

    if (maxv == 0 || height <= 0)
    {
        veejay_memset(bars, 0, 256);
        return;
    }

    for (int i = 0; i < 256; i++)
        bars[i] = (uint8_t)(((uint64_t)hist[i] * (uint64_t)height + (maxv >> 1)) / maxv);
}


static inline void veejay_histogram_qdraw(
    const uint32_t *restrict histi,
    VJFrame *restrict f,
    uint8_t *restrict plane,
    int left,
    int down)
{
    if (!f || !plane || !histi)
        return;

    const int w = f->width;
    const int h = f->height;

    if (w <= 0 || h <= 0)
        return;

    int panel_w = w / 5;
    int panel_h = h / 5;

    if (panel_w < 16 || panel_h < 8)
        return;

    left = (left < 0) ? 0 : left;
    down = (down < 0) ? 0 : down;

    if (left >= w)
        return;

    if (left + panel_w > w)
        panel_w = w - left;

    int top = h - panel_h - down;

    if (top < 0)
        top = 0;

    if (top + panel_h > h)
        panel_h = h - top;

    if (panel_w <= 0 || panel_h <= 0)
        return;

    // Every thread builds a local stack copy of the bars to avoid #pragma omp single synchronization locks.
    uint8_t bars[256];
    veejay_histogram_make_bars(bars, histi, panel_h - 2);

    uint8_t *restrict U = f->data[1];
    uint8_t *restrict V = f->data[2];
    const int ssm = f->ssm;

    #pragma omp for schedule(static)
    for (int y = 0; y < panel_h; y++)
    {
        const int yy = top + y;
        uint8_t *restrict rowY = plane + (yy * w) + left;

        for (int x = 0; x < panel_w; x++) {
            rowY[x] = (uint8_t)((rowY[x] >> 2) + 16);
        }

        // Fix the color bleed: neutral gray the U/V planes in the histogram area
        if (U && V) {
            if (ssm) {
                uint8_t *restrict rowU = U + (yy * w) + left;
                uint8_t *restrict rowV = V + (yy * w) + left;
                for (int x = 0; x < panel_w; x++) {
                    rowU[x] = 128;
                    rowV[x] = 128;
                }
            } else {
                // Support 4:2:2 half-width spacing
                const int uv_w = w / 2;
                const int uv_left = left / 2;
                const int uv_panel_w = panel_w / 2;
                uint8_t *restrict rowU = U + (yy * uv_w) + uv_left;
                uint8_t *restrict rowV = V + (yy * uv_w) + uv_left;
                for (int x = 0; x < uv_panel_w; x++) {
                    rowU[x] = 128;
                    rowV[x] = 128;
                }
            }
        }
    }

    #pragma omp for schedule(static)
    for (int x = 0; x < panel_w; x++)
    {
        const int bin = (x * 256) / panel_w;
        const int bh = bars[bin];

        for (int y = 0; y < bh; y++)
        {
            const int yy = top + panel_h - 2 - y;

            if (yy >= top && yy < top + panel_h)
                plane[(yy * w) + left + x] = 235;
        }
    }

    #pragma omp for schedule(static)
    for (int x = 0; x < panel_w; x++) {
        plane[((top + panel_h - 1) * w) + left + x] = 255;
    }
}


void veejay_histogram_equalize(void *his, VJFrame *f, int intensity, int strength)
{
    histogram_t *h = (histogram_t*) his;

    if (!h || !f || !f->data[0] || f->len <= 0)
        return;

    uint8_t LUT[256];
    uint8_t *restrict y = f->data[0];
    const int len = f->len;

    #pragma omp single copyprivate(LUT)
    {
        veejay_lut_calc(h->hY, LUT, intensity, strength, len);
    }

    #pragma omp for schedule(static) 
    for (int i = 0; i < len; i++)
        y[i] = LUT[y[i]];
}

void veejay_histogram_equalize_rgb(
    void *his,
    VJFrame *f,
    uint8_t *rgb,
    int intensity,
    int strength,
    int mode)
{
    histogram_t *h = (histogram_t*) his;

    if (!h || !f || !rgb)
        return;

    const int pixels = f->width * f->height;

    if (pixels <= 0)
        return;

    uint8_t LUTr[256];
    uint8_t LUTg[256];
    uint8_t LUTb[256];

    #pragma omp single copyprivate(LUTr, LUTg, LUTb)
    {
        if (mode == 0 || mode == 3) veejay_lut_calc(h->hR, LUTr, intensity, strength, pixels);
        if (mode == 1 || mode == 3) veejay_lut_calc(h->hG, LUTg, intensity, strength, pixels);
        if (mode == 2 || mode == 3) veejay_lut_calc(h->hB, LUTb, intensity, strength, pixels);
    }

    switch (mode)
    {
        case 0:
            #pragma omp for schedule(static)
            for (int i = 0; i < pixels; i++)
            {
                const int k = i * 3;
                rgb[k + 0] = LUTr[rgb[k + 0]];
            }
            break;

        case 1:
            #pragma omp for schedule(static) 
            for (int i = 0; i < pixels; i++)
            {
                const int k = i * 3;
                rgb[k + 1] = LUTg[rgb[k + 1]];
            }
            break;

        case 2:
            #pragma omp for schedule(static)
            for (int i = 0; i < pixels; i++)
            {
                const int k = i * 3;
                rgb[k + 2] = LUTb[rgb[k + 2]];
            }
            break;

        case 3:
        default:
            #pragma omp for schedule(static)
            for (int i = 0; i < pixels; i++)
            {
                const int k = i * 3;

                rgb[k + 0] = LUTr[rgb[k + 0]];
                rgb[k + 1] = LUTg[rgb[k + 1]];
                rgb[k + 2] = LUTb[rgb[k + 2]];
            }
            break;
    }
}

void veejay_histogram_draw(void *his, VJFrame *org, VJFrame *f, int intensity, int strength)
{
    histogram_t *h = (histogram_t*) his;

    if (!h || !org || !f || !f->data[0])
        return;

    veejay_histogram_analyze(his, org, 0);
    
    veejay_histogram_qdraw(h->hY, f, f->data[0], 0, 0);

    veejay_histogram_equalize(his, org, intensity, strength);
    veejay_histogram_analyze(his, org, 0);

    veejay_histogram_qdraw(h->hY, f, f->data[0], (f->width / 4) + 10, 0);
}

void veejay_histogram_draw_rgb(
    void *his,
    VJFrame *f,
    uint8_t *rgb,
    int in,
    int st,
    int mode)
{
    histogram_t *h = (histogram_t*) his;

    if (!h || !f || !rgb || !f->data[0])
        return;

    veejay_histogram_analyze_rgb(his, rgb, f);

    switch (mode)
    {
        case 0:
            veejay_histogram_qdraw(h->hR, f, f->data[0], 0, f->height / 4);
            break;
        case 1:
            veejay_histogram_qdraw(h->hG, f, f->data[0], 0, f->height / 4);
            break;
        case 2:
            veejay_histogram_qdraw(h->hB, f, f->data[0], 0, f->height / 4);
            break;
        case 3:
        default:
            veejay_histogram_qdraw(h->hR, f, f->data[0], 0, f->height / 4);
            veejay_histogram_qdraw(h->hG, f, f->data[0], (f->width / 4) + 10, f->height / 4);
            veejay_histogram_qdraw(h->hB, f, f->data[0], ((f->width / 4) + 10) * 2, f->height / 4);
            break;
    }

    veejay_histogram_equalize_rgb(his, f, rgb, in, st, mode);
    veejay_histogram_analyze_rgb(his, rgb, f);

    switch (mode)
    {
        case 0:
            veejay_histogram_qdraw(h->hR, f, f->data[0], 0, 0);
            break;
        case 1:
            veejay_histogram_qdraw(h->hG, f, f->data[0], 0, 0);
            break;
        case 2:
            veejay_histogram_qdraw(h->hB, f, f->data[0], 0, 0);
            break;
        case 3:
        default:
            veejay_histogram_qdraw(h->hR, f, f->data[0], 0, 0);
            veejay_histogram_qdraw(h->hG, f, f->data[0], (f->width / 4) + 10, 0);
            veejay_histogram_qdraw(h->hB, f, f->data[0], ((f->width / 4) + 10) * 2, 0);
            break;
    }
}

void vje_histogram_auto_eq(VJFrame *frame)
{
    if (!frame || !frame->data[0] || frame->len <= 0)
        return;

    uint32_t *global_H = NULL;

    #pragma omp single copyprivate(global_H)
    {
        global_H = (uint32_t*) vj_calloc(256 * sizeof(uint32_t));
    }

    uint32_t LH[256] = {0};
    const uint8_t *restrict p = frame->data[0];
    const int len = frame->len;

    #pragma omp for schedule(static) nowait
    for (int i = 0; i < len; i++) {
        LH[p[i]]++;
    }

    #pragma omp critical
    {
        for (int i = 0; i < 256; i++) {
            global_H[i] += LH[i];
        }
    }
    #pragma omp barrier

    uint8_t LUT[256];

    #pragma omp single copyprivate(LUT)
    {
        veejay_lut_calc(global_H, LUT, 255, 255, len);
        free(global_H);
    }

    uint8_t *restrict Y = frame->data[0];

    #pragma omp for schedule(static)
    for (int i = 0; i < len; i++) {
        Y[i] = LUT[Y[i]];
    }
}

void vje_histogram_auto_eq_serial(VJFrame *frame)
{
    if (!frame || !frame->data[0] || frame->len <= 0)
        return;

    uint32_t H[256] = {0};
    const uint8_t *restrict p = frame->data[0];
    const int len = frame->len;

    for (int i = 0; i < len; i++) {
        H[p[i]]++;
    }

    uint8_t LUT[256];
    veejay_lut_calc(H, LUT, 255, 255, len);

    uint8_t *restrict Y = frame->data[0];
    for (int i = 0; i < len; i++) {
        Y[i] = LUT[Y[i]];
    }
}

void veejay_histogram_analyze_rgb(void *his, uint8_t *rgb, VJFrame *f)
{
    histogram_t *h = (histogram_t*) his;

    if (!h || !rgb || !f)
        return;

    build_histogram_rgb(rgb, h, f);
}

void veejay_histogram_analyze(void *his, VJFrame *f, int type)
{
    histogram_t *h = (histogram_t*) his;

    (void) type;

    if (!h || !f)
        return;

    build_histogram(h, f);
}