/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "raster.h"

#define RASTER_PARAMS 2

#define P_GRID_SIZE 0
#define P_MODE      1

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *raster_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RASTER_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    int grid_max = h >> 2;

    if(grid_max < 4)
        grid_max = 4;

    ve->limits[0][P_GRID_SIZE] = 4; ve->limits[1][P_GRID_SIZE] = grid_max; ve->defaults[P_GRID_SIZE] = 4;
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 1;             ve->defaults[P_MODE] = 1;

    ve->description = "Grid";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Grid size",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Black", "White");

    int grid_hi = grid_max;

    if(grid_hi > 32)
        grid_hi = 32;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 4,                  grid_hi,            4, 14, 3000, 8200, 2200, 24,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );
    return ve;
}

void raster_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int grid = clampi(args[P_GRID_SIZE], 4, height >> 2);
    const uint8_t pixel_color = args[P_MODE] ? pixel_Y_hi_ : pixel_Y_lo_;
    const int n_threads = vje_advise_num_threads(frame->len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int yline = (y % grid) <= 1;

        if(yline) {
            for(int x = 0; x < width; x++) {
                const int i = row + x;

                Y[i] = pixel_color;
                Cb[i] = 128;
                Cr[i] = 128;
            }
        }
        else {
            int xmod = 0;

            for(int x = 0; x < width; x++) {
                if(xmod <= 1) {
                    const int i = row + x;

                    Y[i] = pixel_color;
                    Cb[i] = 128;
                    Cr[i] = 128;
                }

                xmod++;
                if(xmod >= grid)
                    xmod = 0;
            }
        }
    }
}
