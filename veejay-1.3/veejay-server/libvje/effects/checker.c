/* 
 * Linux VeeJay
 *
 * Copyright(C)2007 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include "motionmap.h"
#include "common.h"
#include "softblur.h"
#include "opacity.h"

#define HIS_DEFAULT 2
#define HIS_LEN (8*25)
#define ACT_TOP 4000
#define MAXCAPBUF 55

typedef struct {
	int	p;
	int	t;
} boxes_t;

vj_effect *motionmap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // motionmap
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 50;  // reverse
    ve->limits[1][1] = 10000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][4] = 0; // buffer 
    ve->limits[1][4] = 255;
    ve->limits[0][3] = HIS_DEFAULT;
    ve->limits[1][3] = HIS_LEN;
    ve->defaults[0] = 40;
    ve->defaults[1] = ACT_TOP;
    ve->defaults[2] = 1;
    ve->defaults[3] = HIS_DEFAULT;
    ve->defaults[4] = 0;
    ve->description = "Motion Mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->n_out = 2;
    return ve;
}

static uint8_t *binary_img = NULL;
static uint8_t *original_img = NULL;
static uint8_t *previous_img = NULL;
static uint8_t *large_buf = NULL;
static	uint32_t	key1_ = 0, key2_ = 0, keyv_ = 0, keyp_ = 0;
static int have_bg = 0;
static int running = 0;
static boxes_t *boxes = NULL;

#define    RUP8(num)(((num)+8)&~8)

int		motionmap_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h * 3) );
	original_img = binary_img + RUP8(w*h);
	previous_img = original_img + RUP8(w*h);
	large_buf = vj_malloc(sizeof(uint8_t) * RUP8(w*h*3) * (MAXCAPBUF+1));
	if(!large_buf)
	{
		veejay_msg(0, "Memory allocation error for Motion Mapping. Too large: %ld bytes",(long) ((RUP8(w*h*3)*(MAXCAPBUF+1))));
		return 0;
	}
	nframe_ = 0;
	boxes = (boxes_t*) vj_malloc(sizeof(boxes_t) * 64 * 64 );
	int i ;
	for ( i = 0;i < 64*64; i ++ ) {
		boxes[i].p = 0;
		boxes[i].t = 0;
	}
	return 1;
}

void		motionmap_free(void)
{
	if(binary_img)
		free(binary_img);
	if(large_buf)
		free(large_buf);
	have_bg = 0;
	nframe_ = 0;
	running = 0;
	binary_img = NULL;
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

static	void	update_bgmask( uint8_t *dst,uint8_t *in, uint8_t *src, int len, int threshold )
{
	int i;
	unsigned int op0,op1;
	for( i =0; i < len ; i ++ )
	{
	  if( abs(in[i] - src[i]) > threshold )
	  {
		dst[i] = 1;
		in[i] = (in[i] + src[i])>>1;
	 }
	 else
	 {
		dst[i] = 0;
	 }
	}
}


static void put_photo( uint8_t *dst_plane, uint8_t *src_plane, int dst_w, int dst_h, int index , matrix_t matrix,
	int box_w, int box_h)
{
	int x,y;
	uint8_t *P = dst_plane + (matrix.h*dst_w);
	uint8_t *Q = src_plane + (matrix.h*dst_w);
	int	offset = matrix.w;

	for( y = 0; y < box_h; y ++ )
	{
		for( x = 0; x < box_w; x ++ )
		{
			*(P+offset+x) = *(Q+offset+x);
		}
		P += dst_w;
		Q += dst_w;
	}
}

void motionmap_apply( VJFrame *frame, int width, int height, int threshold, int param1, int param2, int history, int capbuf )
{
	unsigned int i,x,y;
	int len = (width * height);
    	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	int w = frame->width;
	int h = frame->height;

	veejay_memcpy( original_img, frame->data[0], len );

//	softblur_apply( frame, width,height,0 );
	if(!have_bg)	
	{
		veejay_memcpy( previous_img, frame->data[0], len );
		have_bg = 1;
		nframe_ = 0;
		running = 0;
		return;
	}
	else
	{
		update_bgmask( binary_img, previous_img, frame->data[0], len , threshold);
	}

	int x,y,dx,dy;
	int sum;
	int dst_x, dst_y;
	int step_y;
	int step_x;
	int box_width = photo_list[index]->w;
	int box_height = photo_list[index]->h;
	int it = 0;
	uint8_t *plane = binary_img;

	matrix_f matrix_placement = get_matrix_func(mode);


	step_x = w / box_width;
	step_y = h / box_height;

	for( y = 0 ; y < h ; y += step_y )
	{
		for( x = 0;  x < w ; x+= step_x )
		{
			sum = 0;
			for( dy = 0; dy < step_y; dy ++ )
			{
				for( dx = 0; dx < step_x; dx++)	
				{
					sum += plane[ ((y+dy)*w+(dx+x)) ];	
				}
			}
		}

		boxes[ it ].p = sum;
		it ++;
		sum = 0;
	}


	for( y = 0 ; y < it ; y ++ )
	{
		if(boxes[it].p > param1) //@ sufficient motion
		{
			if( boxes[it].t < 5 ) //@ update timer
				boxes[it].t = param2; //@ Time stop
			
		}
		if( boxes[it].t > 0 )
			boxes[it].t --;
		if( boxes[it].t == 0 ) {
			boxes[it].p = 0;
		}
	}
	for ( i = 0; i < num_photos; i ++ )
	{
		matrix_t m = matrix_placement(i, size,width,height );
		put_photo( dstY, photo_list[i]->data[0],width,height,i, m);
		put_photo( dstU, photo_list[i]->data[1],width,height,i, m);
		put_photo( dstV, photo_list[i]->data[2],width,height,i, m);
	}



}
