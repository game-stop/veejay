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
    ve->num_params = 7;

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
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 7;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = 2;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 2;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;
    ve->defaults[6] = 0;
    ve->description = "Dotillism";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Levels", "Vertical Spacing", "Horizontal Spacing", "Invert", "Orientation", "Parity" );

    ve->hints = vje_init_value_hint_list( ve->num_params );
    vje_build_value_hint_list( ve->hints, ve->limits[1][5],5, "Centered", "North", "North East", "East" , "South East", "South West", "West" , "North West");
    vje_build_value_hint_list( ve->hints, ve->limits[1][6],6, "Even", "Odd", "No parity"); //TODO add 'Berzek?' parameter aka broken/random parity; very cool on Mode animation

    return ve;
}

typedef struct {
    uint8_t *map;
} dotillism_t;

void *dotillism_malloc(int w, int h) 
{
    dotillism_t *d = (dotillism_t*) vj_calloc(sizeof(dotillism_t));
    if(!d) {
        return NULL;
    }
    d->map = (uint8_t*) vj_malloc( w*h );
    if(!d->map) {
        free(d->map);
        free(d);
        return NULL;
    }

    return (void*) d;
}

void dotillism_free(void *ptr) 
{
    dotillism_t *d = (dotillism_t*) ptr;
    free(d->map);
    free(d);
}

static void dotillism_posterize_input(uint8_t *map, uint8_t *Y, const int len, const int levels, const int invert)
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

static inline void draw_circle_add_Y( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value )
{
    const int tx = bw >> 1;
    const int ty = bh >> 1;
    const int radiusSquared = radius * radius;
    const int minX = (cx - tx < 0) ? 0 : (cx - tx);
    const int minY = (cy - ty < 0) ? 0 : (cy - ty);
    const int maxX = (cx + tx >= w) ? (w - 1) : (cx + tx);
    const int maxY = (cy + ty >= h) ? (h - 1) : (cy + ty);

    for (int y = minY; y <= maxY; y++)
    {
        int yOffset = (y - cy) * (y - cy);
        for (int x = minX; x <= maxX; x++)
        {
            int xOffset = (x - cx) * (x - cx);
            int distanceSquared = xOffset + yOffset;

            if (distanceSquared <= radiusSquared)
            {
                data[y * w + x] = (data[y * w + x] + value) % 0xff;
            }
        }
    }
	
}
static inline void draw_circle_add_UV( uint8_t *data, int cx, int cy, const int bw, const int bh, const int w, const int h, int radius, uint8_t value )
{
    const int tx = bw >> 1;
    const int ty = bh >> 1;
    const int radiusSquared = radius * radius;
    const int minX = (cx - tx < 0) ? 0 : (cx - tx);
    const int minY = (cy - ty < 0) ? 0 : (cy - ty);
    const int maxX = (cx + tx >= w) ? (w - 1) : (cx + tx);
    const int maxY = (cy + ty >= h) ? (h - 1) : (cy + ty);

    for (int y = minY; y <= maxY; y++)
    {
        int yOffset = (y - cy) * (y - cy);
        for (int x = minX; x <= maxX; x++)
        {
            int xOffset = (x - cx) * (x - cx);
            int distanceSquared = xOffset + yOffset;

            if (distanceSquared <= radiusSquared)
            {
                data[y * w + x] = ( 128 + (( data[y * w + x ] - 128 ) + (value - 128)) ) % 0xff;
            }
        }
    }
	
}

static void dotillism_apply_stage1(uint8_t *map, VJFrame *frame, int radius, int orientation, int parity)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius / 2;
    const int bw = radius;
    const int bh = radius;

    int x, y, x1, y1, x_inf, y_inf, x_sup, y_sup;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup);


	for (y = 0; y < y_inf; y++) {
#pragma omp simd
		for (x = 0; x < w; x++) {
        	int index = y * w + x;
        	Y[index] = pixel_Y_lo_;
        	U[index] = 128;
        	V[index] = 128;
    	}
	}

	for (y = (h - radius); y < h; y++) {
#pragma omp simd
		for (x = 0; x < w; x++) {
        	int index = y * w + x;
        	Y[index] = pixel_Y_lo_;
        	U[index] = 128;
        	V[index] = 128;
    	}
	}

	for (y = y_inf; y < (h - radius); y++) {
#pragma omp simd
		for (x = 0; x < x_inf; x++) {
        	int index = y * w + x;
        	Y[index] = pixel_Y_lo_;
        	U[index] = 128;
        	V[index] = 128;
    	}
	}

	for (y = y_inf; y < (h - radius); y++) {
#pragma omp simd
		for (x = (w - radius); x < w; x++) {
        	int index = y * w + x;
        	Y[index] = pixel_Y_lo_;
        	U[index] = 128;
        	V[index] = 128;
    	}
	}

    for (y = y_inf; y < (h - radius); y += radius) {
        for (x = x_inf; x < (w - radius); x += radius) {
            uint8_t u = U[y * w + x];
            uint8_t v = V[y * w + x];

            int lim_x = x + radius;
            int lim_y = y + radius;

            uint32_t val = map[y * w + x];
            int wrad = 1 + (int)(((double)val / 255.0) * rad);

            for (y1 = y; y1 < lim_y; y1++) {
#pragma omp simd
				for (x1 = x; x1 < lim_x; x1++) {
                    int index = y1 * w + x1;
                    Y[index] = pixel_Y_lo_;
                    U[index] = 128;
                    V[index] = 128;
                }
            }

            veejay_draw_circle(Y, x, y, bw, bh, w, h, wrad, val);
            veejay_draw_circle(U, x, y, bw, bh, w, h, wrad, u);
            veejay_draw_circle(V, x, y, bw, bh, w, h, wrad, v);
        }
    }
}



static void dotillism_apply_stage2( uint8_t *map, VJFrame *frame, int radius, int space_y, int space_x, int orientation, int parity)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    
    const int w = frame->width;
    const int h = frame->height;
    const int rad = radius/2;
    const int bw = radius;
    const int bh = radius;

    int x,y,x_inf,y_inf, x_sup, y_sup;

    int incr_y = rad + space_y;
    int incr_x = radius;

    x_inf = 0; // initial init for North East
    y_inf = 0;
    x_sup = w;
    y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup);

    for( y = y_inf; y < h; y += incr_y ) {
        for( x = x_inf; x < w; x += incr_x ) {

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


void dotillism_apply( void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int levels = args[1];
    int min_v_spacing = args[2];
    int min_h_spacing = args[3];
    int invert = args[4];
    int orientation = args[5];
    int parity = args[6];

    dotillism_t *d = (dotillism_t*) ptr;

    dotillism_posterize_input( d->map,frame->data[0], frame->len, levels, invert );

    dotillism_apply_stage1( d->map, frame, radius, orientation, parity);

    dotillism_apply_stage2( d->map, frame, radius, min_v_spacing, min_h_spacing, orientation, parity );
}
