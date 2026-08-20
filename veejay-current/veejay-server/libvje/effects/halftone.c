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

#define HALFTONE_PARAMS 4

#define P_RADIUS      0
#define P_MODE        1
#define P_ORIENTATION 2
#define P_PARITY      3

typedef struct {
    uint8_t *wrad_grid;
    uint8_t *y_grid;
    uint8_t *u_grid;
    uint8_t *v_grid;
    int max_cells;
    int n_threads;
} halftone_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t halftone_dot_radius(int v, int half_radius)
{
    return (uint8_t)(1 + ((v * half_radius + 127) / 255));
}

static inline unsigned int halftone_hash_u32(unsigned int x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static inline void halftone_berserk_coords(int gx, int gy, int radius, int w, int h, int *bx, int *by, double timecode)
{
    const int time_seed = (int)(timecode * 1000.0);
    unsigned int hash = (unsigned int)gx * 374761397u + (unsigned int)gy * 668265263u + (unsigned int)time_seed * 127412657u;
    const unsigned int direction = (halftone_hash_u32(hash) >> 16) & 3u;
    const int shift = radius >> 2;

    switch(direction) {
        case 0: *bx -= shift; break;
        case 1: *bx += shift; break;
        case 2: *by -= shift; break;
        default: *by += shift; break;
    }

    const int max_x = w > radius ? w - radius : 0;
    const int max_y = h > radius ? h - radius : 0;

    if(*bx < 0)
        *bx = 0;
    else if(*bx > max_x)
        *bx = max_x;

    if(*by < 0)
        *by = 0;
    else if(*by > max_y)
        *by = max_y;
}

vj_effect *halftone_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = HALFTONE_PARAMS;
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

    const int min_dim = w < h ? w : h;
    int max_radius = min_dim >> 1;
    int default_radius = min_dim >> 6;
    int soft_max = min_dim / 14;
    int soft_min = 4;

    if(max_radius < 2)
        max_radius = 2;
    if(soft_min > max_radius)
        soft_min = 2;
    if(soft_max < soft_min)
        soft_max = soft_min;
    if(soft_max > max_radius)
        soft_max = max_radius;
    default_radius = clampi(default_radius, 2, max_radius);

    ve->limits[0][P_RADIUS] = 2;      ve->limits[1][P_RADIUS] = max_radius; ve->defaults[P_RADIUS] = default_radius;
    ve->limits[0][P_MODE] = 0;        ve->limits[1][P_MODE] = 8;            ve->defaults[P_MODE] = 0;
    ve->limits[0][P_ORIENTATION] = 0; ve->limits[1][P_ORIENTATION] = 7;     ve->defaults[P_ORIENTATION] = 0;
    ve->limits[0][P_PARITY] = 0;      ve->limits[1][P_PARITY] = 3;          ve->defaults[P_PARITY] = 0;

    ve->description = "Halftone";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius", "Mode", "Orientation", "Parity");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "White Dots", "Black Dots", "Gray Dots", "Colored Dots", "Averaged Colored Dots", "Brightest Dot", "Darkest Dot", "Inverted Gray", "Chroma Swap");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ORIENTATION], P_ORIENTATION, "Centered", "North", "North East", "East", "South East", "South West", "West", "North West");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_PARITY], P_PARITY, "Even", "Odd", "No parity", "Berserk");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, soft_min, soft_max, 78, 100, 15, 520, 0, 1, 140, VJ_BEAT_COST_MODERATE, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *halftone_malloc(int w, int h)
{
    halftone_t *s = (halftone_t*) vj_calloc(sizeof(halftone_t));

    if(!s)
        return NULL;

    const int len = w * h;

    s->wrad_grid = (uint8_t*) vj_malloc((size_t)len * 4u);

    if(!s->wrad_grid) {
        free(s);
        return NULL;
    }

    s->y_grid = s->wrad_grid + len;
    s->u_grid = s->y_grid + len;
    s->v_grid = s->u_grid + len;
    s->max_cells = len;
    s->n_threads = vje_advise_num_threads(len);

    return s;
}

void halftone_free(void *ptr)
{
    halftone_t *s = (halftone_t*) ptr;

    free(s->wrad_grid);
    free(s);
}

static void halftone_prepare_cells(halftone_t *s,
                                   VJFrame *frame,
                                   int radius,
                                   int mode,
                                   int orientation,
                                   int parity,
                                   int x_inf,
                                   int y_inf,
                                   int grid_w,
                                   int grid_h)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;
    const int half_r = radius >> 1;

    uint8_t *restrict wrad_grid = s->wrad_grid;
    uint8_t *restrict y_grid = s->y_grid;
    uint8_t *restrict u_grid = s->u_grid;
    uint8_t *restrict v_grid = s->v_grid;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int gy = 0; gy < grid_h; gy++) {
        for(int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + gx * radius;
            int by = y_inf + gy * radius;
            const int g_idx = gy * grid_w + gx;

            if(parity == 3)
                halftone_berserk_coords(gx, gy, radius, w, h, &bx, &by, frame->timecode);

            const int sx0 = bx < 0 ? 0 : (bx >= w ? w - 1 : bx);
            const int sy0 = by < 0 ? 0 : (by >= h ? h - 1 : by);
            int sx1 = bx + radius;
            int sy1 = by + radius;

            if(sx1 < 0)
                sx1 = 0;
            else if(sx1 > w)
                sx1 = w;

            if(sy1 < 0)
                sy1 = 0;
            else if(sy1 > h)
                sy1 = h;

            if(sx1 <= sx0 || sy1 <= sy0) {
                wrad_grid[g_idx] = 0;
                y_grid[g_idx] = pixel_Y_lo_;
                u_grid[g_idx] = 128;
                v_grid[g_idx] = 128;
                continue;
            }

            if(mode == 5) {
                uint8_t brightest = 0;

                for(int y = sy0; y < sy1; y++) {
                    const int row = y * w;
                    for(int x = sx0; x < sx1; x++) {
                        const uint8_t v = Y[row + x];
                        if(v > brightest)
                            brightest = v;
                    }
                }

                wrad_grid[g_idx] = halftone_dot_radius(brightest, half_r);
                y_grid[g_idx] = pixel_Y_hi_;
                u_grid[g_idx] = 128;
                v_grid[g_idx] = 128;
            }
            else if(mode == 6) {
                uint8_t darkest = 255;

                for(int y = sy0; y < sy1; y++) {
                    const int row = y * w;
                    for(int x = sx0; x < sx1; x++) {
                        const uint8_t v = Y[row + x];
                        if(v < darkest)
                            darkest = v;
                    }
                }

                wrad_grid[g_idx] = halftone_dot_radius(255 - darkest, half_r);
                y_grid[g_idx] = pixel_Y_lo_;
                u_grid[g_idx] = 128;
                v_grid[g_idx] = 128;
            }
            else {
                uint32_t sum_y = 0;
                uint32_t hit = 0;
                uint64_t total_y = 0;
                int64_t sum_u = 0;
                int64_t sum_v = 0;

                for(int y = sy0; y < sy1; y++) {
                    const int row = y * w;
                    for(int x = sx0; x < sx1; x++) {
                        const int pos = row + x;
                        const uint8_t yv = Y[pos];

                        sum_y += yv;
                        hit++;

                        if(mode == 4 || mode == 8) {
                            total_y += yv;
                            sum_u += (int64_t)((int)U[pos] - 128) * (int64_t)yv;
                            sum_v += (int64_t)((int)V[pos] - 128) * (int64_t)yv;
                        }
                    }
                }

                if(hit > 0) {
                    const int avg = (int)(sum_y / hit);
                    int dot_y = avg;

                    if(mode == 0)
                        dot_y = pixel_Y_hi_;
                    else if(mode == 1)
                        dot_y = pixel_Y_lo_;
                    else if(mode == 7)
                        dot_y = 255 - avg;

                    wrad_grid[g_idx] = halftone_dot_radius((mode == 7) ? (255 - avg) : avg, half_r);
                    y_grid[g_idx] = (uint8_t)dot_y;

                    if(mode == 3) {
                        const int p = sy0 * w + sx0;
                        u_grid[g_idx] = U[p];
                        v_grid[g_idx] = V[p];
                    }
                    else if((mode == 4 || mode == 8) && total_y > 0) {
                        const int avg_u = 128 + (int)(sum_u / (int64_t)total_y);
                        const int avg_v = 128 + (int)(sum_v / (int64_t)total_y);

                        if(mode == 8) {
                            u_grid[g_idx] = (uint8_t)clampi(avg_v, 0, 255);
                            v_grid[g_idx] = (uint8_t)clampi(avg_u, 0, 255);
                        }
                        else {
                            u_grid[g_idx] = (uint8_t)clampi(avg_u, 0, 255);
                            v_grid[g_idx] = (uint8_t)clampi(avg_v, 0, 255);
                        }
                    }
                    else {
                        u_grid[g_idx] = 128;
                        v_grid[g_idx] = 128;
                    }
                }
                else {
                    wrad_grid[g_idx] = 0;
                    y_grid[g_idx] = pixel_Y_lo_;
                    u_grid[g_idx] = 128;
                    v_grid[g_idx] = 128;
                }
            }
        }
    }
}

static void halftone_render_cells(halftone_t *s,
                                  VJFrame *frame,
                                  int radius,
                                  int mode,
                                  int x_inf,
                                  int y_inf,
                                  int grid_w,
                                  int grid_h)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const int half_r = radius >> 1;
    const int chroma_mode = (mode == 3 || mode == 4 || mode == 8);
    const uint8_t bg_y = (mode == 1 || mode == 6) ? pixel_Y_hi_ : pixel_Y_lo_;

    veejay_memset(Y, bg_y, len);
    veejay_memset(U, 128, len);
    veejay_memset(V, 128, len);
    grid_clear_margins(Y, chroma_mode ? U : NULL, chroma_mode ? V : NULL, w, h, x_inf, y_inf, radius, chroma_mode);

    for(int gy = 0; gy < grid_h; gy++) {
        for(int gx = 0; gx < grid_w; gx++) {
            const int g_idx = gy * grid_w + gx;
            const int wrad = s->wrad_grid[g_idx];

            if(wrad > 0) {
                const int cx = x_inf + gx * radius + half_r;
                const int cy = y_inf + gy * radius + half_r;

                veejay_draw_circle(Y, cx, cy, radius, radius, w, h, wrad, s->y_grid[g_idx]);

                if(chroma_mode) {
                    veejay_draw_circle(U, cx, cy, radius, radius, w, h, wrad, s->u_grid[g_idx]);
                    veejay_draw_circle(V, cx, cy, radius, radius, w, h, wrad, s->v_grid[g_idx]);
                }
            }
        }
    }
}

void halftone_apply(void *ptr, VJFrame *frame, int *args)
{
    halftone_t *s = (halftone_t*) ptr;

    const int min_dim = frame->width < frame->height ? frame->width : frame->height;
    int max_radius = min_dim >> 1;
    if(max_radius < 2)
        max_radius = 2;
    const int radius = clampi(args[P_RADIUS], 2, max_radius);
    const int mode = args[P_MODE];
    const int orientation = args[P_ORIENTATION];
    const int parity = args[P_PARITY];

    int x_inf = 0;
    int y_inf = 0;
    int x_sup = frame->width;
    int y_sup = frame->height;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup, frame->width, frame->height);

    int grid_w = (frame->width - x_inf + radius - 1) / radius;
    int grid_h = (frame->height - y_inf + radius - 1) / radius;

    if(grid_w < 1)
        grid_w = 1;
    if(grid_h < 1)
        grid_h = 1;

    while(grid_w > 1 && grid_w * grid_h > s->max_cells)
        grid_w--;
    while(grid_h > 1 && grid_w * grid_h > s->max_cells)
        grid_h--;

    halftone_prepare_cells(s, frame, radius, mode, orientation, parity, x_inf, y_inf, grid_w, grid_h);
    halftone_render_cells(s, frame, radius, mode, x_inf, y_inf, grid_w, grid_h);
}
