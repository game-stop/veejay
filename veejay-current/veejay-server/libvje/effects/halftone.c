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
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 4;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 7;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->description = "Halftone";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Mode", "Orientation", "Parity" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][1],1, "White Dots", "Black Dots", "Gray Dots", "Colored Dots", "Averaged Colored Dots" );
    vje_build_value_hint_list( ve->hints, ve->limits[1][2],2, "Centered", "North", "North East", "East" , "South East", "South West", "West" , "North West");
    vje_build_value_hint_list( ve->hints, ve->limits[1][3],3, "Even", "Odd", "No parity"); //TODO add 'Berzek?' parameter aka broken/random parity; very cool on Mode animation

    return ve;
}


static void clear_margins(uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int x_inf, int y_inf, int radius, int color) {
    int x, y;
    int x_end = x_inf + ((w - x_inf) / radius) * radius;
    int y_end = y_inf + ((h - y_inf) / radius) * radius;

    for (y = 0; y < y_inf; y++) {
#pragma omp simd
        for (x = 0; x < w; x++) {
            int idx = y * w + x;
            Y[idx] = pixel_Y_lo_;
            if(U && V) { U[idx] = 128; V[idx] = 128; }
        }
    }

    for (y = y_end; y < h; y++) {
#pragma omp simd
        for (x = 0; x < w; x++) {
            int idx = y * w + x;
            Y[idx] = pixel_Y_lo_;
            if(U && V) { U[idx] = 128; V[idx] = 128; }
        }
    }
    
    for (y = y_inf; y < y_end; y++) {
#pragma omp simd
        for (x = 0; x < x_inf; x++) {
            int idx = y * w + x;
            Y[idx] = pixel_Y_lo_;
            if(U && V) { U[idx] = 128; V[idx] = 128; }
        }
    }

    for (y = y_inf; y < y_end; y++) {
#pragma omp simd
        for (x = x_end; x < w; x++) {
            int idx = y * w + x;
            Y[idx] = pixel_Y_lo_;
            if(U && V) { U[idx] = 128; V[idx] = 128; }
        }
    }
}

static void halftone_apply_avg_col2( VJFrame *frame, int radius, int orientation, int odd)
{
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];
    const int w = frame->width, h = frame->height;
    int x,y,x1,y1,x_inf=0,y_inf=0, x_sup=w, y_sup=h;

    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup,w,h);
    clear_margins(Y, U, V, w, h, x_inf, y_inf, radius, 1);

    for( y = y_inf; y < y_sup; y += radius ) {
        for( x = x_inf; x < x_sup; x += radius ) {
            uint32_t sum = 0, hit = 0;
            int32_t  sumU = 0, sumV = 0;

            for( y1 = y ; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x ; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    int idx = y1 * w + x1;
                    sum += Y[idx];
                    sumU += (U[idx] - 128);
                    sumV += (V[idx] - 128);
                    hit ++;
                    Y[idx] = pixel_Y_lo_; U[idx] = 128; V[idx] = 128;
                }
            }
            if (!hit) continue;
            uint32_t val = (sum / hit);
            int wrad = 1 + (int) ( ((double) val / 255.0  ) * (radius/2));
            veejay_draw_circle( Y , x,y, radius, radius, w, h, wrad, val );
            veejay_draw_circle( U , x,y, radius, radius, w, h, wrad, 128 + (sumU/hit) );
            veejay_draw_circle( V , x,y, radius, radius, w, h, wrad, 128 + (sumV/hit) );
        }
    }
}

static void halftone_apply_avg_col( VJFrame *frame, int radius, int orientation, int odd)
{
    uint8_t *Y = frame->data[0], *U = frame->data[1], *V = frame->data[2];
    const int w = frame->width, h = frame->height;
    int x,y,x1,y1,x_inf=0,y_inf=0, x_sup=w, y_sup=h;

    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup,w,h);
    clear_margins(Y, U, V, w, h, x_inf, y_inf, radius, 1);

    for( y = y_inf; y < y_sup; y += radius ) {
        for( x = x_inf; x < x_sup; x += radius ) {
            uint32_t sum = 0, hit = 0;
            uint8_t u = U[ y * w + x ], v = V[ y * w + x ];

            for( y1 = y ; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x ; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_; U[ y1 * w + x1 ] = 128; V[ y1 * w + x1 ] = 128;
                }
            }
            if (!hit) continue;
            int wrad = 1 + (int) ( ((double) (sum/hit) / 255.0  ) * (radius/2));
            veejay_draw_circle( Y , x,y, radius, radius, w, h, wrad, (sum/hit) );
            veejay_draw_circle( U , x,y, radius, radius, w, h, wrad, u );
            veejay_draw_circle( V , x,y, radius, radius, w, h, wrad, v );
        }
    }
}

static void halftone_apply_avg_gray( VJFrame *frame, int radius, int orientation, int odd)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x,y,x1,y1,x_inf=0,y_inf=0, x_sup=w, y_sup=h;

    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup,w,h);
    clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    for( y = y_inf; y < y_sup; y += radius ) {
        for( x = x_inf; x < x_sup; x += radius ) {
            uint32_t sum = 0, hit = 0;
            for( y1 = y ; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x ; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                }
            }
            if (!hit) continue;
            uint32_t val = (sum / hit);
            veejay_draw_circle( Y,x,y, radius, radius, w, h, 1 + (int)(((double)val/255.0)*(radius/2)), val );
        }
    }
    veejay_memset( frame->data[1], 128, w * h );
    veejay_memset( frame->data[2], 128, w * h );
}

static void halftone_apply_avg_black( VJFrame *frame, int radius, int orientation, int odd)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x,y,x1,y1,x_inf=0,y_inf=0, x_sup=w, y_sup=h;

    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup,w,h);
    
    veejay_memset(Y, pixel_Y_hi_, w * h);

    for( y = y_inf; y < y_sup; y += radius ) {
        for( x = x_inf; x < x_sup; x += radius ) {
            uint32_t sum = 0, hit = 0;
            for( y1 = y ; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x ; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                }
            }
            if (!hit) continue;
            int wrad = 1 + (int) ( ((double) (sum/hit) / 255.0  ) * (radius/2));
            veejay_draw_circle( Y,x,y, radius, radius, w, h, wrad, pixel_Y_lo_ );
        }
    }
    veejay_memset( frame->data[1], 128, w * h );
    veejay_memset( frame->data[2], 128, w * h );
}

static void halftone_apply_avg_white( VJFrame *frame, int radius, int orientation, int odd)
{
    uint8_t *Y = frame->data[0];
    const int w = frame->width, h = frame->height;
    int x,y,x1,y1,x_inf=0,y_inf=0, x_sup=w, y_sup=h;

    grid_getbounds_from_orientation(radius, orientation, odd, &x_inf, &y_inf, &x_sup, &y_sup,w, h);
    clear_margins(Y, NULL, NULL, w, h, x_inf, y_inf, radius, 0);

    for( y = y_inf; y < y_sup; y += radius ) {
        for( x = x_inf; x < x_sup; x += radius ) {
            uint32_t sum = 0, hit = 0;
            for( y1 = y ; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x ; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                    Y[ y1 * w + x1 ] = pixel_Y_lo_;
                }
            }
            if (!hit) continue;
            int wrad = 1 + (int) ( ((double) (sum/hit) / 255.0  ) * (radius/2));
            veejay_draw_circle( Y,x,y, radius, radius, w, h, wrad, pixel_Y_hi_ );
        }
    }
    veejay_memset( frame->data[1], 128, w * h );
    veejay_memset( frame->data[2], 128, w * h );
}

void halftone_apply( void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0], mode = args[1], orientation = args[2], parity = args[3];
    switch(mode) {
        case 0: halftone_apply_avg_white( frame, radius, (vj_effect_orientation)orientation, (vj_effect_parity)parity); break;
        case 1: halftone_apply_avg_black( frame, radius , (vj_effect_orientation)orientation, (vj_effect_parity)parity); break;
        case 2: halftone_apply_avg_gray( frame, radius , (vj_effect_orientation)orientation, (vj_effect_parity)parity); break;
        case 3: halftone_apply_avg_col( frame, radius , (vj_effect_orientation)orientation, (vj_effect_parity)parity); break;
        case 4: halftone_apply_avg_col2( frame, radius , (vj_effect_orientation)orientation, (vj_effect_parity)parity); break;
    }
}