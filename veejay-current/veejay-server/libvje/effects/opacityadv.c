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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "opacityadv.h"

vj_effect *opacityadv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[0] = 150;
    ve->defaults[1] = 40;
    ve->defaults[2] = 176;
    ve->parallel = 0;
	ve->description = "Soft-Edge Luma Key";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user =0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Min Threshold", "Max Threshold");
    return ve;
}

typedef struct {
	int n_threads;
} opacityadv_t;

void *opacityadv_malloc(int w, int h) {
	opacityadv_t *opa = (opacityadv_t*) vj_malloc(sizeof(opacityadv_t));
	if(!opa)
		return NULL;
	opa->n_threads = vje_advise_num_threads( w * h );
	return (void*) opa;
}

void opacityadv_free(void *ptr) {
	opacityadv_t *opa = (opacityadv_t*) ptr;
	if(opa) {
		free(opa);
	}
}

void opacityadv_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {

	opacityadv_t *opa = (opacityadv_t*) ptr;

    const int opacity = args[0];
    const int tmin    = args[1];
    const int tmax    = args[2];
    const int width   = frame->width;
    const int height  = frame->height;
    const int len     = width * height;

    uint8_t *Y1 = frame->data[0],  *Y2 = frame2->data[0];
    uint8_t *U1 = frame->data[1],  *U2 = frame2->data[1];
    uint8_t *V1 = frame->data[2],  *V2 = frame2->data[2];

    const int global_weight = (opacity > 255) ? 256 : opacity;

    #pragma omp parallel num_threads(opa->n_threads)
    {
        #pragma omp for schedule(static)
        for (int i = 0; i < len; i++) {
            int y_val = Y1[i];
            int local_mask = 0;

            if (y_val >= tmin && y_val <= tmax) {
                local_mask = 256;
            }
            else if (y_val > tmin - 4 && y_val < tmin) {
                local_mask = (y_val - (tmin - 4)) << 6;
            }
            else if (y_val > tmax && y_val < tmax + 4) {
                local_mask = ((tmax + 4) - y_val) << 6;
            }

            int final_w2 = (local_mask * global_weight) >> 8;
            int final_w1 = 256 - final_w2;

            if (final_w2 > 0) {
                Y1[i] = (final_w1 * Y1[i] + final_w2 * Y2[i] + 128) >> 8;
                U1[i] = (final_w1 * U1[i] + final_w2 * U2[i] + 128) >> 8;
                V1[i] = (final_w1 * V1[i] + final_w2 * V2[i] + 128) >> 8;
            }
        }
    }
}