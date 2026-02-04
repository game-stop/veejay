/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "common.h"
#include <veejaycore/vjmem.h>
#include "shutterdrag.h"

#define TILE_W 64
#define TILE_H 64
#define FIXED_BITS 16
#define FIXED_ONE  (1 << FIXED_BITS)

vj_effect *shutterdrag_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);


    // Y boost to max and luma ceiling to max will take pixels outside legal limits


    ve->defaults[0] = 15;// Trail Strength (alpha)
    ve->defaults[1] = 10;// Duration (history mix)
    ve->defaults[2] = 0; // Loop / forever trails
    ve->defaults[3] = 5; // Y Boost (matches 5% decay)
    ve->defaults[4] = 140;// Sharpen (1.0x = 128)
    ve->defaults[5] = 40; // Propagate percentage
    ve->defaults[6] = 255;// Luma Ceiling (255 = stay at legal limit)
    ve->defaults[7] = 0;

    // Param Limits [Min][Max]
    ve->limits[0][0] = 0;   ve->limits[1][0] = 100;  // intensity/alpha
    ve->limits[0][1] = 0;   ve->limits[1][1] = 100;  // duration
    ve->limits[0][2] = 0;   ve->limits[1][2] = 1;    // loop toggle/forever
    ve->limits[0][3] = 0;   ve->limits[1][3] = 100;  // y_boost
    ve->limits[0][4] = 0;   ve->limits[1][4] = 255;  // sharpen
    ve->limits[0][5] = 0;   ve->limits[1][5] = 100;  // propagation strength %
    ve->limits[0][6] = 0;   ve->limits[1][6] = 511;  // luma celing
    ve->limits[0][7] = 0;   ve->limits[1][7] = 1;  // reset


    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trail Strength",
        "Duration",
        "Loop",
        "Y Boost",
        "Sharpen",
        "Propagate",
        "Luma Ceiling",
        "Reset"
    );

    ve->description = "Shutter Drag";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->has_user = 0;
    ve->parallel = 0;   

    return ve;
}

static inline uint8_t clamp_u8(int v)
{
    v = v & -(v >= 0);
    v = v | ((255 - v) & -(v > 255));
    return (uint8_t)v;
}

typedef struct {
    int width;
    int height;

    int32_t *feedbackY;
    int32_t *feedbackU;
    int32_t *feedbackV;

    uint8_t *historyY;
    uint8_t *historyU;
    uint8_t *historyV;
    int history_len;
    int history_pos;

    int32_t alpha;
    int32_t history_mix;

    int8_t first_frame;
} shutterdrag_t;

void *shutterdrag_malloc(int width, int height)
{
    shutterdrag_t *sb = calloc(1, sizeof(shutterdrag_t));
    if (!sb) return NULL;

    const int pixels = width * height;
    const int hlen = 3;
    sb->width  = width;
    sb->height = height;
    sb->history_len = hlen;
    sb->first_frame = 1;

    sb->feedbackY = vj_malloc(3 * pixels * sizeof(int32_t));
    sb->feedbackU = sb->feedbackY + pixels;
    sb->feedbackV = sb->feedbackY + (2 * pixels);

    sb->historyY = vj_malloc(pixels * hlen);
    sb->historyU = vj_malloc(pixels * hlen);
    sb->historyV = vj_malloc(pixels * hlen);

    if (!sb->feedbackY || !sb->historyY || !sb->historyU || !sb->historyV)
        goto fail;

    return sb;

fail:
    if (sb->feedbackY) free(sb->feedbackY);
    if (sb->historyY)  free(sb->historyY);
    if (sb->historyU)  free(sb->historyU);
    if (sb->historyV)  free(sb->historyV);
    free(sb);
    return NULL;
}

void shutterdrag_free(void *ptr)
{
    shutterdrag_t *sb = (shutterdrag_t*)ptr;
    if (!sb) return;
    if (sb->feedbackY) free(sb->feedbackY);
    if (sb->historyY)  free(sb->historyY);
    if (sb->historyU)  free(sb->historyU);
    if (sb->historyV)  free(sb->historyV);
    free(sb);
}

//
void shutterdrag_apply(void *ptr, VJFrame *frame, int *args)
{
    shutterdrag_t *sb = (shutterdrag_t*)ptr;
    const int w = sb->width, h = sb->height, hlen = 3;
    const int64_t inv100 = 42949673;
    const int64_t h_mix_val = (int64_t)args[1] * inv100;
    const int64_t f_mix_val = (int64_t)(100 - args[1]) * inv100;
    const int32_t alpha = (int32_t)((args[1] * FIXED_ONE * inv100) >> 32);
    const int32_t ialpha = FIXED_ONE - alpha;
    const int64_t decay_factor = args[2] ? FIXED_ONE : ((95 * FIXED_ONE * inv100) >> 32);
    //const int64_t inv_hlen_fp = (FIXED_ONE << FIXED_BITS) / hlen;
    const int64_t inv_hlen_fp = FIXED_ONE / hlen;
    const int64_t lock_limit = (args[6] > 0) ? ((int64_t)args[6] << FIXED_BITS) : 0;
    const int64_t uv_limit = (int64_t)127 << FIXED_BITS;
    const int64_t y_boost_scaled = (int64_t)args[3] * inv100;
    const int64_t sharpen_val = args[5] ? args[4] : 128;
    const int64_t prop_scaled = (int64_t)args[5] * inv100;
    const int64_t inv_prop_scaled = (int64_t)(100 - args[5]) * inv100;

    sb->history_pos = (sb->history_pos + 1) % hlen;
    const int pos = sb->history_pos;

    uint8_t *restrict Y = frame->data[0], *restrict U = frame->data[1], *restrict V = frame->data[2];
    int32_t *restrict fbY = sb->feedbackY, *restrict fbU = sb->feedbackU, *restrict fbV = sb->feedbackV;
    uint8_t *restrict hY = sb->historyY, *restrict hU = sb->historyU, *restrict hV = sb->historyV;

    // reset
    if (sb->first_frame || args[7]) {
        for (int i = 0; i < (w * h); i++) {
            fbY[i] = (int32_t)Y[i] << FIXED_BITS;
            fbU[i] = ((int32_t)U[i] - 128) << FIXED_BITS;
            fbV[i] = ((int32_t)V[i] - 128) << FIXED_BITS;
            int i3 = i * 3;
            hY[i3] = hY[i3+1] = hY[i3+2] = Y[i];
            hU[i3] = hU[i3+1] = hU[i3+2] = U[i];
            hV[i3] = hV[i3+1] = hV[i3+2] = V[i];
        }
        sb->first_frame = 0;
    }

    const int tiles_x = (w + TILE_W - 1) / TILE_W;
    const int tiles_y = (h + TILE_H - 1) / TILE_H;

    for (int ty = 0; ty < tiles_y; ty++) {
        const int y_start = ty * TILE_H, y_end = (y_start + TILE_H > h) ? h : y_start + TILE_H;
        const int ty_start = (y_start == 0) ? 1 : y_start;
        const int ty_end   = (y_end == h) ? h - 1 : y_end;

        for (int tx = 0; tx < tiles_x; tx++) {
            const int x_start = tx * TILE_W, x_end = (x_start + TILE_W > w) ? w : x_start + TILE_W;
            const int ax_start = (x_start == 0) ? 1 : x_start;
            const int ax_end   = (x_end == w) ? w - 1 : x_end;

            if (ax_end > ax_start) {
                if (lock_limit > 0) {
                    for (int y = ty_start; y < ty_end; y++) {
                        const int row_off = y * w;
                        uint8_t *restrict pY = &Y[row_off + ax_start];
                        int32_t *restrict pfbY = &fbY[row_off + ax_start];
                        uint8_t *restrict phY = &hY[(row_off + ax_start) * 3];
                        const uint8_t *restrict pY_prev = pY - 1, *restrict pY_next = pY + 1;
                        const uint8_t *restrict pY_up = pY - w, *restrict pY_down = pY + w;

                        for (int x = ax_start; x < ax_end; x++) {
                            int64_t fb = *pfbY;
                            phY[pos] = *pY;
                            fb = (fb * ialpha + ((int64_t)(*pY) << FIXED_BITS) * alpha) >> FIXED_BITS;
                            fb += (fb * y_boost_scaled) >> 32;
                            fb = (fb > lock_limit) ? lock_limit : fb;
                            fb = (fb * decay_factor) >> FIXED_BITS;

                            const int gx = (int)(*pY_next) - (int)(*pY_prev), gy = (int)(*pY_down) - (int)(*pY_up);
                            const int sx = (gx >> 31) | 1, sy_w = (gy >> 31) ? -w : w;
                            const int agx = (gx ^ (gx >> 31)) - (gx >> 31), agy = (gy ^ (gy >> 31)) - (gy >> 31);
                            const int mask = (agy - agx) >> 31;

                            fb = (fb * inv_prop_scaled + (int64_t)pfbY[(mask & sx) | (~mask & sy_w)] * prop_scaled) >> 32;
                            *pfbY = (int32_t)fb;

                            const int32_t h_sum = (int32_t)phY[0] + phY[1] + phY[2];
                            fb = (fb * f_mix_val + (((int64_t)h_sum * inv_hlen_fp) >> FIXED_BITS) * h_mix_val) >> 32;
                            *pY = clamp_u8((int)((fb * sharpen_val) >> 23));

                            pY++; pY_prev++; pY_next++; pY_up++; pY_down++; pfbY++; phY += 3;
                        }
                    }
                } else {
                    for (int y = ty_start; y < ty_end; y++) {
                        const int row_off = y * w;
                        uint8_t *restrict pY = &Y[row_off + ax_start];
                        int32_t *restrict pfbY = &fbY[row_off + ax_start];
                        uint8_t *restrict phY = &hY[(row_off + ax_start) * 3];
                        const uint8_t *restrict pY_prev = pY - 1, *restrict pY_next = pY + 1;
                        const uint8_t *restrict pY_up = pY - w, *restrict pY_down = pY + w;

                        for (int x = ax_start; x < ax_end; x++) {
                            int64_t fb = *pfbY;
                            phY[pos] = *pY;
                            fb = (fb * ialpha + ((int64_t)(*pY) << FIXED_BITS) * alpha) >> FIXED_BITS;
                            fb += (fb * y_boost_scaled) >> 32;
                            fb = (fb * decay_factor) >> FIXED_BITS;

                            const int gx = (int)(*pY_next) - (int)(*pY_prev), gy = (int)(*pY_down) - (int)(*pY_up);
                            const int sx = (gx >> 31) | 1, sy_w = (gy >> 31) ? -w : w;
                            const int agx = (gx ^ (gx >> 31)) - (gx >> 31), agy = (gy ^ (gy >> 31)) - (gy >> 31);
                            const int mask = (agy - agx) >> 31;

                            fb = (fb * inv_prop_scaled + (int64_t)pfbY[(mask & sx) | (~mask & sy_w)] * prop_scaled) >> 32;
                            *pfbY = (int32_t)fb;

                            const int32_t h_sum = (int32_t)phY[0] + phY[1] + phY[2];
                            fb = (fb * f_mix_val + (((int64_t)h_sum * inv_hlen_fp) >> FIXED_BITS) * h_mix_val) >> 32;
                            *pY = clamp_u8((int)((fb * sharpen_val) >> 23));

                            pY++; pY_prev++; pY_next++; pY_up++; pY_down++; pfbY++; phY += 3;
                        }
                    }
                }
            }

            for (int y = y_start; y < y_end; y++) {
                const int row_off = y * w;
                uint8_t *restrict pU = &U[row_off + x_start], *restrict pV = &V[row_off + x_start];
                int32_t *restrict pfbU = &fbU[row_off + x_start], *restrict pfbV = &fbV[row_off + x_start];
                uint8_t *restrict phU = &hU[(row_off + x_start) * 3], *restrict phV = &hV[(row_off + x_start) * 3];

                int x = x_start;
                const int x_end_even = x_start + ((x_end - x_start) & ~1);

                for (; x < x_end_even; x += 2) {
                    phU[pos] = pU[0]; phV[pos] = pV[0];
                    phU[pos + 3] = pU[1]; phV[pos + 3] = pV[1];

                    int64_t fu1 = ((int64_t)pfbU[0] * ialpha + ((int64_t)(pU[0] - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;
                    int64_t fu2 = ((int64_t)pfbU[1] * ialpha + ((int64_t)(pU[1] - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;
                    int64_t fv1 = ((int64_t)pfbV[0] * ialpha + ((int64_t)(pV[0] - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;
                    int64_t fv2 = ((int64_t)pfbV[1] * ialpha + ((int64_t)(pV[1] - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;

                    fu1 = (fu1 * decay_factor) >> FIXED_BITS; fu2 = (fu2 * decay_factor) >> FIXED_BITS;
                    fv1 = (fv1 * decay_factor) >> FIXED_BITS; fv2 = (fv2 * decay_factor) >> FIXED_BITS;

                    fu1 = (fu1 >  uv_limit) ?  uv_limit : ((fu1 < -uv_limit) ? -uv_limit : fu1);
                    fu2 = (fu2 >  uv_limit) ?  uv_limit : ((fu2 < -uv_limit) ? -uv_limit : fu2);
                    fv1 = (fv1 >  uv_limit) ?  uv_limit : ((fv1 < -uv_limit) ? -uv_limit : fv1);
                    fv2 = (fv2 >  uv_limit) ?  uv_limit : ((fv2 < -uv_limit) ? -uv_limit : fv2);

                    pfbU[0] = (int32_t)fu1; pfbU[1] = (int32_t)fu2;
                    pfbV[0] = (int32_t)fv1; pfbV[1] = (int32_t)fv2;

                    int32_t hu_s1 = (int32_t)phU[0] + phU[1] + phU[2] - 384;
                    int32_t hu_s2 = (int32_t)phU[3] + phU[4] + phU[5] - 384;
                    int32_t hv_s1 = (int32_t)phV[0] + phV[1] + phV[2] - 384;
                    int32_t hv_s2 = (int32_t)phV[3] + phV[4] + phV[5] - 384;

                    pU[0] = clamp_u8((int)(((fu1 * f_mix_val + ((int64_t)hu_s1 * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32) * sharpen_val >> 23) + 128);
                    pU[1] = clamp_u8((int)(((fu2 * f_mix_val + ((int64_t)hu_s2 * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32) * sharpen_val >> 23) + 128);
                    pV[0] = clamp_u8((int)(((fv1 * f_mix_val + ((int64_t)hv_s1 * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32) * sharpen_val >> 23) + 128);
                    pV[1] = clamp_u8((int)(((fv2 * f_mix_val + ((int64_t)hv_s2 * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32) * sharpen_val >> 23) + 128);

                    pU += 2; pV += 2; pfbU += 2; pfbV += 2; phU += 6; phV += 6;
                }

                if (x < x_end) {
                    phU[pos] = *pU; phV[pos] = *pV;

                    int64_t fu = ((int64_t)(*pfbU) * ialpha + ((int64_t)(*pU - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;
                    int64_t fv = ((int64_t)(*pfbV) * ialpha + ((int64_t)(*pV - 128) << FIXED_BITS) * alpha) >> FIXED_BITS;

                    fu = (fu * decay_factor) >> FIXED_BITS;
                    fv = (fv * decay_factor) >> FIXED_BITS;

                    fu = (fu > uv_limit) ? uv_limit : ((fu < -uv_limit) ? -uv_limit : fu);
                    fv = (fv > uv_limit) ? uv_limit : ((fv < -uv_limit) ? -uv_limit : fv);

                    *pfbU = (int32_t)fu; *pfbV = (int32_t)fv;

                    int32_t hu_s = (int32_t)phU[0] + phU[1] + phU[2] - 384;
                    int32_t hv_s = (int32_t)phV[0] + phV[1] + phV[2] - 384;

                    int64_t res_u = (fu * f_mix_val + ((int64_t)hu_s * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32;
                    int64_t res_v = (fv * f_mix_val + ((int64_t)hv_s * inv_hlen_fp >> FIXED_BITS) * h_mix_val) >> 32;

                    *pU = clamp_u8((int)((res_u * sharpen_val) >> 23) + 128);
                    *pV = clamp_u8((int)((res_v * sharpen_val) >> 23) + 128);
                }
            }
        }
    }
    
}