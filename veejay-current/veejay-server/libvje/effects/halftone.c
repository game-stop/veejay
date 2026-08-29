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

    return s;
}

void halftone_free(void *ptr)
{
    halftone_t *s = (halftone_t*) ptr;

    free(s->wrad_grid);
    free(s);
}

typedef void (*halftone_coord_fn)(int gx, int gy, int radius, int w, int h,
                                  int *bx, int *by, double timecode);
typedef void (*halftone_cell_fn)(halftone_t *s, VJFrame *frame, int g_idx,
                                 int sx0, int sy0, int sx1, int sy1, int half_r);
typedef void (*halftone_luma_style_fn)(uint8_t *wrad, uint8_t *y, int avg, int half_r);
typedef void (*halftone_color_style_fn)(uint8_t *u, uint8_t *v, int avg_u, int avg_v);

static void halftone_regular_coords(int gx, int gy, int radius, int w, int h,
                                    int *bx, int *by, double timecode)
{
    (void)gx;
    (void)gy;
    (void)radius;
    (void)w;
    (void)h;
    (void)timecode;
    (void)bx;
    (void)by;
}

static void halftone_style_average(uint8_t *wrad, uint8_t *y, int avg, int half_r)
{
    *wrad = halftone_dot_radius(avg, half_r);
    *y = (uint8_t)avg;
}

static void halftone_style_white(uint8_t *wrad, uint8_t *y, int avg, int half_r)
{
    *wrad = halftone_dot_radius(avg, half_r);
    *y = pixel_Y_hi_;
}

static void halftone_style_black(uint8_t *wrad, uint8_t *y, int avg, int half_r)
{
    *wrad = halftone_dot_radius(avg, half_r);
    *y = pixel_Y_lo_;
}

static void halftone_style_inverted(uint8_t *wrad, uint8_t *y, int avg, int half_r)
{
    const int inverted = 255 - avg;

    *wrad = halftone_dot_radius(inverted, half_r);
    *y = (uint8_t)inverted;
}

static void halftone_prepare_luma_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                       int sx0, int sy0, int sx1, int sy1, int half_r,
                                       halftone_luma_style_fn style)
{
    const int w = frame->width;
    const uint8_t *restrict Y = frame->data[0];
    uint32_t sum_y = 0;
    uint32_t hit = 0;

    for(int y = sy0; y < sy1; y++) {
        const int row = y * w;
        for(int x = sx0; x < sx1; x++) {
            sum_y += Y[row + x];
            hit++;
        }
    }

    if(hit > 0) {
        style(&s->wrad_grid[g_idx], &s->y_grid[g_idx], (int)(sum_y / hit), half_r);
        s->u_grid[g_idx] = 128;
        s->v_grid[g_idx] = 128;
    }
    else {
        s->wrad_grid[g_idx] = 0;
        s->y_grid[g_idx] = pixel_Y_lo_;
        s->u_grid[g_idx] = 128;
        s->v_grid[g_idx] = 128;
    }
}

static void halftone_prepare_average_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                          int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_luma_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                               halftone_style_average);
}

static void halftone_prepare_white_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                        int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_luma_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                               halftone_style_white);
}

static void halftone_prepare_black_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                        int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_luma_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                               halftone_style_black);
}

static void halftone_prepare_inverted_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                           int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_luma_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                               halftone_style_inverted);
}

static void halftone_prepare_brightest_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                            int sx0, int sy0, int sx1, int sy1, int half_r)
{
    const int w = frame->width;
    const uint8_t *restrict Y = frame->data[0];
    uint8_t brightest = 0;

    for(int y = sy0; y < sy1; y++) {
        const int row = y * w;
        for(int x = sx0; x < sx1; x++) {
            const uint8_t v = Y[row + x];
            if(v > brightest)
                brightest = v;
        }
    }

    s->wrad_grid[g_idx] = halftone_dot_radius(brightest, half_r);
    s->y_grid[g_idx] = pixel_Y_hi_;
    s->u_grid[g_idx] = 128;
    s->v_grid[g_idx] = 128;
}

static void halftone_prepare_darkest_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                          int sx0, int sy0, int sx1, int sy1, int half_r)
{
    const int w = frame->width;
    const uint8_t *restrict Y = frame->data[0];
    uint8_t darkest = 255;

    for(int y = sy0; y < sy1; y++) {
        const int row = y * w;
        for(int x = sx0; x < sx1; x++) {
            const uint8_t v = Y[row + x];
            if(v < darkest)
                darkest = v;
        }
    }

    s->wrad_grid[g_idx] = halftone_dot_radius(255 - darkest, half_r);
    s->y_grid[g_idx] = pixel_Y_lo_;
    s->u_grid[g_idx] = 128;
    s->v_grid[g_idx] = 128;
}

static void halftone_prepare_first_color_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                              int sx0, int sy0, int sx1, int sy1, int half_r)
{
    const int w = frame->width;
    const uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict U = frame->data[1];
    const uint8_t *restrict V = frame->data[2];
    uint32_t sum_y = 0;
    uint32_t hit = 0;

    for(int y = sy0; y < sy1; y++) {
        const int row = y * w;
        for(int x = sx0; x < sx1; x++) {
            sum_y += Y[row + x];
            hit++;
        }
    }

    if(hit > 0) {
        const int avg = (int)(sum_y / hit);
        const int p = sy0 * w + sx0;

        s->wrad_grid[g_idx] = halftone_dot_radius(avg, half_r);
        s->y_grid[g_idx] = (uint8_t)avg;
        s->u_grid[g_idx] = U[p];
        s->v_grid[g_idx] = V[p];
    }
    else {
        s->wrad_grid[g_idx] = 0;
        s->y_grid[g_idx] = pixel_Y_lo_;
        s->u_grid[g_idx] = 128;
        s->v_grid[g_idx] = 128;
    }
}

static void halftone_color_normal(uint8_t *u, uint8_t *v, int avg_u, int avg_v)
{
    *u = (uint8_t)clampi(avg_u, 0, 255);
    *v = (uint8_t)clampi(avg_v, 0, 255);
}

static void halftone_color_swapped(uint8_t *u, uint8_t *v, int avg_u, int avg_v)
{
    *u = (uint8_t)clampi(avg_v, 0, 255);
    *v = (uint8_t)clampi(avg_u, 0, 255);
}

static void halftone_prepare_color_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                        int sx0, int sy0, int sx1, int sy1, int half_r,
                                        halftone_color_style_fn style)
{
    const int w = frame->width;
    const uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict U = frame->data[1];
    const uint8_t *restrict V = frame->data[2];
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
            total_y += yv;
            sum_u += (int64_t)((int)U[pos] - 128) * (int64_t)yv;
            sum_v += (int64_t)((int)V[pos] - 128) * (int64_t)yv;
        }
    }

    if(hit > 0) {
        const int avg = (int)(sum_y / hit);

        s->wrad_grid[g_idx] = halftone_dot_radius(avg, half_r);
        s->y_grid[g_idx] = (uint8_t)avg;

        if(total_y > 0) {
            const int avg_u = 128 + (int)(sum_u / (int64_t)total_y);
            const int avg_v = 128 + (int)(sum_v / (int64_t)total_y);

            style(&s->u_grid[g_idx], &s->v_grid[g_idx], avg_u, avg_v);
        }
        else {
            s->u_grid[g_idx] = 128;
            s->v_grid[g_idx] = 128;
        }
    }
    else {
        s->wrad_grid[g_idx] = 0;
        s->y_grid[g_idx] = pixel_Y_lo_;
        s->u_grid[g_idx] = 128;
        s->v_grid[g_idx] = 128;
    }
}

static void halftone_prepare_color_cell_normal(halftone_t *s, VJFrame *frame, int g_idx,
                                               int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_color_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                                halftone_color_normal);
}

static void halftone_prepare_color_cell_swapped(halftone_t *s, VJFrame *frame, int g_idx,
                                                int sx0, int sy0, int sx1, int sy1, int half_r)
{
    halftone_prepare_color_cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r,
                                halftone_color_swapped);
}

static void halftone_prepare_cells(halftone_t *s,
                                   VJFrame *frame,
                                   int radius,
                                   int mode,
                                   int parity,
                                   int x_inf,
                                   int y_inf,
                                   int grid_w,
                                   int grid_h)
{
    const int w = frame->width;
    const int h = frame->height;
    const int half_r = radius >> 1;
    const halftone_coord_fn coord =
        parity == 3 ? halftone_berserk_coords : halftone_regular_coords;
    halftone_cell_fn cell;

    switch(mode) {
        case 0: cell = halftone_prepare_white_cell; break;
        case 1: cell = halftone_prepare_black_cell; break;
        case 3: cell = halftone_prepare_first_color_cell; break;
        case 4: cell = halftone_prepare_color_cell_normal; break;
        case 5: cell = halftone_prepare_brightest_cell; break;
        case 6: cell = halftone_prepare_darkest_cell; break;
        case 7: cell = halftone_prepare_inverted_cell; break;
        case 8: cell = halftone_prepare_color_cell_swapped; break;
        default: cell = halftone_prepare_average_cell; break;
    }

#pragma omp for schedule(static)
    for(int gy = 0; gy < grid_h; gy++) {
        for(int gx = 0; gx < grid_w; gx++) {
            int bx = x_inf + gx * radius;
            int by = y_inf + gy * radius;
            const int g_idx = gy * grid_w + gx;

            coord(gx, gy, radius, w, h, &bx, &by, frame->timecode);

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
                s->wrad_grid[g_idx] = 0;
                s->y_grid[g_idx] = pixel_Y_lo_;
                s->u_grid[g_idx] = 128;
                s->v_grid[g_idx] = 128;
                continue;
            }

            cell(s, frame, g_idx, sx0, sy0, sx1, sy1, half_r);
        }
    }
}

typedef void (*halftone_draw_cell_fn)(halftone_t *s, VJFrame *frame, int g_idx,
                                      int cx, int cy, int radius, int wrad);

static void halftone_draw_luma_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                    int cx, int cy, int radius, int wrad)
{
    if(wrad > 0)
        veejay_draw_circle(frame->data[0], cx, cy, radius, radius,
                           frame->width, frame->height, wrad, s->y_grid[g_idx]);
}

static void halftone_draw_color_cell(halftone_t *s, VJFrame *frame, int g_idx,
                                     int cx, int cy, int radius, int wrad)
{
    if(wrad > 0) {
        veejay_draw_circle(frame->data[0], cx, cy, radius, radius,
                           frame->width, frame->height, wrad, s->y_grid[g_idx]);
        veejay_draw_circle(frame->data[1], cx, cy, radius, radius,
                           frame->width, frame->height, wrad, s->u_grid[g_idx]);
        veejay_draw_circle(frame->data[2], cx, cy, radius, radius,
                           frame->width, frame->height, wrad, s->v_grid[g_idx]);
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
    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const int half_r = radius >> 1;
    const int chroma_mode = (mode == 3 || mode == 4 || mode == 8);
    const uint8_t bg_y = (mode == 1 || mode == 6) ? pixel_Y_hi_ : pixel_Y_lo_;
    const halftone_draw_cell_fn draw =
        chroma_mode ? halftone_draw_color_cell : halftone_draw_luma_cell;

    veejay_memset(frame->data[0], bg_y, len);
    veejay_memset(frame->data[1], 128, len);
    veejay_memset(frame->data[2], 128, len);
    grid_clear_margins(frame->data[0], chroma_mode ? frame->data[1] : NULL,
                       chroma_mode ? frame->data[2] : NULL, w, h, x_inf, y_inf,
                       radius, chroma_mode);

    for(int gy = 0; gy < grid_h; gy++) {
        for(int gx = 0; gx < grid_w; gx++) {
            const int g_idx = gy * grid_w + gx;
            const int cx = x_inf + gx * radius + half_r;
            const int cy = y_inf + gy * radius + half_r;

            draw(s, frame, g_idx, cx, cy, radius, s->wrad_grid[g_idx]);
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

    halftone_prepare_cells(s, frame, radius, mode, parity, x_inf, y_inf, grid_w, grid_h);
#pragma omp single
    halftone_render_cells(s, frame, radius, mode, x_inf, y_inf, grid_w, grid_h);
}
