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
#include "dotillism.h"


vj_effect *dotillism_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 2;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = h;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = w;
    ve->limits[0][4] = 0;
    ve->limits[1][4] = 1;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 2;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->description = "Dotillism";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Levels", "Vertical Spacing", "Horizontal Spacing", "Invert" );

    return ve;
}

static uint8_t *map = NULL;

int  dotillism_malloc(int w, int h) 
{
    if(map == NULL) {
        map = (uint8_t*) vj_malloc( RUP8(w*h) );
        if(!map) {
            return 0;
        }
    }

    return 1;
}

void dotillism_free() 
{
    if(map) {
        free(map);
        map = NULL;
    }
}

static void dotillism_posterize_input(uint8_t *Y, const int len, const int levels, const int invert)
{
    const unsigned int factor = (256 / levels);
    unsigned int i;

    if(!invert) {
        for( i = 0; i < len; i ++ ) {
            map[i] = Y[i] - ( Y[i] % factor );
        }
    }
    else {
        for( i = 0; i < len; i ++ ) {
            map[i] = (0xff-Y[i]) - ( (0xff-Y[i]) % factor );
        }
    }
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

static inline void draw_circle_add_Y( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value )
{
  const int tx = (bw / 2);
  const int ty = (bh / 2);
  int x, y;

  for (y = -radius; y <= radius; y++)
    for (x = -radius; x <= radius; x++)
      if ((x * x) + (y * y) <= (radius * radius)) {
          if( (tx + x + cx) < w &&
              (ty + y + cy) < h ) {
            int pos = (ty + cy + y) * w + (tx + cx + x);
            data[pos] = (data[pos] + value) % 0xff;
        }
      }
}
static inline void draw_circle_add_UV( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value )
{
  const int tx = (bw / 2);
  const int ty = (bh / 2);
  int x, y;

  for (y = -radius; y <= radius; y++)
    for (x = -radius; x <= radius; x++)
      if ((x * x) + (y * y) <= (radius * radius)) {
          if( (tx + x + cx) < w &&
              (ty + y + cy) < h ) {
            int pos = (ty + cy + y) * w + (tx + cx + x);
            data[pos] = (128 + ((data[pos] - 128) + (value - 128)) ) % 0xff;
        }
      }
}


static void dotillism_apply_stage1( VJFrame *frame, int radius)
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
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                    U[ y1 * w + x1 ] = 128;
                    V[ y1 * w + x1 ] = 128;
                }
            }

            uint32_t val = map[ y * w + x ];
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
               
            draw_circle( Y , x,y, bw, bh, w, h, wrad, val );
            draw_circle( U , x,y, bw, bh, w, h, wrad, u );
            draw_circle( V , x,y, bw, bh, w, h, wrad, v );
        }
    }

}

static void dotillism_apply_stage2( VJFrame *frame, int radius, int space_y, int space_x)
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

    int incr_y = rad + space_y;
    int incr_x = radius;

    for( y = 0; y < h; y += incr_y ) {
        for( x = 0; x < w; x += incr_x ) {

            uint8_t u = U[ y * w + x ];
            uint8_t v = V[ y * w + x ];

            incr_x = radius + space_x + ( map[ y * w + x ] % rad );
            uint32_t val = map[ y * w + x ];
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * rad);
               
            draw_circle_add_Y( Y , x,y, bw, bh, w, h, wrad, val );
            draw_circle_add_UV( U , x,y, bw, bh, w, h, wrad, u );
            draw_circle_add_UV( V , x,y, bw, bh, w, h, wrad, v );
        }
    }

}


void dotillism_apply( VJFrame *frame, int radius, int levels, int min_v_spacing, int min_h_spacing, int invert)
{
    dotillism_posterize_input( frame->data[0], frame->len, levels, invert );

    dotillism_apply_stage1( frame, radius );

    dotillism_apply_stage2( frame, radius, min_v_spacing, min_h_spacing );
}
