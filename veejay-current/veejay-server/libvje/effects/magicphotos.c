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
#include "magicphotos.h"

vj_effect *photoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2; // divider
    ve->limits[1][0] = max_power(w);
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1; // waterfall
	ve->limits[0][2] = 0;
	ve->limits[1][2] = get_matrix_func_n(); // mode
    ve->defaults[0] = 2;
	ve->defaults[1] = 0;
	ve->defaults[2] = 1;  
    ve->description = "Photoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Photos", "Waterfall", "Mode" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list (ve->hints, ve->limits[1][2],2,
	                           "Random",								//0
	                           "TopLeft to BottomRight : Horizontal",	//1
	                           "TopLeft to BottomRight : Vertical",		//2
	                           "BottomRight to TopLeft : Horizontal",	//3
	                           "BottomRight to TopLeft : Vertical",		//4
	                           "BottomLeft to TopRight : Horizontal",	//5
	                           "TopRight to BottomLeft : Vertical",		//6
	                           "TopRight to BottomLeft : Horizontal",	//7
	                           "BottomLeft to TopRight : Vertical");	//8


    return ve;
}

typedef struct {
    picture_t **photo_list;
    int num_photos;
    int frame_counter;
} photoplay_t;

static	int prepare_filmstrip(photoplay_t *p, int film_length, int w, int h)
{
	int i,j;
	int picture_width = w / sqrt(film_length);
	int picture_height = h / sqrt(film_length);

	p->photo_list = (picture_t**) vj_malloc(sizeof(picture_t*) * (film_length + 1) );
	if(!p->photo_list)
		return 0;

	p->num_photos = film_length;

	uint8_t val = 0;
	int inc = num_photos % 255;

	for ( i = 0; i < p->num_photos; i ++ )
	{
		p->photo_list[i] = vj_calloc(sizeof(picture_t));
		if(!p->photo_list[i])
			return 0;
		p->photo_list[i]->w = picture_width;
		p->photo_list[i]->h = picture_height;
		for( j = 0; j < 3; j ++ )
		{
			p->photo_list[i]->data[j] = vj_malloc(sizeof(uint8_t) * picture_width * picture_height );
			if(!p->photo_list[i]->data[j])
				return 0;
			memset(p->photo_list[i]->data[j], (j==0 ? val : 128), picture_width *picture_height );
		}
		val+= inc;
	}
	frame_counter = 0;

	return 1;
}

static void destroy_filmstrip(photoplay_t *p)
{
	if(p->photo_list)
	{
		int i = 0;
		while(i < p->num_photos)
		{
			if( p->photo_list[i] )
			{
				int j;
				for( j = 0; j < 3; j ++ ) {
					if(p->photo_list[i]->data[j]) {
					    free(p->photo_list[i]->data[j]);
                        p->photo_list[i]->data[j] = NULL;
                    }
				    free(p->photo_list[i]);
                    p->photo_list[i] = NULL;
                }
			}
			i++;
		}
		free(p->photo_list);
        p->photo_list = NULL;
	}
}



void *photoplay_malloc(int w, int h )
{
    photoplay_t *p = (photoplay_t*) vj_calloc( sizeof(photoplay_t) );
    if(!p) {
        return NULL;
    }
    return p;
}


void	photoplay_free(void *ptr)
{
    photoplay_t *p = (photoplay_t*) ptr;
	destroy_filmstrip(p);
    free(p);
}

static void	take_photo(photoplay_t *p, uint8_t *plane, uint8_t *dst_plane, int w, int h, int index )
{

	int x,y,dx,dy;
	int sum;
	int dst_x, dst_y;
	int step_y;
	int step_x;
	int box_width = p->photo_list[index]->w;
	int box_height = p->photo_list[index]->h;

	step_x = w / box_width;
	step_y = h / box_height;

	for( y = 0 ,dst_y = 0; y < h && dst_y < box_height; y += step_y )
	{
		for( x = 0, dst_x = 0; x < w && dst_x < box_width; x+= step_x )
		{
			sum = 0;
			for( dy = 0; dy < step_y; dy ++ )
			{
				for( dx = 0; dx < step_x; dx++)	
				{
					sum += plane[ ((y+dy)*w+(dx+x)) ];	
				}
			}
			// still problem here!
			if(sum > 0)
			  dst_plane[(dst_y*box_width)+dst_x] = sum / (step_y*step_x);
			else
			  dst_plane[(dst_y*box_width)+dst_x] = 16;

			dst_x++;
		}
		dst_y++;
	}
}

static void put_photo( photoplay_t *p, uint8_t *dst_plane, uint8_t *photo, int dst_w, int dst_h, int index , matrix_t matrix)
{
	int box_w = p->photo_list[index]->w;
	int box_h = p->photo_list[index]->h;
	int x,y;

	uint8_t *P = dst_plane + (matrix.h*dst_w);
	int	offset = matrix.w;

	for( y = 0; y < box_h; y ++ )
	{
		for( x = 0; x < box_w; x ++ )
		{
			*(P+offset+x) = photo[(y*box_w)+x];
		}
		P += dst_w;
	}
}

void photoplay_apply( void *ptr, VJFrame *frame, int *args ) {
    int size = args[0];
    int delay = args[1];
    int mode = args[2]

	unsigned int i;
	uint8_t *dstY = frame->data[0];
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];
    
    int width = frame->width;
    int height = frame->height;


	matrix_f matrix_placement = get_matrix_func(mode);

	if( (size*size) != p->num_photos || p->num_photos == 0 )
	{
		destroy_filmstrip(p);
		if(!prepare_filmstrip(p, size*size, width,height))
		{
            destroy_filmstrip(p);
			return;
		}
	}

	if(p->frame_counter < p->num_photos)
	{
		for( i = 0; i < 3 ; i ++ )
			veejay_memset( frame->data[i], (i==0?16:128), (width*height));
		p->frame_counter ++;
		return;
	}

	for( i = 0; i < 3; i ++ )
	{
		take_photo(p, frame->data[i], photo_list[(frame_counter%num_photos)]->data[i], width, height , frame_counter % num_photos);
	}

	for ( i = 0; i < num_photos; i ++ )
	{
		matrix_t m = matrix_placement(i, size,width,height );
		put_photo( p, dstY, photo_list[i]->data[0],width,height,i, m);
		put_photo( p, dstU, photo_list[i]->data[1],width,height,i, m);
		put_photo( p, dstV, photo_list[i]->data[2],width,height,i, m);
	}

	p->frame_counter ++;
}

