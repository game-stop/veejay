/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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
#include <config.h>
#include <stdlib.h>
#include "chromium.h"
#include "common.h"

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

vj_effect *chromium_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = w;
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 360;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = w;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = h;
    ve->defaults[0] = w;
    ve->defaults[1] = 250;
    ve->defaults[2] = w / 2;
    ve->defaults[3] = h / 2;
    ve->description = "Polar Coordinates";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data= 0;
    return ve;
}

static double *chromium_map;
static double *angle_map;
static int    *cache_map;
static double *uv_chromium_map;
static double *uv_angle_map;
static unsigned int _v = 0;

static uint8_t *chromium_buf[3];

int chromium_malloc(int w, int h)
{
	int x,y,p;
        const int W = (w / 2);
        const int H = (h / 2);
        const int uW = W / 2;
        const int uH = H / 2;

	chromium_map = (double*) vj_malloc (sizeof(double) * w * h );
	if(!chromium_map) return 0;
	angle_map = (double*) vj_malloc (sizeof(double) * w * h );
	if(!angle_map) return 0;
	uv_chromium_map = (double*) vj_malloc (sizeof(double) * ((w * h)/4) );
	if(!chromium_map) return 0;
	uv_angle_map = (double*) vj_malloc (sizeof(double) * ((w * h)/4) );
	if(!angle_map) return 0;

	chromium_buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h );
	if(!chromium_buf[0]) return 0;
	chromium_buf[1] = (uint8_t*) vj_malloc( sizeof(uint8_t) * ((w*h)/4));
	if(!chromium_buf[1]) return 0;
	chromium_buf[2] = (uint8_t*) vj_malloc( sizeof(uint8_t) * ((w*h)/4));
	if(!chromium_buf[2]) return 0;

	cache_map = (int*) vj_malloc (sizeof(uint8_t) * w * h );
	if(!cache_map) return 0;

	for(y = (-1 * H); y < (h-H); y ++ )
	{
		for(x=(-1 * W); x < (w-W); x ++)
		{
			double r;
			fast_sqrt( r , (double) (y * y + x * x) );
			p = ( H + y ) * w + ( W + x );
			chromium_map[ p ] = r;
			angle_map[ p ] = atan2( (float) y, x );	
		}
	}
	
	h = h / 2;
	w = w / 2;

	for(y = uH; y < (h-uH); y ++ )
	{
		for(x=0; x < (w-uW); x ++)
		{
			double r;
			fast_sqrt( r , (double) (y * y + x * x) );
			uv_chromium_map[ y * w + x ] = r;
			uv_angle_map[ y * w + x ] = atan2( (float) y, x );	
		}
	}
	_v = 0;

	return 1;
}

void chromium_free()
{
	if(chromium_map) free(chromium_map);
	if(angle_map) free(angle_map);
	if(uv_chromium_map) free(uv_chromium_map);
	if(uv_angle_map) free(uv_angle_map);
	if(chromium_buf[0]) free(chromium_buf[0]);
	if(chromium_buf[1]) free(chromium_buf[1]);
	if(chromium_buf[2]) free(chromium_buf[2]);
	chromium_map = NULL;
	angle_map = NULL;
	uv_chromium_map = NULL;
	uv_angle_map = NULL;
	chromium_buf[0] = NULL;
	chromium_buf[1] = NULL;
	chromium_buf[2] = NULL;
}

void chromium_apply(uint8_t *yuv[3], int width, int height, int radius, int yy, int x_offset, int y_offset)
{
	const int len = width * height;
	unsigned int i;
    const int half_width = x_offset;
	const int half_height = y_offset;

    veejay_memcpy( chromium_buf[0], yuv[0], (width*height));
   // veejay_memcpy( chromium_buf[1], yuv[1], (width*height)/4);
   // veejay_memcpy( chromium_buf[2], yuv[2], (width*height)/4);

//	if ( yy != _v )
 //  	{
		const unsigned int R = radius;
		const double coeef = yy;
	    double _sin, _cos, r;
        int dx,dy;
		for(i = 0; i < len ; i++)
		{
			cache_map[i] = -1;
			if ( chromium_map[i] <= R )
			{
				sin_cos( _cos, _sin,  angle_map[i]  );				
				dx = (int) ( chromium_map[i] * _cos );
				dy = (int) ( chromium_map[i] * _sin );
				dx += half_width; //center
				dy += half_height;
			    if( dx < 0 ) dx = 0; else if ( dx > width ) dx = width;
				if( dy < 0 ) dy = 0; else if ( dy >= height ) dy = height-1; 
				cache_map[i] = dy * width + dx;
			}
		}
		_v = yy;
//	}
	
	for( i = 0 ; i < len ; i ++ )
	{
		if(cache_map[i] == -1) 
		{
			yuv[0][i] = 16;
		}
		else
		{
			yuv[0][i] = chromium_buf[0][ (cache_map[i]) ];
		}

	}
    memset( yuv[1], 128, len/4);
    memset( yuv[2], 128, len/4);
	 
}
