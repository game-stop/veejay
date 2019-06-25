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
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = ( w > h ? w / 2 : h / 2 );
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;
    ve->defaults[0] = ( w > h ? w / 64 : h / 64 );
    ve->defaults[1] = 0;
    ve->description = "Squares";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
	ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Mode" );

    ve->hints = vje_init_value_hint_list( ve->num_params );


    vje_build_value_hint_list( ve->hints, ve->limits[1][1],1, "Average", "Min", "Max" );

    return ve;
}

int  squares_malloc(int w, int h) 
{
    return 1;
}

void squares_free() 
{
}

static void squares_apply_max( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;
    int x,y;
    int x1,y1;

    int32_t sum = 0;
    uint8_t  val = 0;

    double v = 0;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            val = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    if( Y[ y1 * w + x1 ] > val )
                        val = Y[ y1 * w + x1 ];
                }
            }
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }
    
    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }
}

static void squares_apply_min( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;
    
    int x,y;
    int x1,y1;
    int32_t sum = 0;
    uint8_t  val = 0;
    double v = 0;

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            val = 0xff;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    if(Y[ y1 * w + x1 ] < val)
                        val = Y[ y1 * w + x1];
                }
            }
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }
    
    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

}

static void squares_apply_average( VJFrame *frame, int radius)
{
    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];
    int w = frame->width;
    int h = frame->height;

    int x,y,x1,y1;
    int32_t sum = 0;
    uint8_t val = 0;
    double v = 0;
  
    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += Y[ y1 * w + x1 ]; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    Y[y1 * w + x1] = val;
                }
            }
        }
    }

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += U[ y1 * w + x1 ]-128; 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    U[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

    for( y = 0; y < h; y += radius ) {
        for( x = 0; x < w; x += radius ) {
            sum = 0;
            uint32_t hit = 0;
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    sum += (V[ y1 * w + x1 ]-128); 
                    hit ++;
                }
            }
            v = 1.0 / (double) hit;
            val = (sum * v);
            for( y1 = y; y1 < (y + radius) && y1 < h; y1 ++ ) {
                for( x1 = x; x1 < (x + radius) && x1 < w; x1 ++ ) {
                    V[y1 * w + x1] = 128 + val;
                }
            }
        }
    }

}

void squares_apply( VJFrame *frame, int radius, int mode)
{
    switch(mode) {
        case 0:
            squares_apply_average( frame, radius );
            break;
        case 1:
            squares_apply_min( frame, radius );
            break;
        case 2:
            squares_apply_max( frame, radius );
            break;
    }

}
