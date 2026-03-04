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
#include "vbar.h"

vj_effect *vbar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */

	ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;

    for(int i=0; i<5; i++) {
        ve->limits[0][i] = 0;
        ve->limits[1][i] = (i == 0) ? width : (i < 3 ? height : width);
    }
    ve->limits[0][0] = 1;

    ve->description = "Vertical Sliding Bars";
    ve->sub_format = 1;
	ve->parallel = 0;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Divider", "Top Y", "Bot Y", "Top X", "Bot X" ); 
   return ve;
}

typedef struct {
    int bar_top_auto;
    int bar_bot_auto;
    int bar_top_vert;
    int bar_bot_vert;
} vbar_t;

void *vbar_malloc(int w, int h) {
    return (void*) vj_calloc(sizeof(vbar_t));
}

void vbar_free(void *ptr) {
    free(ptr);
}

void vbar_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args) {
    int divider = args[0] > 0 ? args[0] : 1;
    int top_y_delta = args[1];
    int bot_y_delta = args[2];
    int top_x_delta = args[3];
    int bot_x_delta = args[4];

    vbar_t *vbar = (vbar_t*) ptr;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    
    int left_width = width / divider;

    vbar->bar_top_auto = (vbar->bar_top_auto + top_y_delta) % height;
    vbar->bar_top_vert = (vbar->bar_top_vert + top_x_delta) % width;
    vbar->bar_bot_auto = (vbar->bar_bot_auto + bot_y_delta) % height;
    vbar->bar_bot_vert = (vbar->bar_bot_vert + bot_x_delta) % width;

    int y_off_left = vbar->bar_top_auto;
    int x_off_left = vbar->bar_top_vert;
    int y_off_right = vbar->bar_bot_auto;
    int x_off_right = vbar->bar_bot_vert;

    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    
    uint8_t *Y2 = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    for (unsigned int y = 0; y < height; y++) {
        unsigned int src_y = (y + y_off_left) % height;
        
        for (unsigned int x = 0; x < (unsigned int)left_width; x++) {
            unsigned int src_x = (x + x_off_left) % width;
            
            unsigned int dst_pos = y * width + x;
            unsigned int src_pos = src_y * width + src_x;

            Y[dst_pos]  = Y2[src_pos];
            Cb[dst_pos] = Cb2[src_pos];
            Cr[dst_pos] = Cr2[src_pos];
        }
    }

    for (unsigned int y = 0; y < height; y++) {
        unsigned int src_y = (y + y_off_right) % height;
        
        for (unsigned int x = left_width; x < width; x++) {
            unsigned int src_x = (x + x_off_right) % width;
            
            unsigned int dst_pos = y * width + x;
            unsigned int src_pos = src_y * width + src_x;

            Y[dst_pos]  = Y2[src_pos];
            Cb[dst_pos] = Cb2[src_pos];
            Cr[dst_pos] = Cr2[src_pos];
        }
    }
}
