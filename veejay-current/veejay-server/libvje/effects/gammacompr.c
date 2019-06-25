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
static double *gamma_table = NULL;
static void gammacompr_setup();
static double gamma_value = 0;

int gammacompr_malloc(int w, int h)
{
    if(gamma_table == NULL) {
        gamma_table = (double**) vj_calloc(sizeof(double) * GAMMA_MAX );
    }
    
    return 1;
}   

void gammacompr_free() 
{
    if(gamma_table) {
        free(gamma_table);
        gamma_table = NULL;
    }
}

static void gammacompr_setup()
{
    int i;
    double val;
    double gm = (double) GAMMA_MAX;
    for (i = 0; i < GAMMA_MAX; i++) {
	     val = i / gm;
	     val = pow(val, gamma_value + ((double) i * 0.01));
	     val = gm * val;
	     gamma_table[i] = (val < 0.0 ? 0.0 : val > 255.0 ? 255.0 : val);
    }
}

void gammacompr_apply(VJFrame *frame, int value, int white_threshold, int black_threshold)
{
	unsigned int i;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    double v = ( (double) value - 3000.0) / 1000.0;
    
    if (v != gamma_value) {
        gamma_value = v;
        gammacompr_setup();
	}

    for (i = 0; i < len; i++) {
		Y[i] = (uint8_t) gamma_table[Y[i]];
    }

    if(white_threshold > 0) {
        for ( i = 0; i < len; i ++ ) {
            if( Y[i] > white_threshold ) {
                U[i] = 128;
                V[i] = 128;
            }
        }
    }

    if( black_threshold > 0 ) {
        for( i = 0; i < len; i ++ ) {
            if( Y[i] < black_threshold) {
                U[i] = 128;
                V[i] = 128;
            }
        }
    }
}
