/* 
 * Linux VeeJay
 *
 * Copyright(C)2019 Niels Elburg <nwelburg@gmail.com>
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
#include "halftone.h"


/*
 * simple effect that iterates over a frame using a bounding box
 * a new value will be determined (average of all pixels in the bounding box, the brightest or the darkest pixel)
 * and the bounding box will be filled with a circle filled with this new value
 */

vj_effect *halftone_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 3;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 0;
    ve->description = "Halftone";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Mode" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][1],1, "White Dots", "Black Dots", "Gray Dots", "Colored Dots" );

    return ve;
}

int  halftone_malloc(int w, int h) 
{
    return 1;
}

void halftone_free() 
{
}

static inline void draw_circle( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value )
{
  const int tx = (bw / 2);
  const int ty = (bh / 2);
  int x, y;

  for (y = -radius; y <= radius; y++)
    for (x = -radius; x <= radius; x++)
      if ((x * x) + (y * y) <= (radius * radius)) {
          if( (tx + x + cx) < w &&
              (ty + y + cy) < h ) {
            data[(ty + cy + y) * w + (tx + cx + x) ] = value;
        }
      }
}

static void halftone_apply_avg_col( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    
    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius/2;
    const int bw = radius;
    const int bh = radius;

    int x,y,x1,y1;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            uint32_t sum = 0;
            uint32_t hit = 0;

            uint8_t u = U[ y * w + x ];
            uint8_t v = V[ y * w + x ];

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = y; y1 < lim_y; y1 ++ ) {
                for( x1 = x; x1 < lim_x; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                    U[ y1 * w + x1 ] = 128;
                    V[ y1 * w + x1 ] = 128;
                }
            }

            uint32_t val = (sum / hit);
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
               
            draw_circle( Y , x,y, bw, bh, w, h, wrad, val );
            draw_circle( U , x,y, bw, bh, w, h, wrad, u );
            draw_circle( V , x,y, bw, bh, w, h, wrad, v );
        }
    }

}


static void halftone_apply_avg_gray( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    
    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius/2;
    const int bw = radius;
    const int bh = radius;

    int x,y,x1,y1;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            uint32_t sum = 0;
            uint32_t hit = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                }
            }

            uint32_t val = (sum / hit);
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
            draw_circle( Y,x,y, bw, bh, w, h, wrad, val );
        }
    }

    veejay_memset( U, 128, w * h );
    veejay_memset( V, 128, w * h );
}


static void halftone_apply_avg_black( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    
    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius/2;
    const int bw = radius;
    const int bh = radius;

    int x,y,x1,y1;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            uint32_t sum = 0;
            uint32_t hit = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_hi_;
                }
            }

            uint32_t val = (sum / hit);
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
            draw_circle( Y,x,y, bw, bh, w, h, wrad, pixel_Y_lo_ );
        }
    }

    veejay_memset( U, 128, w * h );
    veejay_memset( V, 128, w * h );
}

static void halftone_apply_avg_white( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    
    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius/2;
    const int bw = radius;
    const int bh = radius;

    int x,y,x1,y1;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            uint32_t sum = 0;
            uint32_t hit = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                }
            }

            uint32_t val = (sum / hit);
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
            draw_circle( Y,x,y, bw, bh, w, h, wrad, pixel_Y_hi_ );
        }
    }

    veejay_memset( U, 128, w * h );
    veejay_memset( V, 128, w * h );
}

void halftone_apply( VJFrame *frame, int radius, int mode)
{
    switch(mode) {
        case 0:
            halftone_apply_avg_white( frame, radius );
            break;
        case 1:
            halftone_apply_avg_black( frame, radius );
            break;
        case 2:
            halftone_apply_avg_gray( frame, radius );
            break;
        case 3:
            halftone_apply_avg_col( frame, radius );
            break;
    }
}
