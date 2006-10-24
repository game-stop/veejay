/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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
#include <config.h>
#include <stdlib.h>
#include "common.h"
#include "split.h"
#include <stdlib.h>
#include <stdio.h>

static uint8_t *zoom_buffer[3];

vj_effect *zoom_init(int width , int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = width/2;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = height/2;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 4;

    ve->description = "Zoom x 2 (fixme)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;


    return ve;
}
int	zoom_malloc(int width, int height)
{
	return 1;
}

void zoom_free() {
}


void zoom_apply( VJFrame *frame, int width, int height, int x_offset, int y_offset, int factor)

{
	
}

