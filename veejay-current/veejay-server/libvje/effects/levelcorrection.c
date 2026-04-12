/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "levelcorrection.h"
#include <omp.h>

typedef struct {
    int n_threads;
} level_t;

vj_effect *levelcorrection_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 0;    /* Level Min */
    ve->defaults[1] = 255;  /* Level Max */
    ve->defaults[2] = 0;    /* Shrink Min */
    ve->defaults[3] = 255;  /* Shrink Max (Corrected default) */

    for(int i=0; i<4; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = 255;
    }

    ve->param_description = vje_build_param_list(ve->num_params, "Level Min", "Level Max", "Shrink Min", "Shrink Max");

    ve->has_user = 0;
    ve->description = "Alpha: Level Correction";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->rgb_conv = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_SRC_A;
    return ve;
}

void levelcorrection_apply(void *ptr, VJFrame *frame, int *args) {
    int min = args[0];
    int max = args[1];
    int bmin = args[2];
    int bmax = args[3];

    uint8_t *A = frame->data[3];
    const int len = frame->len;

    int n_threads = vje_advise_num_threads(len);

    uint8_t lut[256];
    uint8_t tmp_lut[256];
    int apply_levels = (max > min);
    int apply_shrink = (bmax > bmin);

    if (!apply_levels && !apply_shrink) return;

    if (apply_levels && apply_shrink) {
        uint8_t lut1[256];
        uint8_t lut2[256];
        __init_lookup_table(lut1, 256, (float)min, (float)max, 0, 0xff);
        __init_lookup_table(lut2, 256, 0.0f, 255.0f, bmin, bmax);
        for(int i = 0; i < 256; i++) {
            lut[i] = lut2[lut1[i]];
        }
    } else if (apply_levels) {
        __init_lookup_table(lut, 256, (float)min, (float)max, 0, 0xff);
    } else {
        __init_lookup_table(lut, 256, 0.0f, 255.0f, bmin, bmax);
    }

#pragma omp parallel for simd schedule(static) num_threads(n_threads)
    for(int pos = 0; pos < len; pos++) {
        A[pos] = lut[A[pos]];
    }
}