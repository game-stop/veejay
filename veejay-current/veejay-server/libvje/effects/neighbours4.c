/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "neighbours4.h"

vj_effect *neighbours4_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = 32;	/* radius */
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 200;     /* distance from center */
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 255;	/* smoothness */
    ve->limits[0][3] = 0; 	/* luma only / include chroma */
    ve->limits[1][3] = 1;
    ve->defaults[0] = 4;
    ve->defaults[1] = 24;
    ve->defaults[2] = 8;
    ve->defaults[3] = 1;
    ve->description = "ZArtistic Filter (Round Brush)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Radius", "Distance from center","Smoothness", "Mode" );
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3, "Luma Only", "Luma and Chroma" );


    return ve;
}

typedef struct
{
	double x;
	double y;
} relpoint_t;

typedef struct {
	int pixel_histogram[256];
	int y_map[256];
    int cb_map[256];
    int cr_map[256];
    uint8_t *tmp_buf[2];
    uint8_t *chromacity[2];
    relpoint_t points[2048];
} nb_t;

typedef struct
{
	uint8_t y;
	uint8_t u;
	uint8_t v;
} pixel_t;

void *neighbours4_malloc(int w, int h )
{
    nb_t *n = (nb_t*) vj_calloc(sizeof(nb_t));
    if(!n) {
        return NULL;
    }

	n->tmp_buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h * 2));
	if(!n->tmp_buf[0] ) {
        free(n);
        return NULL;
    }

	n->tmp_buf[1] = n->tmp_buf[0] + (w*h);

	n->chromacity[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w * h *2));
	if(!n->chromacity[0]) {
        free(n->tmp_buf[0]);
        free(n);
        return NULL;
    }
	
    n->chromacity[1] = n->chromacity[0] + (w*h);
	return (void*) n;
}

void neighbours4_free(void *ptr)
{
    nb_t *n = (nb_t*) ptr;
	free(n->tmp_buf[0]);
	free(n->chromacity[0]);
    free(n);
}

static 	void create_circle( double radius, int depth, int w,relpoint_t *points )
{
	double t = 0.0;
	int index = 0;
	double theta = ((double)w/depth);
	while( index <= depth)
	{
		double r = (t / 180.0) * M_PI;
		points[index].x = a_cos(r) * radius;
		points[index].y = a_sin(r) * radius;
		t += theta;
		index++;
	}
}

static inline pixel_t evaluate_pixel_bc(
		int x, int y,			/* center pixel */
		const int brush_size,		/* brush size (works like equal sized rectangle) */
		const double intensity,		/* Luma value * scaling factor */
		const int w,			/* width of image */
		const int h,			/* height of image */
		const uint8_t *premul,		/* map data */
		const uint8_t *image,		/* image data */
		const uint8_t *image_cb,
		const uint8_t *image_cr,
		const relpoint_t *pts,	/* relative coordinate map */
        int *pixel_histogram,
        int *y_map,
        int *cb_map,
        int *cr_map
)
{
	unsigned int 	brightness;		/* scaled brightnes */
	int 		peak_value = 0;
	int 		peak_index = 0;
	unsigned int		i;
	const int 	max_ = (int) ( 0xff * intensity );
	int		dx,dy;

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
           many loads and stores. */
	i = y;

	/* loop over perimter of circle (!) */
	for( i = 0; i < brush_size; i ++)
	{
		dx = pts[i].x + x;
		dy = pts[i].y + y;
		if(dx < 0) dx = 0; else if (dx >= w) dx = w-1;
		if(dy < 0) dy = 0; else if (dy >= h) dy = h-1;

		brightness = premul[ dy * w + dx];
		pixel_histogram[ brightness ] ++;

		y_map[ brightness ] += image[ dy * w + dx];
		cb_map[ brightness ] += image_cb[ dy * w + dx ];
		cr_map[ brightness ] += image_cr[ dy * w + dx ];
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
		val.y = image[y * w + x];
		val.u = image_cb[y  * w + x];
		val.v = image_cr[y * w + x];
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
		const uint8_t *image,		/* image data */
		const relpoint_t *pts,	/* relative coordinate map*/
        int *pixel_histogram,
        int *y_map
)
{
	unsigned int 	brightness;		/* scaled brightnes */
	int 		peak_value = 0;
	int 		peak_index = 0;
	unsigned int i;
	int 		x0 = x - brush_size;
	int 		x1 = x + brush_size;
	int 		y0 = y - brush_size;
	int 		y1 = y + brush_size;
	const int 	max_ = (int) ( 0xff * intensity );
	int		dx,dy;
	if( x0 < 0 ) x0 = 0;			
	if( x1 > w ) x1 = w;
	if( y0 < 0 ) y0 = 0;
	if( y1 >= h ) y1 = h-1;

	/* clear histogram and y_map */
	for( i =0 ; i < max_; i ++ )
	{
		pixel_histogram[i] = 0;
		y_map[i]  = 0;
	}

	// points in circle
	for( i = 0; i < brush_size; i ++)
	{
		dx = pts[i].x + x;
		dy = pts[i].y + y;
		if(dx < 0) dx = 0; else if (dx > w) dx = w;
		if(dy < 0) dy = 0; else if (dy >= h) dy = h-1;

		brightness = premul[ dy * w + dx];
		pixel_histogram[ brightness ] ++;
		y_map[ brightness ] += image[ dy * w + dx];
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

void neighbours4_apply( void *ptr, VJFrame *frame, int *args ) {
    int radius = args[0];
    int brush_size = args[1];
    int intensity_level = args[2];
    int mode = args[3];

    nb_t *n = (nb_t*) ptr;

	int x,y; 
	const double intensity = intensity_level / 255.0;
	uint8_t *Y = n->tmp_buf[0];
	uint8_t *Y2 = n->tmp_buf[1];

	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;
	uint8_t *dstY = frame->data[0];
	uint8_t *dstCb = frame->data[1];
	uint8_t *dstCr = frame->data[2];
	double	r      = (double)radius;
	// keep luma
	vj_frame_copy1( frame->data[0], Y2, len );
	create_circle( r, brush_size,width, n->points ); 

	relpoint_t *p_points = &(n->points[0]);

	if(mode)
	{
		int strides[4] = { 0,len, len };
		uint8_t *dest[3] = { NULL, n->chromacity[0], n->chromacity[1] };
		vj_frame_copy( frame->data, dest, strides );
	}


	// premultiply intensity map
#pragma omp simd
	for( y = 0 ; y < len ; y ++ )
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
						Y2,
						p_points,
                        n->pixel_histogram,
                        n->y_map
				);
			}
		}
		veejay_memset( frame->data[1], 128, len );
		veejay_memset( frame->data[2], 128, len );
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
						n->chromacity[0],
						n->chromacity[1],
						p_points,
                        n->pixel_histogram,
                        n->y_map,
                        n->cb_map,
                        n->cr_map
					);
				*(dstY++) = tmp.y;
				*(dstCb++) = tmp.u;
				*(dstCr++) = tmp.v;
			}
		}
	}

}
