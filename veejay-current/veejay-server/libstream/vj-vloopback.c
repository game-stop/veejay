/* veejay - Linux VeeJay
 * 	     (C) 2002-2016 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>
#ifdef HAVE_V4L2
#include <linux/videodev2.h>
#include <libstream/v4l2utils.h>
#endif
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <libavutil/pixfmt.h>
#include <libel/avcommon.h>
#include <libstream/vj-vloopback.h>
#include <libvje/effects/common.h>

typedef struct
{
	char *dev_name;		/* device name */
	int fd;
	long size;		/* size of image out_buf */
	uint8_t *out_buf;
	int jfif;
	int swap;
	int v4l2_pixfmt;	
	void *scaler;
	float fps;
	VJFrame	*src1;
	VJFrame *dst1;
} vj_vloopback_t;

static struct {
	int fmt;
	const char *name;
} vloopback_pixfmt[] = {
	{ PIX_FMT_YUV420P, "YUV420P" },
	{ PIX_FMT_YUV422P, "YUV422P" },
	{ PIX_FMT_YUV444P, "YUV444P" },
	{ PIX_FMT_YUVJ420P, "YUVJ420P" },
	{ PIX_FMT_YUVJ422P, "YUVJ422P" },
	{ PIX_FMT_YUVJ444P, "YUVJ444P" },
	{ PIX_FMT_RGB24,   "RGB24" },
	{ PIX_FMT_BGR24,   "BGR24" },
	{ PIX_FMT_RGB32,   "RGB32" },
	{ PIX_FMT_BGR32,   "BGR32" },
	{ PIX_FMT_ARGB,	   "ARGB" },
	{ PIX_FMT_ABGR,	   "ABGR" },
	{ 0, NULL },
};

int vj_vloopback_get_pixfmt( int v ) 
{
	if( v >= 0 && v <= 11 )
		return vloopback_pixfmt[v].fmt;
	return -1;
}

/* Open the vloopback device */

static int vj_vloopback_query_current_format( vj_vloopback_t *v, int *dst_w, int *dst_h, int *dst_format )
{
#ifdef HAVE_V4L2
	struct v4l2_format format1;
	
	veejay_memset(&format1,0,sizeof(format1));
	
	format1.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	int res = ioctl( v->fd, VIDIOC_G_FMT, &format1 );
	if( res == -1 ) 
		return 0;

	*dst_w = format1.fmt.pix.width;
	*dst_h = format1.fmt.pix.height;
	*dst_format = format1.fmt.pix.pixelformat;
	
	return 1;
#else
	return 0;
#endif
}

static int vj_vloopback_set_format( vj_vloopback_t *v, int dst_w, int dst_h, int dst_v4l2_format, int dst_stride )
{
#ifdef HAVE_V4L2
	struct v4l2_format format;
	veejay_memset(&format,0,sizeof(format));
	
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format.fmt.pix.width = dst_w;
	format.fmt.pix.height= dst_h;
	format.fmt.pix.pixelformat = dst_v4l2_format;
	format.fmt.pix.field = V4L2_FIELD_NONE;
	format.fmt.pix.bytesperline = dst_stride;
	format.fmt.pix.colorspace = (v->jfif == 1 ? V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_SMPTE170M );
	
	int res = ioctl( v->fd, VIDIOC_S_FMT, &format );
	if( res < 0 )
	{
		return 0;
	}
	
	veejay_memset( &format, 0, sizeof(format) );
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	res = ioctl( v->fd, VIDIOC_G_FMT, &format );	
	if( res < 0 )
	{
		return 0;
	}	

	if( format.fmt.pix.width != dst_w ||
	    format.fmt.pix.height != dst_h ||
	    format.fmt.pix.pixelformat != dst_v4l2_format ) 
	{
		return 0;
	}

	v->size = RUP8( format.fmt.pix.sizeimage ); 

	struct v4l2_streamparm sfps;
	veejay_memset(&sfps,0,sizeof(sfps));

	sfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sfps.parm.capture.timeperframe.numerator= ( v->fps == 29.97f ? 1001 : 1 );
	sfps.parm.capture.timeperframe.denominator=( v->fps == 29.97f ? 30000 : (int)(v->fps));

	if( -1 == ioctl( v->fd, VIDIOC_S_PARM,&sfps ) )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: VIDIOC_S_PARM fails with:%s", strerror(errno) );
		return -1;
	}

	return 1;
#else
	return 0;
#endif
}

// TODO: refactor yuv_yuv_template to take 4 planes and setup pointers correctly --> vj_frame_alloc(...)
static void vj_vloopback_setup_ptrs( uint8_t *buf, uint8_t *planes[4], int pixfmt, int w, int h )
{
	int uv_width = 0;
	int uv_height = 0;

	planes[0] = buf;
	planes[1] = NULL;
	planes[2] = NULL;
	planes[3] = NULL;

	switch( pixfmt ) 
	{
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUVA422P:
			uv_width = w>>1;
			uv_height= h;
			break;
		case PIX_FMT_YUV444P:
		case PIX_FMT_YUVJ444P:
		case PIX_FMT_YUVA444P:
			uv_width = w;
			uv_height= h;
			break;
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
		case PIX_FMT_YUVA420P:		
			uv_width = w>>1;
			uv_height= h>>1;
			break;
		default:
			/* non planar formats */
			break;
	}

	if( uv_width > 0 && uv_height > 0 ) {
		planes[1] = planes[0] + (w * h);
		planes[2] = planes[1] + (uv_width * uv_height);
		planes[3] = planes[2] + (uv_width * uv_height);
	}		
}

static	int vj_vloopback_user_pixelformat( VJFrame *src )
{
	int result = -1;
#ifdef HAVE_V4L2
	char *str = getenv( "VEEJAY_VLOOPBACK_PIXELFORMAT" );
	if( str != NULL ) {
		int i;
		for( i = 0; vloopback_pixfmt[i].name != NULL; i ++ ) {
			if( strcasecmp( str, vloopback_pixfmt[i].name ) == 0 ) {
				veejay_msg(VEEJAY_MSG_DEBUG, "vloop: user defined pixel format %s (%d)",
					vloopback_pixfmt[i].name, vloopback_pixfmt[i].fmt );
				result = i;
				break;
			}
		}

		if( result == -1) {
			veejay_msg(0, "Invalid pixel format for VEEJAY_VLOOPBACK_PIXELFORMAT. Please use one of the following:");
			for( i = 0; vloopback_pixfmt[i].name != NULL; i ++ ) {
				veejay_msg(0, "\t%s", vloopback_pixfmt[i].name );
			}
			
			return v4l2_ffmpeg2v4l2( src->format );
		}

	
		veejay_msg(VEEJAY_MSG_INFO, "Selected pixel format %s for vloopback device. Choose another with VEEJAY_VLOOPBACK_PIXELFORMAT",
			vloopback_pixfmt[ result ].name);

		return v4l2_ffmpeg2v4l2( vloopback_pixfmt[ result ].fmt );
	}
#else
	return src->format;
#endif

}

void *vj_vloopback_open(const char *device_name, VJFrame *src, int dst_w, int dst_h, int dst_format ) 
{
#ifdef HAVE_V4L2
	int dst_v4l2_format = v4l2_ffmpeg2v4l2( dst_format );
	int dst_v4l2_w = dst_w;
	int dst_v4l2_h = dst_h;
	int no_conv = 0;

	/* output image is same as input image */
	if( dst_w == -1 && dst_h == -1 ) {
		dst_v4l2_w = src->width;
		dst_v4l2_h = src->height;
	}

	if( dst_format == -1 ) {
		dst_v4l2_format = vj_vloopback_user_pixelformat( src );
	}


	vj_vloopback_t *v = (vj_vloopback_t*) vj_calloc(sizeof(vj_vloopback_t));
	if(!v) {
		return NULL;	
	}

	v->fd = open( device_name, O_RDWR ); //, S_IRUSR|S_IWUSR );
	if( v->fd < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "vloop: Cannot open vloopback device '%s': %s", device_name, strerror(errno) );
		free(v);
		return NULL;
	}

	/* colorspace */
	v->v4l2_pixfmt = v4l2_ffmpeg2v4l2( src->format );
	if( v->v4l2_pixfmt == PIX_FMT_YUVJ420P || v->v4l2_pixfmt == PIX_FMT_YUVJ422P || v->v4l2_pixfmt == PIX_FMT_YUVJ444P )
		v->jfif = 1;

	v->dev_name = strdup( device_name );
	v->fps = src->fps;
	/* swap U V source image pointers */
	char *swap_uv_planes = getenv( "VEEJAY_VLOOPBACK_SWAP_UV" );
	if( swap_uv_planes != NULL ) {
		v->swap = atoi( swap_uv_planes );	
		if(v->swap) {
			veejay_msg(VEEJAY_MSG_DEBUG, "vloop: Vloopback UV swap enabled" );
		}
	}

	/* user wants the current configured format */
	if( dst_w == 0 && dst_h == 0 && dst_format == 0 )
	{
		if( vj_vloopback_query_current_format( v, &dst_v4l2_w, &dst_v4l2_h, &dst_v4l2_format ) == 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "vloop: You must set frame dimensions (width,height) and pixel format");
			vj_vloopback_close(v);
			return NULL;
		}
	}


	VJFrame *tmp = yuv_yuv_template( NULL,NULL,NULL, dst_v4l2_w, dst_v4l2_h, v4l2_pixelformat2ffmpeg(dst_v4l2_format) );
	if( vj_vloopback_set_format(v, dst_v4l2_w, dst_v4l2_h, dst_v4l2_format, tmp->stride[0] ) == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "vloop: Unable to set video format to %dx%d in %d", dst_v4l2_w,dst_v4l2_h, dst_v4l2_format );
		vj_vloopback_close(v);
		free(tmp);
		return NULL;
	}
	free(tmp);

	
	veejay_msg(VEEJAY_MSG_DEBUG, "vloop: video is %dx%d, @%d", dst_v4l2_w, dst_v4l2_h, dst_v4l2_format );

        sws_template tmpl;
        tmpl.flags = 1;

	v->src1 = yuv_yuv_template( NULL, NULL, NULL, src->width, src->height, src->format );  

	uint8_t *buf = (uint8_t*) vj_malloc( sizeof(uint8_t) * v->size );
	if(buf == NULL) {
		veejay_msg(VEEJAY_MSG_ERROR, "vloop: unable to allocate %ld bytes", v->size );
		vj_vloopback_close( v );
		return NULL;
	}

	v->out_buf = buf;
	
	uint8_t *planes[4];
	vj_vloopback_setup_ptrs( buf, planes, v4l2_pixelformat2ffmpeg( dst_v4l2_format ), dst_v4l2_w, dst_v4l2_h );

	v->dst1 = yuv_yuv_template( planes[0], planes[1], planes[2], dst_v4l2_w, dst_v4l2_h, v4l2_pixelformat2ffmpeg( dst_v4l2_format ) );
	if(v->dst1 == NULL ) {
		veejay_msg(0, "vloop: output frame configuration error");
		vj_vloopback_close( v );
		return NULL;
	}

	if( v->src1->width == dst_v4l2_w && v->src1->height == dst_v4l2_h &&
	   v4l2_ffmpeg2v4l2( v->src1->format ) == dst_v4l2_format )
		no_conv = 1;

	if( no_conv ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "vloop: direct write (no colorspace conversion)");
	}

	
	if( no_conv == 0 ) {
		v->scaler = yuv_init_swscaler( v->src1,v->dst1,&tmpl,yuv_sws_get_cpu_flags() );

		if( v->scaler == NULL ) {
			veejay_msg(0, "vloop: Unable to initialize video scaler %dx%d in %x to %dx%d in %x",	
				src->width,src->height,src->format,
				dst_v4l2_w, dst_v4l2_h,  v4l2_pixelformat2ffmpeg( dst_v4l2_format ) );
			vj_vloopback_close( v );
			return NULL;
		}
	}

	return (void*) v;
#else
	return NULL;
#endif
}

int	vj_vloopback_write( void *vloop  )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) 
		return 0;

	int res = write( v->fd, v->dst1->data[0],v->size );
	if( res < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to write to vloopback device: %s", strerror(errno));
		return 0;
	}
 
	if(res <= 0) 
		return 0;
	
	return 1;
}

int	vj_vloopback_fill_buffer( void *vloop, uint8_t **data )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) return 0;

	uint8_t *planes[4];

	planes[0] = data[0];
	if( v->swap )
	{
		planes[1] = data[2];
		planes[2] = data[1];
		planes[3] = data[3];
	}
	else
	{
		planes[1] = data[1];
		planes[2] = data[2];
		planes[3] = data[3];
	}

	if( v->scaler )
	{
		v->src1->data[0] = planes[0];
		v->src1->data[1] = planes[1];
		v->src1->data[2] = planes[2];
		v->src1->data[3] = planes[3];

		yuv_convert_and_scale( v->scaler, v->src1, v->dst1 );
	}
	else
	{
		veejay_memcpy( v->dst1->data[0], planes[0], v->dst1->len );
		if( v->dst1->data[1] )
		{
			veejay_memcpy( v->dst1->data[1], planes[1], v->dst1->uv_len );
			veejay_memcpy( v->dst1->data[2], planes[2], v->dst1->uv_len );
		}
	}

	return 1;
}

void	vj_vloopback_close( void *vloop )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(v)
	{
		if( v->scaler )
			yuv_free_swscaler( v->scaler );
		if( v->src1 )
			free(v->src1);
		if( v->dst1 )
			free(v->dst1);
		if( v->fd )
			close( v->fd );
		if( v->out_buf )
			free(v->out_buf);
		if( v->dev_name )
			free(v->dev_name);
		free(v);
	}
}

