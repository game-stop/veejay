
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
#include "videoplay.h"

typedef struct {
    picture_t **video_list;
    int	num_videos;
    int	frame_counter;
    int	frame_delay;
    int	*rt;
    int last_mode;
} videowall_t;

#define DEFAULT_NUM_PHOTOS 2

static void destroy_filmstrip(videowall_t *vw);

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
    ve->limits[1][2] = 1 + get_matrix_func_n(); // mode
    ve->defaults[0] = DEFAULT_NUM_PHOTOS;
    ve->defaults[1] = 1;
    ve->defaults[2] = 2;  
    ve->description = "Videoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Photos", "Waterfall", "Mode");
	
	ve->hints = vje_init_value_hint_list (ve->num_params);
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

static void *prepare_filmstrip(videowall_t *vw, int film_length, int w, int h)
{
	int i,j;
	int picture_width = w / sqrt(film_length);
	int picture_height = h / sqrt(film_length);

	vw->video_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1) );
	if(!vw->video_list) {
        destroy_filmstrip(vw);
		return NULL;
    }
	vw->rt = (int*) vj_calloc(sizeof(int) * film_length );
	if(!vw->rt) {
       destroy_filmstrip(vw); 
	   return NULL;
    }

	vw->num_videos = film_length;

	for ( i = 0; i < film_length; i ++ )
	{
		vw->video_list[i] = vj_calloc(sizeof(picture_t));
		if(!vw->video_list[i]) {
            destroy_filmstrip(vw);
			return NULL;
        }
		vw->video_list[i]->w = picture_width;
		vw->video_list[i]->h = picture_height;
		for( j = 0; j < 3; j ++ )
		{
			vw->video_list[i]->data[j] = vj_malloc(sizeof(uint8_t) * picture_width * picture_height );
			if(!vw->video_list[i]->data[j]) {
                destroy_filmstrip(vw);
				return NULL;
            }
			veejay_memset(vw->video_list[i]->data[j], (j==0 ? pixel_Y_lo_ : 128), picture_width *picture_height );
		}
	}
	vw->frame_counter = 0;
    vw->last_mode = -1;

	return (void*) vw;
}

static void destroy_filmstrip(videowall_t *vw)
{
	if(vw->video_list)
	{
		int i = 0;
		while(i < vw->num_videos)
		{
			if( vw->video_list[i] )
			{
				int j;
				for( j = 0; j < 3; j ++ )
					if(vw->video_list[i]->data[j]) 
					 free(vw->video_list[i]->data[j]);
				free(vw->video_list[i]);
			}
			i++;
		}
		free(vw->video_list);
	}
	if( vw->rt ) 
		free(vw->rt);

    vw->video_list = NULL;
    vw->num_videos = 0;
    vw->rt = NULL;
    vw->last_mode = -1;
    vw->frame_counter = 0;
    vw->frame_delay = 0;
}



void *videoplay_malloc(int w, int h )
{
    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    return prepare_filmstrip(vw,DEFAULT_NUM_PHOTOS, w,h);
}

void	videoplay_free(void *ptr)
{
	destroy_filmstrip(ptr);

    free(ptr);
}

static void	take_video( videowall_t *vw, uint8_t *plane, uint8_t *dst_plane, int w, int h, int index )
{

	int x,y,dx,dy;
	int sum;
	int dst_x, dst_y;
	int step_y;
	int step_x;
	int box_width = vw->video_list[index]->w;
	int box_height = vw->video_list[index]->h;

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

static void put_video( videowall_t *vw, uint8_t *dst_plane, uint8_t *video, int dst_w, int dst_h, int index , matrix_t matrix)
{
	int box_w = vw->video_list[index]->w;
	int box_h = vw->video_list[index]->h;
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

void videoplay_apply( void *ptr, VJFrame *frame, VJFrame *B, int *args)
{
	unsigned int i;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	uint8_t *dstY = frame->data[0];
	uint8_t *dstU = frame->data[1];
	uint8_t *dstV = frame->data[2];
    videowall_t *vw = (videowall_t*) ptr;
    int size = args[0];
    int delay = args[1];
    int mode = args[2];

	if( (size*size) != vw->num_videos || vw->num_videos == 0 )
	{
		destroy_filmstrip(vw);
		if(!prepare_filmstrip(vw, size*size, width,height))
		{
			return;
		}
		vw->frame_delay = delay;
		
		for( i = 0; i < vw->num_videos; i ++ )
			vw->rt[i] = i;

		if( mode == 0)
			fx_shuffle_int_array( vw->rt, vw->num_videos );
	}

	if( vw->last_mode != mode )
	{
		for( i = 0; i < vw->num_videos; i ++ )
			vw->rt[i] = i;

		if (mode == 0)
			fx_shuffle_int_array( vw->rt, vw->num_videos );

	}

	vw->last_mode = mode;

	if( vw->frame_delay )
		vw->frame_delay --;	

	if( vw->frame_delay == 0)
	{	
		vw->frame_delay = delay;
	}

	
	if(vw->frame_delay == delay)
	{
		for( i = 0; i < 3; i ++ )
		{
			take_video( vw,B->data[i], vw->video_list[(vw->frame_counter%vw->num_videos)]->data[i],
				width, height , vw->frame_counter % vw->num_videos);
		}
		for( i = 0; i < 3; i ++ )
		{
			take_video( vw,frame->data[i], vw->video_list[((vw->frame_counter+1)%vw->num_videos)]->data[i],
			width, height , (vw->frame_counter+1) % vw->num_videos);
		}
	}
	else
	{
		int n = vw->frame_counter - 1;
		if(n>=0)
		{	
			for( i = 0; i < 3; i ++ )
			{
				take_video( vw,frame->data[i], vw->video_list[(n%vw->num_videos)]->data[i],
					width, height , vw->frame_counter % vw->num_videos);
			}
			n++;
			for( i = 0; i < 3; i ++ )
			{
				take_video( vw,B->data[i], vw->video_list[(n%vw->num_videos)]->data[i],
				width, height , (vw->frame_counter+1) % vw->num_videos);
			}
		}
	}
	
	matrix_f matrix_placement;
	if( mode == 0 ) {
		matrix_placement = get_matrix_func(0); // !important in random mode
	}
	else {
		matrix_placement = get_matrix_func(mode-1);
	}

	for ( i = 0; i < vw->num_videos; i ++ )
	{
		matrix_t m = matrix_placement(vw->rt[i], size,width,height );
		put_video( vw, dstY, vw->video_list[i]->data[0],width,height,i, m);
		put_video( vw, dstU, vw->video_list[i]->data[1],width,height,i, m);
		put_video( vw, dstV, vw->video_list[i]->data[2],width,height,i, m);
	}

	if( vw->frame_delay == delay)
		vw->frame_counter+=2;

}

