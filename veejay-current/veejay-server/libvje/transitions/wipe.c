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
#include <libvjmem/vjmem.h>
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
    ve->defaults[1] = 0;

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

//FIXME: private data

static double g_wipe_width = 0;
static double g_wipe_height = 0;

int  wipe_ready(int width, int height) { //FIXME pass in fx instance data
    if (g_wipe_width == width && g_wipe_height == height)
	    return TRANSITION_COMPLETED;
    return TRANSITION_RUNNING;
}

void    *wipe_instantiate(VJFrame *frame)
{
    fx_wipe_t *prv = (fx_wipe_t*) vj_calloc(sizeof(fx_wipe_t));
    return prv;
}

void    wipe_destroy(void *ptr)
{
    free(ptr);
}


void wipe_apply( VJFrame *frame, VJFrame *frame2, int inc, int restart) //FIXME pass in fx instance data, take parameters from array
{
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    double ratio = (double) width / (double) height;

    transop_apply(frame, frame2, (int) g_wipe_width, (int) g_wipe_height, 0, 0, 0, 0);
    
    g_wipe_width += ratio * ((double ) inc);
    g_wipe_height += ((double) inc);

    if(g_wipe_width > width && g_wipe_height > height) {
        g_wipe_width = width;
        g_wipe_height = height;
    }
    if(restart) {
        g_wipe_width = 0;
        g_wipe_height = 0;
    }
}
