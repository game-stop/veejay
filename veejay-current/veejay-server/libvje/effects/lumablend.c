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
#include "lumablend.h"

vj_effect *lumablend_init(int w, int h)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;
    ve->defaults[0]  = 0;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1]  = 0;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2]  = 35;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3]  = 150;
    ve->description = "Soft-Edge Luma Flow Mixer";
    ve->param_description = vje_build_param_list(ve->num_params,"Trigger Source","Min Threshold","Max Threshold","Master Opacity");
	ve->extra_frame = 1;
	ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Threshold A", "Threshold B", "Opacity" );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0],0,"Source A", "Source B");

	return ve;
}

typedef struct {
    int n_threads;
} lumablend_t;

void *lumablend_malloc(int w, int h) {
    lumablend_t *lb = (lumablend_t*) vj_malloc(sizeof(lumablend_t));
    if(!lb) return NULL;
    lb->n_threads = vje_advise_num_threads(w*h);
    return (void*) lb;
}

void lumablend_free(void *ptr ) {
    lumablend_t *lb = (lumablend_t*) ptr;
    if(lb) {
        free(lb);
    }
}

void lumablend_apply(void *ptr, VJFrame * frame, VJFrame * frame2, int *args)
{
    lumablend_t *lb = (lumablend_t*) ptr;

    const int type    = args[0];
    const int t1      = (args[1] < args[2]) ? args[1] : args[2];
    const int t2      = (args[1] < args[2]) ? args[2] : args[1];
    const int opacity = (args[3] > 255) ? 255 : (args[3] < 0 ? 0 : args[3]);

    const int len = frame->len;
    const int t_range = t2 - t1;

    uint8_t *restrict y1 = frame->data[0];
    uint8_t *restrict u1 = frame->data[1];
    uint8_t *restrict v1 = frame->data[2];
    uint8_t *restrict y2 = frame2->data[0];
    uint8_t *restrict u2 = frame2->data[1];
    uint8_t *restrict v2 = frame2->data[2];

    const int op1 = opacity;
    const int op0 = 256 - op1;

    #pragma omp parallel for schedule(static) num_threads(lb->n_threads)
    for (int i = 0; i < len; ++i) {
        const int trigger_y = (type == 0) ? y1[i] : y2[i];
        int mask = 0;
        if (trigger_y >= t1 && trigger_y <= t2) {
            mask = 256;
        } else if (trigger_y > t1 - 4 && trigger_y < t1) {
            mask = (trigger_y - (t1 - 4)) << 6;
        } else if (trigger_y > t2 && trigger_y < t2 + 4) {
            mask = ((t2 + 4) - trigger_y) << 6;
        }

        if (mask > 0) {
            const int w2 = (mask * op1) >> 8;
            const int w1 = 256 - w2;

            y1[i] = (w1 * y1[i] + w2 * y2[i] + 128) >> 8;
            u1[i] = (w1 * u1[i] + w2 * u2[i] + 128) >> 8;
            v1[i] = (w1 * v1[i] + w2 * v2[i] + 128) >> 8;
        }
    }
}