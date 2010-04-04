
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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "videoplay.h"
#include "common.h"

vj_effect *videoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 2; // divider
    ve->limits[1][0] = max_power(w);
    ve->limits[0][1] = 1;
    ve->limits[1][1] = 250; // waterfall
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 3; // mode
    ve->defaults[0] = 2;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1;  
    ve->description = "Videoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Photos", "Waterfall", "Mode");
    return ve;
}

static picture_t **video_list = NULL;
static int	   num_videos = 0;
static int	  frame_counter = 0;
static int	  frame_delay = 0;

static	int prepare_filmstrip(int film_length, int w, int h)
{
	int i,j;
	int picture_width = w / sqrt(film_length);
	int picture_height = h / sqrt(film_length);

	video_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1) );
	if(!video_list)
		return 0;

	num_videos = film_length;

	uint8_t val = 0;
//	int inc = num_videos % 255;

	for ( i = 0; i < num_videos; i ++ )
	{
		video_list[i] = vj_calloc(sizeof(picture_t));
		if(!video_list[i])
			return 0;
		video_list[i]->w = picture_width;
		video_list[i]->h = picture_height;
		for( j = 0; j < 3; j ++ )
		{
			video_list[i]->data[j] = vj_malloc(sizeof(uint8_t) * picture_width * picture_height );
			if(!video_list[i]->data[j])
				return 0;
			veejay_memset(video_list[i]->data[j], (j==0 ? pixel_Y_lo_ : 128), picture_width *picture_height );
		}
	//	val+= inc;
	}
	frame_counter = 0;

	return 1;
}

static void destroy_filmstrip(void)
{
	if(video_list)
	{
		int i = 0;
		while(i < num_videos)
		{
			if( video_list[i] )
			{
				int j;
				for( j = 0; j < 3; j ++ )
					if(video_list[i]->data[j]) 
					 free(video_list[i]->data[j]);
				free(video_list[i]);
			}
			i++;
		}
		free(video_list);
	}
	video_list = NULL;
	num_videos = 0;
	frame_counter = 0;
}



int	videoplay_malloc(int w, int h )
{
	num_videos = 0;
	return 1;
}


void	videoplay_free(void)
{
	destroy_filmstrip();
}

static void	take_video( uint8_t *plane, uint8_t *dst_plane, int w, int h, int index )
{

	int x,y,dx,dy;
	int sum;
	int dst_x, dst_y;
	int step_y;
	int step_x;
	int box_width = video_list[index]->w;
	int box_height = video_list[index]->h;

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
			  dst_plane[(dst_y*box_width)+dst_x] = pixel_Y_lo_;

			dst_x++;
		}
		dst_y++;
	}
}

static void put_video( uint8_t *dst_plane, uint8_t *video, int dst_w, int dst_h, int index , matrix_t matrix)
{
	int box_w = video_list[index]->w;
	int box_h = video_list[index]->h;
	int x,y;

	uint8_t *P = dst_plane + (matrix.h*dst_w);
	int	offset = matrix.w;

	for( y = 0; y < box_h; y ++ )
	{
		for( x = 0; x < box_w; x ++ )
		{
			*(P+offset+x) = video[(y*box_w)+x];
		}
		P += dst_w;
	}
}

void videoplay_apply( VJFrame *frame, VJFrame *B, int width, int height, int size, int delay, int mode )
{
	unsigned int i;
	uint8_t *dstY = frame->data[0];
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];

	matrix_f matrix_placement = get_matrix_func(mode);

	if( (size*size) != num_videos || num_videos == 0 )
	{
		destroy_filmstrip();
		if(!prepare_filmstrip(size*size, width,height))
		{
			return;
		}
		frame_delay = delay;
	}

	if( frame_delay )
		frame_delay --;	

	if( frame_delay == 0)
	{	
		frame_delay = delay;
	}

	
	if(frame_delay == delay)
	{
		for( i = 0; i < 3; i ++ )
		{
			take_video( B->data[i], video_list[(frame_counter%num_videos)]->data[i],
				width, height , frame_counter % num_videos);
		}
		for( i = 0; i < 3; i ++ )
		{
			take_video( frame->data[i], video_list[((frame_counter+1)%num_videos)]->data[i],
			width, height , (frame_counter+1) % num_videos);
		}
	}
	else
	{
		int n = frame_counter - 1;
		if(n>=0)
		{	
			for( i = 0; i < 3; i ++ )
			{
				take_video( frame->data[i], video_list[(n%num_videos)]->data[i],
					width, height , frame_counter % num_videos);
			}
			n++;
			for( i = 0; i < 3; i ++ )
			{
				take_video( B->data[i], video_list[(n%num_videos)]->data[i],
				width, height , (frame_counter+1) % num_videos);
			}
		}
	}
	
	for ( i = 0; i < num_videos; i ++ )
	{
		matrix_t m = matrix_placement(i, size,width,height );
		put_video( dstY, video_list[i]->data[0],width,height,i, m);
		put_video( dstU, video_list[i]->data[1],width,height,i, m);
		put_video( dstV, video_list[i]->data[2],width,height,i, m);
	}

	if( frame_delay == delay)
		frame_counter+=2;

}

