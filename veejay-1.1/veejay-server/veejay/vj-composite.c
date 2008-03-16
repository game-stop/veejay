/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2007 Niels Elburg <nelburg@looze.net>
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

	this c file does a perspective transform on an input image in software.

 */
#include <config.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#include <libavutil/avutil.h>
#include <libyuv/yuvconv.h>
#include <veejay/vims.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	int proj_width;
	int proj_height;
	int img_width;
	int img_height;
	int mode;
	int focus;
	uint8_t *proj_plane[3];
//	uint8_t *img_plane[3];
	uint8_t *large_plane[3]; //@slow
	void *vp1;
	void *sampler;
	void *scaler;
	int  sample_mode;
	int  pf;
	int  blit;
	uint8_t *ptr[3];
	int use_ptr;
} composite_t;

//@ round to multiple of 8
#define    RUP8(num)(((num)+8)&~8)


void	*composite_get_vp( void *data )
{
	composite_t *c = (composite_t*) data;
	return c->vp1;
}

void	*composite_init( int pw, int ph, int iw, int ih, const char *homedir, int sample_mode, int zoom_type, int pf )
{
	composite_t *c = (composite_t*) vj_calloc(sizeof(composite_t));
	int 	vp1_enabled = 0;
	int	vp1_frontback = 0;

	c->sample_mode = sample_mode;

	c->mode = 0;

	c->proj_width = pw;
	c->proj_height = ph;
	c->img_width = iw;
	c->img_height = ih;

	c->pf = pf;
	
	c->proj_plane[0] = (uint8_t*) vj_malloc( RUP8( pw * ph * 3) + RUP8(pw * 3));
	c->proj_plane[1] = c->proj_plane[0] + RUP8(pw * ph) + RUP8(pw);
	c->proj_plane[2] = c->proj_plane[1] + RUP8(pw * ph) + RUP8(pw);
	c->large_plane[0] = (uint8_t*) vj_malloc( RUP8(pw * ph * 3)+ RUP8(pw*3) );
	c->large_plane[1] = c->large_plane[0] + RUP8(pw * ph) + RUP8(pw);
	c->large_plane[2] = c->large_plane[1] + RUP8(pw * ph) + RUP8(pw);
	veejay_memset(c->large_plane[0], 0xff, pw*ph);
	veejay_memset(c->large_plane[1], 128,  pw*ph);
	veejay_memset(c->large_plane[2], 128,  pw*ph);

	c->vp1 = viewport_init( 0,0,pw,ph,pw, ph, homedir, &vp1_enabled, &vp1_frontback, 1);

	viewport_set_marker( c->vp1, 1 );

	c->sampler = subsample_init( pw );

	sws_template sws_templ;
	veejay_memset(&sws_templ,0,sizeof(sws_template));
	sws_templ.flags = zoom_type;

	VJFrame *src = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1],c->proj_plane[2],
					iw,ih, PIX_FMT_YUV444P);

	VJFrame *dst = yuv_yuv_template( c->large_plane[0],c->large_plane[1],c->large_plane[2],
					pw, ph, PIX_FMT_YUV444P );

	c->scaler = yuv_init_swscaler( src, dst, &sws_templ, 0 );

	free(dst);
	free(src);

	veejay_msg(VEEJAY_MSG_INFO, "Configuring projection:");
	veejay_msg(VEEJAY_MSG_INFO, "\tSoftware scaler  : %s", yuv_get_scaler_name(zoom_type) );
	veejay_msg(VEEJAY_MSG_INFO, "\tVideo resolution : %dx%d", iw,ih );
	veejay_msg(VEEJAY_MSG_INFO, "\tScreen resolution: %dx%d", pw,ph );
	veejay_msg(VEEJAY_MSG_INFO, "Usage:");
	veejay_msg(VEEJAY_MSG_INFO, "Press Middle-Mouse button to activate setup,");
	veejay_msg(VEEJAY_MSG_INFO, "Press CTRL+p to switch between projection and viewport focus");
	veejay_msg(VEEJAY_MSG_INFO, "Press CTRL+v to activate user viewport");
	
	return (void*) c;
}

void	composite_destroy( void *compiz )
{
	composite_t *c = (composite_t*) compiz;
	if(c)
	{
		if(c->proj_plane[0]) free(c->proj_plane[0]);
		if(c->large_plane[0]) free(c->large_plane[0]);
//		if(c->img_plane[0]) free(c->img_plane[0]);
		if(c->vp1) viewport_destroy( c->vp1 );
		if(c->scaler)	yuv_free_swscaler( c->scaler );
		if(c->sampler) subsample_free(c->sampler);
		free(c);
	}
	c = NULL;
}

void	composite_event( void *compiz, uint8_t *in[3], int mouse_x, int mouse_y, int mouse_button, int w_x, int w_y )
{
	composite_t *c = (composite_t*) compiz;
	viewport_external_mouse( c->vp1, c->large_plane, mouse_x, mouse_y, mouse_button, 1,w_x,w_y );
}

static inline void	composite_fit_l( composite_t *c, VJFrame *img_data, uint8_t *planes[3] )
{
	if(img_data->width == c->proj_width && img_data->height == c->proj_height )
	{	//@ if sizes match, copy pointers
		c->use_ptr = 1;
		c->ptr[0] = planes[0];
		c->ptr[1] = planes[1];
		c->ptr[2] = planes[2];
	}
	else
	{
		//@ software scaling
		c->use_ptr = 0;
		VJFrame *src1 = yuv_yuv_template( planes[0], planes[1], planes[2],
					  img_data->width,   img_data->height, PIX_FMT_YUV444P );

		VJFrame *dst1 = yuv_yuv_template( c->large_plane[0],c->large_plane[1], c->large_plane[2],
					  c->proj_width,c->proj_height, PIX_FMT_YUV444P );

		yuv_convert_and_scale(c->scaler,src1,dst1 );
		free(src1);
		free(dst1);
	}
}

static inline void	composite_fit( composite_t *c, VJFrame *img_data, uint8_t *planes[3] )
{
	if(img_data->width == c->proj_width && img_data->height == c->proj_height )
	{
		c->use_ptr = 1;
		c->ptr[0] = planes[0];
		c->ptr[1] = planes[1];
		c->ptr[2] = planes[2];
	}
	else
	{
		c->use_ptr = 0;
		VJFrame *src1 = yuv_yuv_template( planes[0], planes[1], planes[2],
					  img_data->width,   img_data->height, PIX_FMT_YUV444P );

		VJFrame *dst1 = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1], c->proj_plane[2],
					  c->proj_width,c->proj_height, PIX_FMT_YUV444P );

		yuv_convert_and_scale( c->scaler, src1, dst1 );
		free(src1);
		free(dst1);
	}
}

static void	composite_configure( composite_t *c,VJFrame *img_data, uint8_t *planes[3] )
{
	composite_fit(c,img_data, planes );
	if( c->use_ptr )
		viewport_draw_interface_color(c->vp1, c->ptr );
	else
		viewport_draw_interface_color( c->vp1, c->proj_plane );
}
/*
static void	composite_prerender( composite_t *c, VJFrame *img_data, uint8_t *planes[3])
{
	VJFrame *src1 = yuv_yuv_template( planes[0], planes[1], planes[2],
					  img_data->width,   img_data->height, PIX_FMT_YUV444P);

	VJFrame *dst1 = yuv_yuv_template( c->large_plane[0],c->large_plane[1], c->large_plane[2],
					  c->proj_width,c->proj_height, PIX_FMT_YUV444P);

	yuv_convert_and_scale( c->scaler, src1, dst1 );
	free(src1);
	free(dst1);
}
*/
void	composite_process(  void *compiz, uint8_t *img_dat[3], VJFrame *input, int use_vp, int focus )
{
	composite_t *c = (composite_t*) compiz;

	if(!input->ssm)
	{
		chroma_supersample( c->sample_mode, c->sampler, img_dat, input->width, input->height );
		input->ssm = 1;
	}
	int proj_active = viewport_active(c->vp1 );

	c->blit = 0;

	//@ If the current focus is VIEWPORT, pass trough
	if( focus == 0 )
	{
		composite_fit( c, input, img_dat );
		if(!c->use_ptr)
			chroma_subsample( c->sample_mode, c->sampler, c->proj_plane, c->proj_width,c->proj_height );
		else
			chroma_subsample( c->sample_mode, c->sampler, c->ptr, c->proj_width,c->proj_height);
		input->ssm = 0;
		return;
	}
	else
	{
		if( !proj_active ) //@ render projection to yuyv surface
		{
			composite_fit_l(c, input,img_dat );
			c->blit = 1;
			input->ssm = 0; //@ blit subsamples data
		}
		else
		{
			if(use_vp)
			{	//@ dont show viewport data
				composite_configure(c,input,input->data);
			}
			else
				composite_configure(c, input, img_dat );

			if(!c->use_ptr)
				chroma_subsample( c->sample_mode, c->sampler, c->proj_plane, c->proj_width,c->proj_height );
			else
				chroma_subsample( c->sample_mode, c->sampler, c->ptr, c->proj_width, c->proj_height );
			input->ssm = 0;
		}
	}
}
void	composite_transform_points( void *compiz, void *coords, int n, int blob_id, int cx, int cy,int w, int h,int num_objects,uint8_t *plane )
{
	composite_t *c = (composite_t*) compiz;
	viewport_transform_coords( c->vp1, coords, n , blob_id,cx,cy,w,h,num_objects, plane);
}

void	composite_dummy( void *compiz )
{
	if(!compiz) return;
	composite_t *c = (composite_t*) compiz;
	viewport_dummy_send( c->vp1 );
}

void	composite_blit( void *compiz, uint8_t *yuyv )
{
	composite_t *c = (composite_t*) compiz;
	
	if(c->use_ptr)
	{
		if(c->blit)
			viewport_produce_full_img_yuyv(c->vp1,c->ptr,yuyv);
		else
		{
			if(c->pf == FMT_420 || c->pf == FMT_420F )
				yuv420p_to_yuv422(c->ptr, yuyv, c->proj_width,c->proj_height );
			else
				yuv422_to_yuyv(c->ptr,yuyv,c->proj_width,c->proj_height );
		}
	}
	else {
		if(c->blit)
			viewport_produce_full_img_yuyv( c->vp1, c->large_plane, yuyv);
		else
		{
			if( c->pf == FMT_420 || c->pf == FMT_420F )
				yuv420p_to_yuv422( c->proj_plane, yuyv, c->proj_width,c->proj_height );
			else
				yuv422_to_yuyv( c->proj_plane, yuyv, c->proj_width,c->proj_height );
		}
	}
}

void	composite_get_blit_buffer( void *compiz, uint8_t *buf[3] )
{
	composite_t *c = (composite_t*) compiz;
	if(c->blit)
	{
		buf[0] = c->large_plane[0];
		buf[1] = c->large_plane[1];
		buf[2] = c->large_plane[2];
	}
	else
	{
		buf[0] = c->proj_plane[0];
		buf[1] = c->proj_plane[1];
		buf[2] = c->proj_plane[2];
	}
}

