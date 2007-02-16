/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#ifdef USE_GDK_PIXBUF
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <ffmpeg/avcodec.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>
#include <libyuv/yuvconv.h>
#include <libel/pixbuf.h>
#include <veejay/vj-global.h>
#include <ffmpeg/swscale.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

typedef struct
{
	char *filename;
	VJFrame	  *picA;
	VJFrame   *picB;
	VJFrame	  *img;
	uint8_t   *space;
	int	  display_w;
	int	  display_h;
	int	  real_w;
	int	  real_h;
	int	  fmt;
} vj_pixbuf_t;


typedef struct
{
	char *filename;
	char *type;
	int	  out_w;
	int	  out_h;
} vj_pixbuf_out_t;

static int	__initialized = 0;

extern int	get_ffmpeg_pixfmt(int id);

static	VJFrame *open_pixbuf( const char *filename, int dst_w, int dst_h, int dst_fmt,
			uint8_t *dY, uint8_t *dU, uint8_t *dV )
{
	GdkPixbuf *image =
		gdk_pixbuf_new_from_file( filename, NULL );
	if(!image)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to load image '%s'", filename);
		return NULL;
	}

	/* convert image to veejay frame in proper dimensions, free image */

	VJFrame *dst = yuv_yuv_template( dY, dU, dV, dst_w, dst_h, dst_fmt );
	VJFrame *src = yuv_rgb_template(
			(uint8_t*) gdk_pixbuf_get_pixels( image ),
				   gdk_pixbuf_get_width(  image ),
				   gdk_pixbuf_get_height( image ),
				   PIX_FMT_RGB24
			);

	yuv_convert_any( src, dst, src->format, dst->format );
	
	gdk_pixbuf_unref( image ); 
	
	free(src);


	return dst;
}

void	vj_picture_cleanup( void *pic )
{
	vj_pixbuf_t *picture = ( vj_pixbuf_t*) pic;
	if(picture)
	{
		if( picture->filename )
			free(picture->filename );
		if(picture->img)
			free( picture->img );
		if(picture->space)
			free(picture->space);
		if( picture )
			free(picture);		
	}
	picture = NULL;
}


VJFrame *vj_picture_get(void *pic)
{
	if(!pic)
		return NULL;
	vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
	return picture->img;
}

int	vj_picture_get_height( void *pic )
{
	vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
	if(!picture)
		return 0;
	return picture->real_h;
}	

int	vj_picture_get_width( void *pic )
{
	vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
	if(!picture)
		return 0;
	return picture->real_w;
}	

void	*vj_picture_open( const char *filename, int v_outw, int v_outh, int v_outf )
{
	vj_pixbuf_t *pic = NULL;
	if(filename == NULL )
	{
		veejay_msg(0, "No image filename given");
		return NULL;
	}

	if(v_outw <= 0 || v_outh <= 0 )
	{
		veejay_msg(0, "No image dimensions setup");
		return NULL;
	}

	pic = (vj_pixbuf_t*)  vj_calloc(sizeof(vj_pixbuf_t));
	if(!pic) 
	{
		veejay_msg(0, "Memory allocation error in %s", __FUNCTION__ );
		return NULL;
	}
	pic->filename = strdup( filename );
	pic->display_w = v_outw;
	pic->display_h = v_outh;
	pic->fmt = v_outf;

	int len = v_outw * v_outh;
	int ulen = len;
	switch( v_outf )
	{
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			ulen = len / 4;
			break;
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
			ulen = len / 2;
			break;
		default:
#ifdef STRICT_CHECKING
		assert(0);
#endif
			break;
	}

	pic->space = (uint8_t*) vj_malloc( sizeof(uint8_t) * (len + 2*ulen) );
	pic->img = open_pixbuf(
			filename,	
			v_outw,
			v_outh,
			v_outf,
			pic->space,
			pic->space + len,
			pic->space + len + ulen );

	return (void*) pic;
}

int	vj_picture_probe( const char *filename )
{
	int ret = 0;
	GdkPixbuf *image =
		gdk_pixbuf_new_from_file( filename, NULL );
	if(image)
	{
		ret = 1;
		gdk_pixbuf_unref( image );
	}
	return ret;
}

/* image saving */

static	void	add_if_writeable( GdkPixbufFormat *data, GSList **list)
{
	if( gdk_pixbuf_format_is_writable( data ))
		*list = g_slist_prepend( *list, data );
	gchar *name = gdk_pixbuf_format_get_name( data );
	if(name) g_free(name);
} 

char	*vj_picture_get_filename( void *pic )
{
	vj_pixbuf_out_t *p = (vj_pixbuf_out_t*) pic;
	if(!p) return NULL;
	return p->filename;
}

void *	vj_picture_prepare_save(
	const char *filename, char *type, int out_w, int out_h)
{
	if(!type || !filename )
	{
		veejay_msg(0, "Missing filename or file extension");
		return NULL;
	}

	vj_pixbuf_out_t *pic =  (vj_pixbuf_out_t*) vj_calloc(sizeof(vj_pixbuf_out_t));

	if(!pic)
		return NULL;
	  
	if(filename)
		pic->filename = strdup( filename );
	else
		pic->filename = NULL;

	if(strncasecmp(type,"jpg",3 ) == 0)
		pic->type = strdup("jpeg");
	else
		pic->type     = strdup( type );

	pic->out_w = out_w;
	pic->out_h = out_h;

	return (void*) pic;
}



static	void	display_if_writeable( GdkPixbufFormat *data, GSList **list)
{
	if( gdk_pixbuf_format_is_writable( data ))
		*list = g_slist_prepend( *list, data );
	gchar *name = gdk_pixbuf_format_get_name( data );
	if( name ) g_free(name);
} 
void	vj_picture_display_formats()
{
	GSList *f = gdk_pixbuf_get_formats();
	GSList *res = NULL;

	g_slist_foreach( f, display_if_writeable, &res);

	g_slist_free( f );
	g_slist_free( res );
}

static	void	vj_picture_out_cleanup( vj_pixbuf_out_t *pic )
{
	if(pic)
	{
		if(pic->filename)
		 	free(pic->filename);
		if(pic->type)
			free(pic->type);
		free(pic);
	}
	pic = NULL;
}

void	vj_picture_init()
{
	if(!__initialized)
	{
		g_type_init();
		veejay_msg(VEEJAY_MSG_DEBUG, "Using gdk pixbuf %s", gdk_pixbuf_version );
		__initialized = 1;
	}
}

int	vj_picture_save( void *picture, uint8_t **frame, int w, int h , int fmt )
{
	int ret = 0;
	vj_pixbuf_out_t *pic = (vj_pixbuf_out_t*) picture;

	GdkPixbuf *img_ = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, w, h );
	if(!img_)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant allocate buffer for RGB");
		return 0;
	}
	
	// convert frame to yuv
	VJFrame *src = yuv_yuv_template( frame[0],frame[1],frame[2],w,h, fmt );
	VJFrame *dst = yuv_rgb_template(
		(uint8_t*) gdk_pixbuf_get_pixels( img_ ),
				   gdk_pixbuf_get_width(  img_ ),
				   gdk_pixbuf_get_height( img_ ),
				   PIX_FMT_RGB24
			);

	yuv_convert_any( src, dst, fmt, PIX_FMT_RGB24 );
	
	if( gdk_pixbuf_savev( img_, pic->filename, pic->type, NULL, NULL, NULL ))
	{
		veejay_msg(VEEJAY_MSG_INFO, "Save frame as %s of type %s",
			pic->filename, pic->type );
		ret = 1;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR,
			"Cant save file as %s (%s) size %d x %d", pic->filename,pic->type, pic->out_w, pic->out_h);
	}
	
	if( img_ ) 
		gdk_pixbuf_unref( img_ );

	free(src);
	free(dst);

	vj_picture_out_cleanup( pic );
	
	return ret;
}

static	sws_template	sws_templ;
static  sws_template	bw_templ;
static  void *scaler = NULL;
static  void *bwscaler = NULL;
static  int scale_dim_[2] = {0,0};
static int scale_dim_bw[2] = {0,0};
void		vj_picture_free()
{
	if(scaler)
		yuv_free_swscaler( scaler );
	if(bwscaler)
		yuv_free_swscaler( bwscaler );
}

veejay_image_t		*vj_fast_picture_save_to_mem( VJFrame *frame, int out_w, int out_h, int fmt )
{
	veejay_image_t *image = (veejay_image_t*) vj_calloc(sizeof(veejay_image_t));
	if(!image)
		return NULL;

	image->image= (void*) gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, out_w, out_h );
	VJFrame src,dst;
	veejay_memset(&src,0,sizeof(VJFrame));
	veejay_memset(&dst,0,sizeof(VJFrame));

	if( frame->ssm )
		fmt = PIX_FMT_YUV444P;

	vj_get_yuv_template( &src,frame->width,frame->height,fmt );
	src.data[0] = frame->data[0];
	src.data[1] = frame->data[1];	
	src.data[2] = frame->data[2];
	vj_get_rgb_template( &dst, out_w, out_h);
	dst.data[0] = (uint8_t*) gdk_pixbuf_get_pixels( (GdkPixbuf*) image->image );

	if( !scaler || scale_dim_[0] != out_w && scale_dim_[1] != out_h )
	{
		if(scaler)
			yuv_free_swscaler(scaler);
		scaler = yuv_init_swscaler( &src,&dst, &sws_templ, 
				yuv_sws_get_cpu_flags() );
		scale_dim_[0] = out_w;
		scale_dim_[1] = out_h;
	}
	else
	{
#ifdef STRICT_CHECKING
		assert( scale_dim_[0] == dst.width );
		assert( scale_dim_[1] == dst.height );
		assert( scaler != NULL );
#endif
	}

	yuv_convert_and_scale_rgb( scaler, &src, &dst );

	return image;
}

veejay_image_t		*vj_fastbw_picture_save_to_mem( VJFrame *frame, int out_w, int out_h, int fmt )
{
	veejay_image_t *image = (veejay_image_t*) vj_calloc(sizeof(veejay_image_t));
	if(!image)
		return NULL;

	image->image= (void*) gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, out_w, out_h );
	VJFrame src,dst;
	veejay_memset(&src,0,sizeof(VJFrame));
	veejay_memset(&dst,0,sizeof(VJFrame));

	vj_get_yuv_template( &src,frame->width,frame->height,fmt);
	src.data[0] = frame->data[0];
	src.data[1] = frame->data[1];	
	src.data[2] = frame->data[2];
	vj_get_yuv_template( &dst,out_w,out_h,fmt );
	dst.data[0] = (uint8_t*) gdk_pixbuf_get_pixels( image->image );
	dst.data[1] = dst.data[0] + (out_w * out_h );
	dst.data[2] = dst.data[1] + (out_w * out_h );

	if( !bwscaler || scale_dim_bw[0] != out_w || scale_dim_bw[1] != out_h )
	{
		if(bwscaler )
			yuv_free_swscaler( bwscaler );

		bwscaler = yuv_init_swscaler( &src,&dst, &bw_templ, 
				yuv_sws_get_cpu_flags() );

		scale_dim_bw[0] = out_w;
		scale_dim_bw[1] = out_h;
	}
#ifdef STRICT_CHECKING
	assert( bwscaler != NULL );
#endif
	yuv_convert_and_scale( bwscaler, &src, &dst );

	return image;
}

#endif
