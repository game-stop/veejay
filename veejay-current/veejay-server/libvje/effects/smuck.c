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
#include "smuck.h"

vj_effect *smuck_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
	ve->defaults[1] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 18;

	ve->limits[0][1] = 0;
	ve->limits[1][1] = 1;

    ve->description = "SmuckTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Smuck", "Mode");
    return ve;
}

typedef struct {
    int rand_val;
} smuck_t;

void* smuck_malloc(int w, int h) {
    return (void*) vj_calloc( sizeof(smuck_t) );
}

void smuck_free(void *ptr) {
    free(ptr);
}

static inline unsigned int smuck_fastrand(smuck_t *s)
{
    return (s->rand_val = s->rand_val * 1103516245 + 12345);
}

/* this effect comes from Effect TV as well; the code for this one is in Transform 
   different is the smuck table containing some values. 
   This effect was originally created by Buddy Smith, one of EffecTV's developers from the USA
*/
void smuck_apply( void *ptr, VJFrame *frame, int *args)
{
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
    unsigned int yd, xd, x, y;
    smuck_t *s = (smuck_t*) ptr;
    int n = args[0];
	int mode = args[1];

    VJFrame *frame2 = frame;

	// different table ...
    const unsigned int smuck[18] =
	{ 12, 21, 30, 60, 58, 59, 57, 56, 55, 54, 53, 89, 90, 88, 87, 86, 85, 114 };
	uint8_t *Y = frame->data[0];
	uint8_t *Y2 = frame2->data[0];

	if( mode == 0 ) {
    	for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				yd = (y + (smuck_fastrand(s) >> smuck[n]) - 2) % height;
            	xd = (x + (smuck_fastrand(s) >> smuck[n]) - 2) % width;

			    Y[x + y * width] = Y2[xd + yd * width];
			}
    	}

	} else {
		uint8_t *U = frame->data[1];
		uint8_t *V = frame->data[2];
		uint8_t *U2 = frame->data[1];
		uint8_t *V2 = frame->data[2];

    	for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				yd = (y + (smuck_fastrand(s) >> smuck[n]) - 2) % height;
            	xd = (x + (smuck_fastrand(s) >> smuck[n]) - 2) % width;
					
			    Y[x + y * width] = Y2[xd + yd * width];
				U[x + y * width] = U2[xd + yd * width];
				V[x + y * width] = V2[xd + yd * width];
			}
    	}


	}
}
