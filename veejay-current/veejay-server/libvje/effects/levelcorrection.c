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

#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "levelcorrection.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"

vj_effect *levelcorrection_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;	/* minimum level */
    ve->defaults[1] = 255;	/* maximum level */

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

	ve->param_description = vje_build_param_list(ve->num_params, "Level Min", "Level Max");

	ve->has_user = 0;
    ve->description = "Alpha: Level Correction";
    ve->extra_frame = 0;
    ve->sub_format = -1;
	ve->rgb_conv = 0;
    ve->parallel = 1;
	return ve;
}

void levelcorrection_apply(VJFrame *frame, int min, int max)
{
	unsigned int pos;
	uint8_t *A = frame->data[3];
	const unsigned int len = frame->len;  
	uint8_t __lookup_table[256];

	/* level correction table */
	__init_lookup_table( __lookup_table, 256, (float)min, (float)max, 0, 0xff ); 

	for( pos = 0; pos < len; pos ++ ) {
		A[pos]  = __lookup_table[A[pos]];
	}

}
