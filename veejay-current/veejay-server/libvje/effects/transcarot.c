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

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include "transcarot.h"

vj_effect *transcarot_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 1;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 1;
	ve->limits[0][0] = 0;
	ve->limits[1][0] = (width < height ? width : height);

	ve->sub_format = 1;
	ve->param_description = vje_build_param_list(ve->num_params, "Speed" );
	ve->description = "Transition Wipe Diagonal";
	ve->has_user = 0;
	ve->extra_frame = 1;
	return ve;
}

typedef struct {
	float wipePositionX;
	float wipePositionY;
	int directionX;
	int directionY;
} wipe_t;

void *transcarot_malloc(int w, int h)
{
	wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
	wipe->directionX = 1;
	wipe->directionY = 1;
	return wipe;
}

void transcarot_free(void *ptr) {
	free(ptr);
}

void transcarot_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    wipe_t *wipe = (wipe_t *)ptr;
    int width = frame->width;
    int height = frame->height;
    int speed = args[0];
    int restart = args[1];

    // Update the wipe position
    wipe->wipePositionX += speed * wipe->directionX;
    wipe->wipePositionY += speed * wipe->directionY;

    // If the wipe reaches the edge of the frame, reverse direction
    if (wipe->wipePositionX >= width || wipe->wipePositionX < 0) {
        wipe->directionX *= -1;
    }
    if (wipe->wipePositionY >= height || wipe->wipePositionY < 0) {
        wipe->directionY *= -1;
    }

    // Apply the wipe effect to the frame data
    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            int index = i * width + j;

            // If the pixel is within the wipe region, copy the pixel from the second frame
            if (j <= wipe->wipePositionX && i <= wipe->wipePositionY) {
                frame->data[0][index] = frame2->data[0][index];
                frame->data[1][index] = frame2->data[1][index];
                frame->data[2][index] = frame2->data[2][index];
            }
        }
    }


}

