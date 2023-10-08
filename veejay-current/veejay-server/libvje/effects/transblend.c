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
#include "transblend.h"
#include <libvje/internal.h>

vj_effect *transblend_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 1;
    ve->defaults[1] = 1;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = (width > height ? width : height);
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;

    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->description = "Transition Wipe Clockwise";
    ve->param_description = vje_build_param_list( ve->num_params, "Speed", "Bounce");

    return ve;
}


typedef struct {
	float wipePositionX;
	float wipePositionY;
	int directionX;
	int directionY;
} wipe_t;

void *transblend_malloc(int w, int h)
{
	wipe_t *wipe = (wipe_t*) vj_calloc(sizeof(wipe_t));
	wipe->directionX = 1;
	wipe->directionY = 1;
	return wipe;
}

void transblend_free(void *ptr) {
	free(ptr);
}
void transblend_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    wipe_t *wipe = (wipe_t *)ptr;
    int width = frame->width;
    int height = frame->height;
    int speed = args[0];
    int bounce = args[1];

    int centerX = width / 2;
    int centerY = height / 2;

    wipe->wipePositionX += speed * wipe->directionX;

    if (wipe->wipePositionX >= centerX || wipe->wipePositionX <= 0) {
        if (bounce) {
            wipe->directionX *= -1;
		}
        wipe->wipePositionX = (wipe->directionX > 0) ? 0 : centerX;
    }

    const int d = wipe->wipePositionX * wipe->wipePositionX;

    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            int index = i * width + j;
            int distance = (i - centerY) * (i - centerY) + (j - centerX) * (j - centerX);

            if (distance <= d) {
                frame->data[0][index] = frame2->data[0][index];
                frame->data[1][index] = frame2->data[1][index];
                frame->data[2][index] = frame2->data[2][index];
            }
        }
    }

}


