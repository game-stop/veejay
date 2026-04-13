/*
 * VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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
#include "flare.h"

#ifndef CLAMP_Y
#define CLAMP_Y(v) ((v) > 255 ? 255 : ((v) < 0 ? 0 : (v)))
#endif

#ifndef CLAMP_UV
#define CLAMP_UV(v) ((v) > 255 ? 255 : ((v) < 0 ? 0 : (v)))
#endif

#define FLARE_LEVELS 6
vj_effect *flare_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults =  (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;
    ve->defaults[1] = 25;
    ve->defaults[2] = 15;
    ve->defaults[3] = 160;

    ve->description = "Filmic Glow";

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 5;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 100;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params , "Mode", "Opacity", "Spread", "Threshold" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Additive",
        "Screen",
        "Soft Light",
        "Filmic Add",
        "Highlight Compression",
        "Energy Preserving"
    );

    return ve;
}

typedef struct {
    uint8_t *flare_buf[3];
    uint8_t *pyrY[FLARE_LEVELS];
    int pyrW[FLARE_LEVELS];
    int pyrH[FLARE_LEVELS];
    int n_threads;
    uint16_t lin_lut[256];
    uint8_t  inv_lut[1024];
    uint8_t gamma_lut[256];
    uint8_t *blur_tmp;
    int blur_tmp_size;
} flare_t;

static void init_lut(flare_t *g)
{
    for (int i = 0; i < 256; i++) {
        float f = i / 255.0f;
        g->lin_lut[i] = (uint16_t)(powf(f, 2.2f) * 1023.0f);
    }
    for (int i = 0; i < 1024; i++) {
        float f = i / 1023.0f;
        g->inv_lut[i] = (uint8_t)(powf(f, 1.0f / 2.2f) * 255.0f);
    }

    for (int i = 0; i < 256; i++)
    {
        float x = i / 255.0f;
        float gamma = powf(x, 1.6f);
        gamma = gamma * 255.0f;
        gamma = (gamma > 255.0f) ? 255.0f : gamma;
        g->gamma_lut[i] = (uint8_t)gamma;
    }
}

void *flare_malloc(int w, int h)
{
    flare_t *f = (flare_t*) vj_calloc(sizeof(flare_t));
    if (!f) return NULL;

    const int len = w * h;

    f->flare_buf[0] = (uint8_t*) vj_malloc(len * 3);
    if (!f->flare_buf[0]) {
        free(f);
        return NULL;
    }

    f->flare_buf[1] = f->flare_buf[0] + len;
    f->flare_buf[2] = f->flare_buf[1] + len;

    int cw = w;
    int ch = h;

    for (int i = 0; i < FLARE_LEVELS; i++)
    {
        cw >>= 1;
        ch >>= 1;

        if (cw < 2) cw = 2;
        if (ch < 2) ch = 2;

        f->pyrW[i] = cw;
        f->pyrH[i] = ch;

        f->pyrY[i] = (uint8_t*) vj_malloc(cw * ch);
        if (!f->pyrY[i]) {
            flare_free(f);
            return NULL;
        }
    }

    f->n_threads = vje_advise_num_threads(len);

    init_lut(f);

    return f;
}

void flare_free(void *ptr)
{
    flare_t *f = (flare_t*) ptr;
    if (!f) return;

    if (f->flare_buf[0]) free(f->flare_buf[0]);

    for (int i = 0; i < FLARE_LEVELS; i++)
        if (f->pyrY[i]) free(f->pyrY[i]);

    free(f);
}

static void blur_line_inplace(uint8_t *data, int len, int radius, int step) 
{
    if (radius <= 0 || len < 2) return;

    uint8_t src[len]; // on heap thread safe
    for (int i = 0; i < len; i++)
        src[i] = data[i * step];

    const int length = radius * 2 + 1;
    const uint32_t inv = (uint32_t)(((1ULL << 24) + (length / 2)) / length);

    uint32_t sum = src[0] * (radius + 1);
    for (int i = 1; i <= radius; i++) {
        sum += src[(i < len ? i : len - 1)];
    }

    for (int i = 0; i < len; i++) {
        data[i * step] = (uint8_t)((sum * inv + (1 << 23)) >> 24);

        int out_idx = i - radius;
        int in_idx  = i + radius + 1;

        uint8_t out_val = out_idx < 0 ? src[0] : src[out_idx];
        uint8_t in_val  = in_idx >= len ? src[len - 1] : src[in_idx];

        sum += in_val - out_val;
    }
}

static const uint8_t bloom_weights[FLARE_LEVELS] = {
    255, 200, 150, 100, 64, 32
};

void flare_apply(void *ptr, VJFrame *frame, int *args)
{
    const int type      = args[0];
    const int opacity   = args[1];
    const int spread    = args[2];
    const int threshold = args[3];

    flare_t *f = (flare_t*) ptr;

    const int W = frame->width;
    const int H = frame->height;
    const int len = frame->len;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];
    uint8_t *restrict srcY = f->flare_buf[0];

    int minX = W, maxX = 0, minY = H, maxY = 0;
    int hasGlow = 0;

    uint16_t *restrict lin = f->lin_lut;


    #pragma omp parallel for reduction(|:hasGlow) reduction(min:minX,minY) reduction(max:maxX,maxY) num_threads(f->n_threads)
    for (int i = 0; i < len; i++)
    {
        int y = lin[dstY[i]];
        int t = (threshold << 2 ) + 16;
        int v = y - t;

        if (v < 0) v = 0;

        v = v + (v >> 1);

        int out = 0;

        if (v > 0)
        {
            int denom = 1023 - (t >> 1);
            if (denom < 128) denom = 128;

            int x = (v << 8) / denom;
            if (x > 320) x = 320;
            x = x + ((x * x) >> 8); // quadratic boost
            if (x > 512) x = 512;

            int knee = (t >> 1) + 16;
            int k = (v * 256) / knee;
            if (k > 256) k = 256;

            int smooth = (k * k * (3 * 256 - 2 * k)) >> 16;
            x = (x * smooth) >> 8;
            x = x >> 1;
            if (x > 255) x = 255;

            out = (uint8_t)x;
            hasGlow = 1;
        }
        else
        {
            out = 0;
        }

        srcY[i] = (uint8_t) out;

        if (out)
        {
            int px = i % W;
            int py = i / W;

            if (px < minX) minX = px;
            if (px > maxX) maxX = px;
            if (py < minY) minY = py;
            if (py > maxY) maxY = py;
        }
    }
    if (!hasGlow) return; // nothing to do

    int bounding_padding = (spread * 4) + (1 << FLARE_LEVELS);
    minX = (minX - bounding_padding < 0) ? 0 : minX - bounding_padding;
    maxX = (maxX + bounding_padding >= W) ? W - 1 : maxX + bounding_padding;
    minY = (minY - bounding_padding < 0) ? 0 : minY - bounding_padding;
    maxY = (maxY + bounding_padding >= H) ? H - 1 : maxY + bounding_padding;

    int rw = maxX - minX + 1;
    int rh = maxY - minY + 1;

    uint8_t *restrict prev = srcY + minY * W + minX;
    int pw = rw;
    int ph = rh;

    // downsample pyramid
    for (int l = 0; l < FLARE_LEVELS; l++)
    {
        uint8_t *dst = f->pyrY[l];
        int nw = f->pyrW[l];
        int nh = f->pyrH[l];

        #pragma omp parallel for num_threads(f->n_threads)
        for (int y = 0; y < nh; y++)
        {
            int sy = y * 2;
            int row_dst = y * nw;
            int max_x = nw; // pw << 1
            #pragma omp simd
            for (int x = 0; x < max_x; x++)
            {
                int sx = x * 2;
                int idx = sy * pw + sx;
                int sum = prev[idx] + prev[idx + 1] + prev[idx + pw] + prev[idx + pw + 1];
                dst[row_dst + x] = sum >> 2;
            }

            for (int x = max_x; x < nw; x++)
            {
                int sx = x * 2;
                int sy_clamped = (sy + 1 < ph) ? sy + 1 : sy;

                int idx00 = sy * pw + sx;
                int idx01 = (sx + 1 < pw) ? idx00 + 1 : idx00;
                int idx10 = sy_clamped * pw + sx;
                int idx11 = (sx + 1 < pw) ? idx10 + 1 : idx10;

                dst[row_dst + x] = (prev[idx00] + prev[idx01] + prev[idx10] + prev[idx11]) >> 2;
            }
        }
        prev = dst;
        pw = nw;
        ph = nh;
    }

    // multi octave spread on downsampled buffers
    #pragma omp parallel num_threads(f->n_threads)
    for (int l = 0; l < FLARE_LEVELS; l++)
    {
        int nw = f->pyrW[l];
        int nh = f->pyrH[l];

        int r = spread >> (l + 1);
        if (r < 1 && spread > 0) r = 1;
        if (r < 1) continue;

        uint8_t *buf = f->pyrY[l];

        #pragma omp for schedule(static)
        for (int y = 0; y < nh; y++) {
            blur_line_inplace(buf + y * nw, nw, r, 1);
        }

        #pragma omp for schedule(static)
        for (int x = 0; x < nw; x++) {
            blur_line_inplace(buf + x, nh, r, nw);
        }
    }

    // bilinear upsample and accumulate
    for (int l = FLARE_LEVELS - 1; l > 0; l--)
    {
        uint8_t *restrict src = f->pyrY[l];
        uint8_t *restrict dst = f->pyrY[l-1];
        const int sw = f->pyrW[l];
        const int sh = f->pyrH[l];
        const int dw = f->pyrW[l-1];
        const int dh = f->pyrH[l-1];

        const int scaleX = dw > 1 ? ((sw - 1) << 8) / (dw - 1) : 0;
        const int scaleY = dh > 1 ? ((sh - 1) << 8) / (dh - 1) : 0;

        #pragma omp parallel for schedule(static) num_threads(f->n_threads)
        for (int y = 0; y < dh; y++)
        {
            int gy = y * scaleY;
            int gyi = gy >> 8;
            int ty = gy & 255;

            int row0 = gyi * sw;
            int row1 = (gyi + 1 < sh ? gyi + 1 : gyi) * sw;

            int row_dst = y * dw;

            #pragma omp simd
            for (int x = 0; x < dw; x++)
            {
                int gx = x * scaleX;
                int gxi = gx >> 8;
                int tx = gx & 255;

                int x1 = (gxi + 1 < sw) ? gxi + 1 : gxi;

                int c00 = src[row0 + gxi];
                int c10 = src[row0 + x1];
                int c01 = src[row1 + gxi];
                int c11 = src[row1 + x1];

                int a = (c00 << 8) + (c10 - c00) * tx;
                int b = (c01 << 8) + (c11 - c01) * tx;

                int interpolated = ((a << 8) + (b - a) * ty) >> 16;
                interpolated = (interpolated * bloom_weights[l]) >> 8;

                int v = dst[row_dst + x] + interpolated;
                dst[row_dst + x] = (v > 255) ? 255 : v;
            }
        }
    }

    uint8_t *base_bloom = f->pyrY[0];
    const int bw = f->pyrW[0];
    const int bh = f->pyrH[0];

    const int scaleX = rw > 1 ? ((bw - 1) << 8) / (rw - 1) : 0;
    const int scaleY = rh > 1 ? ((bh - 1) << 8) / (rh - 1) : 0;

    #pragma omp parallel for schedule(static) num_threads(f->n_threads)
    for (int y = 0; y < rh; y++)
    {
        int gy = y * scaleY;
        int gyi = gy >> 8;
        int ty = gy & 255;
        int inv_ty = 256 - ty;

        int row0 = gyi * bw;
        int row1 = (gyi + 1 < bh ? gyi + 1 : gyi) * bw;

        int dst_row = (minY + y) * W + minX;

        #pragma omp simd
        for (int x = 0; x < rw; x++)
        {
            int gx = x * scaleX;
            int gxi = gx >> 8;
            int tx = gx & 255;
            int inv_tx = 256 - tx;

            int x1 = (gxi + 1 < bw) ? gxi + 1 : gxi;

            int c00 = base_bloom[row0 + gxi];
            int c10 = base_bloom[row0 + x1];
            int c01 = base_bloom[row1 + gxi];
            int c11 = base_bloom[row1 + x1];

            int a = c00 * inv_tx + c10 * tx;
            int b = c01 * inv_tx + c11 * tx;

            int interpolated = (a * inv_ty + b * ty) >> 16;

            int i = dst_row + x;
            int v = srcY[i] + interpolated;
            srcY[i] = (v > 255) ? 255 : v;
        }
    }

    // composite
    switch (type)
    {
        case 0:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;

                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int lin_base = lin[dstY[i]];
                    int lin_glow = lin[glow];

                    int sum = lin_base + lin_glow;
                    if (sum > 1023) sum = 1023;

                    dstY[i] = f->inv_lut[sum];

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;

        case 1:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;

                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int a = lin[dstY[i]];
                    int b = lin[glow];

                    int inv = ((1023 - a) * (1023 - b)) >> 10;
                    int res = 1023 - inv;

                    dstY[i] = f->inv_lut[res];

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;
        case 2:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;
                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int base = dstY[i];
                    int res = base + glow + ((base * glow) >> 8);

                    dstY[i] = CLAMP_Y(res);

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;
        case 3:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;

                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int lin_base = lin[dstY[i]];
                    int lin_glow = lin[glow];

                    int sum = lin_base + lin_glow;
                    int mapped = (sum << 10) / (sum + 1023);

                    dstY[i] = f->inv_lut[mapped];

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;
        case 4:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;

                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int lin_base = lin[dstY[i]];
                    int lin_glow = lin[glow];

                    int combined = lin_base + lin_glow;

                    int compressed = combined - ((combined * combined) >> 11);
                    if (compressed > 1023) compressed = 1023;

                    dstY[i] = f->inv_lut[compressed];

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;
        case 5:
            #pragma omp parallel for num_threads(f->n_threads) schedule(static)
            for (int y = minY; y <= maxY; y++) {
                //#pragma omp simd
                for (int x = minX; x <= maxX; x++) {
                    int i = y * W + x;

                    int glow = (srcY[i] * opacity) >> 8;
                    if (glow <= 0) continue;

                    int lin_base = lin[dstY[i]];
                    int lin_glow = lin[glow];

                    int res = lin_base + ((lin_glow * (1023 - lin_base)) >> 10);

                    dstY[i] = f->inv_lut[res];

                    int u = dstU[i] - 128;
                    int v = dstV[i] - 128;
                    dstU[i] = CLAMP_UV(128 + u + ((u * glow) >> 9));
                    dstV[i] = CLAMP_UV(128 + v + ((v * glow) >> 9));
                }
            }
            break;
    }
}
