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
#include "3bar.h"

vj_effect *bar_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->defaults[2] = 3;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = height;

    ve->sub_format = 1;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = height;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = height;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = width;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = width;
    ve->description = "Horizontal Sliding Bars";

    ve->extra_frame = 1;
	ve->has_user = 0;
    ve->parallel = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Divider", "Top Y", "Bot Y", "Top X", "Bot X");
    return ve;
}

typedef struct {
    int bar_top_auto_x;
    int bar_bot_auto_x;
    int bar_top_auto_y;
    int bar_bot_auto_y;
} bar_t;

void *bar_malloc(int w, int h) {
    return vj_calloc(sizeof(bar_t));
}

void bar_free(void *ptr) {
    free(ptr);
}

void bar_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int divider   = args[0];
    const int top_y_arg = args[1];
    const int bot_y_arg = args[2];
    const int top_x_arg = args[3];
    const int bot_x_arg = args[4];

    bar_t *bar = (bar_t *) ptr;

    const unsigned int width  = frame->width;
    const unsigned int height = frame->height;

    const unsigned int safe_divider = (divider > 0) ? divider : 1;
    const unsigned int top_height    = height / safe_divider;
    const unsigned int bottom_height = height - top_height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

    bar->bar_top_auto_y = (bar->bar_top_auto_y + top_y_arg) % top_height;
    bar->bar_top_auto_x = (bar->bar_top_auto_x + top_x_arg) % width;

    for (unsigned int y = 0; y < top_height; y++)
    {
        unsigned int src_y = (y + bar->bar_top_auto_y) % top_height;
        
        uint8_t *dY  = Y  + y * width;
        uint8_t *dCb = Cb + y * width;
        uint8_t *dCr = Cr + y * width;

        const uint8_t *sY  = Y2  + src_y * width;
        const uint8_t *sCb = Cb2 + src_y * width;
        const uint8_t *sCr = Cr2 + src_y * width;

        for (unsigned int x = 0; x < width; x++)
        {
            unsigned int src_x = (x + bar->bar_top_auto_x) % width;
            dY[x]  = sY[src_x];
            dCb[x] = sCb[src_x];
            dCr[x] = sCr[src_x];
        }
    }

    if (bottom_height > 0) {
        bar->bar_bot_auto_y = (bar->bar_bot_auto_y + bot_y_arg) % bottom_height;
        bar->bar_bot_auto_x = (bar->bar_bot_auto_x + bot_x_arg) % width;

        const unsigned int bottom_start = top_height * width;

        for (unsigned int y = 0; y < bottom_height; y++)
        {
            unsigned int src_y = (y + bar->bar_bot_auto_y) % bottom_height;
            
            uint8_t *dY  = Y  + bottom_start + y * width;
            uint8_t *dCb = Cb + bottom_start + y * width;
            uint8_t *dCr = Cr + bottom_start + y * width;

            const uint8_t *sY  = Y2  + bottom_start + src_y * width;
            const uint8_t *sCb = Cb2 + bottom_start + src_y * width;
            const uint8_t *sCr = Cr2 + bottom_start + src_y * width;

            for (unsigned int x = 0; x < width; x++)
            {
                unsigned int src_x = (x + bar->bar_bot_auto_x) % width;
                dY[x]  = sY[src_x];
                dCb[x] = sCb[src_x];
                dCr[x] = sCr[src_x];
            }
        }
    }
}

