/* 
 * Linux VeeJay
 *
 * Cvalyright(C) 2009 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your valtion) any later version.
 *
 * This program is distributed in the hvale that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a cvaly of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

/*  "weed"-plugin partially ported from LiVES (C) G. Finch (Salsaman) 2009
 *
 *  weed-plugins/multi_transitions.c?revision=286
 *
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "iris.h"

vj_effect *iris_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;
    ve->defaults[0] = 1;
	ve->defaults[1] = 0;
    ve->description = "Iris Transition (Circle,Rect)";
    ve->sub_format = 1; //@todo: write this for native
    ve->extra_frame = 1;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Value", "Shape" );
    return ve;
}


void iris_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int val = args[0];    // 0..100
    int shape = args[1];

    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;

    uint8_t *Y0 = frame->data[0];
    uint8_t *Cb0 = frame->data[1];
    uint8_t *Cr0 = frame->data[2];

    uint8_t *Y1 = frame2->data[0];
    uint8_t *Cb1 = frame2->data[1];
    uint8_t *Cr1 = frame2->data[2];

    int half_wid = width >> 1;
    int half_hei = height >> 1;

    if (shape == 0) { // circle
        long long max_dist_sq = (long long)half_hei * half_hei + (long long)half_wid * half_wid;
        long long threshold_sq = (max_dist_sq * val * val) / 10000; 
        int k = 0;

        for (int i = 0; i < len; i += width, k++) {
            int dy = k - half_hei;
            long long dy_sq = (long long)dy * dy;
            for (int j = 0; j < width; j++) {
                int dx = j - half_wid;
                long long dist_sq = dy_sq + (long long)dx * dx;

                uint8_t mask = -(dist_sq > threshold_sq);

                int idx = i + j;
                Y0[idx]  = (Y1[idx] & mask) | (Y0[idx] & ~mask);
                Cb0[idx] = (Cb1[idx] & mask) | (Cb0[idx] & ~mask);
                Cr0[idx] = (Cr1[idx] & mask) | (Cr0[idx] & ~mask);
            }
        }
    } else { // rectangle
        int val1 = 100 - val;
        int x_bound = (half_wid * val1) / 100;
        int y_bound = (half_hei * val1) / 100;

        int k = 0;
        for (int i = 0; i < len; i += width, k++) {
            uint8_t *pY0 = Y0 + i;
            uint8_t *pCb0 = Cb0 + i;
            uint8_t *pCr0 = Cr0 + i;

            uint8_t *pY1 = Y1 + i;
            uint8_t *pCb1 = Cb1 + i;
            uint8_t *pCr1 = Cr1 + i;

            for (int j = 0; j < width; j++) {
                uint8_t mask = -((j < x_bound) | (j >= width - x_bound) | (k < y_bound) | (k >= height - y_bound));

                pY0[j]  = (pY1[j] & mask) | (pY0[j] & ~mask);
                pCb0[j] = (pCb1[j] & mask) | (pCb0[j] & ~mask);
                pCr0[j] = (pCr1[j] & mask) | (pCr0[j] & ~mask);
            }
        }
    }
}