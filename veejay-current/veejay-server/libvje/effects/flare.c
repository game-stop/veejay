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
#include "flare.h"

#include <math.h>
#include <stdint.h>

#define FLARE_LEVELS 6

typedef struct {
    uint8_t *flare_buf[3];
    uint8_t *pyrY[FLARE_LEVELS];
    int pyrW[FLARE_LEVELS];
    int pyrH[FLARE_LEVELS];
    int n_threads;
    uint16_t lin_lut[256];
    uint8_t inv_lut[1024];
} flare_t;

static const uint8_t bloom_weights[FLARE_LEVELS] = {
    255, 200, 150, 100, 64, 32
};

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t flare_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline uint8_t flare_uv(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static void flare_init_lut(flare_t *f)
{
    for(int i = 0; i < 256; i++) {
        float x = (float)i * (1.0f / 255.0f);
        int v = (int)(powf(x, 2.2f) * 1023.0f + 0.5f);
        f->lin_lut[i] = (uint16_t) clampi(v, 0, 1023);
    }

    for(int i = 0; i < 1024; i++) {
        float x = (float)i * (1.0f / 1023.0f);
        int v = (int)(powf(x, 1.0f / 2.2f) * 255.0f + 0.5f);
        f->inv_lut[i] = flare_u8(v);
    }
}

vj_effect *flare_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 4;
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

    ve->defaults[0] = 0;
    ve->defaults[1] = 25;
    ve->defaults[2] = 15;
    ve->defaults[3] = 160;

    ve->limits[0][0] = 0; ve->limits[1][0] = 5;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;
    ve->limits[0][2] = 0; ve->limits[1][2] = 100;
    ve->limits[0][3] = 0; ve->limits[1][3] = 255;

    ve->description = "Filmic Glow";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Opacity", "Spread", "Threshold");

    ve->hints = vje_init_value_hint_list(ve->num_params);
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

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_GLOW,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                48,                 245,                18, 68,  600, 2600, 0,    90,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,           6,                  92,                 4,  14, 3000, 8200, 2200, 22,
        VJ_BEAT_DETAIL,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,            56,                 220,                14, 54,  800, 3000, 0,    78
    );

    return ve;
}

void *flare_malloc(int w, int h)
{

    flare_t *f = (flare_t*) vj_calloc(sizeof(flare_t));

    if(!f)
        return NULL;

    const int len = w * h;

    f->flare_buf[0] = (uint8_t*) vj_malloc((size_t)len * 3);
    if(!f->flare_buf[0]) {
        free(f);
        return NULL;
    }

    f->flare_buf[1] = f->flare_buf[0] + len;
    f->flare_buf[2] = f->flare_buf[1] + len;

    int cw = w;
    int ch = h;

    for(int i = 0; i < FLARE_LEVELS; i++) {
        cw = (cw + 1) >> 1;
        ch = (ch + 1) >> 1;

        if(cw < 1)
            cw = 1;
        if(ch < 1)
            ch = 1;

        f->pyrW[i] = cw;
        f->pyrH[i] = ch;
        f->pyrY[i] = (uint8_t*) vj_malloc((size_t)cw * (size_t)ch);

        if(!f->pyrY[i]) {
            flare_free(f);
            return NULL;
        }
    }

    f->n_threads = vje_advise_num_threads(len);

    flare_init_lut(f);

    return f;
}

void flare_free(void *ptr)
{
    flare_t *f = (flare_t*) ptr;

    free(f->flare_buf[0]);

    for(int i = 0; i < FLARE_LEVELS; i++)
        free(f->pyrY[i]);

    free(f);
}

static void flare_downsample2(const uint8_t *restrict src,
                              int sw,
                              int sh,
                              uint8_t *restrict dst,
                              int dw,
                              int dh,
                              int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < dh; y++) {
        int sy0 = y << 1;
        int sy1 = sy0 + 1;

        if(sy0 >= sh)
            sy0 = sh - 1;
        if(sy1 >= sh)
            sy1 = sh - 1;

        const int row0 = sy0 * sw;
        const int row1 = sy1 * sw;
        uint8_t *restrict drow = dst + y * dw;

        for(int x = 0; x < dw; x++) {
            int sx0 = x << 1;
            int sx1 = sx0 + 1;

            if(sx0 >= sw)
                sx0 = sw - 1;
            if(sx1 >= sw)
                sx1 = sw - 1;

            drow[x] = (uint8_t)(((int)src[row0 + sx0] + (int)src[row0 + sx1] +
                                 (int)src[row1 + sx0] + (int)src[row1 + sx1] + 2) >> 2);
        }
    }
}

static void flare_box_blur_u8(uint8_t *restrict buf,
                              uint8_t *restrict tmp,
                              int w,
                              int h,
                              int radius,
                              int n_threads)
{

    const int diameter = radius * 2 + 1;
    const uint32_t inv = (uint32_t)(((1ULL << 24) + (diameter >> 1)) / (uint32_t)diameter);

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++) {
        const uint8_t *restrict src = buf + y * w;
        uint8_t *restrict dst = tmp + y * w;
        uint32_t sum = (uint32_t)src[0] * (uint32_t)(radius + 1);

        for(int k = 1; k <= radius; k++) {
            int ix = k < w ? k : w - 1;
            sum += src[ix];
        }

        for(int x = 0; x < w; x++) {
            dst[x] = (uint8_t)((sum * inv + (1 << 23)) >> 24);

            int out_idx = x - radius;
            int in_idx = x + radius + 1;

            uint8_t out_val = out_idx < 0 ? src[0] : src[out_idx];
            uint8_t in_val = in_idx >= w ? src[w - 1] : src[in_idx];

            sum += in_val;
            sum -= out_val;
        }
    }

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int x = 0; x < w; x++) {
        uint32_t sum = (uint32_t)tmp[x] * (uint32_t)(radius + 1);

        for(int k = 1; k <= radius; k++) {
            int iy = k < h ? k : h - 1;
            sum += tmp[iy * w + x];
        }

        for(int y = 0; y < h; y++) {
            buf[y * w + x] = (uint8_t)((sum * inv + (1 << 23)) >> 24);

            int out_y = y - radius;
            int in_y = y + radius + 1;

            uint8_t out_val = out_y < 0 ? tmp[x] : tmp[out_y * w + x];
            uint8_t in_val = in_y >= h ? tmp[(h - 1) * w + x] : tmp[in_y * w + x];

            sum += in_val;
            sum -= out_val;
        }
    }
}

static void flare_upsample_add(uint8_t *restrict dst,
                               int dw,
                               int dh,
                               const uint8_t *restrict src,
                               int sw,
                               int sh,
                               int weight,
                               int n_threads)
{
    const int scale_x = dw > 1 ? ((sw - 1) << 8) / (dw - 1) : 0;
    const int scale_y = dh > 1 ? ((sh - 1) << 8) / (dh - 1) : 0;

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < dh; y++) {
        int gy = y * scale_y;
        int iy = gy >> 8;
        int fy = gy & 255;
        int ify = 256 - fy;
        int row0 = iy * sw;
        int row1 = (iy + 1 < sh ? iy + 1 : iy) * sw;
        uint8_t *restrict drow = dst + y * dw;

        for(int x = 0; x < dw; x++) {
            int gx = x * scale_x;
            int ix = gx >> 8;
            int fx = gx & 255;
            int ifx = 256 - fx;
            int ix1 = ix + 1 < sw ? ix + 1 : ix;

            int c00 = src[row0 + ix];
            int c10 = src[row0 + ix1];
            int c01 = src[row1 + ix];
            int c11 = src[row1 + ix1];

            int a = c00 * ifx + c10 * fx;
            int b = c01 * ifx + c11 * fx;
            int v = ((a * ify + b * fy) >> 16) * weight >> 8;
            int out = drow[x] + v;

            drow[x] = (uint8_t)(out > 255 ? 255 : out);
        }
    }
}

static inline int flare_sample_bilinear(const uint8_t *restrict src,
                                        int sw,
                                        int sh,
                                        int x,
                                        int y,
                                        int scale_x,
                                        int scale_y)
{
    int gx = x * scale_x;
    int gy = y * scale_y;
    int ix = gx >> 8;
    int iy = gy >> 8;
    int fx = gx & 255;
    int fy = gy & 255;
    int ifx = 256 - fx;
    int ify = 256 - fy;
    int ix1 = ix + 1 < sw ? ix + 1 : ix;
    int iy1 = iy + 1 < sh ? iy + 1 : iy;
    int row0 = iy * sw;
    int row1 = iy1 * sw;
    int c00 = src[row0 + ix];
    int c10 = src[row0 + ix1];
    int c01 = src[row1 + ix];
    int c11 = src[row1 + ix1];
    int a = c00 * ifx + c10 * fx;
    int b = c01 * ifx + c11 * fx;

    return (a * ify + b * fy) >> 16;
}

#define FLARE_PUSH_CHROMA(IDX, GLOW) do { \
    int u__ = (int)dstU[(IDX)] - 128; \
    int v__ = (int)dstV[(IDX)] - 128; \
    dstU[(IDX)] = flare_uv(128 + u__ + ((u__ * (GLOW)) >> 9)); \
    dstV[(IDX)] = flare_uv(128 + v__ + ((v__ * (GLOW)) >> 9)); \
} while(0)

void flare_apply(void *ptr, VJFrame *frame, int *args)
{
    flare_t *f = (flare_t*) ptr;
    const int type = args[0];
    const int opacity = args[1];
    const int spread = args[2];
    const int threshold = args[3];

    if(opacity <= 0)
        return;

    const int W = frame->width;
    const int H = frame->height;
    const int len = frame->len;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];
    uint8_t *restrict maskY = f->flare_buf[0];
    uint8_t *restrict tmp = f->flare_buf[2];
    const uint16_t *restrict lin = f->lin_lut;

    int minX = W;
    int minY = H;
    int maxX = 0;
    int maxY = 0;
    int hasGlow = 0;
    const int t = (threshold << 2) + 16;

#pragma omp parallel for reduction(|:hasGlow) reduction(min:minX,minY) reduction(max:maxX,maxY) schedule(static) num_threads(f->n_threads)
    for(int i = 0; i < len; i++) {
        int v = (int)lin[dstY[i]] - t;
        int out = 0;

        if(v > 0) {
            int denom = 1023 - (t >> 1);

            if(denom < 128)
                denom = 128;

            int x = (v << 8) / denom;

            if(x > 320)
                x = 320;

            x += (x * x) >> 8;

            if(x > 512)
                x = 512;

            int knee = (t >> 1) + 16;
            int k = (v * 256) / knee;

            if(k > 256)
                k = 256;

            int smooth = (k * k * (3 * 256 - 2 * k)) >> 16;
            x = ((x * smooth) >> 8) >> 1;

            if(x > 255)
                x = 255;

            out = x;
            hasGlow = 1;

            int py = i / W;
            int px = i - py * W;

            if(px < minX)
                minX = px;
            if(px > maxX)
                maxX = px;
            if(py < minY)
                minY = py;
            if(py > maxY)
                maxY = py;
        }

        maskY[i] = (uint8_t)out;
    }

    if(!hasGlow)
        return;

    int padding = spread * 4 + (1 << FLARE_LEVELS);

    minX = minX - padding < 0 ? 0 : minX - padding;
    minY = minY - padding < 0 ? 0 : minY - padding;
    maxX = maxX + padding >= W ? W - 1 : maxX + padding;
    maxY = maxY + padding >= H ? H - 1 : maxY + padding;

    flare_downsample2(maskY, W, H, f->pyrY[0], f->pyrW[0], f->pyrH[0], f->n_threads);

    for(int l = 1; l < FLARE_LEVELS; l++)
        flare_downsample2(f->pyrY[l - 1], f->pyrW[l - 1], f->pyrH[l - 1], f->pyrY[l], f->pyrW[l], f->pyrH[l], f->n_threads);

    for(int l = 0; l < FLARE_LEVELS; l++) {
        int r = spread >> (l + 1);

        if(r < 1 && spread > 0)
            r = 1;

        if(r > 0)
            flare_box_blur_u8(f->pyrY[l], tmp, f->pyrW[l], f->pyrH[l], r, f->n_threads);
    }

    for(int l = FLARE_LEVELS - 1; l > 0; l--)
        flare_upsample_add(f->pyrY[l - 1], f->pyrW[l - 1], f->pyrH[l - 1], f->pyrY[l], f->pyrW[l], f->pyrH[l], bloom_weights[l], f->n_threads);

    const uint8_t *restrict base_bloom = f->pyrY[0];
    const int bw = f->pyrW[0];
    const int bh = f->pyrH[0];
    const int scale_x = W > 1 ? ((bw - 1) << 8) / (W - 1) : 0;
    const int scale_y = H > 1 ? ((bh - 1) << 8) / (H - 1) : 0;

    switch(type) {
        case 0:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    int sum = (int)lin[dstY[i]] + (int)lin[glow];

                    if(sum > 1023)
                        sum = 1023;

                    dstY[i] = f->inv_lut[sum];
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;

        case 1:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    int a = lin[dstY[i]];
                    int b = lin[glow];
                    int inv = ((1023 - a) * (1023 - b)) >> 10;
                    int res = 1023 - inv;

                    dstY[i] = f->inv_lut[clampi(res, 0, 1023)];
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;

        case 2:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    dstY[i] = flare_u8((int)dstY[i] + glow + (((int)dstY[i] * glow) >> 8));
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;

        case 3:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    int sum = (int)lin[dstY[i]] + (int)lin[glow];
                    int mapped = (sum << 10) / (sum + 1023);

                    dstY[i] = f->inv_lut[clampi(mapped, 0, 1023)];
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;

        case 4:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    int combined = (int)lin[dstY[i]] + (int)lin[glow];
                    int compressed = combined - ((combined * combined) >> 11);

                    dstY[i] = f->inv_lut[clampi(compressed, 0, 1023)];
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;

        case 5:
        default:
#pragma omp parallel for schedule(static) num_threads(f->n_threads)
            for(int y = minY; y <= maxY; y++) {
                int row = y * W;
                for(int x = minX; x <= maxX; x++) {
                    int i = row + x;
                    int glow = maskY[i] + flare_sample_bilinear(base_bloom, bw, bh, x, y, scale_x, scale_y);

                    if(glow > 255)
                        glow = 255;

                    glow = (glow * opacity) >> 8;

                    if(glow <= 0)
                        continue;

                    int lin_base = lin[dstY[i]];
                    int lin_glow = lin[glow];
                    int res = lin_base + ((lin_glow * (1023 - lin_base)) >> 10);

                    dstY[i] = f->inv_lut[clampi(res, 0, 1023)];
                    FLARE_PUSH_CHROMA(i, glow);
                }
            }
            break;
    }
}

#undef FLARE_PUSH_CHROMA
