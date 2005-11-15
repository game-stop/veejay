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
#include "avcodec.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libvje/effects/common.h>
#include <libyuv/yuvconv.h>
#include <libel/pixbuf.h>
typedef struct
{
	char *filename;
	GdkPixbuf *image;
	uint8_t   *raw;	
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

static	GdkPixbuf *open_pixbuf( const char *filename )
{
	GdkPixbuf *image =
		gdk_pixbuf_new_from_file( filename, NULL );
	if(!image)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant load image");
		return NULL;
	}
	return image;
}

static int	picture_prepare_decode( vj_pixbuf_t *picture )
{
	if(!picture) 
		return 0;
	if(!picture->image)
		return 0;

	int scale = 0;
	picture->raw = (uint8_t*) vj_malloc(sizeof(uint8_t) *
				   (picture->display_w * picture->display_h * 3));
	if(!picture->raw)
		return 0;


	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
        memset(&pict2,0,sizeof(pict2));

	GdkPixbuf *img = NULL;

	int len = picture->display_w * picture->display_h;
	int uv_len = (picture->display_w >> 1 ) * (picture->display_h >> picture->fmt );

	if( gdk_pixbuf_get_width( picture->image ) != picture->display_w ||
		gdk_pixbuf_get_height( picture->image ) != picture->display_h )
	{
		img = gdk_pixbuf_scale_simple( picture->image, picture->display_w,
				picture->display_h, GDK_INTERP_BILINEAR );
		scale = 1;
	}
	else
	{
		img = picture->image;
	}


	if(!img)
		return 0;

	pict1.data[0] = (uint8_t*) gdk_pixbuf_get_pixels( img );
	pict1.linesize[0] = picture->display_w * 3;

	pict2.data[0] = picture->raw;
	pict2.data[1] = picture->raw + len;
	pict2.data[2] = picture->raw + len + uv_len;
	pict2.linesize[0] = picture->display_w;
	pict2.linesize[1] = picture->display_w >> 1;
	pict2.linesize[2] = picture->display_w >> 1;

	img_convert( &pict2, 
		(picture->fmt == 1 ? PIX_FMT_YUV420P: PIX_FMT_YUV422P ),
		&pict1,
		PIX_FMT_RGB24,
		picture->display_w, picture->display_h );

	if(scale && img)
		gdk_pixbuf_unref( img );
	return 1;
}

void	vj_picture_cleanup( void *pic )
{
	vj_pixbuf_t *picture = ( vj_pixbuf_t*) pic;
	if(picture)
	{
		if(picture->raw)
			free(picture->raw);
		if( picture->filename )
			free(picture->filename );
		if(picture->image)
			gdk_pixbuf_unref( picture->image );
		if( picture )
			free(picture);		
	}
	picture = NULL;
}


uint8_t *vj_picture_get(void *pic)
{
	if(!pic)
		return NULL;
	vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
	return picture->raw;
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

	if(filename == NULL || v_outw <= 0 || v_outh <= 0 )
		return NULL;

	pic = (vj_pixbuf_t*)
			    vj_malloc(sizeof(vj_pixbuf_t));
	if(!pic) return NULL;
	memset( pic, 0, sizeof( vj_pixbuf_t ));

	pic->filename = strdup( filename );
	pic->display_w = v_outw;
	pic->display_h = v_outh;
	pic->image = open_pixbuf( filename );
	if(!pic->image)
	{
		free(pic);
		return NULL;
	}
	pic->real_w = gdk_pixbuf_get_width( pic->image );
	pic->real_h = gdk_pixbuf_get_height( pic->image );
	if(pic->display_w == 0 && pic->display_h == 0)
	{
		pic->display_w = pic->real_w;
		pic->display_h = pic->real_h;
	}
	// error check


	if(!picture_prepare_decode( pic ))
	{
		vj_picture_cleanup( (void*) pic );
		return NULL;
	}
//	if(pic->image)
//		gdk_pixbuf_unref( pic->image );
	return (void*) pic;
}

int	vj_picture_probe( const char *filename )
{
	int ret = 0;
	GdkPixbuf *image = open_pixbuf( filename );
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
	vj_pixbuf_out_t *pic =  (vj_pixbuf_out_t*) vj_malloc(sizeof(vj_pixbuf_out_t));
	if(!pic)
		return NULL;
	  
/* 
	GSList *f = gdk_pixbuf_get_formats();
	GSList *res = NULL;

	g_slist_foreach( f, add_if_writeable, &res);

	g_slist_free( f );
	g_slist_free( res ); */

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
	if(!picture)	
		return ret;
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
        memset(&pict2,0,sizeof(pict2));

	vj_pixbuf_out_t *pic = (vj_pixbuf_out_t*) picture;

	pict1.data[0] = frame[0];
	pict1.data[1] = frame[1];
	pict1.data[2] = frame[2];
        pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;

	GdkPixbuf *img_ = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, w, h );
	if(!img_)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant allocate buffer for RGB");
		return 0;
	}

	pict2.data[0] =  (uint8_t*) gdk_pixbuf_get_pixels( img_ );;
        pict2.linesize[0] = w * 3;

	img_convert( &pict2, PIX_FMT_RGB24, &pict1, (fmt == 0 ? PIX_FMT_YUV420P:
                                                                         PIX_FMT_YUV422P),
				w, 	h );

	GdkPixbuf *save = NULL;
	if( pic->out_w != w || pic->out_h != h )
	{
		save = gdk_pixbuf_scale_simple(
				img_, pic->out_w, pic->out_h,
			 GDK_INTERP_BILINEAR );
		if(save)
		{
			gdk_pixbuf_unref( img_ );
			img_ = save;
		}
	}
	
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

	vj_picture_out_cleanup( pic );
	
	return ret;
}

veejay_image_t *vj_picture_save_to_memory( uint8_t **frame, int w, int h , int out_w, int out_h, int fmt  )
{
	veejay_image_t *image = (veejay_image_t*) vj_malloc(sizeof(veejay_image_t));
	if(!image)
		return NULL;

	memset(image, 0,sizeof(veejay_image_t));

	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
        memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = frame[0];
	pict1.data[1] = frame[1];
	pict1.data[2] = frame[2];
        pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;

	image->image = (void*)gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, w, h );
	if(!image->image)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant allocate buffer for RGB");
		return NULL;
	}

	pict2.data[0] =  (uint8_t*) gdk_pixbuf_get_pixels( (GdkPixbuf*) image->image );;
        pict2.linesize[0] = w * 3;

	img_convert( &pict2, PIX_FMT_RGB24, &pict1, (fmt == 0 ? PIX_FMT_YUV420P:
                                                                         PIX_FMT_YUV422P),
				w, 	h );

	if( out_w != w || out_h != h )
	{
		image->scaled_image = (void*)gdk_pixbuf_scale_simple(
				(GdkPixbuf*) image->image, out_w, out_h,
			 GDK_INTERP_BILINEAR );
	}

	return image;
}


#endif
