/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#include <veejay/vj-misc.h>
#include <libavutil/pixfmt.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-split.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vims.h>
#ifdef HAVE_GL
#include <veejay/gl.h>
#endif
#include <veejaycore/avcommon.h>

typedef struct
{
	uint8_t *proj_plane[4];
	uint8_t *mirror_plane[4];
	void *vp1;
	void *scaler;
	void *back_scaler;
	VJFrame	*frame1;
	VJFrame *frame2;
	VJFrame *frame3;
	VJFrame *frame4;
	VJFrame *frame5;
	int sample_mode;
	int pf;
	int use_back;
	int Y_only;
	int proj_width;			/* projection (output) */
	int proj_height;
	int img_width;			/* image (input) */
	int img_height;
	int has_mirror_plane;
	int mirror_row_start;
	int mirror_row_end;
	char tmp_path[1024];
} composite_t;

//@ round to multiple of 8
#define    RUP8(num)(((num)+8)&~8)

void	*composite_get_vp( void *data )
{
	composite_t *c = (composite_t*) data;
	return c->vp1;
}

void	composite_set_file_mode(void *data, const char *homedir, int mode, int id)
{
	composite_t *c = (composite_t*) data;
	switch(mode) {
		case 2: snprintf(c->tmp_path,sizeof(c->tmp_path),"%s/viewport.cfg", homedir ); break;
		case 1: snprintf(c->tmp_path,sizeof(c->tmp_path),"%s/viewport-stream-%d", homedir,id );break;
		default:snprintf(c->tmp_path,sizeof(c->tmp_path),"%s/viewport-sample-%d.cfg", homedir,id );break; 
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "Save/load from File [%s]",c->tmp_path );
}

int	composite_get_ui(void *data )
{
	composite_t *c = (composite_t*) data;
	return viewport_get_mode(c->vp1);
}

void	*composite_init( int pw, int ph, int iw, int ih, char *homedir, int sample_mode, int zoom_type, int pf, int *vp1_e )
{
	composite_t *c = (composite_t*) vj_calloc(sizeof(composite_t));
	int	vp1_frontback = 0;	
	int	vp1_enabled = 0;
	if( pw <= 0 || ph <= 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING ,"Missing projection dimensions,using image dimensions %dx%d",iw,ih);
		pw = iw;
		ph = ih;
	}

	c->sample_mode = sample_mode;
	c->proj_width = pw;
	c->proj_height = ph;
	c->img_width = iw;
	c->img_height = ih;
	c->pf = pf;
	c->Y_only = 0;

	c->vp1 = viewport_init( 0,0,pw,ph,pw, ph,iw,ih, homedir, &vp1_enabled, &vp1_frontback, 1);
	if(!c->vp1) {
		free(c);
		return NULL;
	}

	c->proj_plane[0] = (uint8_t*) vj_calloc( RUP8( pw * ph * 3) + RUP8(pw * 3) * sizeof(uint8_t));
	c->proj_plane[1] = c->proj_plane[0] + RUP8(pw * ph) + RUP8(pw);
	c->proj_plane[2] = c->proj_plane[1] + RUP8(pw * ph) + RUP8(pw);
	viewport_set_marker( c->vp1, 1 );

	sws_template sws_templ;
	veejay_memset(&sws_templ,0,sizeof(sws_template));
	sws_templ.flags = zoom_type;

	c->frame1 = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1],c->proj_plane[2],iw,ih,get_ffmpeg_pixfmt( pf ));
	c->frame2 = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1],c->proj_plane[2],pw, ph, c->frame1->format );
	c->frame3 = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1],c->proj_plane[2],pw,ph,c->frame1->format );
	c->frame4 = yuv_yuv_template( c->proj_plane[0],c->proj_plane[1],c->proj_plane[2],iw,ih,c->frame1->format );

	c->scaler = yuv_init_swscaler( c->frame1, c->frame2, &sws_templ, 0 );
	c->back_scaler = yuv_init_swscaler( c->frame4, c->frame3, &sws_templ, 0 );

	veejay_msg(VEEJAY_MSG_DEBUG, "\tSoftware scaler  : %s", yuv_get_scaler_name(zoom_type) );
	veejay_msg(VEEJAY_MSG_DEBUG, "\tVideo resolution : %dx%d", iw,ih );
	veejay_msg(VEEJAY_MSG_DEBUG, "\tScreen resolution: %dx%d", pw,ph );
	veejay_msg(VEEJAY_MSG_DEBUG, "\tStatus           : %s",(vp1_enabled ? "Active":"Inactive"));

	*vp1_e = (vp1_enabled ? 1 : 2);

	char *gf_instr = getenv( "VEEJAY_ORIGINAL_FRAME" );
	if( gf_instr == NULL ) {
		return (void*) c;
	}

	if( strncasecmp( gf_instr, "1", 1 ) == 0 ) {
		c->has_mirror_plane = 1;
		c->mirror_plane[0] = (uint8_t*) vj_calloc( RUP8( iw * ih * 3) + RUP8(iw * 3) * sizeof(uint8_t));
		c->mirror_plane[1] = c->mirror_plane[0] + RUP8(iw * ih) + RUP8(iw);
		c->mirror_plane[2] = c->mirror_plane[1] + RUP8(iw * ih) + RUP8(iw);
		c->mirror_row_start = 0;
		c->mirror_row_end = 0;
	}

	return (void*) c;
}

void	*composite_clone( void *compiz )
{
	composite_t *c = (composite_t*) compiz;
	if(!c) return NULL;
	void *v = viewport_clone(c->vp1,c->img_width,c->img_height);
//	viewport_reconfigure(v);
	
	veejay_msg(VEEJAY_MSG_WARNING, "Cloned new backing %p", v);

	return v;
}

void	composite_destroy( void *compiz )
{
	composite_t *c = (composite_t*) compiz;
	if(c)
	{
		if(c->proj_plane[0]) free(c->proj_plane[0]);
		if(c->vp1) viewport_destroy( c->vp1 );
		if(c->scaler)	yuv_free_swscaler( c->scaler );	
		if(c->back_scaler) yuv_free_swscaler(c->back_scaler);
		if(c->frame1) free(c->frame1);
		if(c->frame2) free(c->frame2);
		if(c->frame3) free(c->frame3);
		if(c->frame4) free(c->frame4);
		free(c);
	}
	c = NULL;
}

int	composite_get_status(void *compiz )
{
	composite_t *c = (composite_t*) compiz; 
	return viewport_get_initial_active( c->vp1 );
}

void	composite_set_status(void *compiz, int mode)
{
	composite_t *c = (composite_t*) compiz; 
	viewport_set_initial_active( c->vp1, mode );
}

void	composite_set_ui(void *compiz, int status )
{
	composite_t *c = (composite_t*) compiz; 

	viewport_set_ui( c->vp1, status );
}


void	composite_add_to_config( void *compiz, void *vc, int which_vp )
{
	composite_t *c = (composite_t*) compiz; 
	viewport_set_composite( vc, which_vp, c->Y_only );
}

int	composite_event( void *compiz, uint8_t *in[4], int mouse_x, int mouse_y, int mouse_button, int w_x, int w_y, char *homedir, int mode, int id )
{
	composite_t *c = (composite_t*) compiz; 
	if(viewport_external_mouse( c->vp1, c->proj_plane, mouse_x, mouse_y, mouse_button, 1,w_x,w_y, homedir, mode, id )) {
		return 1;
	}
	return 0;
}

int	composite_get_original_frame(void *compiz, uint8_t *current_in[4], uint8_t *out[4], int which_vp, int row_start, int row_end )
{
	composite_t *c = (composite_t*) compiz;
	if( c->has_mirror_plane ) {
		out[0] = c->mirror_plane[0];
		out[1] = c->mirror_plane[1];
		out[2] = c->mirror_plane[2];
		c->mirror_row_start = row_start;
		c->mirror_row_end   = row_end;
		return c->frame1->format;
	} 
	return -1;
}

int	composite_get_top(void *compiz, uint8_t *current_in[4], uint8_t *out[4], int which_vp )
{
	composite_t *c = (composite_t*) compiz;
	int vp1_active = viewport_active(c->vp1);
	if( vp1_active )
	{
		out[0] = c->proj_plane[0];
		out[1] = c->proj_plane[1];
		out[2] = c->proj_plane[2];
		return c->frame2->format;
	}

	if (c->proj_width != c->img_width &&
		c->proj_height != c->img_height && which_vp == 2  )
	{
		out[0] = c->proj_plane[0];
		out[1] = c->proj_plane[1];
		out[2] = c->proj_plane[2];
		return c->frame3->format;
	} else 	if( which_vp == 1) {
		out[0] = c->proj_plane[0];
		out[1] = c->proj_plane[1];
		out[2] = c->proj_plane[2];
		return c->frame2->format;
	} else if ( which_vp == 2 ) {
		out[0] = current_in[0];
		out[1] = current_in[1];
		out[2] = current_in[2];
		return c->frame1->format;
	} 
	return c->frame1->format; 
}

/* Top frame, blit */
void	composite_blit_yuyv( void *compiz, uint8_t *in[4], uint8_t *yuyv, int which_vp )
{
	composite_t *c = (composite_t*) compiz;
	int vp1_active = viewport_active(c->vp1);

	if( which_vp == 2 && vp1_active ) {
		yuv422_to_yuyv(c->proj_plane,yuyv,c->proj_width,c->proj_height );
		return;
	} else if (which_vp == 2 ) {
		if (c->proj_width != c->img_width &&
			c->proj_height != c->img_height && which_vp == 2  )
			{
				yuv422_to_yuyv(c->proj_plane,yuyv,c->proj_width,c->proj_height);
			} 
			else {
				yuv422_to_yuyv(in,yuyv,c->proj_width,c->proj_height );	
			}
		return;
	} 

	if( which_vp == 1 && !vp1_active ) {
		viewport_produce_full_img_yuyv( c->vp1,c->proj_plane,yuyv);
		return;
	}
}

void	composite_process_divert( void *compiz,uint8_t *in[4], VJFrame *out, void *splitter, int which_vp )
{
	composite_t *c = (composite_t*) compiz;
	int vp1_active = viewport_active(c->vp1);

	if( vp1_active && which_vp == 2 ) {
		vj_split_process( splitter, out );
	}
	else if( which_vp == 2 ) {
		viewport_produce_full_img(c->vp1,c->proj_plane,out->data);
		vj_split_process( splitter, out );
	}
	else if ( which_vp == 1 && !vp1_active ) {
		viewport_produce_full_img(c->vp1,c->proj_plane,out->data);
		vj_split_process( splitter, out );
	}
}

//@OBSOLETE
void	composite_blit_ycbcr( void *compiz, 
			      uint8_t *in[4], 
                              int which_vp,
			      void *gl )
{
}

void	*composite_get_config(void *compiz)
{
	composite_t *c = (composite_t*) compiz;
	return viewport_get_configuration( c->vp1, c->tmp_path );
}

/* Top frame */
int	composite_process(void *compiz, VJFrame *output, VJFrame *input, int which_vp, int pff )
{
	composite_t *c = (composite_t*) compiz;
	
	int vp1_active = viewport_active(c->vp1);
	
	if( c->has_mirror_plane ) {
		// the copy is needed for vj_event_get_image_part
		// mirror plane is only active when VEEJAY_ORIGINAL_FRAME is set to 1
		if(c->mirror_row_start == 0 && (c->mirror_row_end == input->height || c->mirror_row_end == 0) ) {
			int strides[4] = { input->len, input->uv_len, input->uv_len, 0 };
			vj_frame_copy( input->data, c->mirror_plane, strides );
		}
		else {
			unsigned int i,j = 0;
			for( i = c->mirror_row_start; i < c->mirror_row_end; i ++, j ++ ) {
				veejay_memcpy( c->mirror_plane[0] + ( j * input->width ), input->data[0] + ( i * input->width ), input->width );
				veejay_memcpy( c->mirror_plane[1] + ( j * input->uv_width ), input->data[1] + ( i * input->uv_width ), input->uv_width );
				veejay_memcpy( c->mirror_plane[2] + ( j * input->uv_width), input->data[2] + ( i * input->uv_width ), input->uv_width );
			}
		}
	}	

	if( which_vp == 2 && !vp1_active ) 
	{
		if( input->width != output->width ||
	           input->height != output->height ) {
			c->frame4->data[0] = output->data[0];
			c->frame4->data[1] = output->data[1];
			c->frame4->data[2] = output->data[2];
	/*		if(output->ssm || pff == PIX_FMT_YUV444P || pff == PIX_FMT_YUVJ444P) 
				c->frame4->format = PIX_FMT_YUV444P;
			else*/
			c->frame4->format = pff;
			yuv_convert_and_scale(c->back_scaler,c->frame4,c->frame3);
		}
		if( pff == PIX_FMT_YUV444P || pff == PIX_FMT_YUVJ444P )	
			return 1;

		return output->ssm;
	} 

	if( which_vp && vp1_active ) /* for both modes, render ui from vp1 */
	{
		viewport_push_frame( /* push frame to preview in setup */
			c->vp1,
			input->width,
			input->height,
			input->data[0],
			input->data[1],
			input->data[2]
			);	
		
		veejay_memset( c->proj_plane[0], 125, c->proj_width * c->proj_height );	
		veejay_memset( c->proj_plane[1], 128, c->proj_width* c->proj_height );
		veejay_memset( c->proj_plane[2], 128, c->proj_width* c->proj_height );

		viewport_draw_interface_color( c->vp1, c->proj_plane );
	} 
	else if ( which_vp == 1 ) 
	{
		int strides[4] = { input->len, input->len, input->len, 0 };
		vj_frame_copy( input->data, c->frame2->data, strides );
	}
	return 1;
}

int	composite_get_colormode(void *compiz)
{
	composite_t *c = (composite_t*) compiz;
	return c->Y_only;
}

void	composite_set_colormode( void *compiz, int mode )
{
	composite_t *c = (composite_t*) compiz;
	if( mode == 0 )
		c->Y_only = 0;
	else if( mode == 1 )
		c->Y_only = 1;
}

void	*composite_get_draw_buffer( void *compiz )
{
	composite_t *c = (composite_t*) compiz;
	VJFrame *frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	vj_get_yuv444_template( frame, c->proj_width,c->proj_height);
	frame->data[0] = c->proj_plane[0];
	frame->data[1] = c->proj_plane[1];
	frame->data[2] = c->proj_plane[2];
	return (void*)frame;
}

