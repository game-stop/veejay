/* 
 * Linux VeeJay
 *
 * Copyright(C)2010-2011 Niels Elburg <nwelburg@gmail.com / niels@dyne.org >
 *             - re-use Freej's v4l2 cam driver
 *             - implemented controls method and image format negotiation 
 *             - added jpeg decoder
 *             - added avcodec decoder
 *             - added threading
 *
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


//@ FIXME: fix greyscale
//@ FIXME: not all drivers implement TRY_FMT
//@ FIXME: find maximum width/height: start with large and TRY_FMT
//@ TODO:  add support for tuner (set frequency)
//
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
#include <dirent.h>
#include <pthread.h>
#include <poll.h>
#include <libvje/vje.h>
#include <libavutil/pixfmt.h>
#include <libvevo/libvevo.h>
#include <libstream/v4l2utils.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <veejay/jpegutils.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
//#include <pthread.h>
#include <libavutil/pixdesc.h>
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

	bufs 		*buffers;
	VJFrame 	*info;
	void		*scaler;
	int		planes[4];
	int		rw;
	int		composite;
	int		is_jpeg;
	int		sizeimage;
	VJFrame		*buffer_filled;
	uint8_t 	*tmpbuf;
	int		is_streaming;

	AVCodec *codec;
	AVCodecContext *c;
	AVFrame *picture;

	void		*video_info;
	int		processed_buffer;
	int		allinthread;
	int		grey;
	int		threaded;
} v4l2info;

static	void	lock_( v4l2_thread_info *i ) {
	int res = pthread_mutex_lock(&(i->mutex));
	if( res < 0 ) {
		veejay_msg(0, "v4l2: lock error %s", strerror(errno));
	}
}

static	void	unlock_(v4l2_thread_info *i) {
	int res = pthread_mutex_unlock(&(i->mutex));
	if( res < 0 ) { 
		veejay_msg(0, "v4l2: unlock error %s",strerror(errno));
	}
}

static	int	vioctl( int fd, int request, void *arg )
{
	int ret;
	do {	
		ret = ioctl( fd, request, arg );
	}
	while( ret == -1 && EINTR == errno );
	return ret;
}

static	void	v4l2_free_buffers( v4l2info *v )
{
	int i;
	for( i = 0; i < v->reqbuf.count ; i ++ ) {
		munmap( v->buffers[i].start, v->buffers[i].length );
	}
	free(v->buffers);
}

static	int		v4l2_start_video_capture( v4l2info *v )
{
	if(!v->is_streaming) {
		if( -1 == vioctl( v->fd, VIDIOC_STREAMON, &(v->buftype)) ) {
			veejay_msg(0, "v4l2: unable to start streaming: %d,%s",errno,strerror(errno));
			return 0;
		}
		v->is_streaming = 1;
	}
	return 1;
}

static	int	v4l2_vidioc_qbuf( v4l2info *v )
{
	int i;
	for( i = 0; i < v->reqbuf.count ; i ++ ) {
		veejay_memset( &(v->buffer),0,sizeof(v->buffer));
		v->buffer.type = v->reqbuf.type;
		v->buffer.memory=V4L2_MEMORY_MMAP;
		v->buffer.index = i;

		if( -1 == vioctl( v->fd, VIDIOC_QBUF, &(v->buffer)) ) {
			veejay_msg(0, "v4l2: first VIDIOC_QBUF failed with %s", strerror(errno));
			return -1;
		}
	}
	return 1;
}

static	int		v4l2_stop_video_capture( v4l2info *v )
{
	if(v->is_streaming) {
		if( -1 == vioctl( v->fd, VIDIOC_STREAMOFF, &(v->buftype)) ) {
			veejay_msg(0,"v4l2: unable to stop streaming:%d, %s",errno,strerror(errno));
			return 0;
		}
		v->is_streaming = 0;
	}
	return 1;
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
		case V4L2_PIX_FMT_MJPEG:
			return PIX_FMT_YUVJ420P; //@ FIXME untested
		case V4L2_PIX_FMT_JPEG:
			return PIX_FMT_YUVJ420P; //@ decode_jpeg_raw downsamples all yuv, FIXME: format negotation
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
#ifdef STRICT_CHECKING
			assert( pf >= 0 );
			veejay_msg(VEEJAY_MSG_WARNING, "v4l2: using BGR24 (default) for unhandled pixfmt '%s'",
				av_pix_fmt_descriptors[ pf ] );
#endif
			return V4L2_PIX_FMT_BGR24;
	}
	return V4L2_PIX_FMT_BGR24;
}

static	int	v4l2_set_framerate( v4l2info *v , float fps ) 
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
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: unable to set video standard.");
		return 1;//@ show must go on 
	} else {
		veejay_msg(VEEJAY_MSG_INFO,"v4l2: set video standard PAL");
	}	

	return 1;

}

static	void	v4l2_enum_frame_sizes( v4l2info *v )
{
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmsizeenum fmtsize;
	struct v4l2_frmivalenum frmival;
	const char *buf_types[] = { "Video Capture" , "Video Output", "Video Overlay" };
	const char *flags[] = { "uncompressed", "compressed" };
	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: discovering supported video formats");

	for( fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		 fmtdesc.type < V4L2_BUF_TYPE_VIDEO_OVERLAY;
		 fmtdesc.type ++ ) {
		fmtdesc.index = 0;
		while( vioctl( v->fd, VIDIOC_ENUM_FMT, &fmtdesc ) >= 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: Enumerate (%d,%s)", fmtdesc.index, buf_types[ fmtdesc.type ] );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tindex:%d", fmtdesc.index );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tflags:%s", flags[ fmtdesc.type ] );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tdescription:%s", fmtdesc.description );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tpixelformat:%c%c%c%c",
						fmtdesc.pixelformat & 0xff,
						(fmtdesc.pixelformat >> 8 ) & 0xff,
						(fmtdesc.pixelformat >> 16) & 0xff,
						(fmtdesc.pixelformat >> 24) & 0xff );

			fmtsize.index = 0;
			fmtsize.pixel_format = fmtdesc.pixelformat;
			while( vioctl( v->fd, VIDIOC_ENUM_FRAMESIZES, &fmtsize ) >= 0 ) {
				if( fmtsize.type == V4L2_FRMSIZE_TYPE_DISCRETE ) {
					veejay_msg(VEEJAY_MSG_DEBUG, "\t\t%d x %d", fmtsize.discrete.width, fmtsize.discrete.height );
					//while( vioctl( v->fd, VIDIOC_ENUM_FRAMEINTERVAL, &frmival ) >= 0 ) {
					//	frmival.index ++;
					//	veejay_msg(0, "\t\t\t<discrete>");
					//}
				} else if( fmtsize.type == V4L2_FRMSIZE_TYPE_STEPWISE ) {
					veejay_msg(VEEJAY_MSG_DEBUG,"\t\t%d x %d - %d x %d with step %d / %d",
							fmtsize.stepwise.min_width,
							fmtsize.stepwise.min_height,
							fmtsize.stepwise.max_width,
							fmtsize.stepwise.min_height,
							fmtsize.stepwise.step_width,
							fmtsize.stepwise.step_height );
					//while( vioctl( v->fd, VIDIOC_ENUM_FRAMEINTERVAL, &frmival ) >= 0 ) {
					//	veejay_msg(0, "\t\t\t<stepwise interval>");
				//		frmival.index ++;
//
//					}
				}
				fmtsize.index++;
			}
				
			fmtdesc.index ++;
		}
	}
}

static	int	v4l2_try_pix_format( v4l2info *v, int pixelformat, int wid, int hei, int *pp )
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

	//@ or take from environment
	if( *pp == 0 ) {
		char *greycap = getenv( "VEEJAY_V4L2_GREYSCALE_ONLY" );
		if( greycap ) {
			int gc = atoi(greycap);
			if( gc == 1 ) {
				v4l2_pixel_format = V4L2_PIX_FMT_GREY; //@ FIXME
				veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: setting grey scale (env)");
				v->grey=1;
			}
		}
	}


	*pp = format.fmt.pix.pixelformat;

	format.fmt.pix.pixelformat = v4l2_pixel_format;
	
	if( vioctl( v->fd, VIDIOC_TRY_FMT, &format ) != -1 ) {

		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: format %s not supported by capture card... ",
				(char*) &v4l2_pixel_format);

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


 		if( format.fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG )
		{
			struct v4l2_jpegcompression jpegcomp;
			ioctl(v->fd, VIDIOC_G_JPEGCOMP, &jpegcomp);
			jpegcomp.jpeg_markers |= V4L2_JPEG_MARKER_DQT; // DQT
			ioctl(v->fd, VIDIOC_S_JPEGCOMP, &jpegcomp);
			v->is_jpeg = 1;
			v->tmpbuf  = (uint8_t*) vj_malloc(sizeof(uint8_t) * wid * hei * 3 );
		}
		else if( format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
		{
			struct v4l2_jpegcompression jpegcomp;
			ioctl(v->fd, VIDIOC_G_JPEGCOMP, &jpegcomp);
			jpegcomp.jpeg_markers |= V4L2_JPEG_MARKER_DQT; // DQT
			ioctl(v->fd, VIDIOC_S_JPEGCOMP, &jpegcomp);
			v->is_jpeg = 2;

			v->codec = avcodec_find_decoder( CODEC_ID_MJPEG );
			if(v->codec == NULL) {
				veejay_msg(0, "Codec not found.");
				return -1;
			}

			v->c 	   = avcodec_alloc_context();
			//v->c->codec_id = CODEC_ID_MJPEG;
			v->c->width    = format.fmt.pix.width;
			v->c->height   = format.fmt.pix.height;
			v->picture = avcodec_alloc_frame();
			v->picture->data[0] = vj_malloc(wid * hei + wid);
			v->picture->data[1] = vj_malloc(wid * hei + wid);
			v->picture->data[2] = vj_malloc(wid * hei + wid);
			v->tmpbuf  = (uint8_t*) vj_malloc(sizeof(uint8_t) * wid * hei * 3 );
			if( v->codec->capabilities & CODEC_CAP_TRUNCATED)
				v->c->flags |= CODEC_FLAG_TRUNCATED;

			if( avcodec_open( v->c, v->codec ) < 0 ) 
			{
				veejay_msg(0, "Error opening codec");
				free(v->picture->data[0]);
				free(v->picture->data[1]);
				free(v->picture->data[2]);
				free(v->picture);
				av_free(v->c);
				free(v->tmpbuf);
				return -1;
			}

		} else 	if( format.fmt.pix.pixelformat != v4l2_pixel_format ) {
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
	
	if( -1 == vioctl( v->fd, VIDIOC_G_FMT, &format) ) {
		veejay_msg(0, "v4l2: VIDIOC_G_FMT failed with %s", strerror(errno));
		return -1;
	}

	veejay_msg(VEEJAY_MSG_INFO,"v4l2: using palette %4.4s (%dx%d)",
		(char*) &format.fmt.pix.pixelformat,
		format.fmt.pix.width,
		format.fmt.pix.height
	);

	if( v->info )
		free(v->info);


	v->format.fmt.pix.pixelformat = format.fmt.pix.pixelformat;
	v->format.fmt.pix.width = format.fmt.pix.width;
	v->format.fmt.pix.height = format.fmt.pix.height;	

	v->info = yuv_yuv_template( NULL,NULL,NULL,format.fmt.pix.width, format.fmt.pix.height,
					v4l2_pixelformat2ffmpeg( format.fmt.pix.pixelformat ) );

	yuv_plane_sizes( v->info, &(v->planes[0]),&(v->planes[1]),&(v->planes[2]),&(v->planes[3]) );

	return 1;
}

static void	v4l2_set_output_pointers( v4l2info *v, void *src )
{
	uint8_t *map = (uint8_t*) src;
//@A
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
	if(v->threaded)
		lock_(v->video_info);
	v->buffer_filled->data[0] = Y;
	v->buffer_filled->data[1] = U;
	v->buffer_filled->data[2] = V;
	if(v->threaded)
		unlock_(v->video_info);
	return v->buffer_filled;
}

static	int	v4l2_channel_choose( v4l2info *v, const int pref_channel )
{
	int chan = pref_channel;
	int n    = 0;
	int i;
	int	pref_ok = 0;
	int other   = -1;
	for ( i = 0; i < (pref_channel+1); i ++ ) {
		if( -1 == vioctl( v->fd, VIDIOC_S_INPUT, &i )) {
#ifdef STRICT_CHECKING
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: input channel %d does not exist", i);
#endif
			if( errno == EINVAL )
				continue;
			return 0;
		} else {
			if(pref_channel==i)
				pref_ok = 1;
			else
				other = i;
		}
	}

	if( pref_ok )
		return pref_channel;

	return other;
}

static	int	v4l2_verify_file( const char *file )
{
	struct stat st;
	if( -1 == stat( file, &st )) {
		veejay_msg(0, "v4l2: cannot identify '%s':%d, %s",file,errno,strerror(errno));
		return 0;
	}
	if( !S_ISCHR(st.st_mode)) {
		veejay_msg(0, "v4l2: '%s' is not a device", file);
		return 0;
	}

	int fd = open( file, O_RDWR | O_NONBLOCK );

	if( -1 == fd ) {
		veejay_msg(0, "v4l2: cannot open '%s': %d, %s", file, errno, strerror(errno));
		return 0;
	}

	close(fd);

	return 1;
}

int	v4l2_poll( void *d , int nfds, int timeout )
{
	struct pollfd p;
	int    err = 0;
	v4l2info *v = (v4l2info*) d;

	p.fd = v->fd;
	p.events = POLLIN | POLLERR | POLLHUP | POLLPRI;

	err = poll( &p, nfds, timeout );
	if( err == -1 ) {
		if( errno == EAGAIN || errno == EINTR ) {
#ifdef STRICT_CHECKING
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: capture device busy, try again.");
#endif
			return 0;
		}
	}
	if( p.revents & POLLIN || p.revents & POLLPRI )
		return 1;
	
	return err;
}

void *v4l2open ( const char *file, const int input_channel, int host_fmt, int wid, int hei, float fps, char norm  )
{
	if(!v4l2_verify_file( file ) ) {
		return NULL;
	}

	char *flt = getenv( "VEEJAY_V4L2_ALL_IN_THREAD" );
	int   flti = 1; //@ on by default
	if( flt ) {
		flti = atoi(flt);
	}

	int fd = open( file , O_RDWR );

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Video4Linux2 device opened: %s", file );

	v4l2info *v = (v4l2info*) vj_calloc(sizeof(v4l2info));

	v->fd = fd;
	v->allinthread = flti;

	if( v->grey == 1 ) {
		v->buffer_filled = yuv_yuv_template( NULL,NULL,NULL, wid,hei, PIX_FMT_GRAY8 );
	} else {	
		v->buffer_filled = yuv_yuv_template( NULL,NULL,NULL,wid,hei,host_fmt );
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: output in %s", av_pix_fmt_descriptors[ v->buffer_filled->format ] );

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

	int can_stream = 1;
	int	can_read = 1;
	int	cap_read = 0;

	if( (v->capability.capabilities & V4L2_CAP_STREAMING ) == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: %s does not support streaming capture", v->capability.card );
		can_stream = 0;
	}

	if( (v->capability.capabilities & V4L2_CAP_READWRITE ) == 0  ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: %s does not support read/write interface.", v->capability.card);
		can_read = 0;
	}

	if( can_stream == 0 && can_read == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: giving up on %s", v->capability.card);
		close(fd);
		free(v);
		return NULL;
	}

	if( can_read && can_stream ) {
		char *vio = getenv("VEEJAY_V4L2_CAPTURE_METHOD");
		if(vio) {
			int method = atoi(vio);
			switch(method) {
				case 0:
					can_stream = 0;
					break;
				case 1:
					can_read = 0;
					break;
			}
		} else {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: preferring mmap() capture, override with VEEJAY_V4L2_CAPTURE_METHOD=0");
			can_read = 0;
			cap_read = 1;
		}
 	}

	if( can_read && can_stream == 0)
		v->rw = 1;	

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture driver: %s",
			v->capability.driver );
	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture card: %s",
			v->capability.card );
	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture method: %s",
			(can_read ? "read/write interface" : "mmap"));
	

	//@ which video input ?
	int chan = v4l2_channel_choose( v, input_channel );
	if(chan == -1) {
		veejay_msg(0, "v4l2: Video device has no input channels ? What video device is that?");
		free(v);
		close(fd);
		return NULL;
	}
	if( -1 == vioctl( fd, VIDIOC_S_INPUT, &chan )) {
		int lvl = 0;
		if( errno == EINVAL )
			lvl = VEEJAY_MSG_WARNING;
		veejay_msg(0, "v4l2: VIDIOC_S_INPUT failed with %s, arg was %x", strerror(errno),chan);
		if( errno != EINVAL ) {
			free(v);
			close(fd);
			return NULL;
		}
		
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


	int cur_fmt = 0;
	if( v4l2_try_pix_format( v, host_fmt, wid, hei, &cur_fmt ) < 0 ) {
		if( v4l2_try_pix_format(v, v4l2_pixelformat2ffmpeg( cur_fmt ), wid,hei,&cur_fmt ) < 0 ) {
			free(v);
			close(fd);
			return NULL;
		}
	}

	if( v4l2_set_framerate( v, fps ) == -1 ) {
		veejay_msg(0, "v4l2: failed to set frame rate to %2.2f", fps );
	}

	if( v->rw == 0 ) {
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


		if( v4l2_vidioc_qbuf( v ) == -1 ) {
				veejay_msg(0, "v4l2: VIDIOC_QBUF failed with:%d, %s", errno,strerror(errno));
				free(v->buffers);
				free(v);
				close(fd);
				return NULL;
		}

		if( !v4l2_start_video_capture( v ) ) {
			if(cap_read) {
				veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: fallback read/write");
				v->rw = 1;
				v4l2_free_buffers(v);

				//@ close and re-open
				close(v->fd);
 				v->fd = open( file , O_RDWR );
				if(v->fd <= 0 ) {
					veejay_msg(0,"v4l2: cannot re-open device:%d,%s",errno,strerror(errno));
					free(v->buffers);
					free(v);
					return NULL;
				}

				goto v4l2_rw_fallback;
			} else{
			  	free(v->buffers);
				free(v);
				close(fd);
				return NULL;
			}
		}
	} else {
v4l2_rw_fallback:
		v->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v->format.fmt.pix.width = wid;
		v->format.fmt.pix.height = hei;
		//format.fmt.pix.pixelformat;
		//format.fmt.pix.field
		//
		if( -1 == vioctl( v->fd, VIDIOC_S_FMT, &(v->format) ) ) {
			veejay_msg(0, "V4l2: VIDIOC_S_FMT failed with %s", strerror(errno));
			close(v->fd);
			free(v);
			return NULL;
		}

		int min = v->format.fmt.pix.width * 2;
		if( v->format.fmt.pix.bytesperline < min )
			v->format.fmt.pix.bytesperline = min;
		min = v->format.fmt.pix.bytesperline * v->format.fmt.pix.height;
		if( v->format.fmt.pix.sizeimage < min )
			v->format.fmt.pix.sizeimage = min;

		v->sizeimage = v->format.fmt.pix.sizeimage;
		v->buffers = (bufs*) calloc( 1, sizeof(*v->buffers));
		veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: read/write buffer size is %d bytes", v->format.fmt.pix.sizeimage );
		veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: requested format %s, %d x %d", 
			 &(v->format.fmt.pix.pixelformat), v->format.fmt.pix.width,v->format.fmt.pix.height );

		v->buffers[0].length = v->sizeimage;
		v->buffers[0].start = malloc( v->sizeimage * 2 );
	}	


	return v;
}

static	double	calc_tc( struct v4l2_timecode *tc, float fps )
{
#ifdef STRICT_CHECKING
	assert( fps > 0.0 );
#endif
	return (double) tc->frames / fps;
}
static	int	v4l2_pull_frame_intern( v4l2info *v )
{ //@ fixme more functions no pasta
	void *src = NULL;
	int  length = 0;
	int  n = 0;
	if( v->rw == 0 ) {
	
		if( -1 == vioctl( v->fd, VIDIOC_DQBUF, &(v->buffer))) {
			veejay_msg(0, "v4l2: VIDIOC_DQBUF: %s", strerror(errno));
			if( errno != EIO ) {
				veejay_msg(0,"v4l2: unable to grab a frame: %d, %s",errno,strerror(errno));
			}
			v4l2_stop_video_capture(v);
			v4l2_vidioc_qbuf( v );
			v4l2_start_video_capture(v);
			return 0;
		}

		src = v->buffers[ v->buffer.index ].start;
		length = v->buffers[v->buffer.index].length;
	}
	else {
		length = v->buffers[0].length;
		src = v->buffers[0].start;

		n = read( v->fd, src, length);
		if( -1 == n ) {
			switch(errno) {
				case EAGAIN:
					return 1;
				default:
					veejay_msg(0,"v4l2: error while reading from capture device: %s", strerror(errno));
					return 0;
			}
		}
	}

	int got_picture = 0;

	switch(v->is_jpeg) {
		case 1:
			v4l2_set_output_pointers(v,v->tmpbuf);
			length = decode_jpeg_raw( src, n, 0,0, v->info->width,v->info->height,v->info->data[0],v->info->data[1],v->info->data[2] );
			if( length == 0 ) { //@ success
			  length = 1;
			}
			break;
		case 2:
			length = avcodec_decode_video( v->c, v->picture, &got_picture, v->tmpbuf,src );
			if( length == -1 ) {
			 veejay_msg(0,"v4l2: error while decoding frame");
			 return 0;
			}	
			v->info->data[0] = v->picture->data[0];
			v->info->data[1] = v->picture->data[1];
			v->info->data[2] = v->picture->data[2];

			break;
		default:
			v4l2_set_output_pointers(v,src);
			break;
	}

	if( v->allinthread )
	{
		if( v->scaler == NULL )
		{
			sws_template templ;     
   			memset(&templ,0,sizeof(sws_template));
   			templ.flags = yuv_which_scaler();
			v->scaler = yuv_init_swscaler( v->info,v->buffer_filled, &templ, yuv_sws_get_cpu_flags() );
		}
		
		lock_(v->video_info);
		yuv_convert_and_scale( v->scaler, v->info, v->buffer_filled );
		unlock_(v->video_info);

	}

	if(!v->rw) {
		if( -1 == vioctl( v->fd, VIDIOC_QBUF, &(v->buffer))) {
			veejay_msg(0, "v4l2: VIDIOC_QBUF failed with %s", strerror(errno));
		}
	}


	return 1;
}

int		v4l2_pull_frame(void *vv,VJFrame *dst)
{
	void *src = NULL;
	int	 length = 0;
	int	n = 0;

	v4l2info *v = (v4l2info*) vv;
	if( v->scaler == NULL ) {
		int tmp = dst->format;
		sws_template templ;     
   		memset(&templ,0,sizeof(sws_template));
   		templ.flags = yuv_which_scaler();
	
		if( v->grey ) {
			dst->format = PIX_FMT_GRAY8;
		}
		v->scaler = yuv_init_swscaler( v->info,dst, &templ, yuv_sws_get_cpu_flags() );
		if( v->grey ) {
			dst->format = tmp;
		}
	}

	if( v->rw == 0 ) {
	
		if( -1 == vioctl( v->fd, VIDIOC_DQBUF, &(v->buffer))) {
			veejay_msg(0, "v4l2: VIDIOC_DQBUF: %s", strerror(errno));
			return 0;
		}

		src = v->buffers[ v->buffer.index ].start;
		length = v->buffers[v->buffer.index].length;
	}
	else {
		length = v->buffers[0].length;
		src = v->buffers[0].start;

		n = read( v->fd, src, length);
		if( -1 == n ) {
			switch(errno) {
				case EAGAIN:
					return 1;
				default:
					veejay_msg(0,"v4l2: error while reading from capture device: %s", strerror(errno));
					return 0;
			}
		}
	}

	int got_picture = 0;

	switch(v->is_jpeg) {
		case 1:
			v4l2_set_output_pointers(v,v->tmpbuf);
			length = decode_jpeg_raw( src, n, 0,0, v->info->width,v->info->height,v->info->data[0],v->info->data[1],v->info->data[2] );
			if( length == 0 ) { //@ success
			  length = 1;
			}
			break;
		case 2:
			length = avcodec_decode_video( v->c, v->picture, &got_picture, v->tmpbuf,src );
			if( length == -1 ) {
			 veejay_msg(0,"v4l2: error while decoding frame");
			 return 0;
			}	
			v->info->data[0] = v->picture->data[0];
			v->info->data[1] = v->picture->data[1];
			v->info->data[2] = v->picture->data[2];

			break;
		default:
			v4l2_set_output_pointers(v,src);
			break;
	}

	if( length > 0 ) {
		yuv_convert_and_scale( v->scaler, v->info, dst );
	}
	if(!v->rw) {
		if( -1 == vioctl( v->fd, VIDIOC_QBUF, &(v->buffer))) {
			veejay_msg(0, "v4l2: VIDIOC_QBUF failed with %s", strerror(errno));
		}
	}

	return 1;
}

void	v4l2_close( void *d )
{
	v4l2info *v = (v4l2info*) d;
	int i;

	if( v->rw == 0 ) {
		if( -1 == vioctl( v->fd, VIDIOC_STREAMOFF, &(v->buftype)) ) {
			veejay_msg(0, "v4l2: VIDIOC_STREAMOFF failed with %s", strerror(errno));
		}

		for( i = 0; i < v->reqbuf.count; i ++ ) {
			munmap( v->buffers[i].start, v->buffers[i].length );
		}
	} else {
		free( v->buffers[0].start );
	}	

	close(v->fd);

	if( v->scaler )
		yuv_free_swscaler( v->scaler );
	
	if( v->buffer_filled) {
		if( v->allinthread && !v->picture )
			free(v->buffer_filled->data[0]);
		free(v->buffer_filled);
	}

	if(v->picture) {
		free(v->picture->data[0]);
		free(v->picture->data[1]);
		free(v->picture->data[2]);
		free(v->picture);
	}
	if(v->c) {
		av_free(v->c);
	}
	if(v->tmpbuf) {
		free(v->tmpbuf);
	}
	
	if(v->codec) {
		avcodec_close(v->codec);
		v->codec = NULL;
	}

	if( v->buffers )
		free(v->buffers);

	free(v);
}


void	v4l2_set_hue( void *d, int32_t value ) {
	v4l2_set_control( d, V4L2_CID_HUE, value );
}
int32_t	v4l2_get_hue( void *d ) {
	return v4l2_get_control(d, V4L2_CID_HUE );
}

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

static	char **v4l2_dummy_list()
{

	const char *list[] = {
		"/dev/video0",
		"/dev/video1",
		"/dev/video2",
		"/dev/video3",
		"/dev/video4",
		"/dev/video5",
		"/dev/video6",
		"/dev/video7",
		NULL
	};
	char **dup = (char**) malloc(sizeof(char*)*9);
	int i;
	for( i = 0; list[i] != NULL ; i ++ )
		dup[i] = strdup( list[i]);
	veejay_msg(VEEJAY_MSG_DEBUG, "Using dummy video device list");
	return dup;
}

char **v4l2_get_device_list()
{

	DIR *dir;
	struct dirent *dirp;
	const char prefix[] = "/sys/class/video4linux/";
	const char v4lprefix[] = "/dev/";
	if( (dir = opendir( prefix )) == NULL ) {
		veejay_msg(VEEJAY_MSG_WARNING,"Failed to open '%s':%d, %s", prefix, errno,strerror(errno));
		return v4l2_dummy_list();
	}

	char *list[255];
	int   n_devices = 0;

	memset(list,0,sizeof(list));

	while((dirp = readdir(dir)) != NULL) {
		if(strncmp( dirp->d_name, "video", 5 ) != 0) 
			continue;
		list[n_devices] = strdup( dirp->d_name );
		n_devices ++;
	}
	closedir(dir);	
	if( n_devices == 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING,"No devices found!");
		return v4l2_dummy_list();
	}

	int i;
	char **files = (char**) malloc(sizeof(char*) * (n_devices + 1));
	memset( files, 0, sizeof(char*) * (n_devices+1));
	
	for( i = 0;i < n_devices; i ++ ) {
		files[i] = (char*) malloc(sizeof(char) * (strlen(list[i]) + 5));
		sprintf(files[i],"%s%s",v4lprefix, list[i]);
		veejay_msg(VEEJAY_MSG_DEBUG, "Found %s", files[i]);
    	}
	return files;

}


/************* Threading wrapper below
 *
 */



//@ this is the grabber thread.
//@ it tries to open v4l device from template structure v4l2_thread_info
//@ if successfull:
//@   - it stores the result of v4l2open (v4l2info) in v4l2_thread_info->v4l2
//@   - it stores the pointer to v4l_thread_info in v4l2info->video_info
//@ then, it enters a loop that ends on
//@   - error from capture device after 15 retries (VEEJAY_V4L2_MAX_RETRIES)
//@   - error to capture frame after n retries 
static	void	*v4l2_grabber_thread( void *v )
{
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL );
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);


	int max_retries = 15;
	char *retry = getenv( "VEEJAY_V4L2_MAX_RETRIES" );
	if(retry) {
			max_retries = atoi( retry );
	}
	if( max_retries < 0 || max_retries > 99999 ) {
		max_retries = 15;
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VEEJAY_V4L2_MAX_RETRIES out of bounds, set to default (%d)",max_retries);
	}

	lock_( v );
	v4l2_thread_info *i = (v4l2_thread_info*) v;

	v4l2info *v4l2 = v4l2open( i->file, i->channel, i->host_fmt, i->wid, i->hei, i->fps, i->norm );
	if( v4l2 == NULL ) {
		veejay_msg(0, "v4l2: error opening v4l2 device '%s'",i->file );
		pthread_exit(NULL);
		return NULL;
	}

	i->v4l2 = v4l2;
	v4l2->video_info = i;
	v4l2->threaded = 1;
	if(v4l2==NULL) {
		unlock_(v);
		pthread_exit(NULL);
		return NULL;
	}
		
	if( v4l2->allinthread ) {
		v4l2->buffer_filled->data[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (v4l2->buffer_filled->len * 3));
		v4l2->buffer_filled->data[1] = v4l2->buffer_filled->data[0] + v4l2->buffer_filled->len;
		v4l2->buffer_filled->data[2] = v4l2->buffer_filled->data[1] + v4l2->buffer_filled->len;
		veejay_msg(VEEJAY_MSG_INFO, "v4l2: allocated %d bytes for output buffer", (v4l2->buffer_filled->len*3));
		veejay_msg(VEEJAY_MSG_INFO, "v4l2: output buffer is %d x %d", v4l2->buffer_filled->width,v4l2->buffer_filled->height);
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: image processing (scale/convert) in (%s)",
			(v4l2->allinthread ? "thread" : "host" ));

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: capture format: %d x %d (%s)",
			v4l2->info->width,v4l2->info->height, av_pix_fmt_descriptors[ v4l2->info->format ].name );
	veejay_msg(VEEJAY_MSG_INFO, "v4l2:  output format: %d x %d (%s)",
			v4l2->buffer_filled->width,v4l2->buffer_filled->height, av_pix_fmt_descriptors[v4l2->buffer_filled->format]);

	
	i->grabbing = 1;
	i->retries  = max_retries;
	unlock_(v);

	while( 1 ) {
		int err = (v4l2->rw);

		while( ( err = v4l2_poll( v4l2, 1, 200 ) ) < 0 ) {
			if( !(err == -1) ) {
				break;
			}
		}
		if( err > 0 && !v4l2_pull_frame_intern( v4l2 ) )
			err = -1;

		if( err == -1 ) {
			if( i->retries < 0 ) {
				veejay_msg(0,"v4l2: giving up on this device, too many errors.");
				v4l2_close( v4l2 );
				pthread_exit(NULL);
				return NULL;
			} else {
#ifdef STRICT_CHECKING
				veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: error queuing frame, %d retries left", i->retries );
#endif
				i->retries --;
			}
		}

		if( i->stop ) {
			v4l2_close(v4l2);
			pthread_exit(NULL);
			return NULL;
		}
	}
}

int	v4l2_thread_start( v4l2_thread_info *i ) 
{
	pthread_attr_init( &(i->attr) );
	pthread_attr_setdetachstate( &(i->attr), PTHREAD_CREATE_DETACHED );

	int err = pthread_create( &(i->thread), NULL, v4l2_grabber_thread, i );

	pthread_attr_destroy( &(i->attr) );

	if( err == 0 ) {
		return 1;
	}

	veejay_msg(0, "v4l2: failed to start thread: %d, %s", errno, strerror(errno));
	return 0;
}

void	v4l2_thread_stop( v4l2_thread_info *i )
{
	lock_(i);
	i->stop = 1;
	unlock_(i);
}

int	v4l2_thread_pull( v4l2_thread_info *i , VJFrame *dst )
{
	v4l2info *v    = (v4l2info*) i->v4l2;
	int	status = 0;
	lock_(i);
	
	if(!v->allinthread && v->scaler == NULL ) {
		sws_template templ;     
   		memset(&templ,0,sizeof(sws_template));
   		templ.flags = yuv_which_scaler();
		v->scaler = yuv_init_swscaler( v->info,dst, &templ, yuv_sws_get_cpu_flags() );
	}
//@A
	if( v->info->data[0] == NULL )
	{
		unlock_(i); 
#ifdef STRICT_CHECKING
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: capture device not ready yet.");
#endif
		return 1;
	}
	
	if(!v->allinthread) {
		yuv_convert_and_scale( v->scaler, v->info, dst );
	} else {
		veejay_memcpy( dst->data[0], v->buffer_filled->data[0], v->planes[0]);	
		if(!v->grey) {
			veejay_memcpy( dst->data[1], v->buffer_filled->data[1], v->planes[1]);
			veejay_memcpy( dst->data[2], v->buffer_filled->data[2], v->planes[2]);
		} else {
			veejay_memset( dst->data[1], 127, dst->uv_len );
			veejay_memset( dst->data[2], 127, dst->uv_len );
		}
	}
		
	status = i->grabbing;
	unlock_(i);	
	return status;
}

v4l2_thread_info *v4l2_thread_info_get( void *vv ) {
	v4l2info *v = (v4l2info*) vv;
	return (v4l2_thread_info*) v->video_info;
}

void *v4l2_thread_new( char *file, int channel, int host_fmt, int wid, int hei, float fps, char norm )
{
	v4l2_thread_info *i = (v4l2_thread_info*) vj_calloc(sizeof(v4l2_thread_info));
	i->file = strdup(file);
	i->channel = channel;
	i->host_fmt = host_fmt;
	i->wid = wid;
	i->hei = hei;
	i->fps = fps;
	i->norm = norm;

	pthread_mutexattr_t type;
	pthread_mutexattr_init(&type);
	pthread_mutexattr_settype(&type, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init(&(i->mutex), &type);


	if( v4l2_thread_start( i ) == 0 ) {
		free(i->file);
		free(i);
		return NULL;
	}

	int ready     = 0;
	int retries   = 50;
	//@ wait until thread is ready
	while(1) {
		usleep(100);
		lock_(i);
		ready = i->grabbing;
		unlock_(i);
		if(ready) 
		 break;
		retries--;
	}

#ifdef STRICT_CHECKING
	v4l2info *v = (v4l2info*)i->v4l2;
	assert( v != NULL );
#endif
	return i->v4l2;
}
#endif
