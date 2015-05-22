/* veejay - Linux VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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


/* Changes:
 * Import patch by Xendarboh xendarboh@gmail.com to write to v4l2vloopback device
 *
 *
 *
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
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#ifdef HAVE_V4L
#include <linux/videodev.h>
#endif
#ifdef HAVE_V4L2
#include <linux/videodev2.h>
#endif
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libavutil/pixfmt.h>
#define VLOOPBACK_MMAP 0 	// commented out
#define VLOOPBACK_PIPE 1
#define VLOOPBACK_N_BUFS 2

typedef struct
{
	char *dev_name;		/* device name */
	int   palette;		/* palette from vjframe */
	int   width;
	int   height;
	int   norm;
	int   fd;
	int   size;		/* size of image out_buf */
	uint8_t *out_buf;
	uint8_t *out_map;	/* mmap segment */
	int   jfif;
	int	  vshift;
	int	  hshift;
	int	  iov;
	void	*scaler;
	VJFrame	*src1;
	VJFrame *dst1;
} vj_vloopback_t;

extern int      v4l2_pixelformat2ffmpeg( int pf );

/* Open the vloopback device */

void *vj_vloopback_open(const char *device_name, int norm, int mode,
		int w, int h, int pixel_format)
{
	void *ret = NULL;
	vj_vloopback_t *v = (vj_vloopback_t*) vj_malloc(sizeof(vj_vloopback_t));
	if(!v) return ret;

	memset(v , 0, sizeof(vj_vloopback_t ));

	v->fd = open( device_name, O_RDWR ); //, S_IRUSR|S_IWUSR );
	if( v->fd <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot open vloopback device '%s': %s", device_name, strerror(errno) );
		return ret;
	}

	v->norm = norm;
	v->width = w;
	v->height = h;

	switch(pixel_format) {
		case FMT_420:
		case FMT_420F:
#ifdef HAVE_V4L
			v->palette = VIDEO_PALETTE_YUV420P; 
#endif
#ifdef HAVE_V4L2
			v->palette = V4L2_PIX_FMT_YUV420;
#endif
			v->hshift = 1;
			v->vshift = 1;
			veejay_msg( VEEJAY_MSG_DEBUG, "Using V4L2_PIX_FMT_YUV420");
			break;
		case FMT_422:
		case FMT_422F:
#ifdef HAVE_V4L
			v->palette = VIDEO_PALETTE_YUV422P;
#endif
#ifdef HAVE_V4L2
			v->palette = V4L2_PIX_FMT_YUV422P;
#endif
			v->vshift = 1;
			veejay_msg(VEEJAY_MSG_DEBUG, "Using V4L2_PIX_FMT_YUV422P");
			break;
		default:
#ifdef HAVE_V4L
			v->palette = VIDEO_PALETTE_PLANAR; 
#endif
#ifdef HAVE_V4L2
			v->palette = V4L2_PIX_FMT_BGR24;
#endif
		
			veejay_msg(VEEJAY_MSG_DEBUG,"Using fallback format %x", v->palette );
			break;
	}

	if(pixel_format == FMT_420F || pixel_format == FMT_422F ) 
		v->jfif = 1;

	v->dev_name = strdup( device_name );

	ret = (void*) v;

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Vloopback %s size %d x %d, palette %d",
		v->dev_name,
		v->width,
		v->height,
		v->palette );

	return (void*) ret;
}
#define    ROUND_UP8(num)(((num)+8)&~8)

/* write mode*/
int	vj_vloopback_start_pipe( void *vloop )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;

	if(!v) return 0;

	int len = v->width * v->height;
	int uv_len = (v->width >> v->hshift ) * (v->height >> v->vshift);

	v->size = len + (2 * uv_len);

	char *dbg = getenv( "VEEJAY_VLOOPBACK_DEBUG" );
	if( dbg ) {
		v->iov = atoi(dbg);
		veejay_msg(VEEJAY_MSG_INFO,"vloop: debug level set to %d", v->iov );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_LOOPBACK_DEBUG=[0|1] not set");
	}

#ifdef HAVE_V4L
	struct video_capability caps;
	struct video_window	win;
	struct video_picture 	pic;

	memset( &win , 0 , sizeof(struct video_window));
	memset( &caps, 0 , sizeof(struct video_capability));
	memset( &pic, 0, sizeof(struct video_picture));
	

	/* the out_palette defines what format ! */

	/* get capabilities */
	if( ioctl( v->fd, VIDIOCGCAP, &caps ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant get video capabilities: %s", strerror(errno));
		return 0;
	}
	/* get picture */
	if( ioctl( v->fd, VIDIOCGPICT, &pic ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant get video picture: %s", strerror(errno));
		return 0;
	}
	/* set palette */
	pic.palette = v->palette;
	if( ioctl( v->fd, VIDIOCSPICT, &pic ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "Cant set video picture (palette %d)",v->palette);
		return 0;
	}
	/* set window */
	win.width  = v->width;
	win.height = v->height;
	if( ioctl( v->fd, VIDIOCSWIN, &win ) < 0 )
	{
		veejay_msg(VEEJAY_MSG_DEBUG ,"Cant set video window %d x %d",
			v->width,v->height );
		return 0;
	}

#endif
#ifdef HAVE_V4L2
	struct v4l2_capability caps;
	struct v4l2_format format;
	struct v4l2_format format1;
	
	memset(&caps,0,sizeof(caps));
	memset(&format,0,sizeof(format));
	memset(&format1,0,sizeof(format1));
	
	int res = ioctl( v->fd, VIDIOC_QUERYCAP, &caps );
	if( res < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot query video capabilities: %s", strerror(errno));
		return 0;
	}

	format1.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	res = ioctl( v->fd, VIDIOC_G_FMT, &format1 );

	veejay_msg(VEEJAY_MSG_DEBUG, "The capture device is currently configured in %dx%d@%x",
			format1.fmt.pix.width,format1.fmt.pix.height,format1.fmt.pix.pixelformat );


	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format.fmt.pix.width = v->width;
	format.fmt.pix.height= v->height;
	format.fmt.pix.pixelformat = v->palette;
	format.fmt.pix.field = V4L2_FIELD_NONE;
	format.fmt.pix.bytesperline = v->width;
	format.fmt.pix.colorspace = (v->jfif == 1 ? V4L2_COLORSPACE_JPEG : V4L2_COLORSPACE_SMPTE170M );
	
	res = ioctl( v->fd, VIDIOC_S_FMT, &format );
	if( res < 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING,"Cannot set preferred video format (%dx%d@%x/%x): %s",
				v->width,v->height,v->palette,v->jfif, strerror(errno) );
	}
	 
	res = ioctl( v->fd, VIDIOC_G_FMT, &format );	
		
	if( format.fmt.pix.width == 0 || format.fmt.pix.height == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid video capture resolution %dx%d. Use v4l2loopback-ctl and setup capabilities.", 
			format.fmt.pix.width, format.fmt.pix.height );	
		return 0;
	}
	

	if( format.fmt.pix.pixelformat != v->palette ||
		format.fmt.pix.width != v->width || format.fmt.pix.height != v->height ) {

		int	cap_palette = v4l2_pixelformat2ffmpeg( format.fmt.pix.pixelformat );
		int	src_palette = v4l2_pixelformat2ffmpeg( v->palette );
		
		veejay_msg(VEEJAY_MSG_WARNING,
				"Capture device cannot handle native format, using converter for %dx%d in %d",format.fmt.pix.width, format.fmt.pix.height, format.fmt.pix.pixelformat);
                sws_template tmpl;
                tmpl.flags = 1;
		v->dst1   = yuv_yuv_template( NULL,NULL,NULL, format.fmt.pix.width,
				format.fmt.pix.height, cap_palette );
		v->src1   = yuv_yuv_template( NULL, NULL, NULL, v->width, v->height,
				src_palette );    
				
                v->scaler = yuv_init_swscaler( v->src1,v->dst1,&tmpl,yuv_sws_get_cpu_flags() );
		
		v->size = format.fmt.pix.sizeimage;
	}

	veejay_msg(VEEJAY_MSG_DEBUG,
		"Configured vloopback device: %d x %d in palette %x, buffer is %d bytes.",
		format.fmt.pix.width,format.fmt.pix.height,
		format.fmt.pix.pixelformat,
		format.fmt.pix.sizeimage );	
#endif


	long sze = ROUND_UP8( v->size );
	
	v->out_buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * sze );

	if(!v->out_buf)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot allocate sufficient memory for vloopback");
		return 0;
	}

	veejay_memset(v->out_buf, 0 , sze );


	return 1;
}

int	vj_vloopback_write_pipe( void *vloop  )
{
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) return 0;
	int res = write( v->fd, v->out_buf, v->size );
	if( res < 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to write to vloopback device: %s", strerror(errno));
		return 0;
	} 
	if( v->iov && res >= 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "vloop: written %d/%d bytes.",res,v->size );
	}
	if(res <= 0)
		return 0;
	return 1;
}

int	vj_vloopback_fill_buffer( void *vloop, uint8_t **frame )
{
	// write frame to v->out_buf (veejay_memcpy)
	vj_vloopback_t *v = (vj_vloopback_t*) vloop;
	if(!v) return 0;

	int len = v->width * v->height ;
	int uv_len = (v->width >> v->hshift ) * (v->height >> v->vshift);

	// copy data to linear buffer */	
	if( v->scaler ) {
		uint8_t *p[3] = { NULL, NULL, NULL };
		v->src1->data[0] = frame[0];
		v->src1->data[1] = frame[1];
		v->src1->data[2] = frame[2];
		switch( v->dst1->format ) {
			case PIX_FMT_YUVJ444P:
			case PIX_FMT_YUV444P:
				p[1] = v->out_buf + len; p[2] = v->out_buf + (2*len); break;
			case PIX_FMT_YUVJ422P:
			case PIX_FMT_YUV422P:
				p[1] = v->out_buf + len; p[2] = v->out_buf + len + uv_len; break;	
			case PIX_FMT_YUV420P:
				p[1] = v->out_buf + len; p[2] = v->out_buf + len + (len/4); break;
			default:				
				p[0] = v->out_buf; break;
		}
		v->dst1->data[0] = p[0];
		v->dst1->data[1] = p[1];
		v->dst1->data[2] = p[2];

		yuv_convert_and_scale( v->scaler, v->src1,v->dst1 );
	}
	else {
	
		veejay_memcpy( v->out_buf, frame[0], len );

		veejay_memcpy( v->out_buf + len,
					frame[1], uv_len );
		veejay_memcpy( v->out_buf + len + uv_len,
					frame[2], uv_len );
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
	
		if(v->fd)
			close( v->fd );
		if(v->out_buf)
			free(v->out_buf);
		free(v);
	}
}	
