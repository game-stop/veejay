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
#include <libvjmem/vjmem.h>
#include "raster.h"

static int *xval;

vj_effect *raster_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);     /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 4;
    ve->limits[1][0] = h/4;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 1;
    ve->description = "Grid";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Grid size", "Mode");

    ve->hints = vje_init_value_hint_list( ve->num_params );
    vje_build_value_hint_list( ve->hints, ve->limits[1][1], 1,"Black", "White" );

    return ve;
}

int raster_malloc (int w , int h)
{
    xval = vj_malloc(sizeof(int)*w);
    if (xval == NULL)
        return 0;
    return 1;
}

void raster_free()
{
    if (xval == NULL) free(xval);
}

void raster_apply(VJFrame *frame, int val, int mode)
{
    int x,y, yval;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb= frame->data[1];
    uint8_t *Cr= frame->data[2];
    const unsigned int width = frame->width;
    const unsigned int height = frame->height;

    if(val == 0 )
      return;

    for(x=0; x < width; x++)
    {
        xval[x] = x%val;
    }

    uint8_t pixel_color = mode ? pixel_Y_hi_ : pixel_Y_lo_;

    for(y=0; y < height; y++)
    {
        yval = y%val;
        for(x=0; x < width; x++)
        {
            Y[y*width+x] = ((xval[x]>1)? ((yval>1) ? Y[y*width+x]: pixel_color):pixel_color);
            Cb[y*width+x] = ((xval[x]>1)? ((yval>1) ? Cb[y*width+x]:128):128);
            Cr[y*width+x] = ((xval[x]>1)? ((yval>1) ? Cr[y*width+x]:128):128);
        }
    }
}
