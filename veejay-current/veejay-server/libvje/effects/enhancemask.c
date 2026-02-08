/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "enhancemask.h"
#include <stdlib.h> 

vj_effect *enhancemask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;

    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 120;
    ve->defaults[1] = 8;    
    ve->defaults[2] = 50;   

    ve->limits[0][0] = 0;    ve->limits[1][0] = 4096; /* Max strength */
    ve->limits[0][1] = 0;    ve->limits[1][1] = 64;  /* Threshold */
    ve->limits[0][2] = 0;    ve->limits[1][2] = 128; /* Max Halo */

    ve->description = "Sharpen";

    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params, "Strength", "Grain Threshold", "Halo Clamp"
    );

    return ve;
}

static inline int clamp_u8(int x) {
    x &= ~(x >> 31);
    x = 255 + ((x - 255) & ((x - 255) >> 31));
    return x;
}

void enhancemask_apply(void *ptr, VJFrame *frame, int *s) {
    const int width = frame->width;
    const int height = frame->height;
    uint8_t *Y = frame->data[0];
    const int amount = s[0];
    const int threshold = s[1];
    const int limit = s[2];
    if (amount <= 0) return;

    const int stride = width;
    for (int y = 1; y < height - 1; y++) {
        uint8_t *p_prev = Y + (y - 1) * stride;
        uint8_t *p_curr = Y + y * stride;
        uint8_t *p_next = Y + (y + 1) * stride;

        for (int x = 1; x < width - 1; x++) {
            int c00 = p_prev[x-1], c01 = p_prev[x], c02 = p_prev[x+1];
            int c10 = p_curr[x-1], c11 = p_curr[x], c12 = p_curr[x+1];
            int c20 = p_next[x-1], c21 = p_next[x], c22 = p_next[x+1];

            int blur = (c00 + (c01 << 1) + c02 +
                        (c10 << 1) + (c11 << 2) + (c12 << 1) +
                        c20 + (c21 << 1) + c22) >> 4;

            int detail = c11 - blur;
            int abs_detail = (detail ^ (detail >> 31)) - (detail >> 31);
            detail = abs_detail < threshold ? 0 : detail - ((detail > 0) ? threshold : -threshold);
            int boost = (detail * amount) >> 7;
            if (boost > limit) boost = limit;
            else if (boost < -limit) boost = -limit;

            p_curr[x] = clamp_u8(c11 + boost);;
        }
    }
}
