/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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


 /* 
    Linux VeeJay - Effectv's SmuckTV
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "smuck.h"

#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

vj_effect *smuck_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4; 
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 12;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 17;

    ve->defaults[1] = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->defaults[2] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->defaults[3] = 2;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;

    ve->description = "SmuckTV (Pro)";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Shimmer",
        "Full color",
        "Static seed",
        "Direction");
        
    ve->sub_format = 1; 
    return ve;
}

typedef struct {
    unsigned int rand_val;
} smuck_t;

void* smuck_malloc(int w, int h) {
    return (void*) vj_calloc(sizeof(smuck_t));
}

void smuck_free(void *ptr) {
    if(ptr) free(ptr);
}

static inline unsigned int smuck_fastrand(smuck_t *s)
{
    s->rand_val = s->rand_val * 1103516245 + 12345;
    return s->rand_val;
}

void smuck_apply(void *ptr, VJFrame *frame, int *args)
{
    smuck_t *s = (smuck_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;
    const int n = CLAMP(args[0], 0, 17);
    const int mode = args[1];
    const int static_mode = args[2];
    const int bias = args[3];

    const unsigned int smuck_table[18] = {
        10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27
    };
    const unsigned int shift = smuck_table[n];

    const int mx = (bias == 0 || bias == 2) ? 1 : 0;
    const int my = (bias == 1 || bias == 2) ? 1 : 0;

    if (static_mode) {
        s->rand_val = 0x1337BEEF;
    }

    uint8_t *restrict Y = frame->data[0];

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned int r = smuck_fastrand(s);
            
            int dx = ((int)((r >> shift) & 0x7) - 3) * mx;
            int dy = ((int)((r >> (shift + 3)) & 0x7) - 3) * my;

            int xd = CLAMP(x + dx, 0, w - 1);
            int yd = CLAMP(y + dy, 0, h - 1);

            Y[y * w + x] = Y[yd * w + xd];
        }
    }

    if (mode == 1) {
        const int cw = w >> 1;
        const int ch = h >> 1;
        uint8_t *restrict U = frame->data[1];
        uint8_t *restrict V = frame->data[2];

        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                unsigned int r = smuck_fastrand(s);
                int dx = ((int)((r >> shift) & 0x3) - 1) * mx;
                int dy = ((int)((r >> (shift + 2)) & 0x3) - 1) * my;

                int xd = CLAMP(x + dx, 0, cw - 1);
                int yd = CLAMP(y + dy, 0, ch - 1);

                U[y * cw + x] = U[yd * cw + xd];
                V[y * cw + x] = V[yd * cw + xd];
            }
        }
    }
}
