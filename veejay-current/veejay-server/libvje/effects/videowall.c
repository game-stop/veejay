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
#include "videowall.h"

typedef struct {
    picture_t **photo_list;
    int	num_photos;
    int frame_counter;
    int *offset_table_x;
    int *offset_table_y;
} videowall_t;

static inline	int	gcd(int p, int q ) { if(q==0) return p; else return(gcd(q,p%q)); }

static void destroy_filmstrip(videowall_t *vw);

static inline	int	n_pics(int w, int h)
{
	return (( w / gcd(w,h)) * 2); 
}

vj_effect *videowall_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0; // selector
    ve->limits[1][0] = n_pics(w,h);
    ve->limits[0][1] = 0;
    ve->limits[1][1] = w; // displacement x
    ve->limits[0][2] = 0;
    ve->limits[1][2] = h; // displacement y
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1; // lock update of x,y in offset[selector]
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->defaults[2] = 1; 
    ve->defaults[3] = 0; 
    ve->description = "VideoWall / Tile Placement";
    ve->sub_format = 1; 
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Photos","X Displacement", "Y displacement", "Lock update");
    return ve;
}

static void *prepare_filmstrip(int w, int h)
{
	int i,j;
	int picture_width = gcd(w,h);
	int picture_height = gcd(w,h);
	int film_length = n_pics(w,h);

    videowall_t *vw = (videowall_t*) vj_calloc(sizeof(videowall_t));
    if(!vw) {
        return NULL;
    }

	vw->photo_list = (picture_t**) vj_calloc(sizeof(picture_t*) * film_length  );
	if(!vw->photo_list) {
        destroy_filmstrip(vw);
		return NULL;
    }

	vw->num_photos = film_length;

	uint8_t val = 0;
	int inc = vw->num_photos % 255;

	for ( i = 0; i < vw->num_photos; i ++ )
	{
		vw->photo_list[i] = vj_calloc(sizeof(picture_t));
		if(!vw->photo_list[i]) {
			destroy_filmstrip(vw);
            return NULL;
        }
		vw->photo_list[i]->w = picture_width;
		vw->photo_list[i]->h = picture_height;
		for( j = 0; j < 3; j ++ )
		{
			vw->photo_list[i]->data[j] = vj_malloc(sizeof(uint8_t) * picture_width * picture_height );
			if(!vw->photo_list[i]->data[j]) {
                destroy_filmstrip(vw);
                return NULL;
            }
		}
		veejay_memset( vw->photo_list[i]->data[0], 0,
				picture_width * picture_height );
		veejay_memset( vw->photo_list[i]->data[1],128,
				picture_width * picture_height );
		veejay_memset( vw->photo_list[i]->data[2],128,
				picture_width * picture_height );
		val+= inc;
	}
	vw->frame_counter = 0;

	vw->offset_table_x = (int*) vj_calloc(sizeof(int) * film_length);
	if(!vw->offset_table_x) {
		destroy_filmstrip(vw);
        return NULL;
    }

	vw->offset_table_y = (int*) vj_calloc(sizeof(int) * film_length);
	if(!vw->offset_table_y) {
        destroy_filmstrip(vw);
        return NULL;
    }

	return (void*) vw;
}

static void destroy_filmstrip(videowall_t *vw)
{
	if(vw->photo_list)
	{
		int i = 0;
		while(i < vw->num_photos)
		{
			if( vw->photo_list[i] )
			{
				int j;
				for( j = 0; j < 3; j ++ )
					if(vw->photo_list[i]->data[j]) 
					 free(vw->photo_list[i]->data[j]);
				free(vw->photo_list[i]);
			}
			i++;
		}
		free(vw->photo_list);
	}
	vw->photo_list = NULL;
	vw->num_photos = 0;
	vw->frame_counter = 0;
	if(vw->offset_table_x) free(vw->offset_table_x);
	if(vw->offset_table_y) free(vw->offset_table_y);
    free(vw);
}

void *videowall_malloc(int w, int h )
{
	return prepare_filmstrip(w,h);
}


void	videowall_free(void *ptr)
{
	destroy_filmstrip(ptr);
}

static void	take_photo( videowall_t *vw, uint8_t *plane, uint8_t *dst_plane, int w, int h, int index )
{
        int x,y,dx,dy;
        int sum;
        int dst_x, dst_y;
        int step_y;
        int step_x;
        int box_width = vw->photo_list[index]->w;
        int box_height = vw->photo_list[index]->h;

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

static void put_photo( videowall_t *vw, uint8_t *dst_plane, uint8_t *photo, int dst_w, int dst_h, int index)
{
	int n = (vw->num_photos/2);
	int box_w = vw->photo_list[index]->w;
	int box_h = vw->photo_list[index]->h;
	int x,y;
	// blits photos left -> right , < n ? :top : bottom
	int	dy = vw->offset_table_y[index];
	int	dx = vw->offset_table_x[index];
	uint8_t *P = (index < n ? dst_plane + ( dy * dst_w ) : dst_plane + ((abs(dst_h-box_h-dy)%dst_h)*dst_w));
	int	offset = (box_w * index + dx) % dst_w;	

	for( y = 0 ; y < box_h ; y ++ )
	{
		for( x = 0; x < box_w; x ++ )
		{
			*(P + offset + x ) = photo[(y*box_h)+x];
		}
		P += dst_w;
	}
}

void videowall_apply( void *ptr, VJFrame *frameA, VJFrame *frameB, int *args )
{
	unsigned int i;
	const unsigned int width = frameA->width;
	const unsigned int height = frameA->height;
	uint8_t *dstY = frameA->data[0];
	uint8_t *dstU = frameA->data[1];
	uint8_t *dstV = frameA->data[2];
    videowall_t *vw = (videowall_t*) ptr;
    int a = args[0];
    int b = args[1];
    int c = args[2];
    int d = args[3];

	if(d==0)
	{
		vw->offset_table_x[a] = b;
		vw->offset_table_y[a] = c;
	}

	for( i = 0; i < 3; i ++ )
	{
		take_photo( vw, frameA->data[i], vw->photo_list[(vw->frame_counter%vw->num_photos)]->data[i], width, height , vw->frame_counter % vw->num_photos);
	}
	vw->frame_counter++;

	for( i = 0; i < 3; i ++ )
	{
		take_photo( vw, frameB->data[i], vw->photo_list[(vw->frame_counter%vw->num_photos)]->data[i], width, height , vw->frame_counter % vw->num_photos);
	}

	for ( i = 0; i < vw->num_photos; i ++ )
	{
		put_photo( vw, dstY, vw->photo_list[i]->data[0],width,height,i);
		put_photo( vw, dstU, vw->photo_list[i]->data[1],width,height,i);
		put_photo( vw, dstV, vw->photo_list[i]->data[2],width,height,i);
	}
	vw->frame_counter++;

}

