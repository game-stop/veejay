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
#include "gammacompr.h"

/* 
 * Before applying Strong Luma Overlay, apply this effect ;)
 *
 */

vj_effect *gammacompr_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 3000;
    ve->defaults[1] = 240;
    ve->defaults[2] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 6000;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 0xff;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 0xff;
    
    ve->description = "Gamma Compression";
    ve->extra_frame = 0;
    ve->sub_format = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Gamma Compression", "White Threshold", "Black Threshold");
    ve->parallel = 1;
	return ve;
}

#define GAMMA_MAX 256
typedef struct {
    double *gamma_table;
    double gamma_value;
} gammacompr_t;

void *gammacompr_malloc(int w, int h)
{
    gammacompr_t *g = (gammacompr_t*) vj_calloc(sizeof(gammacompr_t));
    if(!g) {
        return NULL;
    }

    g->gamma_table = (double*) vj_calloc(sizeof(double) * GAMMA_MAX );
    if(!g->gamma_table) {
        free(g);
        return NULL;
    }
    
    return (void*) g;
}   

void gammacompr_free(void *ptr) 
{
    gammacompr_t *g = (gammacompr_t*) ptr;
    free(g->gamma_table);
    free(g);
}

static void gammacompr_setup(gammacompr_t *g)
{
    int i;
    double val;
    double gm = (double) GAMMA_MAX;
    for (i = 0; i < GAMMA_MAX; i++) {
	     val = i / gm;
	     val = pow(val, g->gamma_value + ((double) i * 0.01));
	     val = gm * val;
	     g->gamma_table[i] = (val < 0.0 ? 0.0 : val > 255.0 ? 255.0 : val);
    }
}

void gammacompr_apply(void *ptr, VJFrame *frame, int *args ) {
    int value = args[0];
    int white_threshold = args[1];
    int black_threshold = args[2];

    gammacompr_t *g = (gammacompr_t*) ptr;

	unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    double v = ( (double) value - 3000.0) / 1000.0;
    
    if (v != g->gamma_value) {
        g->gamma_value = v;
        gammacompr_setup(g);
	}

    double *gamma_table = g->gamma_table;

    for (i = 0; i < len; i++) {
		Y[i] = (uint8_t) gamma_table[Y[i]];
    }

    if (white_threshold > 0 || black_threshold > 0) {
        for (i = 0; i < len; i++) {
            int y = Y[i];

            int white_mask = -(y > white_threshold);
            int black_mask = -(y < black_threshold);

            U[i] = (U[i] & ~(white_mask | black_mask)) | (128 & (white_mask | black_mask));
            V[i] = (V[i] & ~(white_mask | black_mask)) | (128 & (white_mask | black_mask));
        }
    }
}
