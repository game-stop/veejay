/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nelburg@looze.net>
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
#include <stdlib.h>
#include <math.h>
#include "neighbours5.h"

vj_effect *neighbours5_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_malloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;	/* line size */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 255;     /* smoothness */
    ve->limits[0][2] = 0; 	/* luma only / include chroma */
    ve->limits[1][2] = 1;
    ve->defaults[0] = 6;
    ve->defaults[1] = 5;
    ve->defaults[2] = 1;
    ve->description = "Artistic Filter (Vertical strokes)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    return ve;
}

static	int pixel_histogram[256];
static	int y_map[256];
static  int cb_map[256];
static int cr_map[256];
static uint8_t  *tmp_buf[2];
static uint8_t  *chromacity[2];


int		neighbours5_malloc(int w, int h )
{
	tmp_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	if(!tmp_buf[0] ) return 0;
	tmp_buf[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	chromacity[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	chromacity[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
	return 1;
}

int		neighbours5_free(void)
{
	if(tmp_buf[0]) free(tmp_buf[0]);
	if(tmp_buf[1]) free(tmp_buf[1]);
	if(chromacity[0]) free(chromacity[0]);
	if(chromacity[1]) free(chromacity[1]);
	return 1;
}

typedef struct
{
	uint8_t y;
	uint8_t u;
	uint8_t v;
} pixel_t;

static inline pixel_t evaluate_pixel_bc(
		int x, int y,			/* center pixel */
		const int brush_size,		/* brush size (works like equal sized rectangle) */
		const double intensity,		/* Luma value * scaling factor */
		const int w,			/* width of image */
		const int h,			/* height of image */
		const uint8_t *premul,		/* map data */
		const uint8_t *image,		/* image data */
		const uint8_t *image_cb,
		const uint8_t *image_cr
)
{
	unsigned int 	brightness;		/* scaled brightnes */
	int 		peak_value = 0;
	int 		peak_index = 0;
	int		i,j;
	int 		x0 = x - brush_size;
	int 		x1 = x + brush_size;
	int 		y0 = y - brush_size;
	int 		y1 = y + brush_size;
	const int 	max_ = (int) ( 0xff * intensity );

	if( y0 < 0 ) y0 = 0; 
	if( y1 >= h ) y1 = h-1;
	if( x0 < 0 ) x0 = 0;
	if( x1 > w ) x1 = w; 

	/* clear histogram and y_map */
	for( i =0 ; i < max_; i ++ )
	{
		pixel_histogram[i] = 0;
		y_map[i]  = 0;
		cb_map[i] = 0;
		cr_map[i] = 0;
	}

	/* fill histogram, cummulative add of luma values */
	/* this innerloop is executed w * h * brush_size and counts
           many loads and stores. 
	   vertical passing
         */
	for( j = y0; j < y1; j ++ )
	{
		brightness = premul[ j * w + x];
		pixel_histogram[ brightness ] ++;
		y_map[ brightness ] += (image[ j * w + x]);
		cb_map[ brightness ] += (image_cb[ j * w + x ]);
		cr_map[ brightness ] += (image_cr[ j * w + x ]);
	}
	

	/* find most occuring value */
	for( i = 0; i < max_ ; i ++ )
	{
		if( pixel_histogram[i] >= peak_value )
		{
			peak_value = pixel_histogram[i];
			peak_index = i;
		}
	}
	pixel_t val;

	if(peak_value > 0)
	{
		val.y = y_map[peak_index] / peak_value;
		val.u = cb_map[peak_index] / peak_value;
		val.v = cr_map[peak_index] / peak_value;
	}
	else
	{
		val.y = image[ y * w + x];
		val.u = image_cb[ y * w + x];
		val.v = image_cr[ y * w + x];
	}
	return val;	

}


static inline uint8_t evaluate_pixel_b(
		int x, int y,			/* center pixel */
		const int brush_size,		/* brush size (works like equal sized rectangle) */
		const double intensity,		/* Luma value * scaling factor */
		const int w,			/* width of image */
		const int h,			/* height of image */
		const uint8_t *premul,		/* map data */
		const uint8_t *image		/* image data */
)
{
	unsigned int 	brightness;		/* scaled brightnes */
	int 		peak_value = 0;
	int 		peak_index = 0;
	int		i,j;
	int 		x0 = x - brush_size;
	int 		x1 = x + brush_size;
	int 		y0 = y - brush_size;
	int 		y1 = y + brush_size;
	const int 	max_ = (int) ( 0xff * intensity );

	if( y0 < 0 ) y0 = 0;
	if( y1 >= h ) y1 = h - 1;
	if( x0 < 0 ) x0 = 0;
	if( x1 > w ) x1 = w;

	/* clear histogram and y_map */
	for( i =0 ; i < max_; i ++ )
	{
		pixel_histogram[i] = 0;
		y_map[i]  = 0;
	}

	for( j = y0; j < y1; j ++ )
	{
		// average while adding
		brightness = premul[ j * w + x];
		pixel_histogram[ brightness ] ++;
		y_map[ brightness ] += image[ j * w + x];
	}

	/* find most occuring value */
	for( i = 0; i < max_ ; i ++ )
	{
		if( pixel_histogram[i] >= peak_value )
		{
			peak_value = pixel_histogram[i];
			peak_index = i;
		}
	}
	if( peak_value < 16)
		return image[ y * w + x];

	return( (uint8_t) (  y_map[ peak_index] / peak_value ));
}

void neighbours5_apply( VJFrame *frame, int width, int height, int brush_size, int intensity_level, int mode )
{
	int x,y; 
	const double intensity = intensity_level / 255.0;
	uint8_t *Y = tmp_buf[0];
	uint8_t *Y2 = tmp_buf[1];
	uint8_t *dstY = frame->data[0];
	uint8_t *dstCb = frame->data[1];
	uint8_t *dstCr = frame->data[2];
	// keep luma
	veejay_memcpy( Y2, frame->data[0], frame->len );

	if(mode)
	{
		veejay_memcpy( chromacity[0], frame->data[1], frame->len );
		veejay_memcpy( chromacity[1], frame->data[2], frame->len );
	}

	// premultiply intensity map
	for( y = 0 ; y < frame->len ; y ++ )
		Y[y] = (uint8_t) ( (double)Y2[y] * intensity );

	if(!mode)
	{
		for( y = 0; y < height; y ++ )
		{
			for( x = 0; x < width; x ++ )
			{
				*(dstY)++ = evaluate_pixel_b(
						x,y,
						brush_size,
						intensity,
						width,
						height,
						Y,
						Y2
				);
			}
		}
		memset( frame->data[1], 128, frame->len );
		memset( frame->data[2], 128, frame->len );
	} 
	else
	{
		pixel_t tmp;
		for( y = 0; y < height; y ++ )
		{
			for( x = 0; x < width; x ++ )
			{
				tmp = evaluate_pixel_bc(
						x,y,
						brush_size,
						intensity,
						width,
						height,
						Y,
						Y2,
						chromacity[0],
						chromacity[1]
					);
				*(dstY++) = tmp.y;
				*(dstCb++) = tmp.u;
				*(dstCr++) = tmp.v;
			}
		}
	}
}
