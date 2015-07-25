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


//@ FIXME: not all drivers implement TRY_FMT
//@ FIXME: find maximum width/height: start with large and TRY_FMT
//@ TODO:  add support for tuner (set frequency)
//
#include <config.h>
#ifdef HAVE_V4L2
#include <stdio.h>
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
#include <time.h>
#include <poll.h>
#include <libvje/vje.h>
#include <libavutil/pixfmt.h>
#include <libavformat/avformat.h>
#include <libvevo/libvevo.h>
#include <libstream/v4l2utils.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <veejay/jpegutils.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libel/av.h>
#include <libel/avhelper.h>
#define        RUP8(num)(((num)+8)&~8)

//#include <pthread.h>
typedef struct {
		void *start;
		size_t length;
} bufs;

#define N_FRAMES 2
#define LOOP_LIMIT 64

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
	int		out_planes[4];
	int		rw;
	int		composite;
	int		is_jpeg;
	int		sizeimage;
	VJFrame		*frames[N_FRAMES];
	VJFrame		*host_frame;
	int		frames_done[N_FRAMES];
	int		frameidx;
	int		frame_ready;
	int		is_streaming;
	int		pause_read;
	int		pause_capture;
	int		pause_capture_status;
	AVCodec *codec;
	AVCodecContext *c;
	AVFrame *picture;
	uint8_t	*dst_ptr[3];
	void		*video_info;
	int		processed_buffer;
	int		grey;
	int		threaded;
	uint32_t	supported_pixel_formats[LOOP_LIMIT];
	int		is_vloopback;
	int		n_pixel_formats;
} v4l2info;

static struct {
	const uint64_t std;
	const char *descr;
} v4l2_video_standards[] = 
{
	{ V4L2_STD_NTSC, 	"NTSC"      },
	{ V4L2_STD_NTSC_M, 	"NTSC-M"    },
	{ V4L2_STD_NTSC_M_JP, 	"NTSC-M-JP" },
	{ V4L2_STD_NTSC_M_KR,	"NTSC-M-KR" },
	{ V4L2_STD_NTSC_443, 	"NTSC-443"  },
	{ V4L2_STD_PAL, 	"PAL"       },
	{ V4L2_STD_PAL_BG, 	"PAL-BG"    },
	{ V4L2_STD_PAL_B, 	"PAL-B"     },
	{ V4L2_STD_PAL_B1, 	"PAL-B1"    },
	{ V4L2_STD_PAL_G, 	"PAL-G"     },
	{ V4L2_STD_PAL_H, 	"PAL-H"     },
	{ V4L2_STD_PAL_I, 	"PAL-I"     },
	{ V4L2_STD_PAL_DK, 	"PAL-DK"    },
	{ V4L2_STD_PAL_D, 	"PAL-D"     },
	{ V4L2_STD_PAL_D1, 	"PAL-D1"    },
	{ V4L2_STD_PAL_K, 	"PAL-K"     },
	{ V4L2_STD_PAL_M, 	"PAL-M"     },
	{ V4L2_STD_PAL_N, 	"PAL-N"     },
	{ V4L2_STD_PAL_Nc, 	"PAL-Nc"    },
	{ V4L2_STD_PAL_60, 	"PAL-60"    },
	{ V4L2_STD_SECAM, 	"SECAM"     },
	{ V4L2_STD_SECAM_B, 	"SECAM-B"   },
	{ V4L2_STD_SECAM_G, 	"SECAM-G"   },
	{ V4L2_STD_SECAM_H, 	"SECAM-H"   },
	{ V4L2_STD_SECAM_DK, 	"SECAM-DK"  },
	{ V4L2_STD_SECAM_D, 	"SECAM-D"   },
	{ V4L2_STD_SECAM_K, 	"SECAM-K"   },
	{ V4L2_STD_SECAM_K1, 	"SECAM-K1"  },
	{ V4L2_STD_SECAM_L, 	"SECAM-L"   },
	{ V4L2_STD_SECAM_LC, 	"SECAM-Lc"  },
	{ 0, 			"Unknown"   }
};

static	const	char	*v4l2_get_std(int std) {
	unsigned int i;
	for(i=0; v4l2_video_standards[i].std != 0 ; i ++ ) {
		if( v4l2_video_standards[i].std == std )
			return v4l2_video_standards[i].descr;
	}	
	return v4l2_video_standards[i].descr;
}

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

static	void	wait_(v4l2_thread_info *i) {
	pthread_cond_wait( &(i->cond), &(i->mutex));
}

static	void	signal_(v4l2_thread_info *i) {
	pthread_cond_signal( &(i->cond) );
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
	v->buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	int i;
	for( i = 0; i < v->reqbuf.count ; i ++ ) {
		veejay_memset( &(v->buffer),0,sizeof(v->buffer));
		v->buffer.type = v->buftype; //v->reqbuf.type;
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

int	v4l2_pixelformat2ffmpeg( int pf )
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
			return PIX_FMT_YUVJ420P;
		case V4L2_PIX_FMT_JPEG:
			return PIX_FMT_YUVJ420P; 
		default:
			veejay_msg(0, "v4l2: Unhandled pixel format: %d", pf );
			break;
		}

	return PIX_FMT_BGR24;
}

static	int	v4l2_ffmpeg2v4l2( int pf)
{
	switch(pf) {
		case PIX_FMT_RGB24:
			return V4L2_PIX_FMT_RGB24;
		case PIX_FMT_BGR24:
			return V4L2_PIX_FMT_BGR24;
		case PIX_FMT_BGR32:
			return V4L2_PIX_FMT_BGR32;
		case PIX_FMT_RGB32:
			return V4L2_PIX_FMT_RGB32;
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			return V4L2_PIX_FMT_YUV420;
		case PIX_FMT_YUYV422:
			return V4L2_PIX_FMT_YUYV;
		case PIX_FMT_YUV422P:
			return V4L2_PIX_FMT_YUV422P;
		case PIX_FMT_YUVJ422P:
			return V4L2_PIX_FMT_YUV422P;
		case PIX_FMT_UYVY422:
			return V4L2_PIX_FMT_UYVY;
		case PIX_FMT_YUVJ444P:
		case PIX_FMT_YUV444P:
			return V4L2_PIX_FMT_YUV32;

		default:
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
	sfps.parm.capture.timeperframe.denominator=(int)(fps);

	if( -1 == vioctl( v->fd, VIDIOC_S_PARM,&sfps ) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: VIDIOC_S_PARM fails with:%s", strerror(errno) );
		return -1;
	}
	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: framerate set to %2.2f",fps );
	return 1;
}

static int	v4l2_enum_video_standards( v4l2info *v, char norm )
{
	struct v4l2_input input;
	struct v4l2_standard standard;
	v4l2_std_id current;

	memset( &input, 0,sizeof(input));
	memset( &current,0,sizeof(current));

	if( -1 == vioctl( v->fd, VIDIOC_G_INPUT, &input.index )) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VIDIOC_G_INPUT failed with %s",
				strerror(errno));
		
	}

	if( -1 == vioctl( v->fd, VIDIOC_ENUMINPUT, &input )) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VIDIOC_ENUMINPUT failed with %s",
				strerror(errno));
	}

	if( v->is_vloopback )
		return 1;

	memset( &standard, 0,sizeof(standard));
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

	int std_id = (norm == 'p' ? V4L2_STD_PAL: V4L2_STD_NTSC);

	if( -1 == ioctl( v->fd, VIDIOC_G_STD, &current ) ) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: unable to get video standard from video device:%s",
				strerror(errno));
	}

	if( norm == 'n' ) {
		if( current & V4L2_STD_PAL  ) {
			veejay_msg(VEEJAY_MSG_WARNING,"v4l2: running in NTSC but device in norm %x",
					current );
			std_id = V4L2_STD_NTSC;
		}
	} else if (norm == 'p' ) {
		if( current & V4L2_STD_NTSC ) {
			veejay_msg(VEEJAY_MSG_WARNING,"v4l2: running in PAL but device in norm %x",
					current );
			std_id = V4L2_STD_PAL;
		}
	}


	if (-1 == ioctl (v->fd, VIDIOC_S_STD, &std_id)) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: unable to set video standard: %s", strerror(errno));

		if( errno == ENOTTY ) {
			return 0;
		}

	} else {
		veejay_msg(VEEJAY_MSG_INFO,"v4l2: set video standard %s", v4l2_get_std(std_id));
	}	

	return 1;

}

static	void	v4l2_enum_frame_sizes( v4l2info *v )
{
	struct v4l2_fmtdesc fmtdesc;

	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: discovering supported video formats");

	//@clear mem
	memset( &fmtdesc, 0, sizeof( fmtdesc ));

	int loop_limit = LOOP_LIMIT;

	for( fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		 fmtdesc.type < V4L2_BUF_TYPE_VIDEO_OVERLAY;
		 fmtdesc.type ++ ) {
		while( vioctl( v->fd, VIDIOC_ENUM_FMT, &fmtdesc ) >= 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: Enumerate (%d, Video Capture)", fmtdesc.index);
			veejay_msg(VEEJAY_MSG_DEBUG,"\tindex:%d", fmtdesc.index );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tdescription:%s", fmtdesc.description );
			veejay_msg(VEEJAY_MSG_DEBUG,"\tpixelformat:%c%c%c%c",
						fmtdesc.pixelformat & 0xff,
						(fmtdesc.pixelformat >> 8 ) & 0xff,
						(fmtdesc.pixelformat >> 16) & 0xff,
						(fmtdesc.pixelformat >> 24) & 0xff );
			v->supported_pixel_formats[ v->n_pixel_formats ] = fmtdesc.pixelformat;
			v->n_pixel_formats = (v->n_pixel_formats + 1 ) % loop_limit;

			fmtdesc.index ++;

			loop_limit --; //@ endless loop in enumerating video formats 
			if( loop_limit == 0 )
			{
				veejay_msg(0,"Your capture device driver is trying to trick me into believing it has an infinite number of pixel formats! (count limit=64)");
				return; //@ give up
			}
		}
	}
}

static	int	v4l2_tryout_pixel_format( v4l2info *v, int pf, int w, int h, int *src_w, int *src_h, int *src_pf )
{
	struct v4l2_format format;
	memset( &format, 0, sizeof(format));
	
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
/*	format.fmt.pix.width = w;
	format.fmt.pix.height= h;
	format.fmt.pix.field = V4L2_FIELD_NONE; // V4L2_FIELD_ANY;
	format.fmt.pix.pixelformat = pf;
*/
	if( vioctl( v->fd, VIDIOC_G_FMT, &format ) == -1 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VIDIOC_G_FMT failed with %s", strerror(errno));
	}

	format.fmt.pix.width = w;
	format.fmt.pix.height= h;

	if( v->is_vloopback )
		format.fmt.pix.field = V4L2_FIELD_NONE;
	else
		format.fmt.pix.field = V4L2_FIELD_ANY;

	format.fmt.pix.pixelformat = pf;

	if( vioctl( v->fd, VIDIOC_TRY_FMT, &format ) == 0 ) {
 		if( format.fmt.pix.pixelformat == pf ) {
			if( vioctl( v->fd, VIDIOC_S_FMT, &format ) == -1 ) {
				veejay_msg(0, "v4l2: After VIDIOC_TRY_FMT , VIDIOC_S_FMT fails for: %4.4s",
						(char*) &format.fmt.pix.pixelformat);
				return 0;
			}
			*src_w = format.fmt.pix.width;
			*src_h = format.fmt.pix.height;
			*src_pf = format.fmt.pix.pixelformat;
			return 1;
		}
	}

	if( vioctl( v->fd, VIDIOC_S_FMT, &format ) == -1 ) {
		return 0;
	}
	
	if( -1 == vioctl( v->fd, VIDIOC_G_FMT, &format) ) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VIDIOC_G_FMT failed with %s", strerror(errno));
		return 0;
	} 

	veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: Query %dx%d in %d, device supports capture in %4.4s (%dx%d)",
		w,h,pf,
		(char*) &format.fmt.pix.pixelformat,
		format.fmt.pix.width,
		format.fmt.pix.height
	);

	*src_w = format.fmt.pix.width;
	*src_h = format.fmt.pix.height;
	*src_pf = format.fmt.pix.pixelformat;

	return ( pf == format.fmt.pix.pixelformat);
}

static	int	v4l2_setup_avcodec_capture( v4l2info *v, int wid, int hei, int codec_id )
{
	v->is_jpeg = 1;
	
	v->codec = avcodec_find_decoder( codec_id );
	if(v->codec == NULL) {
		veejay_msg(0, "v4l2: codec %x not found", codec_id);
		return 0;
	}

#if LIBAVCODEC_BUILD > 5400
	v->c	   = avcodec_alloc_context3( v->codec );
#else
	v->c 	   = avcodec_alloc_context();
#endif
	v->c->width= wid;
	v->c->height= hei;
	v->picture = avcodec_alloc_frame();
	v->picture->width = wid;
	v->picture->height = hei;
	v->picture->data[0] = vj_malloc( sizeof(uint8_t) * RUP8(wid * hei + wid));
	v->picture->data[1] = vj_malloc( sizeof(uint8_t) * RUP8(wid * hei + wid));
	v->picture->data[2] = vj_malloc( sizeof(uint8_t) * RUP8(wid * hei + wid));

	if( v->codec->capabilities & CODEC_CAP_TRUNCATED)
		v->c->flags |= CODEC_FLAG_TRUNCATED;

#if LIBAVCODEC_BUILD > 5400
	if( avcodec_open2( v->c, v->codec, NULL ) < 0 )
#else
	if( avcodec_open( v->c, v->codec ) < 0 ) 
#endif
	{
		veejay_msg(0, "v4l2: opening codec%x", codec_id);
		free(v->picture->data[0]);
		free(v->picture->data[1]);
		free(v->picture->data[2]);
		free(v->picture);
		avhelper_free_context( &v->c );
		//av_free(v->c);
		return 0;
	}

	return 1;
}

static	int	v4l2_negotiate_pixel_format( v4l2info *v, int host_fmt, int wid, int hei, int *candidate, int *dw, int *dh)
{
	int native_pixel_format = v4l2_ffmpeg2v4l2( host_fmt );

	char *greycap = getenv( "VEEJAY_V4L2_GREYSCALE_ONLY" );

	//@ does user want grey scale capture
	if( greycap ) {
		int gc = atoi(greycap);
		if( gc == 1 ) {
			int have_gs = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_GREY, wid,hei,dw,dh, candidate ); 
			if( have_gs ) {
				veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Setting grey scale (env)");
				v->grey=1;
				return 1;
			} else {
				veejay_msg(VEEJAY_MSG_WARNING, "v4l2: User requested greyscale video but device does not support it.");
			}
		}
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG,"env VEEJAY_V4L2_GREYSCALE_ONLY=[0|1] not set");
	}
	
	//@ does capture card support our native format
	int supported = v4l2_tryout_pixel_format( v, native_pixel_format, wid, hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports native format" );
		return 1;
	}

	supported      = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_JPEG, wid,hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG,"v4l2: Capture device supports JPEG format" );
		if( v4l2_setup_avcodec_capture( v, wid,hei, CODEC_ID_MJPEG ) == 0 )  {
			veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Failed to intialize MJPEG decoder.");
			return 0;
		}

		*candidate = V4L2_PIX_FMT_JPEG;
		return 1;
	}

	supported     = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_MJPEG, wid, hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports MJPEG format");
		if( v4l2_setup_avcodec_capture( v, wid,hei, CODEC_ID_MJPEG ) == 0 )  {
			veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Failed to intialize MJPEG decoder.");
			return 0;
		}
		*candidate = V4L2_PIX_FMT_MJPEG;
		return 1;
	}
	

	//@ does capture support YUYV or UYVU
	supported     = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_YUYV, wid, hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports YUY2" );
		return 1;
	}

	//@ or RGB 24/32
	supported      = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_RGB24, wid, hei ,dw,dh,candidate);
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports RGB 24 bit" );
		return 1;

	}
	
	supported      = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_RGB32, wid, hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports RGB 32 bit");
		return 1;
	}

	supported      = v4l2_tryout_pixel_format( v, V4L2_PIX_FMT_YUV420, wid, hei,dw,dh,candidate );
	if( supported ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports YUV420");
		return 1;
	}

	//@ try anything else
	int k;
	for( k = 0; k < v->n_pixel_formats; k ++ ) {
		if( v->supported_pixel_formats[k] == 0 )
			continue;
		
		supported = v4l2_pixelformat2ffmpeg( v->supported_pixel_formats[k] );
		if( supported ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Capture device supports %x", supported );
			*candidate = supported;

			return 1;
		}
	}

	veejay_msg(VEEJAY_MSG_ERROR, "v4l2: No supported pixel format found!");

	return 0;
}

static	int	v4l2_configure_format( v4l2info *v, int host_fmt, int wid, int hei )
{
	struct v4l2_format format;

	int cap_pf = 0;
	int src_wid = 0;
	int src_hei = 0;

	memset( &format, 0, sizeof(format));
	
	int res = v4l2_negotiate_pixel_format(v, host_fmt, wid, hei, &cap_pf, &src_wid, &src_hei );

	if( res == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: sorry but I don't know how to handle your capture device just yet!");
		return 0;
	}

	if( src_wid == 0 || src_hei == 0 ) {
		src_wid = wid;
		src_hei = hei;
	}

	if( res == 1 ) {
		v->format.fmt.pix.pixelformat = cap_pf;
		v->format.fmt.pix.width = (uint32_t) src_wid;
		v->format.fmt.pix.height = (uint32_t) src_hei;	

		v->info = yuv_yuv_template( NULL,NULL,NULL,src_wid, src_hei, v4l2_pixelformat2ffmpeg( cap_pf ) );

		yuv_plane_sizes( v->info, &(v->planes[0]),&(v->planes[1]),&(v->planes[2]),&(v->planes[3]) );

		veejay_msg(VEEJAY_MSG_INFO, "v4l2: output in %dx%d, source in %dx%d %x", wid,hei,src_wid,src_hei, cap_pf );
		return 1;
	}

	return 0;
}


static void	v4l2_set_output_pointers( v4l2info *v, void *src )
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
	if(v->threaded)
		lock_(v->video_info);
	v->host_frame->data[0] = Y;
	v->host_frame->data[1] = U;
	v->host_frame->data[2] = V;
	if(v->threaded)
		unlock_(v->video_info);
	return v->host_frame;
}

static	int	v4l2_channel_choose( v4l2info *v, const int pref_channel )
{
	int i;
	int	pref_ok = 0;
	int other   = -1;
	for ( i = 0; i < (pref_channel+1); i ++ ) {
		if( -1 == vioctl( v->fd, VIDIOC_S_INPUT, &i )) {
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
		veejay_msg(0, "v4l2: Cannot identify '%s':%d, %s",file,errno,strerror(errno));
		return 0;
	}
	if( !S_ISCHR(st.st_mode)) {
		veejay_msg(0, "v4l2: '%s' is not a device", file);
		return 0;
	}

	int fd = open( file, O_RDWR | O_NONBLOCK );

	if( -1 == fd ) {
		veejay_msg(0, "v4l2: Cannot open '%s': %d, %s", file, errno, strerror(errno));
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
			return 0;
		}
	}
	if( p.revents & POLLIN || p.revents & POLLPRI )
		return 1;
	
	return err;
}

void *v4l2open ( const char *file, const int input_channel, int host_fmt, int wid, int hei, float fps, char norm  )
{
	//@ in case of thread, check below is done twice
	if(!v4l2_verify_file( file ) ) {
		return NULL;
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Opening Video4Linux2 device: %s ...", file );

	int fd = open( file , O_RDWR );
	int i;

	if( fd <= 0 ) {
		return NULL;
	} else {
		veejay_msg(VEEJAY_MSG_INFO, "v4l2: Video4Linux2 device opened: %s", file );
	}

	v4l2info *v = (v4l2info*) vj_calloc(sizeof(v4l2info));
	if( v == NULL )
		return NULL;

	v->fd = fd;

	int dst_fmt = host_fmt;

	if( v->grey == 1 ) {
		dst_fmt = PIX_FMT_GRAY8;
		veejay_msg(VEEJAY_MSG_WARNING,"v4l2: User requested greyscale video");
	}

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
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: %s does not support read/write interface.", v->capability.card);
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
			veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_V4L2_CAPTURE_METHOD=[0|1] not set , defaulting to mmap() capture");
			cap_read = 1;
		}
 	}

	if( can_read && can_stream == 0)
		v->rw = 1;	

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture driver: %s",
			v->capability.driver );
	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture card: %s",
			v->capability.card );
	if( strncasecmp( (char*) v->capability.card, "Dummy" , 5 ) == 0 ) {
		v->is_vloopback = 1;
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: This is a dummy device.");
	}

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: Capture method: %s",
			(can_read ? "read/write interface" : "mmap"));
	

	//@ which video input ?
	int chan = v4l2_channel_choose( v, input_channel );
	if(chan == -1) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: Video device without input channels ? Guessing 0 is valid...");
		chan = 0;
	}

	if( -1 == vioctl( fd, VIDIOC_S_INPUT, &chan )) {
		int lvl = 0;
		if( errno == EINVAL )
			lvl = VEEJAY_MSG_WARNING;
		veejay_msg(lvl, "v4l2: VIDIOC_S_INPUT failed with %s, arg was %x", strerror(errno),chan);
		if( errno != EINVAL ) {
			free(v);
			close(fd);
			return NULL;
		}
	}

	v->input.index = chan;
	if( -1 == vioctl( fd, VIDIOC_ENUMINPUT, &(v->input)) ) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VIDIOC_ENUMINPUT failed with %s", strerror(errno));
	}


	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Selected video channel %d '%s'",
			chan, v->input.name );

	v4l2_enum_video_standards( v, norm );
	v4l2_enum_frame_sizes(v);

	if( v->is_vloopback == 1 && (wid == 0 || hei == 0 ) ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: please set width and height (-w and -h) for video loopback device");
		free(v);
		close(fd);
		return NULL;
	}


	if( v4l2_configure_format( v, host_fmt, wid, hei ) == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Failed to negotiate pixel format with your capture device.");
		free(v);
		close(fd);
		return NULL;
	}

	if( v4l2_set_framerate( v, fps ) == -1 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: Failed to set frame rate to %2.2f", fps );
	}

	if( v->rw == 0 ) {
		v->reqbuf.type 	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v->reqbuf.memory= V4L2_MEMORY_MMAP;
		v->reqbuf.count = N_FRAMES;

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
		if( v->reqbuf.count > N_FRAMES )
		{
			v->reqbuf.count = N_FRAMES;
			veejay_msg(VEEJAY_MSG_INFO, "v4l2: Using %d buffers", v->reqbuf.count );
		}

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
			v->buffers[i].start  = mmap( NULL, v->buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,fd, v->buffer.m.offset );
		
			if( MAP_FAILED == v->buffers[i].start ) {
				veejay_msg(0,  "v4l2: mmap( NULL, %d , PROT_READ|PROT_WRITE , MAP_SHARED , %d, %d ) failed.",
					v->buffer.length,fd, v->buffer.m.offset );
		//	int k;
		//	for( k = 0; k < i; k ++ ) 
		//		munmap( v->buffer[k].start, v->buffer[k].length );
	
				free(v->buffers);
				//free(v);
				//close(fd);
				//return NULL;
				v->rw = 1;
				goto v4l2_rw_fallback;
			}
		}

		if( v4l2_vidioc_qbuf( v ) == -1 ) {
				veejay_msg(0, "v4l2: VIDIOC_QBUF failed with:%d, %s", errno,strerror(errno));
				free(v->buffers);
			//	free(v);
			//	close(fd);
			//	return NULL;
				v->rw = 1;
				goto v4l2_rw_fallback;
		}

		if( !v4l2_start_video_capture( v ) ) {
			if(cap_read) {
				veejay_msg(VEEJAY_MSG_WARNING, "v4l2: Fallback read/write");
				v->rw = 1;
				v4l2_free_buffers(v);

				//@ close and re-open
				close(v->fd);
 				v->fd = open( file , O_RDWR );
				if(v->fd <= 0 ) {
					veejay_msg(0,"v4l2: Cannot re-open device:%d,%s",errno,strerror(errno));
					free(v->buffers);
					free(v);
					return NULL;
				}
				v->rw = 1;
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

		v->buffers[0].length = v->sizeimage;
		v->buffers[0].start = vj_malloc( RUP8( v->sizeimage * 2 ) );

	}	

	for( i = 0; i < N_FRAMES; i ++ ) {
		v->frames[i] = yuv_yuv_template(NULL,NULL,NULL, wid,hei,dst_fmt);
		v->frames_done[i] = 0;
	}

	v->host_frame = yuv_yuv_template( NULL,NULL,NULL,wid,hei,host_fmt );
	v->frame_ready = 0;
	v->frameidx = 0;


	return v;
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

	if(!v->is_jpeg)
		v4l2_set_output_pointers( v,src );

	if( v->is_jpeg ) {
		AVPacket pkt;
		memset( &pkt, 0, sizeof(AVPacket));
		pkt.data = src;
	    pkt.size = length;

	    avcodec_decode_video2( 
						v->c, 
						v->picture, 
						&got_picture,
						&pkt );

		v->info->data[0] = v->picture->data[0];
		v->info->data[1] = v->picture->data[1];
		v->info->data[2] = v->picture->data[2];
		v->info->stride[0] = v->picture->linesize[0];
		v->info->stride[1] = v->picture->linesize[1];
		v->info->stride[2] = v->picture->linesize[2];
		v->info->format = v->picture->format;
		if(v->info->format == -1) {
			v->info->format = v->c->pix_fmt;
		}
	} 

	if( v->scaler == NULL )
	{
		sws_template templ;     
   		memset(&templ,0,sizeof(sws_template));
   		templ.flags = yuv_which_scaler();
		v->scaler = yuv_init_swscaler( v->info,v->frames[ 0 ], &templ, yuv_sws_get_cpu_flags() );
	}
	
	yuv_convert_and_scale( v->scaler, v->info, v->frames[ v->frameidx ] );
	
	lock_(v->video_info);
		v->frames_done[v->frameidx] = 1;
		v->frame_ready = v->frameidx;
		v->frameidx = (v->frameidx + 1) % N_FRAMES;
	unlock_(v->video_info);
	signal_(v->video_info);

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

	if( (v->rw == 1 && v->pause_read ) || (v->rw == 0 && !v->is_streaming) )
		return 0;

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

	if(!v->is_jpeg)
		v4l2_set_output_pointers( v,src );

 	if( v->is_jpeg ) {
		AVPacket pkt;
		memset( &pkt, 0, sizeof(AVPacket));
		pkt.data = src;
	    pkt.size = length;

	    avcodec_decode_video2( 
						v->c, 
						v->picture, 
						&got_picture,
						&pkt );

		v->info->data[0] = v->picture->data[0];
		v->info->data[1] = v->picture->data[1];
		v->info->data[2] = v->picture->data[2];
		v->info->stride[0] = v->picture->linesize[0];
		v->info->stride[1] = v->picture->linesize[1];
		v->info->stride[2] = v->picture->linesize[2];
		v->info->format = v->picture->format;
		if(v->info->format == -1) {
			v->info->format = v->c->pix_fmt;
		}
	} 

	if( v->scaler == NULL )
	{
		sws_template templ;     
   		memset(&templ,0,sizeof(sws_template));
   		templ.flags = yuv_which_scaler();
		v->scaler = yuv_init_swscaler( v->info,v->frames[ 0 ], &templ, yuv_sws_get_cpu_flags() );
	}
	
	yuv_convert_and_scale( v->scaler, v->info, v->frames[ v->frameidx ] );



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
	
	if( !v->picture )
	{
		for ( i = 0; i < N_FRAMES; i ++ ) {
			if(v->frames[i]->data[0])
				free(v->frames[i]->data[0]);
			if(v->frames[i])
				free(v->frames[i]);
			v->frames[i] = NULL;
		}
	}

	if(v->picture) {
		free(v->picture->data[0]);
		free(v->picture->data[1]);
		free(v->picture->data[2]);
		av_free(v->picture);
		v->picture = NULL;
	}

	if(v->codec) {
#if LIBAVCODEC_BUILD > 5400
		avcodec_close(v->c);
#else
		avcodec_close(v->codec);
		avhelper_free_context( &v->c );
		//if(v->c) free(v->c);
#endif
	}

	if( v->host_frame )
		free(v->host_frame );

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
		veejay_msg(VEEJAY_MSG_WARNING,"Failed to open '%s':%s", prefix, strerror(errno));
		return NULL;
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
		char tmp[1024];
		
		snprintf(tmp, sizeof(tmp) - 1, "%03dDevice %02d%03zu%s%s",
				9, // 'Device xx'
				i, // 'device num'
				(5 + strlen(list[i])), //@ '/dev/' + device
				v4lprefix, // '/dev/'
				list[i]    // 'video0'
				);
		files[i] = strdup(tmp);
		veejay_msg(VEEJAY_MSG_DEBUG, "Found '%s'", list[i]);
    }

	for( i = 0; i < n_devices; i ++ ) {
		if( list[i] )
			free( list[i] );
	}

	files[n_devices] = NULL;
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
	struct timespec req;
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL );
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	int max_retries = 15;
	char *retry = getenv( "VEEJAY_V4L2_MAX_RETRIES" );
	if(retry) {
			max_retries = atoi( retry );
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "env VEEJAY_V4L2_MAX_RETRIES=[Num] not set (defaulting to %d)", max_retries );
	}

	if( max_retries < 0 || max_retries > 99999 ) {
		max_retries = 15;
		veejay_msg(VEEJAY_MSG_WARNING, "v4l2: VEEJAY_V4L2_MAX_RETRIES out of bounds, set to default (%d)",max_retries);
	}

	lock_( v );
	v4l2_thread_info *i = (v4l2_thread_info*) v;
	req.tv_sec = 0;
	req.tv_nsec = 1000 * 1000;

	if(!v4l2_verify_file( i->file ) ) {
		i->stop = 1;
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Not a device file: %s" , i->file );
		pthread_exit(NULL);
		return NULL;
	}

	v4l2info *v4l2 = v4l2open( i->file, i->channel, i->host_fmt, i->wid, i->hei, i->fps, i->norm );
	if( v4l2 == NULL ) {
		veejay_msg(0, "v4l2: error opening v4l2 device '%s'",i->file );
		unlock_(v);
		pthread_exit(NULL);
		return NULL;
	}

	i->v4l2 = v4l2;
	v4l2->video_info = i;
	v4l2->threaded = 1;

	int j,c;
	int planes[4] = { 0,0,0,0 };
	yuv_plane_sizes( v4l2->frames[0], &(planes[0]),&(planes[1]),&(planes[2]),&(planes[3]) );
	
	//@ FIXME:  VEEJAY_V4L2_NO_THREADING=1 and no framebuffer is allocated ...
	
	for( j = 0; j < N_FRAMES; j ++ ) {
		uint8_t *ptr = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(planes[0] * 4) );
		if(!ptr) {
			veejay_msg(0, "v4l2: error allocating memory" );
			unlock_(v);
			pthread_exit(NULL);
			return NULL;
		}

		for( c = 0; c < 4; c ++ ) {
			v4l2->frames[j]->data[c] = ptr + (c * planes[0]);
		}
		v4l2->frames_done[j] = 0;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: allocated %d buffers of %d bytes each", N_FRAMES, planes[0]*4);

	for( c = 0; c < 4; c ++ )
		v4l2->out_planes[c] = planes[c];

	veejay_msg(VEEJAY_MSG_INFO, "v4l2: capture format: %d x %d (%x)",
			v4l2->info->width,v4l2->info->height, v4l2->info->format  );
	
	i->grabbing = 1;
	i->retries  = max_retries;
	unlock_(v);

	while( 1 ) {
		int err = (v4l2->rw);
	
		lock_(i);
		if( v4l2->pause_capture ) {
			if( v4l2->rw == 0 ) {
				if( !v4l2->pause_capture_status) {
			  		v4l2_stop_video_capture(v4l2);
					v4l2_vidioc_qbuf( v4l2 );
				} else {
					v4l2_start_video_capture(v4l2);
				}
			} else {
				v4l2->pause_read = !v4l2->pause_capture_status;
			}
			v4l2->pause_capture = 0; 
		}
		unlock_(i);
		
		if( ( !v4l2->is_streaming && v4l2->rw == 0 ) || ( v4l2->rw == 1 && v4l2->pause_read ) ) {
			nanosleep(&req, NULL);
			continue;
		} 

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
				i->stop = 1;	
				v4l2_close( v4l2 );
				pthread_exit(NULL);
				return NULL;
			} else {
				i->retries --;
			}
		}

		if( i->stop ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Closing video capture device");
			v4l2_close(v4l2);
			pthread_exit(NULL);
			return NULL;
		}
	}
}

int	v4l2_thread_start( v4l2_thread_info *i ) 
{
//	pthread_attr_init( &(i->attr) );
//	pthread_attr_setdetachstate( &(i->attr), PTHREAD_CREATE_DETACHED );

	int err = pthread_create( &(i->thread), NULL, v4l2_grabber_thread, i );

//	pthread_attr_destroy( &(i->attr) );

	if( err == 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4l2: Started video capture thread.");
		return 1;
	}

	veejay_msg(0, "v4l2: failed to start thread: %d, %s", errno, strerror(errno));
	return 0;
}
void	v4l2_set_status( void *d , int status) {
	v4l2info *v = (v4l2info*)d;
	v->pause_capture = 1;
	v->pause_capture_status = status;
}

void	v4l2_thread_set_status( v4l2_thread_info *i, int status )
{
	v4l2info *v = (v4l2info* )i->v4l2;
	if(v->threaded)
		lock_(i);	
	v->pause_capture = 1;
	v->pause_capture_status = status;
	if(v->threaded)
		unlock_(i);
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
		//@ block until a buffer is captured
			while( v->frames_done[v->frame_ready] < 1 ) {
				veejay_msg(VEEJAY_MSG_DEBUG, "waiting for frame %d to become ready",
						v->frame_ready );
				wait_(i);
			}
		unlock_(i);
		
		//@ copy buffer
		veejay_memcpy( dst->data[0], v->frames[ v->frame_ready ]->data[0], v->out_planes[0]);	
		if(!v->grey) {
			veejay_memcpy( dst->data[1], v->frames[v->frame_ready]->data[1], v->out_planes[1]);
			veejay_memcpy( dst->data[2], v->frames[v->frame_ready]->data[2], v->out_planes[2]);
		} else {
			veejay_memset( dst->data[1], 127, dst->uv_len );
			veejay_memset( dst->data[2], 127, dst->uv_len );
		}
		//@ "free" buffer
		lock_(i);
		v->frames_done[v->frameidx] = 0;
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
	struct timespec req;
	v4l2_thread_info *i = (v4l2_thread_info*) vj_calloc(sizeof(v4l2_thread_info));
	i->file = strdup(file);
	i->channel = channel;
	i->host_fmt = host_fmt;
	i->wid = wid;
	i->hei = hei;
	i->fps = fps;
	i->norm = norm;

	req.tv_sec= 0;
	req.tv_nsec = 1000 * 1000;

	pthread_mutexattr_t type;
	pthread_mutexattr_init(&type);
	pthread_mutexattr_settype(&type, PTHREAD_MUTEX_NORMAL);
	pthread_mutex_init(&(i->mutex), &type);

	pthread_cond_init( &(i->cond) , NULL );

	if( v4l2_thread_start( i ) == 0 ) {
		free(i->file);
		free(i);
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Unable to start");
		return NULL;
	}

	int ready     = 0;

	int retries   = 4000;
	
	//@ wait until thread is ready
	while(retries) {
		lock_(i);
		ready = i->grabbing;
		if( i->stop ) {
			ready = i->stop;
		}
		unlock_(i);

		if(ready)
			break;	
		nanosleep(&req, NULL);
		retries--;
	}

	if( i->stop ) {
		veejay_msg(VEEJAY_MSG_ERROR, "v4l2: Grabber thread was told to exit.");
		pthread_mutex_destroy(&(i->mutex));
		pthread_cond_destroy(&(i->cond));
	}
	return i->v4l2;
}
#endif
