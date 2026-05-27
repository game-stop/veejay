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
#include "squares.h"

static inline int squares_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t squares_u8(int v)
{
    return (uint8_t)squares_clampi(v, 0, 255);
}

vj_effect *squares_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int max_dim = (w > h) ? w : h;
    int max_radius = max_dim >> 1;
    int def_radius = max_dim >> 6;

    if(max_radius < 1)
        max_radius = 1;
    if(def_radius < 1)
        def_radius = 1;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = max_radius;
    ve->defaults[0] = def_radius;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;
    ve->defaults[1] = 0;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 7;
    ve->defaults[2] = 0;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;
    ve->defaults[3] = 0;

    ve->description = "Squares";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Mode",
        "Orientation",
        "Parity"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Average",
        "Min",
        "Max"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Centered",
        "North",
        "North East",
        "East",
        "South East",
        "South West",
        "West",
        "North West"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Even",
        "Odd",
        "No parity"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1,                  max_radius > 96 ? 96 : max_radius, 6, 22, 2200, 5200, 1800, 25,    /* Radius */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000, /* Orientation */
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,              0, 0,  0,    0,    0,   -1000  /* Parity */
    );

    return ve;
}

static void squares_apply_blocks(VJFrame *frame, int radius, int mode, int orientation, int parity)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;

    int x_inf = 0;
    int y_inf = 0;
    int x_sup = w;
    int y_sup = h;

    grid_getbounds_from_orientation(
        radius,
        (vj_effect_orientation)orientation,
        (vj_effect_parity)parity,
        &x_inf,
        &y_inf,
        &x_sup,
        &y_sup,
        w,
        h
    );

    x_sup = squares_clampi(x_sup, -radius, w + radius);
    y_sup = squares_clampi(y_sup, -radius, h + radius);

    if(x_sup <= x_inf || y_sup <= y_inf)
        return;

    const int nx = ((x_sup - x_inf) + radius - 1) / radius;
    const int ny = ((y_sup - y_inf) + radius - 1) / radius;
    const int n_threads = vje_advise_num_threads(w * h);

#pragma omp parallel for schedule(static) num_threads(n_threads > 0 ? n_threads : 1)
    for(int by = 0; by < ny; by++) {
        const int y = y_inf + by * radius;

        for(int bx = 0; bx < nx; bx++) {
            const int x = x_inf + bx * radius;

            const int x0 = (x < 0) ? 0 : x;
            const int y0 = (y < 0) ? 0 : y;
            int x1 = x + radius;
            int y1 = y + radius;

            if(x1 > w) x1 = w;
            if(y1 > h) y1 = h;

            if(x1 <= x0 || y1 <= y0)
                continue;

            int sum_y = 0;
            int sum_u = 0;
            int sum_v = 0;
            int count = 0;
            uint8_t min_y = 255;
            uint8_t max_y = 0;

            for(int yy = y0; yy < y1; yy++) {
                const int row = yy * w;

                for(int xx = x0; xx < x1; xx++) {
                    const int idx = row + xx;
                    const uint8_t yv = Y[idx];

                    sum_y += yv;
                    sum_u += (int)U[idx] - 128;
                    sum_v += (int)V[idx] - 128;
                    count++;

                    if(yv < min_y)
                        min_y = yv;
                    if(yv > max_y)
                        max_y = yv;
                }
            }

            if(count <= 0)
                continue;

            uint8_t out_y;

            if(mode == 1)
                out_y = min_y;
            else if(mode == 2)
                out_y = max_y;
            else
                out_y = (uint8_t)((sum_y + (count >> 1)) / count);

            const uint8_t out_u = squares_u8(128 + ((sum_u >= 0)
                ? ((sum_u + (count >> 1)) / count)
                : -((-sum_u + (count >> 1)) / count)));

            const uint8_t out_v = squares_u8(128 + ((sum_v >= 0)
                ? ((sum_v + (count >> 1)) / count)
                : -((-sum_v + (count >> 1)) / count)));

            for(int yy = y0; yy < y1; yy++) {
                const int row = yy * w;

                for(int xx = x0; xx < x1; xx++) {
                    const int idx = row + xx;

                    Y[idx] = out_y;
                    U[idx] = out_u;
                    V[idx] = out_v;
                }
            }
        }
    }
}

void squares_apply(void *ptr, VJFrame *frame, int *args)
{
    (void) ptr;

    if(!frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;

    if(w <= 0 || h <= 0 || frame->len <= 0)
        return;

    const int max_dim = (w > h) ? w : h;
    int max_radius = max_dim >> 1;

    if(max_radius < 1)
        max_radius = 1;

    const int radius = squares_clampi(args[0], 1, max_radius);
    const int mode = squares_clampi(args[1], 0, 2);
    const int orientation = squares_clampi(args[2], 0, 7);
    const int parity = squares_clampi(args[3], 0, 2);

    squares_apply_blocks(frame, radius, mode, orientation, parity);
}