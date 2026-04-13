/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "halftone.h"

vj_effect *halftone_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 8;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 7;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 3;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->description = "Halftone";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Mode", "Orientation", "Parity" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][1],1, "White Dots", "Black Dots", "Gray Dots", "Colored Dots",
        "Averaged Colored Dots", "Brightest Dot", "Darkest Dot","Inverted Gray", "Chroma Swap"  );
    vje_build_value_hint_list( ve->hints, ve->limits[1][2],2, "Centered", "North", "North East", "East" , "South East", "South West", "West" , "North West");
    vje_build_value_hint_list( ve->hints, ve->limits[1][3],3, "Even", "Odd", "No parity", "Bezerk");

    return ve;
}

static inline int get_berzek_parity(int gx, int gy, int frame_num) {
    unsigned int hash = (gx * 374761397 + gy * 668265263 + frame_num * 127412657);
    hash ^= (hash << 13);
    hash ^= (hash >> 17);
    hash ^= (hash << 5);
    return (hash & 1);
}

static inline void get_berzek_coords(int gx, int gy, int radius, int w, int h, int *bx, int *by, double timecode) {
    int time_seed = (int)(timecode * 1000.0);
    unsigned int hash = (gx * 374761397 + gy * 668265263 + time_seed * 127412657);
    unsigned int direction = (hash ^ (hash >> 16)) & 3;

    int shift = radius / 4;

    switch(direction) {
        case 0: *bx -= shift; break;
        case 1: *bx += shift; break;
        case 2: *by -= shift; break;
        case 3: *by += shift; break;
    }

    if (*bx < 0) *bx = 0;
    if (*by < 0) *by = 0;
    if (*bx + radius > w) *bx = w - radius;
    if (*by + radius > h) *by = h - radius;
}

#define APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, t) \
    do { \
        if ((parity) == 3) { \
            get_berzek_coords((gx), (gy), (radius), (w), (h), &(bx), &(by), (t)); \
        } \
    } while (0)

static void halftone_apply_avg_col2(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;
    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *y_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *u_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *v_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint64_t total_Y = 0;
            uint32_t sumY = 0, hit = 0;
            int64_t sumU = 0, sumV = 0;

            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    int idx = y1 * w + x1;
                    uint8_t y_val = Y[idx];
                    sumY += y_val;
                    total_Y += y_val;
                    sumU += (int64_t)(U[idx] - 128) * y_val;
                    sumV += (int64_t)(V[idx] - 128) * y_val;
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avgY = sumY / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avgY / 255.0) * (radius / 2));
                y_grid[g_idx] = (uint8_t)avgY;

                if (total_Y > 0) {
                    u_grid[g_idx] = (uint8_t)(128 + (sumU / (int64_t)total_Y));
                    v_grid[g_idx] = (uint8_t)(128 + (sumV / (int64_t)total_Y));
                } else {
                    u_grid[g_idx] = 128;
                    v_grid[g_idx] = 128;
                }
            } else {
                wrad_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    veejay_memset(U, 128, w * h);
    veejay_memset(V, 128, w * h);
    grid_clear_margins(Y, U, V, w, h, x_inf, y_inf, radius, 1);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int g_idx = gy * grid_w + gx;
            int wrad = wrad_grid[g_idx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, y_grid[g_idx]);
                veejay_draw_circle(U, cx, cy, radius, radius, w, h, wrad, u_grid[g_idx]);
                veejay_draw_circle(V, cx, cy, radius, radius, w, h, wrad, v_grid[g_idx]);
            }
        }
    }
}

static void halftone_apply_avg_col(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;
    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *y_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *u_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *v_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint32_t sum = 0, hit = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    sum += Y[y1 * w + x1];
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avg = sum / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avg / 255.0) * (radius / 2));
                y_grid[g_idx] = (uint8_t)avg;
                u_grid[g_idx] = U[by * w + bx];
                v_grid[g_idx] = V[by * w + bx];
            } else {
                wrad_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    veejay_memset(U, 128, w * h);
    veejay_memset(V, 128, w * h);
    grid_clear_margins(Y, U, V, w, h, x_inf, y_inf, radius, 1);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int g_idx = gy * grid_w + gx;
            int wrad = wrad_grid[g_idx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, y_grid[g_idx]);
                veejay_draw_circle(U, cx, cy, radius, radius, w, h, wrad, u_grid[g_idx]);
                veejay_draw_circle(V, cx, cy, radius, radius, w, h, wrad, v_grid[g_idx]);
            }
        }
    }
}

static void halftone_apply_avg_gray(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;
    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *val_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint32_t sum = 0, hit = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    sum += Y[y1 * w + x1];
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avg = sum / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avg / 255.0) * (radius / 2));
                val_grid[g_idx] = (uint8_t)avg;
            } else {
                wrad_grid[g_idx] = 0;
                val_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int g_idx = gy * grid_w + gx;
            int wrad = wrad_grid[g_idx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, val_grid[g_idx]);
            }
        }
    }

    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}

static void halftone_apply_avg_black(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;
    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint32_t sum = 0, hit = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    sum += Y[y1 * w + x1];
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avg = sum / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avg / 255.0) * (radius / 2));
            } else {
                wrad_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_hi_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int wrad = wrad_grid[gy * grid_w + gx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, pixel_Y_lo_);
            }
        }
    }

    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}

static void halftone_apply_avg_white(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;

    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        int y = y_inf + (gy * radius);
        if (y >= h + (radius / 2)) continue;

        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint32_t sum = 0, hit = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    sum += Y[y1 * w + x1];
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avg = sum / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avg / 255.0) * (radius / 2));
            } else {
                wrad_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int wrad = wrad_grid[gy * grid_w + gx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, pixel_Y_hi_);
            }
        }
    }

    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}

static void halftone_apply_brightest(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;
    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;
    if (grid_w <= 0 || grid_h <= 0) return;

    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * grid_w * grid_h);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius), by = y_inf + (gy * radius);
            uint8_t brightest = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    if (Y[y1 * w + x1] > brightest) brightest = Y[y1 * w + x1];
                }
            }
            wrad_grid[gy * grid_w + gx] = 1 + (int)(((double)brightest / 255.0) * (radius / 2));
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            if (wrad_grid[gy * grid_w + gx] > 0)
                veejay_draw_circle(Y, x_inf + (gx * radius) + radius/2, y_inf + (gy * radius) + radius/2, radius, radius, w, h, wrad_grid[gy * grid_w + gx], pixel_Y_hi_);
        }
    }
    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}

static void halftone_apply_darkest(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;
    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;
    if (grid_w <= 0 || grid_h <= 0) return;

    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * grid_w * grid_h);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius), by = y_inf + (gy * radius);
            uint8_t darkest = 255;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    if (Y[y1 * w + x1] < darkest) darkest = Y[y1 * w + x1];
                }
            }
            wrad_grid[gy * grid_w + gx] = 1 + (int)(((double)(255 - darkest) / 255.0) * (radius / 2));
        }
    }

    veejay_memset(Y, pixel_Y_hi_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            if (wrad_grid[gy * grid_w + gx] > 0)
                veejay_draw_circle(Y, x_inf + (gx * radius) + radius/2, y_inf + (gy * radius) + radius/2, radius, radius, w, h, wrad_grid[gy * grid_w + gx], pixel_Y_lo_);
        }
    }
    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}

static void halftone_apply_inverted_gray(VJFrame *frame, int radius, int orientation, int odd, int n_threads)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;
    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;
    if (grid_w <= 0 || grid_h <= 0) return;

    uint8_t *wrad_grid = (uint8_t *)alloca(grid_w * grid_h);
    uint8_t *val_grid = (uint8_t *)alloca(grid_w * grid_h);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius), by = y_inf + (gy * radius);
            uint32_t sum = 0, hit = 0;
            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    sum += Y[y1 * w + x1]; hit++;
                }
            }
            if (hit > 0) {
                uint8_t inv = 255 - (sum / hit);
                wrad_grid[gy * grid_w + gx] = 1 + (int)(((double)inv / 255.0) * (radius / 2));
                val_grid[gy * grid_w + gx] = inv;
            } else wrad_grid[gy * grid_w + gx] = 0;
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    grid_clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            if (wrad_grid[gy * grid_w + gx] > 0)
                veejay_draw_circle(Y, x_inf + (gx * radius) + radius/2, y_inf + (gy * radius) + radius/2, radius, radius, w, h, wrad_grid[gy * grid_w + gx], val_grid[gy * grid_w + gx]);
        }
    }
    veejay_memset(frame->data[1], 128, w * h);
    veejay_memset(frame->data[2], 128, w * h);
}


// same as avg_col2 but swapping chroma when calling veejay_draw_circle
static void halftone_apply_chroma_swap(VJFrame *frame, int radius, int orientation, int parity, int n_threads)
{
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];
    const int w = frame->width, h = frame->height;
    int x_inf = 0, y_inf = 0, x_sup = w, y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, w, h);

    int grid_w = (w - x_inf + radius - 1) / radius;
    int grid_h = (h - y_inf + radius - 1) / radius;

    if (grid_w <= 0 || grid_h <= 0) return;

    int total_cells = grid_w * grid_h;
    uint8_t *wrad_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *y_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *u_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);
    uint8_t *v_grid = (uint8_t *)alloca(sizeof(uint8_t) * total_cells);

    #pragma omp parallel for schedule(static) num_threads(n_threads)
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + (gx * radius);
            int by = y_inf + (gy * radius);

            APPLY_BERZEK_PARITY(parity, gx, gy, radius, w, h, bx, by, frame->timecode);

            uint64_t total_Y = 0;
            uint32_t sumY = 0, hit = 0;
            int64_t sumU = 0, sumV = 0;

            for (int y1 = by; y1 < (by + radius) && y1 < h; y1++) {
                for (int x1 = bx; x1 < (bx + radius) && x1 < w; x1++) {
                    int idx = y1 * w + x1;
                    uint8_t y_val = Y[idx];
                    sumY += y_val;
                    total_Y += y_val;
                    sumU += (int64_t)(U[idx] - 128) * y_val;
                    sumV += (int64_t)(V[idx] - 128) * y_val;
                    hit++;
                }
            }

            int g_idx = gy * grid_w + gx;
            if (hit > 0) {
                uint32_t avgY = sumY / hit;
                wrad_grid[g_idx] = 1 + (int)(((double)avgY / 255.0) * (radius / 2));
                y_grid[g_idx] = (uint8_t)avgY;

                if (total_Y > 0) {
                    u_grid[g_idx] = (uint8_t)(128 + (sumU / (int64_t)total_Y));
                    v_grid[g_idx] = (uint8_t)(128 + (sumV / (int64_t)total_Y));
                } else {
                    u_grid[g_idx] = 128;
                    v_grid[g_idx] = 128;
                }
            } else {
                wrad_grid[g_idx] = 0;
            }
        }
    }

    veejay_memset(Y, pixel_Y_lo_, w * h);
    veejay_memset(U, 128, w * h);
    veejay_memset(V, 128, w * h);
    grid_clear_margins(Y, U, V, w, h, x_inf, y_inf, radius, 1);

    int half_r = radius / 2;
    for (int gy = 0; gy < grid_h; gy++) {
        for (int gx = 0; gx < grid_w; gx++) {
            int g_idx = gy * grid_w + gx;
            int wrad = wrad_grid[g_idx];
            if (wrad > 0) {
                int cx = x_inf + (gx * radius) + half_r;
                int cy = y_inf + (gy * radius) + half_r;
                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, y_grid[g_idx]);
                veejay_draw_circle(U, cx, cy, radius, radius, w, h, wrad, v_grid[g_idx]);
                veejay_draw_circle(V, cx, cy, radius, radius, w, h, wrad, u_grid[g_idx]);
            }
        }
    }
}

void halftone_apply( void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int mode = args[1];
    int orientation = args[2];
    int parity = args[3];
    int n_threads = vje_advise_num_threads(frame->len);
    switch(mode) {
        case 0: halftone_apply_avg_white(frame, radius, orientation, parity, n_threads); break;
        case 1: halftone_apply_avg_black(frame, radius, orientation, parity, n_threads); break;
        case 2: halftone_apply_avg_gray(frame, radius, orientation, parity, n_threads); break;
        case 3: halftone_apply_avg_col(frame, radius, orientation, parity, n_threads); break;
        case 4: halftone_apply_avg_col2(frame, radius, orientation, parity, n_threads); break;
        case 5: halftone_apply_brightest( frame, radius ,orientation,parity, n_threads); break;
        case 6: halftone_apply_darkest( frame, radius , orientation, parity, n_threads); break;
        case 7: halftone_apply_inverted_gray(frame,radius, orientation, parity, n_threads); break;
        case 8: halftone_apply_chroma_swap(frame, radius, orientation, parity, n_threads); break;
    }
}