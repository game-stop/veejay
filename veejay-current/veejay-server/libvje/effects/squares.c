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
#include "squares.h"


/*
 * simple effect that iterates over a frame using a bounding box
 * a new value will be determined (average of all pixels in the bounding box, the brightest or the darkest pixel)
 * and the bounding box will be filled with this new value
 */

vj_effect *squares_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 0;
    ve->defaults[2] = 0;
    ve->defaults[3] = 0;
    ve->description = "Squares";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Mode" , "Orientation", "Parity");

    ve->hints = vje_init_value_hint_list( ve->num_params );


    vje_build_value_hint_list( ve->hints, ve->limits[1][1],1, "Average", "Min", "Max" );
    vje_build_value_hint_list( ve->hints, ve->limits[1][2],2, "Centered", "North West");// , "North", "North East", "East", "South East" ...); // TODO
    vje_build_value_hint_list( ve->hints, ve->limits[1][3],3, "Even", "Odd", "No parity"); //TODO add 'Berzek?' parameter aka broken/random parity; very cool on Mode animation

    return ve;
}

static void squares_apply_max( VJFrame *frame, int radius, int orientation, int parity)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;

    int32_t sum = 0;
    uint8_t  val = 0;

    double v = 0;

    int x,y,x1,y1,x_inf,y_inf, x_sup, y_sup;

    x_inf = 0; // initial init for North East
    y_inf = 0;
    x_sup = w;
    y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup);

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {
            val = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = (y < 0) ? 0 : y ; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    if( Y[ y1 * w + x1 ] > val )
                        val = Y[ y1 * w + x1 ];
                }
            }
            for( y1 = (y < 0) ? 0 : y ; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            sum = 0;
            uint32_t hit = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;


            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }
}

static void squares_apply_min( VJFrame *frame, int radius, int orientation, int parity)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;
    
    int32_t sum = 0;
    uint8_t  val = 0;
    double v = 0;

    int x,y,x1,y1,x_inf,y_inf, x_sup, y_sup;

    x_inf = 0; // initial init for North East
    y_inf = 0;
    x_sup = w;
    y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup);

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {
            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            val = 0xff;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    if(Y[ y1 * w + x1 ] < val)
                        val = Y[ y1 * w + x1];
                }
            }
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }
    
    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            sum = 0;
            uint32_t hit = 0;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            sum = 0;
            uint32_t hit = 0;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

}

static void squares_apply_average( VJFrame *frame, int radius, int orientation, int parity)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;

    int32_t sum = 0;
    uint8_t val = 0;
    double v = 0;

    int x,y,x1,y1,x_inf,y_inf, x_sup, y_sup;

    x_inf = 0; // initial init for North East
    y_inf = 0;
    x_sup = w;
    y_sup = h;

    grid_getbounds_from_orientation(radius, orientation, parity, &x_inf, &y_inf, &x_sup, &y_sup);

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            sum = 0;
            uint32_t hit = 0;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            sum = 0;
            uint32_t hit = 0;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y =  y_inf ; y < h; y += radius ) {
        for( x =  x_inf ; x < w; x += radius ) {

            int lim_x = (x + radius);
            if( lim_x > w )
                lim_x = w;
            int lim_y = (y + radius);
            if( lim_y > h)
                lim_y = h;

            sum = 0;
            uint32_t hit = 0;
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = (y < 0) ? 0 : y; y1 < lim_y; y1 ++ ) {
                for( x1 = (x < 0) ? 0 : x; x1 < lim_x; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

}

void squares_apply( void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int mode = args[1];
    int orientation = args[2];
    int parity = args[3];

    switch(mode) {
        case 0:
            squares_apply_average( frame, radius, (vj_effect_orientation)orientation, (vj_effect_parity)parity );
            break;
        case 1:
            squares_apply_min( frame, radius, (vj_effect_orientation)orientation, (vj_effect_parity)parity );
            break;
        case 2:
            squares_apply_max( frame, radius, (vj_effect_orientation)orientation, (vj_effect_parity)parity );
            break;
    }

}
