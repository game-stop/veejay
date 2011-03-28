/* 
 * Linux VeeJay
 *
 * Copyright(C)2010-2011 Niels Elburg <nwelburg@gmail.com / niels@dyne.org >
 *             - re-use Freej's v4l2 cam driver
 *             - implemented controls method and image format negotiation  
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
 */

/* FreeJ
* (c) Copyright 2010 Denis Roio <jaromil@dyne.org>
*
* This source code is free software; you can redistribute it and/or
* modify it under the terms of the GNU Public License as published
* by the Free Software Foundation; either version of the License,
* or (at your option) any later version.
*
* This source code is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* Please refer to the GNU Public License for more details.
*
* You should have received a copy of the GNU Public License along with
* this source code; if not, write to:
* Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*/

#include <config.h>
#ifdef HAVE_V4L2
#include <stdio.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <libvje/vje.h>
#include <libavutil/pixfmt.h>
#include <libvevo/libvevo.h>
#include <libstream/v4l2utils.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>

typedef struct {
		void *start;
		size_t length;
} bufs;

typedef struct
{
	int fd; 
	int buftype;
	
	struct v4l2_capability capability;
	struct v4l2_input input; 
	struct v4l2_standard standard;
	struct v4l2_format format; 
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer buffer;
	struct v4l2_jpegcompression compr; //@ need this for eyetoy toys

	bufs *buffers;

	VJFrame *info;
	void	*scaler;
	int		planes[4];

	int		composite;

	VJFrame	*dst;
} v4l2info;

static	int	vioctl( int fd, int request, void *arg )
{
	int ret;
	do {	
		ret = ioctl( fd, request, arg );
	}
	while( ret == -1 && EINTR == errno );
	return ret;
}

static int	v4l2_pixelformat2ffmpeg( int pf )
{
	switch(pf) {
		case V4L2_PIX_FMT_RGB24:
			return PIX_FMT_RGB24;
		case V4L2_PIX_FMT_BGR24:
			return PIX_FMT_BGR24;
		case V4L2_PIX_FMT_RGB32:
			return PIX_FMT_RGBA;
		case V4L2_PIX_FMT_BGR32:
			return PIX_FMT_BGRA;
		case V4L2_PIX_FMT_YUYV:
			return PIX_FMT_YUYV422;
		case V4L2_PIX_FMT_UYVY:
			return PIX_FMT_UYVY422;
		case V4L2_PIX_FMT_YUV422P:
			return PIX_FMT_YUV422P;
		case V4L2_PIX_FMT_YUV420:
			return PIX_FMT_YUV420P;
		case V4L2_PIX_FMT_YUV32:
			return PIX_FMT_YUV444P;
		default:
			break;
		}
	return -1;
}
static	int	v4l2_ffmpeg2v4l2( int pf)
{
	switch(pf) {
		case PIX_FMT_RGB24:
			return V4L2_PIX_FMT_RGB24;
		case PIX_FMT_BGR24:
			return V4L2_PIX_FMT_BGR24;
		case PIX_FMT_BGR32:
			return V4L2_PIX_FMT_BGR24;
		case PIX_FMT_RGB32:
			return V4L2_PIX_FMT_RGB24;
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			return V4L2_PIX_FMT_YUV420;
		case PIX_FMT_YUV422P:
			return V4L2_PIX_FMT_YUV422P;

		case PIX_FMT_YUVJ444P:
		case PIX_FMT_YUV444P:
		//	return V4L2_PIX_FMT_YUV32;


		default:
			return V4L2_PIX_FMT_BGR24;
	}
	return V4L2_PIX_FMT_BGR24;
}

static	int	v4l2_is_fixed_format( int fmt ) {
	switch(fmt) {
		case V4L2_PIX_FMT_MJPEG:
		case V4L2_PIX_FMT_JPEG:
			return 0;
	}
	return 1;
}

static	int	v4l2_compressed_format(v4l2info *v) //@untested
{
	/*memset( v->compr , 0, sizeof(v->compr ));
	if( -1 == vioctl( v->fd, VIDIOC_G_JPEGCOMP, &(v->compr)) ) {
		return -1;
	}

	v->compr.quality = 0;
	if( -1 == vioctl( v->fd, VIDIOC_S_JPEGCOMP, &(v->compr)) ) {
		return -1;
	}*/
	return 1;
}

static	int	v4l2_set_framerate( v4l2info *v , float fps ) //@untested
{
	struct v4l2_streamparm sfps;
	memset(&sfps,0,sizeof(sfps));
	sfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sfps.parm.capture.timeperframe.numerator=1;
	sfps.parm.capture.timeperframe.denominator=(int) fps;

	if( -1 == vioctl( v->fd, VIDIOC_S_PARM,&sfps ) )
		return -1;
	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: framerate set to %2.2f",fps );
	return 1;
}


static int	v4l2_enum_video_standards( v4l2info *v, char norm )
{
	struct v4l2_input input;
	struct v4l2_standard standard;

	memset( &input, 0,sizeof(input));
	if( -1 == vioctl( v->fd, VIDIOC_G_INPUT, &input.index )) {
		return 0;
	}

	if( -1 == vioctl( v->fd, VIDIOC_ENUMINPUT, &input )) {
		return 0;
	}

/*	memset( &standard, 0,sizeof(standard));
	standard.index = 0;

	while( 0 == vioctl( v->fd, VIDIOC_ENUMSTD, &standard )) {
		if( standard.id & input.std ) {
			veejay_msg(VEEJAY_MSG_INFO, "v4l2: Current selected video standard is %s", standard.name );
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Device supports video standard %s", standard.name );
		}
		if( norm == 'p' && (input.std & V4L2_STD_PAL)) {
			veejay_msg(VEEJAY_MSG_INFO, "v4l2: Current selected video standard is %s", standard.name );
			break;
		}	
		if( norm == 'n' && (input.std && V4L2_STD_NTSC)) {
			veejay_msg(VEEJAY_MSG_INFO, "v4l2: Current selected video standard is %s", standard.name );
			break;
		}
		standard.index ++;
	}

	if( standard.index == 0 )
		return 0;*/
	int std_id = (norm == 'p' ? V4L2_STD_PAL: V4L2_STD_NTSC);

	if (-1 == ioctl (v->fd, VIDIOC_S_STD, &std_id)) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: unable to set video standard.");
		return 0;
	} else {
		veejay_msg(VEEJAY_MSG_INFO,"v4l2: set video standard PAL");
	}	

	return 1;

}

static	void	v4l2_enum_frame_sizes( v4l2info *v )
{
	struct v4l2_fmtdesc fmtdesc;
	const char *buf_types[] = { "Video Capture" , "Video Output", "Video Overlay" };
	const char *flags[] = { "uncompressed", "compressed" };
	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: discovering supported video formats");

	for( fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		 fmtdesc.type < V4L2_BUF_TYPE_VIDEO_OVERLAY;
		 fmtdesc.type ++ ) {
		while( vioctl( v->fd, VIDIOC_ENUM_FMT, &fmtdesc ) == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: Enumerate (%d,%s)", fmtdesc.index, buf_types[ fmtdesc.type ] );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tindex:%d", fmtdesc.index );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tflags:%s", flags[ fmtdesc.type ] );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tdescription:%s", fmtdesc.description );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tpixelformat:%c%c%c%c",
						fmtdesc.pixelformat & 0xff,
						(fmtdesc.pixelformat >> 8 ) & 0xff,
						(fmtdesc.pixelformat >> 16) & 0xff,
						(fmtdesc.pixelformat >> 24) & 0xff );
			fmtdesc.index ++;
		}

	}
}

static	int	v4l2_try_palette( v4l2info *v , int v4l2_pixfmt )
{
	struct v4l2_format format;
	
	memset(&format,0,sizeof(format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat = v4l2_pixfmt;
	format.fmt.pix.field	   = V4L2_FIELD_ANY;

	int res =vioctl( v->fd, VIDIOC_TRY_FMT, &format );
    if(res == 0 ) {
		return 1;
	} else if (res == -1 ) {
		res = vioctl( v->fd, VIDIOC_S_FMT, &format );
		if( res == 0 )
			return 1;
	}
	return 0;
}	


static	int	v4l2_try_pix_format( v4l2info *v, int pixelformat, int wid, int hei )
{
	struct v4l2_format format;
	
	memset(&format,0,sizeof(format));
	v->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if( -1 == vioctl( v->fd, VIDIOC_G_FMT, &(v->format)) ) {
		veejay_msg(0, "v4l2: VIDIOC_G_FMT failed with %s", strerror(errno));
		return -1;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: current configuration is in %s (%dx%d)",
			(char*) &v->format.fmt.pix.pixelformat,
			v->format.fmt.pix.width,
			v->format.fmt.pix.height );

	//@ try to set preferences
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.width = wid;
	format.fmt.pix.height= hei;
	format.fmt.pix.field = V4L2_FIELD_ANY;

	int ffmpeg_pixelformat = get_ffmpeg_pixfmt(pixelformat);
	int v4l2_pixel_format = v4l2_ffmpeg2v4l2( ffmpeg_pixelformat );
	format.fmt.pix.pixelformat = v4l2_pixel_format;
	
	if( vioctl( v->fd, VIDIOC_TRY_FMT, &format ) != -1 ) {

		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: testing palette %4.4s (%dx%d)",
				(char*)&format.fmt.pix.pixelformat,
				format.fmt.pix.width,
				format.fmt.pix.height );

		if( format.fmt.pix.width != wid || format.fmt.pix.height != hei ) {
			veejay_msg(VEEJAY_MSG_WARNING,"v4l2: adjusting resolution from %dx%d to %dx%d",
				wid,hei,
				format.fmt.pix.width,
				format.fmt.pix.height	);
		}

		if( format.fmt.pix.pixelformat != v4l2_pixel_format ) {
			int pf = v4l2_pixelformat2ffmpeg( format.fmt.pix.pixelformat );
			if( pf == -1) {
				veejay_msg(VEEJAY_MSG_ERROR, "No support for palette %4.4s",
						(char*) &format.fmt.pix.pixelformat);
				return -1;
			}

			veejay_msg(VEEJAY_MSG_WARNING,"v4l2: adjusting palette from %d to %d",
					ffmpeg_pixelformat ,
					v4l2_pixelformat2ffmpeg(format.fmt.pix.pixelformat)
					);
		}

		if( vioctl( v->fd, VIDIOC_S_FMT, &format ) == -1 ) {
			veejay_msg(0,"v4l2: negotation of data fails with %s", strerror(errno));
			return -1;
		}
	
	} else {
		if( vioctl( v->fd, VIDIOC_S_FMT, &format ) == -1 ) {
			veejay_msg(0,"v4l2: negotation of data fails with %s", strerror(errno));
			return -1;
		}

	}
	
	veejay_msg(VEEJAY_MSG_INFO,"v4l2: using palette %4.4s (%dx%d)",
		(char*) &format.fmt.pix.pixelformat,
		format.fmt.pix.width,
		format.fmt.pix.height
	);

	v->info = yuv_yuv_template( NULL,NULL,NULL,format.fmt.pix.width, format.fmt.pix.height,
					v4l2_pixelformat2ffmpeg( format.fmt.pix.pixelformat ) );
	yuv_plane_sizes( v->info, &(v->planes[0]),&(v->planes[1]),&(v->planes[2]),&(v->planes[3]) );

	return 1;
}

static void	v4l2_set_output_dimensions( v4l2info *v, void *src )
{
	uint8_t *map = (uint8_t*) src;
	if( v->planes[0] > 0 ) {
		v->info->data[0] = map;
	}
	if( v->planes[1] > 0 ) {
		v->info->data[1] = map + v->planes[0];
	}
	if( v->planes[2] > 0 ) {
		v->info->data[2] = map + v->planes[1] + v->planes[0];
	}
	if( v->planes[3] > 0) {
		v->info->data[3] = map + v->planes[0] + v->planes[1] + v->planes[2];
	}
}

VJFrame	*v4l2_get_dst( void *vv, uint8_t *Y, uint8_t *U, uint8_t *V ) {
	v4l2info *v = (v4l2info*) vv;
	v->dst->data[0] = Y;
	v->dst->data[1] = U;
	v->dst->data[2] = V;
	return v->dst;
}

void *v4l2open ( const char *file, const int input_channel, int host_fmt, int wid, int hei, float fps, char norm  )
{

	int fd = open( file, O_RDWR | O_NONBLOCK );
	if( fd < 0 ) {
		veejay_msg(0, "v4l2: unable to open capture device %s",file);
		return NULL;
	} else {
		close(fd);
		fd = open( file , O_RDWR );
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Video4Linux2 device opened: %s", file );

	v4l2info *v = (v4l2info*) vj_calloc(sizeof(v4l2info));

	v->fd = fd;
	v->dst = yuv_yuv_template( NULL,NULL,NULL,wid,hei,host_fmt );
	if( -1 == vioctl( fd, VIDIOC_QUERYCAP, &(v->capability) ) ) {
		veejay_msg(0, "v4l2: VIDIOC_QUERYCAP failed with %s",			strerror(errno));
		free(v);
		close(fd);
		return NULL;
	}

	if( (v->capability.capabilities & V4L2_CAP_VIDEO_CAPTURE ) == 0 ) {
		veejay_msg(0, "v4l2: %s does not support video capture!", 	v->capability.card );
		close(fd);
		free(v);
		return NULL;
	}

	if( (v->capability.capabilities & V4L2_CAP_STREAMING ) == 0 ) {
		veejay_msg(0, "v4l2: %s does not support streaming capture", v->capability.card );
		close(fd);
		free(v);
		return NULL;
	}


	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture driver: %s",
			v->capability.driver );
	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture card: %s",
			v->capability.card );

	//@ which video input ?
	int chan = input_channel;
	if( -1 == vioctl( fd, VIDIOC_S_INPUT, &chan )) {
		veejay_msg(0, "v4l2: VIDIOC_S_INPUT failed with %s", strerror(errno));
		free(v);
		close(fd);
		return NULL;
	}

	v->input.index = chan;
	if( -1 == vioctl( fd, VIDIOC_ENUMINPUT, &(v->input)) ) {
		veejay_msg(0, "v4l2: VIDIOC_ENUMINPUT failed with %s", strerror(errno));
		free(v);
		close(fd);
		return NULL;
	}


	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: video channel %d '%s'",
			chan, v->input.name );

	v4l2_enum_video_standards( v, norm );
	v4l2_enum_frame_sizes(v);


/*	if( v4l2_try_palette(v, V4L2_PIX_FMT_MJPEG ) ) {
		
	} else if( v4l2_try_palette( v, V4L2_PIX_FMT_JPEG) ) {

	}

	if( compr ) {
		v4l2_compressed_format( v );
	}
	else {
*/		if( v4l2_try_pix_format( v, host_fmt, wid, hei ) < 0 ) {
			free(v);
			close(fd);
			return NULL;
		}
//	}

	if( v4l2_set_framerate( v, fps ) == -1 ) {
		veejay_msg(0, "v4l2: failed to set frame rate to %2.2f", fps );
	}

	v->reqbuf.type 	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	v->reqbuf.memory= V4L2_MEMORY_MMAP;
	v->reqbuf.count = 32;

	if( -1 == vioctl( fd, VIDIOC_REQBUFS, &(v->reqbuf)) ) {
		if( errno == EINVAL ) {
			veejay_msg(0,"v4l2: No support for mmap streaming ?!");
		} else {
			veejay_msg(0,"v4l2: VIDIOC_REQBUFS failed with %s", strerror(errno));
		}
		close(fd);
		free(v);
		return NULL;
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Card supports %d buffers", v->reqbuf.count );
	v->buffers = (bufs*) calloc( v->reqbuf.count, sizeof(*v->buffers));

	int i;
	for( i = 0; i < v->reqbuf.count; i ++ ) {
		memset( &(v->buffer), 0, sizeof(v->buffer));
		v->buffer.type 	= v->reqbuf.type;
		v->buffer.memory= V4L2_MEMORY_MMAP;
		v->buffer.index = i;

		if( -1 == vioctl( fd, VIDIOC_QUERYBUF, &(v->buffer)) ) {
			veejay_msg(0, "v4l2: VIDIOC_QUERYBUF failed with %s",strerror(errno));
			free(v->buffers);
			free(v);
			close(fd);
			return NULL;
		}

		v->buffers[i].length = v->buffer.length;
		v->buffers[i].start  = mmap( NULL,
									 v->buffer.length,
									 PROT_READ | PROT_WRITE,
									 MAP_SHARED,
									 fd,
									 v->buffer.m.offset );
		
		if( MAP_FAILED == v->buffers[i].start ) {
		//	int k;
		//	for( k = 0; k < i; k ++ ) 
		//		munmap( v->buffer[k].start, v->buffer[k].length );
	
			free(v->buffers);
			free(v);
			close(fd);
			return NULL;
		}

	}

	for( i = 0; i < v->reqbuf.count ; i ++ ) {
		veejay_memset( &(v->buffer),0,sizeof(v->buffer));
		v->buffer.type = v->reqbuf.type;
		v->buffer.memory=V4L2_MEMORY_MMAP;
		v->buffer.index = i;

		if( -1 == vioctl( fd, VIDIOC_QBUF, &(v->buffer)) ) {
			veejay_msg(0, "v4l2: first VIDIOC_QBUF failed with %s", strerror(errno));
		//	int k;
		//	for( k = 0; k < v->reqbuf.count; k ++ ) 
		//		munmap( v->buffer[k].start, v->buffer[k].length );
	
			free(v->buffers);
			free(v);
			close(fd);
			return NULL;
		}

	}

	if( -1 == vioctl( fd, VIDIOC_STREAMON, &(v->buftype)) ) {
		veejay_msg(0, "v4l2: VIDIOC_STREAMON failed with %s", strerror(errno));
	//	int k;
	//		for( k = 0; k < v->reqbuf.count; k ++ ) 
	//			munmap( v->buffer[k].start );
	
			free(v->buffers);
			free(v);
			close(fd);
			return NULL;
	}

	v->fd = fd;

	return v;
}

static	double	calc_tc( struct v4l2_timecode *tc, float fps )
{
#ifdef STRICT_CHECKING
	assert( fps > 0.0 );
#endif
	return (double) tc->frames / fps;
}

int		v4l2_pull_frame(void *vv,VJFrame *dst) {
	v4l2info *v = (v4l2info*) vv;
	if( v->scaler == NULL ) {
		sws_template templ;     
   		memset(&templ,0,sizeof(sws_template));
   		templ.flags = yuv_which_scaler();
		v->scaler = yuv_init_swscaler( v->info,dst, &templ, yuv_sws_get_cpu_flags() );
	}
	if( -1 == vioctl( v->fd, VIDIOC_DQBUF, &(v->buffer))) {
		veejay_msg(0, "v4l2: VIDIOC_DQBUF: %s", strerror(errno));
		return 0;
	}

	void *src = v->buffers[ v->buffer.index ].start;
	v4l2_set_output_dimensions( v, src );
	yuv_convert_and_scale( v->scaler, v->info, dst );
	
	if( -1 == vioctl( v->fd, VIDIOC_QBUF, &(v->buffer))) {
		veejay_msg(0, "v4l2: VIDIOC_QBUF failed with %s", strerror(errno));
		}

	return 1;
}

void	v4l2_close( void *d )
{
	v4l2info *v = (v4l2info*) d;
	int i;

	if( -1 == vioctl( v->fd, VIDIOC_STREAMOFF, &(v->buftype)) ) {
		veejay_msg(0, "v4l2: VIDIOC_STREAMOFF failed with %s", strerror(errno));
	}

	for( i = 0; i < v->reqbuf.count; i ++ ) {
		munmap( v->buffers[i].start, v->buffers[i].length );
	}

	close(v->fd);
	if( v->scaler )
		yuv_free_swscaler( v->scaler );
	if(v->dst)
		free(v->dst);

}


// hue
void	v4l2_set_hue( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_HUE, value );
}
int32_t	v4l2_get_hue( void *d ) {
	return v4l2_get_control(d, V4L2_CID_HUE );
}


// contrast
void	v4l2_set_contrast( void *d,int32_t value ) {
	v4l2_set_control( d, V4L2_CID_CONTRAST, value );
}
int32_t v4l2_get_contrast( void *d ) {
	return v4l2_get_control(d, V4L2_CID_CONTRAST );
}

void	v4l2_set_input_channel( void *d, int num )
{
	v4l2info *v = (v4l2info*) d;
	ioctl( v->fd, VIDIOC_S_INPUT, &num );
}

void	v4l2_set_composite_status( void *d, int status)
{
	v4l2info *v = (v4l2info*) d;
	v->composite = status;
}

int		v4l2_get_composite_status( void *d )
{
	v4l2info *v = (v4l2info*) d;
	return v->composite;
}
// brightness
void	v4l2_set_brightness( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_BRIGHTNESS, value );
}
int32_t v4l2_get_brightness( void *d ) {
	return v4l2_get_control( d, V4L2_CID_BRIGHTNESS );
}

// saturation
void	v4l2_set_saturation( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_SATURATION, value );
}
int32_t	v4l2_get_saturation( void *d ) {
	return v4l2_get_control( d, V4L2_CID_SATURATION );
}


// gamma
void	v4l2_set_gamma( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_GAMMA, value );
}
int32_t v4l2_get_gamma( void *d ) {
	return v4l2_get_control( d, V4L2_CID_GAMMA );
}


// sharpness
void	v4l2_set_sharpness( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_SHARPNESS , value );
}
int32_t v4l2_get_sharpness( void *d ) {
	return v4l2_get_control( d, V4L2_CID_SHARPNESS );
}

// gain
void	v4l2_set_gain( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_GAIN, value );
}
int32_t v4l2_get_gain( void *d ) {
	return v4l2_get_control( d, V4L2_CID_GAIN );
}

// red balance
void	v4l2_set_red_balance( void *d,int32_t value ) {
	v4l2_set_control( d, V4L2_CID_RED_BALANCE, value );
}
int32_t v4l2_get_red_balance( void *d ) {
	return v4l2_get_control( d, V4L2_CID_RED_BALANCE );
}

// auto white balance
void	v4l2_set_auto_white_balance( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_AUTO_WHITE_BALANCE , value );
}
int32_t  v4l2_get_auto_white_balance( void *d ) {
	return v4l2_get_control( d, V4L2_CID_AUTO_WHITE_BALANCE );
}

// blue balance
void	v4l2_set_blue_balance( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_BLUE_BALANCE, value );
}
int32_t	v4l2_get_blue_balance( void *d ) {
	return v4l2_get_control( d, V4L2_CID_BLUE_BALANCE );
}

// backlight compensation
void 	v4l2_set_backlight_compensation( void *d,int32_t value ) {
	v4l2_set_control( d, V4L2_CID_BACKLIGHT_COMPENSATION, value );
}
int32_t v4l2_get_backlight_compensation( void *d ) {
	return v4l2_get_control( d, V4L2_CID_BACKLIGHT_COMPENSATION );
}

// auto gain
void	v4l2_set_autogain( void *d,int32_t value ) {	
	v4l2_set_control( d, V4L2_CID_AUTOGAIN , value );
}
int32_t v4l2_get_autogain( void *d ) {
	return v4l2_get_control( d, V4L2_CID_AUTOGAIN );
}

// auto hue
void	v4l2_set_hue_auto( void *d,int32_t value ) {
	v4l2_set_control( d, V4L2_CID_HUE_AUTO , value );
}
int32_t	v4l2_get_hue_auto( void *d ) {
	return v4l2_get_control( d, V4L2_CID_HUE_AUTO );
}

// hflip
void	v4l2_set_hflip( void *d,int32_t value ) {
	v4l2_set_control(d, V4L2_CID_HFLIP, value );
}
int32_t v4l2_get_hflip( void *d ) {
	return v4l2_get_control( d, V4L2_CID_HFLIP );
}

// white balance temperature
void	v4l2_set_temperature( void *d,int32_t value ) {
	v4l2_set_control(d, V4L2_CID_WHITE_BALANCE_TEMPERATURE,value );
}
int32_t	v4l2_get_temperature( void *d ) {
	return v4l2_get_control( d, V4L2_CID_WHITE_BALANCE_TEMPERATURE );
}

// exposure
void	v4l2_set_exposure( void *d ,int32_t value )
{
	v4l2_set_control( d, V4L2_CID_EXPOSURE, value );
} 

int32_t v4l2_get_exposure( void *d ) {
	return v4l2_get_control( d, V4L2_CID_EXPOSURE );
}

static	struct
{
	uint32_t id;
	const char *key;
	const int  atom;
	void (*set_func)(void *ctx, int32_t value);
	int32_t (*get_func)(void *ctx);
} property_list[] = {
 {	V4L2_CID_BRIGHTNESS,		"brightness",	VEVO_ATOM_TYPE_INT, v4l2_set_brightness, 	v4l2_get_brightness },
 {	V4L2_CID_CONTRAST,			"contrast",		VEVO_ATOM_TYPE_INT, v4l2_set_contrast,	 	v4l2_get_contrast },
 {	V4L2_CID_SATURATION,		"saturation",	VEVO_ATOM_TYPE_INT, v4l2_set_saturation, 	v4l2_get_saturation },
 {	V4L2_CID_HUE,				"hue",			VEVO_ATOM_TYPE_INT, v4l2_set_hue,			v4l2_get_hue },
 {	V4L2_CID_GAIN,				"gain",			VEVO_ATOM_TYPE_INT,	v4l2_set_gain,			v4l2_get_gain },
 {	V4L2_CID_GAMMA,				"gamma",		VEVO_ATOM_TYPE_INT, v4l2_set_gamma,			v4l2_get_gamma },
 {	V4L2_CID_RED_BALANCE,		"red_balance",	VEVO_ATOM_TYPE_INT, v4l2_set_red_balance, 	v4l2_get_red_balance },
 {	V4L2_CID_AUTO_WHITE_BALANCE,"auto_white",	VEVO_ATOM_TYPE_BOOL, v4l2_set_auto_white_balance, v4l2_get_auto_white_balance },
 {	V4L2_CID_BLUE_BALANCE,		"blue_balance",	VEVO_ATOM_TYPE_INT, v4l2_set_blue_balance, 	v4l2_get_blue_balance },
 {	V4L2_CID_SHARPNESS,			"sharpness",	VEVO_ATOM_TYPE_INT, v4l2_set_sharpness, 	v4l2_get_sharpness },
 {	V4L2_CID_BACKLIGHT_COMPENSATION,"bl_compensate",	VEVO_ATOM_TYPE_INT, v4l2_set_backlight_compensation, v4l2_get_backlight_compensation },
 {	V4L2_CID_AUTOGAIN,			"auto_gain",	VEVO_ATOM_TYPE_BOOL, v4l2_set_autogain, 	v4l2_get_autogain },
 {	V4L2_CID_HUE_AUTO,			"auto_hue",		VEVO_ATOM_TYPE_BOOL, v4l2_set_hue_auto, 	v4l2_get_hue_auto },
 {	V4L2_CID_WHITE_BALANCE_TEMPERATURE, "temperature", VEVO_ATOM_TYPE_INT, v4l2_set_temperature, v4l2_get_temperature },
 {	V4L2_CID_HFLIP,				"fliph",		VEVO_ATOM_TYPE_BOOL, v4l2_set_hflip, 		v4l2_get_hflip },
 {	V4L2_CID_EXPOSURE,			"exposure",		VEVO_ATOM_TYPE_INT,  v4l2_set_exposure,		v4l2_get_exposure },
 {	V4L2_CID_BASE,				NULL,			-1 }
};

const char* 			v4l2_get_property_name( const int id ) {
	int i;
	for( i = 0; property_list[i].key != NULL; i ++ ) {
		if( property_list[i].id == id ) 
			return property_list[i].key;
	}
	return NULL;
}

static	int		find_property_id( const uint32_t id ) {
	int i;
	for( i = 0; property_list[i].key != NULL; i ++ ) {
		if( property_list[i].id == id ) 
			return i;
	}
	return -1;
}

void	v4l2_get_controls( void *d, void *port )
{
	v4l2info *v = (v4l2info*) d;


	struct v4l2_queryctrl queryctrl;
	struct v4l2_querymenu querymenu;
	int err;
	memset( &querymenu, 0,sizeof(querymenu));
	
	uint32_t ctrl_id;

	for(ctrl_id = V4L2_CID_BASE;
		ctrl_id < V4L2_CID_LASTP1;
		ctrl_id ++ ) {

		memset( &queryctrl, 0, sizeof(queryctrl));
		queryctrl.id = ctrl_id;

		if( 0 == vioctl( v->fd, VIDIOC_QUERYCTRL, &queryctrl ) ) {
			if( queryctrl.flags & V4L2_CTRL_FLAG_DISABLED )
				continue;

			int id = find_property_id( queryctrl.id );
			if( id != -1 ) {
				
				err = vevo_property_set_f( port, 
										   property_list[id].key, 
										   VEVO_ATOM_TYPE_FUNCPTR,
										   1,
										   property_list[id].set_func,
									       property_list[id].get_func );					   

				if( err != VEVO_NO_ERROR ) {
					veejay_msg(0, "v4l2: error code %d while storing %s", err, property_list[id].key );
				}
				else {
					veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: found control '%s'", property_list[id].key);
				}
				
			}
		} else if ( errno == EINVAL )
			continue;
	}
}

int32_t	v4l2_get_control( void *d, int32_t type )
{
	v4l2info *v = (v4l2info*) d;
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0,sizeof(queryctrl));
	memset( &control,0,sizeof(control));
	
	queryctrl.id = type;

	if( -1 == vioctl( v->fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if( errno != EINVAL ) {
			return 0;
		}
	} else if ( queryctrl.flags & V4L2_CTRL_FLAG_DISABLED ) {
		veejay_msg( VEEJAY_MSG_DEBUG, "v4l2: property type %x not supported",type );
		return 0;
	} else {
		control.id = type;
		if( -1 == vioctl( v->fd, VIDIOC_G_CTRL, &control )) {
			veejay_msg(VEEJAY_MSG_ERROR, "v4l2: error getting property %x reason: %s", type, strerror(errno) );
		}
	}
	return control.value;
}


void	v4l2_set_control( void *d, int32_t type,  int32_t value )
{
	v4l2info *v = (v4l2info*) d;
	struct v4l2_queryctrl queryctrl;
	struct v4l2_control control;

	memset(&queryctrl, 0,sizeof(queryctrl));
	queryctrl.id = type;

	if( -1 == vioctl( v->fd, VIDIOC_QUERYCTRL, &queryctrl)) {
		if( errno != EINVAL ) {
				return;
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: property type %x not supported",type );
		}
	} else if ( queryctrl.flags & V4L2_CTRL_FLAG_DISABLED ) {
		veejay_msg( VEEJAY_MSG_DEBUG, "v4l2: property type %x not supported (disabld)",type );
	} else {
		memset( &control,0,sizeof(control));
		control.id = type;
		control.value = value;
		if (value > 0 ) {
			float s1 = value / 65535.0f;
			int32_t s2 = queryctrl.maximum * s1;
			control.value = s2;
		}

		if( value == -1 ) {
			veejay_msg(VEEJAY_MSG_INFO,"v4l2: changed property type %d to default value %d", type, queryctrl.default_value );
			control.value = queryctrl.default_value;
		}
		if( -1 == vioctl( v->fd, VIDIOC_S_CTRL, &control )) {
			veejay_msg(VEEJAY_MSG_ERROR, "v4l2: error setting property %x to %d, reason: %s", type, control.value,strerror(errno) );
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: changed property type %d to value %d", type, control.value );
		}
	}
}


int		v4l2_set_roi( void *d, int w, int h, int x, int y )
{
	v4l2info *v = (v4l2info*)d;

	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;

	memset (&cropcap, 0, sizeof (cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

	if (-1 == vioctl (v->fd, VIDIOC_CROPCAP, &cropcap)) {
		return 0;
	}

	memset (&crop, 0, sizeof (crop));
	crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	crop.c = cropcap.defrect;

	crop.c.width = w;
	crop.c.height = h;
	crop.c.left = x;
	crop.c.top = y;

	if (-1 == vioctl (v->fd, VIDIOC_S_CROP, &crop) )
		return 0;

	return 1;
}

int		v4l2_reset_roi( void *d )
{
	v4l2info *v = (v4l2info*)d;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;

	memset (&cropcap, 0, sizeof (cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == vioctl (v->fd, VIDIOC_CROPCAP, &cropcap)) {
		return 0;	
	}

	memset (&crop, 0, sizeof (crop));
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c = cropcap.defrect; 

	if (-1 == vioctl (v->fd, VIDIOC_S_CROP, &crop))
		return 0;

	return 1;
}

int		v4l2_get_currentscaling_factor_and_pixel_aspect(void *d)
{
	v4l2info *v = (v4l2info*)d;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format format;
	double hscale, vscale;
	double aspect;
	int dwidth, dheight;

	memset (&cropcap, 0, sizeof (cropcap));
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == vioctl (v->fd, VIDIOC_CROPCAP, &cropcap)) {
		return 0;
	}

	memset (&crop, 0, sizeof (crop));
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == vioctl (v->fd, VIDIOC_G_CROP, &crop)) {
        crop.c = cropcap.defrect;
	}

	memset (&format, 0, sizeof (format));
	//format.fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == vioctl (v->fd, VIDIOC_G_FMT, &format)) {
		return 0;
	}

	hscale = format.fmt.pix.width / (double) crop.c.width;
	vscale = format.fmt.pix.height / (double) crop.c.height;

	aspect = cropcap.pixelaspect.numerator / (double) cropcap.pixelaspect.denominator;
	aspect = aspect * hscale / vscale;

	/* Devices following ITU-R BT.601 do not capture
   	   square pixels. For playback on a computer monitor
   	   we should scale the images to this size. */

	dwidth = format.fmt.pix.width / aspect;
	dheight = format.fmt.pix.height;

	return 1;
}


int	v4l2_num_devices()
{
	return 4;
}

char **v4l2_get_device_list()
{
	const char *list[] = {
		"/dev/v4l/video0",
		"/dev/v4l/video1",
		"/dev/v4l/video2",
		"/dev/v4l/video3",
		NULL
	};
	char **dup = (char**) malloc(sizeof(char*)*5);
	int i;
	for( i = 0; list[i] != NULL ; i ++ )
		dup[i] = strdup( list[i]);
	return dup;
}
#endif
