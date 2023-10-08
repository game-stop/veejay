/* veejay - Linux VeeJay
 *       (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include "transop.h"
#include "wipe.h"

typedef struct {
    double g_wipe_width;
    double g_wipe_height;
} fx_wipe_t;

vj_effect *wipe_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = (w > h ? w: h);
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->description = "Transition Wipe";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Speed", "Restart" );
    ve->is_transition_ready_func = wipe_ready;
    return ve;

}

typedef struct {
	int wipePosition;
	int wipeDirection;
} wipe_t;

int  wipe_ready(void *ptr, int width, int height) { 
    wipe_t *w = (wipe_t*) ptr;
	if( w->wipePosition >= width || w->wipePosition <= 0 )
		return TRANSITION_COMPLETED;
    return TRANSITION_RUNNING;
}

void *wipe_malloc( int w, int h )
{
    wipe_t *prv = (wipe_t*) vj_calloc(sizeof(wipe_t));
	prv->wipeDirection = 1;
    return prv;
}

void wipe_free(void *ptr)
{
    free(ptr);
}


void wipe_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
	wipe_t *wipe = (wipe_t*) ptr;

	int width = frame->width;
    int height = frame->height;
    int speed = args[0];
    int restart = args[1];

    wipe->wipePosition += speed * wipe->wipeDirection;

    if (wipe->wipePosition >= width) {
        wipe->wipePosition = width - 1;
        wipe->wipeDirection = -1;
    } else if (wipe->wipePosition <= 0) {
        wipe->wipePosition = 0;
        wipe->wipeDirection = 1;
    }

    for (int i = 0; i < height; ++i) {
        for (int j = 0; j < width; ++j) {
            int index = i * width + j;

            if (j >= wipe->wipePosition) {
                frame->data[0][index] = frame2->data[0][index];
                frame->data[1][index] = frame2->data[1][index];
                frame->data[2][index] = frame2->data[2][index];
            }
        }
    }
}
