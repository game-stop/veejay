/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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
/*


	This file contains code-snippets from the mjpegtools' EditList
	(C) The Mjpegtools project

	http://mjpeg.sourceforge.net
*/
#include <config.h>
#include <string.h>
#include <stdio.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libel/lav_io.h>
#include <libel/vj-el.h>
#include <libvje/vje.h>
#include <libel/vj-avcodec.h>
#include <libel/elcache.h>
#include <libel/pixbuf.h>
#include <limits.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <libvjmem/vjmem.h>
#include <libyuv/yuvconv.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libel/av.h>
#include <veejay/vj-task.h>
#include <liblzo/lzo.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef SUPPORT_READ_DV2
#include "rawdv.h"
#include "vj-dv.h"
#endif
#define MAX_CODECS 50
#define CODEC_ID_YUV420 999
#define CODEC_ID_YUV422 998
#define CODEC_ID_YUV422F 997
#define CODEC_ID_YUV420F 996
#define CODEC_ID_YUVLZO 900
#define DUMMY_FRAMES 2

//@@ !
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
static struct
{
	const char *name;
} _chroma_str[] = 
{
	{	"Unknown"	}, // CHROMAUNKNOWN
	{	"4:2:0"		},
	{	"4:2:2"		},
	{	"4:4:4"		},
	{	"4:1:1"		},
	{	"4:2:0 full range" },
	{	"4:2:2 full range" }
};


static struct
{
        const char *name;
        int  id;
	int	pf;
} _supported_codecs[] = 
{
	{ "vj20", CODEC_ID_YUV420F,		-1 },
	{ "vj22", CODEC_ID_YUV422F,		-1 },
//	{ "", 		CODEC_ID_YUV422F,		-1 },
        { "mjpg" , CODEC_ID_MJPEG ,		0 },
	{ "mjpg" , CODEC_ID_MJPEG , 		1 },
	{ "mjpb", CODEC_ID_MJPEGB,		0 },
	{ "mjpb", CODEC_ID_MJPEGB,		1 },
        { "msmpeg4",CODEC_ID_MPEG4,		-1},
	{ "fmp4", CODEC_ID_MPEG4,		1},
	{ "fmp4", CODEC_ID_MPEG4,		0},
        { "divx"   ,CODEC_ID_MSMPEG4V3,		-1 },
        { "i420",   CODEC_ID_YUV420,		-1 },
        { "i422",   CODEC_ID_YUV422,		-1 },
	{ "dmb1",	CODEC_ID_MJPEG,		0  },
	{ "dmb1",	CODEC_ID_MJPEG,		1 },
	{ "jpeg",	CODEC_ID_MJPEG,		0 },
	{ "jpeg",	CODEC_ID_MJPEG,		1 },
	{ "mjpa",	CODEC_ID_MJPEG,		0  },
	{ "mjpb",	CODEC_ID_MJPEG,		1 },
	{ "jfif",	CODEC_ID_MJPEG,		0  },
	{ "jfif",	CODEC_ID_MJPEG,		1 },
	{ "png",	CODEC_ID_PNG,		-1 },
	{ "mpng",	CODEC_ID_PNG, 		-1 },
#if LIBAVCODEC_BUILD > 4680
	{ "sp5x",   	CODEC_ID_SP5X,		-1 }, /* sunplus motion jpeg video */
#endif
	{ "jpgl",	CODEC_ID_MJPEG, 	0},
	{ "jpgl",	CODEC_ID_MJPEG,		1},
	{ "dvsd",	CODEC_ID_DVVIDEO,	-1},
	{ "dvcp",	CODEC_ID_DVVIDEO,	-1},
	{ "dv",		CODEC_ID_DVVIDEO,	-1},
	{ "dvhd",	CODEC_ID_DVVIDEO,	-1},
	{ "dvp",	CODEC_ID_DVVIDEO,	-1},
	{ "mp4v",	CODEC_ID_MPEG4,		-1},
	{ "xvid",	CODEC_ID_MPEG4,		-1},
	{ "divx",	CODEC_ID_MPEG4,		-1},
	{ "dxsd",	CODEC_ID_MPEG4,		-1},
	{ "mp4s",	CODEC_ID_MPEG4,		-1},
	{ "m4s2",	CODEC_ID_MPEG4,		-1},	
	{ "avc1",	CODEC_ID_H264,		-1},
	{ "h264",	CODEC_ID_H264,		-1},
	{ "x264",	CODEC_ID_H264,		-1},
	{ "davc",	CODEC_ID_H264,		-1},
	{ "div3",	CODEC_ID_MSMPEG4V3,	-1},
	{ "mp43",	CODEC_ID_MSMPEG4V3,	-1},
	{ "mp42",	CODEC_ID_MSMPEG4V2,	-1},
	{ "mpg4",	CODEC_ID_MSMPEG4V1,	-1},
	{ "yuv",	CODEC_ID_YUV420,	-1},
	{ "iyuv",	CODEC_ID_YUV420,	-1},
	{ "i420",	CODEC_ID_YUV420,	-1},
	{ "yv16",	CODEC_ID_YUV422,	-1},
	{ "yv12",	CODEC_ID_YUV420,	-1},
	{ "mlzo",	CODEC_ID_YUVLZO,	-1},
	{ "pict",	0xffff,			-1}, 
	{ "hfyu",	CODEC_ID_HUFFYUV,	-1},
	{ "cyuv",	CODEC_ID_CYUV,		-1},
	{ "svq1",	CODEC_ID_SVQ1,		-1},
	{ "svq3",	CODEC_ID_SVQ3,		-1},
	{ "rpza",	CODEC_ID_RPZA,		-1},
	{ NULL  , 0,				-1},
};

static struct
{
	const char *name;
	int id;
} _supported_fourcc[] =
{
	{ "vj20", CODEC_ID_YUV420F },
	{ "vj22", CODEC_ID_YUV422F },
	{ "yv12", CODEC_ID_YUV420 },
	{ "mjpg",	CODEC_ID_MJPEG	},
	{ "mjpb",	CODEC_ID_MJPEGB },
	{ "dmb1",	CODEC_ID_MJPEG	},
	{ "jpeg",	CODEC_ID_MJPEG	},
	{ "mjpa",	CODEC_ID_MJPEG  },
	{ "jfif",	CODEC_ID_MJPEG  },
	{ "png",	CODEC_ID_PNG	},
	{ "mpng",	CODEC_ID_PNG	},
#if LIBAVCODEC_BUILD > 4680
	{ "sp5x",   	CODEC_ID_SP5X }, /* sunplus motion jpeg video */
#endif
	{ "jpgl",	CODEC_ID_MJPEG  },
	{ "dvsd",	CODEC_ID_DVVIDEO},
	{ "dv",		CODEC_ID_DVVIDEO},
	{ "dvhd",	CODEC_ID_DVVIDEO},
	{ "dvp",	CODEC_ID_DVVIDEO},
	{ "mp4v",	CODEC_ID_MPEG4  },
	{ "xvid",	CODEC_ID_MPEG4	},
	{ "divx",	CODEC_ID_MPEG4  },
	{ "dxsd",	CODEC_ID_MPEG4	},
	{ "mp4s",	CODEC_ID_MPEG4	},
	{ "m4s2",	CODEC_ID_MPEG4	},	
	{ "fmp4",	CODEC_ID_MPEG4 },
	{ "fmp4",	CODEC_ID_MPEG4 },
	{ "avc1",	CODEC_ID_H264	},
	{ "h264",	CODEC_ID_H264	},
	{ "x264",	CODEC_ID_H264 	},
	{ "davc",	CODEC_ID_H264 	},
	{ "div3",	CODEC_ID_MSMPEG4V3 },
	{ "divx",	CODEC_ID_MPEG4 },
	{ "mp43",	CODEC_ID_MSMPEG4V3 },
	{ "mp42",	CODEC_ID_MSMPEG4V2 },
	{ "mpg4",	CODEC_ID_MSMPEG4V1 },
	{ "yuv",	CODEC_ID_YUV420 },
	{ "iyuv",	CODEC_ID_YUV420 },
	{ "i420",	CODEC_ID_YUV420 },
	{ "mlzo",	CODEC_ID_YUVLZO },
	{ "yv16",	CODEC_ID_YUV422 },
	{ "pict",	0xffff	}, /* invalid fourcc */
	{ "hfyu",	CODEC_ID_HUFFYUV},
	{ "cyuv",	CODEC_ID_CYUV   },
	{ "svq1",	CODEC_ID_SVQ1	},
	{ "svq3",	CODEC_ID_SVQ3	},
	{ "rpza",	CODEC_ID_RPZA	},
	{ NULL, 0 }
};

static	int mmap_size = 0;
static struct {
        int i;
        char *s;
} pixfmtstr[] = {
{       -1    ,         "Unknown/Invalid"},
{	PIX_FMT_YUV420P, "YUVPIX_FMT_YUV420P"},
{       PIX_FMT_YUV422P, "4:2:2 planar, Y-Cb-Cr ( 422P )"},
{       PIX_FMT_YUVJ420P, "4:2:0 planar, Y-U-V (420P JPEG)"},
{       PIX_FMT_YUVJ422P, "4:2:2 planar, Y-U-V (422P JPEG)"},
{       PIX_FMT_RGB24,    "RGB 24 bit"},
{       PIX_FMT_BGR24,    "BGR 24 bit"},
{       PIX_FMT_YUV444P,  "YUV 4:4:4 planar, Y-Cb-Cr (444P)"},
{       PIX_FMT_YUVJ444P, "YUV 4:4:4 planar, Y-U-V (444P JPEG)"},
{       PIX_FMT_RGB32,    "RGB 32 bit"},
{       PIX_FMT_BGR32,    "BGR 32 bit"},
{       PIX_FMT_GRAY8,    "Greyscale"},
{       PIX_FMT_RGB32_1,  "RGB 32 bit LE"},
{       0       ,         NULL}
};


static void vj_el_av_close_input_file( AVFormatContext *s ) {
#if LIBAVCODEC_BUILD > 5400
	avformat_close_input(&s);
#else
	av_close_input_file(s);
#endif
}

static const    char    *el_pixfmt_str(int i)
{
        int j;
        for( j = 0; pixfmtstr[j].s != NULL ; j ++ ) {
                if( i == pixfmtstr[j].i )
                        return pixfmtstr[j].s;
        }
        return pixfmtstr[0].s;
}

void	vj_el_set_mmap_size( int size )
{
	mmap_size = size;
}

void	free_av_packet( AVPacket *pkt )
{
	if( pkt ) {
#if (LIBAVFORMAT_VERSION_MAJOR <= 53)
	//	av_destruct_packet(pkt);
#else
	//	if( pkt->destruct )
	//		pkt->destruct(pkt);
#endif
		pkt->data = NULL;
		pkt->size = 0;
	}
	pkt = NULL;
}

typedef struct
{
        AVCodec *codec; // veejay supports only 2 yuv formats internally
        AVFrame *frame;
        AVCodecContext  *context;
        uint8_t *tmp_buffer;
        uint8_t *deinterlace_buffer[3];
	VJFrame *img;
	int fmt;
        int ref;
#ifdef SUPPORT_READ_DV2
	vj_dv_decoder *dv_decoder;
#endif
	void	      *lzo_decoder;
} vj_decoder;

static	vj_decoder *el_codecs[MAX_CODECS];

static	int _el_get_codec(int id, int in_pix_fmt )
{
	int i;
	for( i = 0; _supported_codecs[i].name != NULL ; i ++ )
	{
		if( _supported_codecs[i].id == id)
		{
			if( _supported_codecs[i].pf == -1) {
				return i;
			} else if ( _supported_codecs[i].pf == 0 && (in_pix_fmt == FMT_420F || in_pix_fmt == FMT_420)) {
				return i;
			} else if ( _supported_codecs[i].pf == 1 && (in_pix_fmt == FMT_422F || in_pix_fmt == FMT_422)) {
				return i;		
			}	
		}
	}
	return -1;
}
static	int	_el_get_codec_id( const char *fourcc )
{
	int i;
	int len = strlen( fourcc );
	for( i = 0; _supported_fourcc[i].name != NULL ; i ++ ) {
		if( strncasecmp( fourcc, _supported_fourcc[i].name,len ) == 0 ) {
			return _supported_fourcc[i].id;
		}
	}
	veejay_msg(VEEJAY_MSG_DEBUG,"No decoder found for fourcc %s" , fourcc );
	return -1;
}

int		vj_el_get_decoder_from_fourcc( const char *fourcc )
{
	return _el_get_codec_id( fourcc );
}

static void	_el_free_decoder( vj_decoder *d )
{
	if(d)
	{
		if(d->tmp_buffer)
			free( d->tmp_buffer );
		if(d->deinterlace_buffer[0])
			free(d->deinterlace_buffer[0]);

		if(d->context)
		{
			avcodec_close( d->context ); 
//			free( d->context );
			d->context = NULL;
		}
//		if(d->frame) 
//			free(d->frame);

		if(d->img)
			free(d->img);
	
		free(d);
	}
	d = NULL;
}
#define LARGE_NUM (256*256*256*64)
/*
static int get_buffer(AVCodecContext *context, AVFrame *av_frame){
	vj_decoder *this = (vj_decoder *)context->opaque;
	int width  = context->width;
	int height = context->height;
	VJFrame *img = this->img;	
	avcodec_align_dimensions(context, &width, &height);

	av_frame->opaque = img;

	av_frame->data[0]= img->data[0];
	av_frame->data[1]= img->data[1];
	av_frame->data[2]= img->data[2];

	av_frame->linesize[0] = img->width;
	av_frame->linesize[1] = img->uv_width;
	av_frame->linesize[2] = img->uv_width;

	av_frame->age = LARGE_NUM;

	av_frame->type= FF_BUFFER_TYPE_USER;

	return 0;
}
static void release_buffer(struct AVCodecContext *context, AVFrame *av_frame){
 VJFrame *img = (VJFrame*)av_frame->opaque;
  av_frame->opaque = NULL;
}*/

static int el_pixel_format_ = 1;
static int el_len_ = 0;
static int el_uv_len_ = 0;
static int el_uv_wid_ = 0;
static long mem_chunk_ = 0;
static int el_switch_jpeg_ = 0;
long	vj_el_get_mem_size()
{
	return mem_chunk_;
}
void	vj_el_init_chunk(int size)
{
//@@ chunk size per editlist
	mem_chunk_ = 1024 * size;
}

static int require_same_resolution = 0;

void	vj_el_init(int pf, int switch_jpeg, int dw, int dh, float fps)
{
	int i;
	for( i = 0; i < MAX_CODECS ;i ++ )
		el_codecs[i] = NULL;
	el_pixel_format_ =pf;
#ifdef STRICT_CHECKING
	assert( pf == FMT_422 || pf == FMT_422F );
#endif
	el_switch_jpeg_ = switch_jpeg;

	lav_set_project( dw,dh, fps, pf );

	char *maxFileSize = getenv( "VEEJAY_MAX_FILESIZE" );
	if( maxFileSize != NULL ) {
		uint64_t mfs = atol( maxFileSize );
		if( mfs > AVI_get_MAX_LEN() )
			mfs = AVI_get_MAX_LEN();
		if( mfs > 0 ) {
			AVI_set_MAX_LEN( mfs );
			veejay_msg(VEEJAY_MSG_INFO, "Changed maximum file size to %ld bytes.", mfs );
		}
	}

	if( has_env_setting( "VEEJAY_RUN_MODE", "CLASSIC" ) )
	{
		require_same_resolution = 1;
	}

}

int	vj_el_is_dv(editlist *el)
{
#ifdef SUPPORT_READ_DV2
	return is_dv_resolution(el->video_width, el->video_height);
#else
	return 0;
#endif
}


void	vj_el_prepare()
{
//	reset_cache( el->cache );
}

void	vj_el_break_cache( editlist *el )
{
	if(el) {
		if( el->cache )
			free_cache( el->cache );
		el->cache = NULL;
	}
}

static int never_cache_ = 0;
void	vj_el_set_caching(int status)
{
	never_cache_ = status;
}

void	vj_el_setup_cache( editlist *el )
{
	if(!el->cache && !never_cache_)
	{
		int n_slots = mem_chunk_ / el->max_frame_size;
		if( el->video_frames > n_slots)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Not caching this EDL to memory (Cachesize too small)");
			veejay_msg(VEEJAY_MSG_DEBUG, "You can increase the cache size with the -m commandline parameter");
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "EditList caches at most %d slots (chunk=%d, framesize=%d)", n_slots, mem_chunk_, el->max_frame_size ); 
			el->cache = init_cache( n_slots );
		}
	}
}

void	vj_el_clear_cache( editlist *el )
{
	if( el != NULL ) {
		if(el->cache)
			reset_cache(el->cache);
	}
}

void	vj_el_deinit()
{
	int i;
	for( i = 0; i < MAX_CODECS ;i ++ )
	{
		if( el_codecs[i] )
			_el_free_decoder( el_codecs[i] );
	}
}

int	vj_el_cache_size()
{
	return cache_avail_mb();
}

vj_decoder *_el_new_decoder( int id , int width, int height, float fps, int pixel_format, int out_fmt, long max_frame_size)
{
        vj_decoder *d = (vj_decoder*) vj_calloc(sizeof(vj_decoder));
        if(!d)
	  return NULL;

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
		d->dv_decoder = vj_dv_decoder_init(1, width, height, pixel_format );
#endif	
	if( id == CODEC_ID_YUVLZO )
	{
		d->lzo_decoder = lzo_new();
	} else  if( id != CODEC_ID_YUV422 && id != CODEC_ID_YUV420 && id != CODEC_ID_YUV420F && id != CODEC_ID_YUV422F)
        {
		d->codec = avcodec_find_decoder( id );
#if LIBAVCODEC_BUILD > 5400
		d->context = avcodec_alloc_context3(NULL); /* stripe was here! */
#else
		d->context = avcodec_alloc_context();
#endif
		d->context->opaque = d;
		d->frame = avcodec_alloc_frame();
		d->img = (VJFrame*) vj_calloc(sizeof(VJFrame));
		d->img->width = width;
		d->img->height = height;
		unsigned int tc = task_num_cpus();
		veejay_msg(VEEJAY_MSG_DEBUG,"Using %d FFmpeg decoder threads", tc );
		d->context->thread_type = FF_THREAD_FRAME;
		d->context->thread_count = tc;
#if LIBAVCODEC_BUILD > 5400
		if ( avcodec_open2( d->context, d->codec, NULL ) < 0 )
#else	
		if ( avcodec_open( d->context, d->codec ) < 0 )
#endif
		{
      		       veejay_msg(VEEJAY_MSG_ERROR, "Error initializing decoder %d",id); 
		       _el_free_decoder( d );
       		       return NULL;
      		}
	}

	d->tmp_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * max_frame_size );

        d->fmt = id;
        return d;
}

void	vj_el_set_image_output_size(editlist *el, int dw, int dh, float fps, int pf)
{
/*	if( el->video_width <= 0 || el->video_height <= 0 )
		lav_set_project( dw,dh, fps, pf );
	else
		lav_set_project(
			el->video_width, el->video_height, el->video_fps , el_pixel_format_ );
*/
}

static int _el_probe_for_pixel_fmt( lav_file_t *fd )
{
//	int old = lav_video_cmodel( fd );

	int new = test_video_frame( fd, el_pixel_format_ );

	switch(new)
	{
		case FMT_422:
				veejay_msg(VEEJAY_MSG_DEBUG,"\tPixel format: YUV Planar 4:2:2 [16-235][16-240]");
				break;
		case FMT_422F:
				veejay_msg(VEEJAY_MSG_DEBUG,"\tPixel format: YUV Planar 4:2:2 [JPEG full range]");
				break;
	}
	
	return new;
}

int	get_ffmpeg_pixfmt( int pf )
{
	switch( pf )
	{
		case FMT_422:
			return PIX_FMT_YUV422P;
		case FMT_422F:
			return PIX_FMT_YUVJ422P;
		case 4:
			return PIX_FMT_YUV444P;
		
	}
	return PIX_FMT_YUV422P;
}
int       get_ffmpeg_shift_size(int fmt)
{
	return 0;
}

static long get_max_frame_size( lav_file_t *fd )
{
	long total_frames = lav_video_frames( fd );
	long i;
	long res = 0;
	for (i = 0; i < total_frames; i++)
	{
		long tmp = lav_frame_size( fd, i );
		if( tmp > res ) {
			res = tmp;
		} 
   	}
	return (((res)+8)&~8);
}


int open_video_file(char *filename, editlist * el, int preserve_pathname, int deinter, int force, char override_norm)
{
	int i, n, nerr;
	int chroma=0;
	int _fc;
	int decoder_id = 0;
	const char *compr_type;
	int pix_fmt = -1;
	char *realname = NULL;	

 	if( filename == NULL ) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No files to open!");
		return -1;
	}

	if (preserve_pathname)
		realname = strdup(filename);
	else
		realname = canonicalize_file_name( filename );

	if(realname == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get full path of '%s'", filename);
		return -1;
	}

	for (i = 0; i < el->num_video_files; i++)
	{
		if (strncmp(realname, el->video_file_list[i], strlen( el->video_file_list[i])) == 0)
		{
		    veejay_msg(VEEJAY_MSG_ERROR, "File %s already in editlist", realname);
		    if(realname) free(realname);
		    return -1;
		}
	}

	if (el->num_video_files >= MAX_EDIT_LIST_FILES)
	{
		// mjpeg_error_exit1("Maximum number of video files exceeded");
		veejay_msg(VEEJAY_MSG_ERROR,"Maximum number of video files exceeded\n");
        	if(realname) free(realname);
		return -1; 
    	}

    	if (el->num_video_files >= 1)
		chroma = el->MJPG_chroma;
        
	int in_pixel_format = 	vj_el_pixfmt_to_veejay(
					detect_pixel_format_with_ffmpeg( filename ));
	
 
    	n = el->num_video_files;
	lav_file_t	*elfd = lav_open_input_file(filename,mmap_size );

	el->lav_fd[n] = NULL;

	if (elfd == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error loading videofile '%s'", realname);
	        veejay_msg(VEEJAY_MSG_ERROR,"%s",lav_strerror());
	 	if(realname) free(realname);
		return -1;
	}

	if(lav_video_frames(elfd) < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to load empty video files");
		if(realname) free(realname);
		lav_close(elfd);
		return -1;
	}

	
	_fc = lav_video_MJPG_chroma(elfd);
#ifdef STRICT_CHECKING
	if( in_pixel_format >= 0 )
	{
		if ( _fc == CHROMA422 )
			assert((in_pixel_format == FMT_420) || (in_pixel_format == FMT_420F) || (in_pixel_format == FMT_422 || in_pixel_format == FMT_422F ) );
	}
#endif

	if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN || _fc == CHROMA411 || _fc == CHROMA422F || _fc == CHROMA420F))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Input file %s is not in a valid format (%d)",filename,_fc);
	    	if(realname) free(realname);
		lav_close( elfd );
		return -1;

	}

	if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  	  el->MJPG_chroma = _fc;
	  chroma = _fc;
	}

	pix_fmt = _el_probe_for_pixel_fmt( elfd );
#ifdef STRICT_CHECKING
	//if( in_pixel_format >= 0 )
	//	assert( pix_fmt == in_pixel_format );
#endif
	
	if(pix_fmt < 0 && in_pixel_format < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to determine pixel format");
		if(elfd) lav_close( elfd );
		if(realname) free(realname);
		return -1;
	}

	if( pix_fmt < 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "(!) Using pixelformat detected by FFmpeg (fallback)");
		pix_fmt = in_pixel_format;
	}

	if(el_switch_jpeg_ ) {
		switch(pix_fmt) {
			case FMT_422F: pix_fmt=FMT_422; break;
			case FMT_422:  pix_fmt=FMT_422F;break;	
		}
	}

	el->yuv_taste[n] = pix_fmt;
	el->lav_fd[n] = elfd;
    	el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
    	el->video_file_list[n] = strndup(realname, strlen(realname));
	
	    /* Debug Output */
	if(n == 0 )
	{
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tFull name:       %s", filename, realname);
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames:          %ld", lav_video_frames(el->lav_fd[n]));
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tWidth:           %d", lav_video_width(el->lav_fd[n]));
	    veejay_msg(VEEJAY_MSG_DEBUG,"\tHeight:          %d", lav_video_height(el->lav_fd[n]));
    	
		const char *int_msg;
		switch (lav_video_interlacing(el->lav_fd[n]))
		{
			case LAV_NOT_INTERLACED:
			    int_msg = "Not interlaced";
			    break;
			case LAV_INTER_TOP_FIRST:
			    int_msg = "Top field first";
			    break;
			case LAV_INTER_BOTTOM_FIRST:
			    int_msg = "Bottom field first";
			    break;
			default:
			    int_msg = "Unknown!";
			    break;
		}

		 if( deinter == 1 && (lav_video_interlacing(el->lav_fd[n]) != LAV_NOT_INTERLACED))
			el->auto_deinter = 1;

 	   	veejay_msg(VEEJAY_MSG_DEBUG,"\tInterlacing:      %s", int_msg);
		veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames/sec:       %f", lav_frame_rate(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tSampling format:  %s", _chroma_str[ lav_video_MJPG_chroma(el->lav_fd[n])].name);
		veejay_msg(VEEJAY_MSG_DEBUG,"\tFOURCC:           %s",lav_video_compressor(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio samps:      %ld", lav_audio_clips(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio chans:      %d", lav_audio_channels(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio bits:       %d", lav_audio_bits(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG,"\tAudio rate:       %ld", lav_audio_rate(el->lav_fd[n]));
	}
	else
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "\tFull name	%s",realname);	
		veejay_msg(VEEJAY_MSG_DEBUG, "\tFrames	        %d", lav_video_frames(el->lav_fd[n]));
		veejay_msg(VEEJAY_MSG_DEBUG, "\tDecodes into    %s", _chroma_str[ lav_video_MJPG_chroma( el->lav_fd[n]) ]);
	}

    nerr = 0;
    if (n == 0) {
	/* First file determines parameters */
		if(el->is_empty)
		{	/* Dummy determines parameters */
			if(el->video_height != lav_video_height(el->lav_fd[n]))
				nerr++;
			if(el->video_width != lav_video_width(el->lav_fd[n]))
				nerr++;
		}
		else
		{
			el->video_height = lav_video_height(el->lav_fd[n]);
			el->video_width = lav_video_width(el->lav_fd[n]);
			el->video_inter = lav_video_interlacing(el->lav_fd[n]);
			el->video_fps = lav_frame_rate(el->lav_fd[n]);
		}
		lav_video_clipaspect(el->lav_fd[n],
				       &el->video_sar_width,
				       &el->video_sar_height);

		if (!el->video_norm)
		{
		    /* TODO: This guessing here is a bit dubious but it can be over-ridden */
		    if (el->video_fps > 24.95 && el->video_fps < 25.05)
			el->video_norm = 'p';
		    else if (el->video_fps > 29.92 && el->video_fps <= 30.02)
			el->video_norm = 'n';
		}

		if (!el->video_norm)
		{
			if(override_norm == 'p' || override_norm == 'n')
				el->video_norm = override_norm;
			else
			{
				veejay_msg(VEEJAY_MSG_ERROR,
				"Invalid video norm - override with -N / --norm");
				nerr++;
			}
		}
	
		if(!el->is_empty)
		{
			el->audio_chans = lav_audio_channels(el->lav_fd[n]);
			if (el->audio_chans > 2) {
		  	  veejay_msg(VEEJAY_MSG_ERROR, "File %s has %d audio channels - cant play that!",
			              filename,el->audio_chans);
			   nerr++;
			}
	
			el->has_audio = (el->audio_chans == 0 ? 0: 1);
			el->audio_bits = lav_audio_bits(el->lav_fd[n]);
			el->audio_rate = lav_audio_rate(el->lav_fd[n]);
			el->audio_bps = (el->audio_bits * el->audio_chans + 7) / 8;
		}
		else
		{
			if(lav_audio_channels(el->lav_fd[n]) != el->audio_chans ||
			 lav_audio_rate(el->lav_fd[n]) != el->audio_rate ||
			 lav_audio_bits(el->lav_fd[n]) != el->audio_bits )
				nerr++;
			else
				el->has_audio = 1;
		}
   	 } else {
		/* All files after first have to match the paramters of the first */
	
		if (el->video_height != lav_video_height(el->lav_fd[n]) ||
		    el->video_width != lav_video_width(el->lav_fd[n])) {
		    veejay_msg( (require_same_resolution ? VEEJAY_MSG_ERROR: VEEJAY_MSG_WARNING),
				"File %s: Geometry %dx%d does not match %dx%d (performance penalty).",
				filename, lav_video_width(el->lav_fd[n]),
				lav_video_height(el->lav_fd[n]), el->video_width,
				el->video_height);
		    if( require_same_resolution )
		    	nerr++;
		}
	if (el->video_inter != lav_video_interlacing(el->lav_fd[n])) {
	    if(force)
	    veejay_msg(VEEJAY_MSG_WARNING,"File %s: Interlacing is %d should be %d",
			filename, lav_video_interlacing(el->lav_fd[n]),
			el->video_inter);
		else
		veejay_msg(VEEJAY_MSG_ERROR, "File %s: Interlacing is %d should be %d",
			filename, lav_video_interlacing(el->lav_fd[n]),
			el->video_inter);


	    if(!el->auto_deinter)
		{
			if(force)
			{
				veejay_msg(VEEJAY_MSG_WARNING, "(Force loading video) Auto deinterlacing enabled");
				el->auto_deinter = 1;  
			}
			else
			{
				nerr++;
			}
		}
	}
	/* give a warning on different fps instead of error , this is better 
	   for live performances */
	if (fabs(el->video_fps - lav_frame_rate(el->lav_fd[n])) >
	    0.0000001) {
	    veejay_msg(VEEJAY_MSG_WARNING,"File %s: fps is %3.2f , but playing at %3.2f", filename,
		       lav_frame_rate(el->lav_fd[n]), el->video_fps);
	}
	/* If first file has no audio, we don't care about audio */

	if (el->has_audio) {
	    if( el->audio_rate < 44000 )
	    {
		veejay_msg(VEEJAY_MSG_ERROR, "File %s: Cannot play %d Hz audio. Use at least 44100 Hz or start with -a0", filename, el->audio_rate);
		nerr++;
	    }
            else {
	    if (el->audio_chans != lav_audio_channels(el->lav_fd[n]) ||
		el->audio_bits != lav_audio_bits(el->lav_fd[n]) ||
		el->audio_rate != lav_audio_rate(el->lav_fd[n])) {
		veejay_msg(VEEJAY_MSG_ERROR,"File %s: Mismatched audio properties: %d channels , %d bit %ld Hz",
			   filename, lav_audio_channels(el->lav_fd[n]),
			   lav_audio_bits(el->lav_fd[n]),
			   lav_audio_rate(el->lav_fd[n]) );
		nerr++;
	    }
            }
	}




	if (nerr) {
	    if(el->lav_fd[n]) 
			lav_close( el->lav_fd[n] );
	    el->lav_fd[n] = NULL;
	    if(realname) free(realname);
	    if(el->video_file_list[n]) 
		free(el->video_file_list[n]);
	    el->video_file_list[n] = NULL;
	    return -1;
        }
    }

	compr_type = (const char*) lav_video_compressor(el->lav_fd[n]);

	if(!compr_type)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to read codec information from file");
		if(el->lav_fd[n])
		 lav_close( el->lav_fd[n] );
		el->lav_fd[n] = NULL;
		if(realname) free(realname);
		if(el->video_file_list[n]) 
			free(el->video_file_list[n]);
		el->video_file_list[n] = NULL;
		return -1;
	}
     // initialze a decoder if needed
	decoder_id = _el_get_codec_id( compr_type );
	if(decoder_id > 0 && decoder_id != 0xffff)
	{
		int c_i = _el_get_codec(decoder_id, el->yuv_taste[n] );
		if(c_i == -1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported codec %s",compr_type);
			if( el->lav_fd[n] ) 
				lav_close( el->lav_fd[n] );
			el->lav_fd[n] = NULL;
			if( realname ) free(realname );
			if( el->video_file_list[n]) 
				free(el->video_file_list[n]);
			el->video_file_list[n] = NULL;
			return -1;
		}
		if( el_codecs[c_i] == NULL )
		{
			long max_frame_size = get_max_frame_size( el->lav_fd[n] );
			int ff_pf = get_ffmpeg_pixfmt( el_pixel_format_ );
			el_codecs[c_i] = _el_new_decoder( decoder_id, el->video_width, el->video_height, el->video_fps, el->yuv_taste[ n ],ff_pf, max_frame_size );
			if(!el_codecs[c_i])
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Cannot initialize %s codec", compr_type);
				if( el->lav_fd[n] ) 
					lav_close( el->lav_fd[n] );
				el->lav_fd[n] = NULL;
			    	if(realname) free(realname);
				if( el->video_file_list[n]) 
					free(el->video_file_list[n]);
				el->video_file_list[n] = NULL;
				return -1;
			}

			el->max_frame_sizes[n] = max_frame_size;
		}
	}

	if(decoder_id <= 0)	
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Dont know how to handle %s (fmt %d) %x", compr_type, pix_fmt,decoder_id);
		if(realname) free(realname);
		if( el->video_file_list[n]) free( el->video_file_list[n] );
		if( el->lav_fd[n] ) 
			lav_close( el->lav_fd[n]);
		el->lav_fd[n] = NULL;
		el->video_file_list[n] = NULL;
		return -1;
	}

	if(realname)
		free(realname);

	if(el->is_empty)
	{
		el->video_frames = el->num_frames[0];
		el->video_frames -= DUMMY_FRAMES;
	}
	el->is_empty = 0;	
	el->has_video = 1;
	el->num_video_files ++;
    return n;
}

void		vj_el_show_formats(void)
{
#ifdef SUPPORT_READ_DV2
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Video containers: AVI (up to 32gb), RAW DV and Quicktime");
#else
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Video containers: AVI (up to 32gb) and  Quicktime");
#endif
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Video fourcc (preferred): mjpg, mjpb, mjpa, dv, dvsd,sp5x,dmb1,dvcp,dvhd, yv16,i420");
		veejay_msg(VEEJAY_MSG_DEBUG,
			"Video codecs (preferred): YV16, I420, Motion Jpeg or Digital Video");
		veejay_msg(VEEJAY_MSG_DEBUG,
			"If the video file is made up out of only I-frames (whole images), you can also decode:");
		veejay_msg(VEEJAY_MSG_DEBUG,
			" mpg4,mp4v,svq3,svq1,rpza,hfyu,mp42,mpg43,davc,div3,x264,h264,avc1,m4s2,divx,xvid");
		
#ifdef USE_GDK_PIXBUF
		vj_picture_display_formats();
#endif
		
}


static int	vj_el_dummy_frame( uint8_t *dst[3], editlist *el ,int pix_fmt)
{
	const int uv_len = (el->video_width * el->video_height) / ( ( (pix_fmt==FMT_422||pix_fmt==FMT_422F) ? 2 : 4));
	const int len = el->video_width * el->video_height;
	veejay_memset( dst[0], 16, len );
	veejay_memset( dst[1],128, uv_len );
	veejay_memset( dst[2],128, uv_len );
	return 1;
}

int	vj_el_get_file_fourcc(editlist *el, int num, char *fourcc)
{
	if(num >= el->num_video_files)
		return 0;
	if( fourcc == NULL)
		return 0;

	const char *compr = lav_video_compressor( el->lav_fd[num] );
	if(compr == NULL)
		return 0;
	snprintf(fourcc,4,"%s", compr );
	fourcc[5] = '\0';
	return 1;
}


int	vj_el_bogus_length( editlist *el, long nframe )
{
	uint64_t n = 0;
	
	if( !el->has_video || el->is_empty )
		return 0;

	if( nframe < 0 )
		nframe = 0;
	else if (nframe > el->total_frames )
		nframe = el->total_frames;

	n = el->frame_list[nframe];

	return lav_bogus_video_length( el->lav_fd[ N_EL_FILE(n) ] );
}

int	vj_el_set_bogus_length( editlist *el, long nframe, int len )
{
	uint64_t n = 0;
	
	if( len <= 0 )
		return 0;	

	if( !el->has_video || el->is_empty )
		return 0;
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

	n = el->frame_list[nframe];

	if( !lav_bogus_video_length( el->lav_fd[N_EL_FILE(n)] ) )
		return 0;

	lav_bogus_set_length( el->lav_fd[N_EL_FILE(n)], len );
	
	return 1;
}
#if LIBAVCODEC_BUILD > 5400
static int avcodec_decode_video( AVCodecContext *avctx, AVFrame *picture, int *got_picture, uint8_t *data, int pktsize ) {
	AVPacket pkt;
	veejay_memset( &pkt, 0, sizeof(AVPacket));
	pkt.data = data;
	pkt.size = pktsize;
	return avcodec_decode_video2( avctx, picture, got_picture, &pkt );
}
#endif


int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[3])
{
	if( el->has_video == 0 || el->is_empty )
	{
		vj_el_dummy_frame( dst, el, el->pixel_format );
		return 2;
	}
	sws_template tmpl;
	tmpl.flags = 1;

	int res = 0;
   	uint64_t n;
	int decoder_id =0;
	int c_i = 0;
	vj_decoder *d = NULL;
	int out_pix_fmt = el->pixel_format;
	int in_pix_fmt  = out_pix_fmt;

	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

	if( nframe < 0 || nframe > el->total_frames )
	{
		return 0;
	}	

	n = el->frame_list[nframe];

	in_pix_fmt = el->yuv_taste[N_EL_FILE(n)];

	uint8_t *in_cache = NULL;
	if(el->cache)
		in_cache = get_cached_frame( el->cache, nframe, &res, &decoder_id );
	
	if(! in_cache )	
	{
		res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
       		decoder_id = lav_video_compressor_type( el->lav_fd[N_EL_FILE(n)] );

		if (res < 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR,"Error setting video position: %s",
				  lav_strerror());
			return -1;
 		}
	}

	if( decoder_id == 0xffff )
	{
		VJFrame *srci  = lav_get_frame_ptr( el->lav_fd[ N_EL_FILE(n) ] );
		if( srci == NULL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error decoding Image %ld",
				N_EL_FRAME(n));
			return -1;
		}
#ifdef STRICT_CHECKING
		assert( dst[0] != NULL  && dst[1] != NULL && dst[2] != NULL );
#endif	
		int strides[4] = { el_len_, el_uv_len_, el_uv_len_,0 };
		vj_frame_copy( srci->data, dst, strides );
                return 1;     
	}

	c_i = _el_get_codec( decoder_id , in_pix_fmt);
	if(c_i >= 0 && c_i < MAX_CODECS && el_codecs[c_i] != NULL)
       	       	d = el_codecs[c_i];
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Choked on decoder %x (%d), slot %d",decoder_id,decoder_id, c_i );
		return -1;
	}

	if(!in_cache)
	{
		if( d == NULL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Codec %x was not initialized", decoder_id);
			return -1;
		}
		if(lav_filetype( el->lav_fd[N_EL_FILE(n)] ) != 'x')
		{
		    res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);
		    if(res > 0 && el->cache)
			cache_frame( el->cache, d->tmp_buffer, res, nframe, decoder_id );
		}
	}

	uint8_t *data = ( in_cache == NULL ? d->tmp_buffer: in_cache );
	int inter = 0;
	int got_picture = 0;
	uint8_t *in[3] = { NULL,NULL,NULL };
	int strides[4] = { el_len_, el_uv_len_, el_uv_len_ ,0};
	uint8_t *dataplanes[3] = { data , data + el_len_, data + el_len_ + el_uv_len_ };
	switch( decoder_id )
	{
		case CODEC_ID_YUV420:
			vj_frame_copy1( data,dst[0], el_len_ );
			in[0] = data; in[1] = data+el_len_ ; in[2] = data + el_len_ + (el_len_/4);
			if( el_pixel_format_ == FMT_422F ) {
				yuv_scale_pixels_from_ycbcr( in[0],16.0f,235.0f, el_len_ );
				yuv_scale_pixels_from_ycbcr( in[1],16.0f,240.0f, el_len_/2);
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;	
		case CODEC_ID_YUV420F:
			vj_frame_copy1( data, dst[0], el_len_);
			in[0] = data; in[1] = data + el_len_; in[2] = data + el_len_+(el_len_/4);
			if( el_pixel_format_ == FMT_422 ) {
				yuv_scale_pixels_from_y( dst[0], el_len_ );
				yuv_scale_pixels_from_uv( dst[1], el_len_ / 2 );
			//	yuv_scale_pixels_from_yuv( dst[0],16.0f,235.0f, el_len_ );
			//	yuv_scale_pixels_from_yuv( dst[1],16.0f,240.0f, el_len_/2);
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;
		case CODEC_ID_YUV422:
			vj_frame_copy( dataplanes,dst,strides );
			if( el_pixel_format_ == FMT_422F ) {
				yuv_scale_pixels_from_ycbcr( dst[0],16.0f,235.0f, el_len_ );
				yuv_scale_pixels_from_ycbcr( dst[1],16.0f,240.0f, el_len_/2);
			}	
			return 1;
			break;
		case CODEC_ID_YUV422F:
			vj_frame_copy( dataplanes, dst, strides );
			if( el_pixel_format_ == FMT_422 ) {
				yuv_scale_pixels_from_y( dst[0], el_len_ );
				yuv_scale_pixels_from_uv( dst[1], el_len_/2);
			//	yuv_scale_pixels_from_yuv( dst[0],16.0f,235.0f, el_len_ );
			//	yuv_scale_pixels_from_yuv( dst[1],16.0f,240.0f, el_len_ );
			}

			return 1;
			break;
		case CODEC_ID_DVVIDEO:
#ifdef SUPPORT_READ_DV2
			return vj_dv_decode_frame( d->dv_decoder,  data, dst[0], dst[1], dst[2], el->video_width,el->video_height,
					out_pix_fmt);
#else
			return 0;
#endif			
			break;
		case CODEC_ID_YUVLZO:

			if(  ( in_pix_fmt == FMT_420F || in_pix_fmt == FMT_420 ) ) {
				inter = lzo_decompress420into422(d->lzo_decoder, data,res,dst, el->video_width,el->video_height );
			} 
			else { 
				inter = lzo_decompress_el( d->lzo_decoder, data,res, dst,el->video_width*el->video_height);
			}

			return inter;

			break;			
		default:
		//	inter = lav_video_interlacing(el->lav_fd[N_EL_FILE(n)]);
			d->img->width = el->video_width;
			d->img->uv_width = el->video_width >> 1;
			d->img->data[0] = dst[0];
			d->img->data[1] = dst[1];
			d->img->data[2] = dst[2];
			
			int decode_len = avcodec_decode_video(
				d->context,
				d->frame,
				&got_picture,
				data,
				res
			);

			if(!got_picture)
			{
				veejay_msg(0, "Cannot decode frame ,unable to get whole picture");
				return 0;
			}
		
			if( decode_len <= 0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot decode frame");
				return 0;
			}

			int dst_fmt = get_ffmpeg_pixfmt( el_pixel_format_ );
			int src_fmt = d->context->pix_fmt;
			if( el_switch_jpeg_ ) {
				switch(src_fmt) {
					case PIX_FMT_YUV420P:src_fmt=PIX_FMT_YUVJ420P; break;
					case PIX_FMT_YUVJ420P:src_fmt=PIX_FMT_YUV420P; break;
					case PIX_FMT_YUV422P:src_fmt=PIX_FMT_YUVJ422P; break;
					case PIX_FMT_YUVJ422P:src_fmt=PIX_FMT_YUV422P; break;
				}
			}

			if(!d->frame->opaque)
			{
				if( el->auto_deinter && inter != LAV_NOT_INTERLACED)
				{
					if( d->deinterlace_buffer[0] == NULL ) {
					     d->deinterlace_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * el->video_width * el->video_height * 3);
					     if(!d->deinterlace_buffer[0]) { if(d) free(d); return NULL; }
					     d->deinterlace_buffer[1] = d->deinterlace_buffer[0] + (el->video_width * el->video_height );
					     d->deinterlace_buffer[2] = d->deinterlace_buffer[0] + (2 * el->video_width * el->video_height );
					}
					AVPicture pict2;
					veejay_memset(&pict2,0,sizeof(AVPicture));
					pict2.data[0] = d->deinterlace_buffer[0];
					pict2.data[1] = d->deinterlace_buffer[1];
					pict2.data[2] = d->deinterlace_buffer[2];
					pict2.linesize[1] = el->video_width >> 1;
					pict2.linesize[2] = el->video_width >> 1;
					pict2.linesize[0] = el->video_width;
					
					avpicture_deinterlace(
						&pict2,
						(const AVPicture*) d->frame,
						src_fmt,
						el->video_width,
						el->video_height);
				
					VJFrame *src1 = yuv_yuv_template( d->deinterlace_buffer[0],
								d->deinterlace_buffer[1], d->deinterlace_buffer[2],
								d->frame->width, d->frame->height,	
								src_fmt );
					VJFrame *dst1 = yuv_yuv_template( dst[0],dst[1],dst[2],
								el->video_width, el->video_height,
								dst_fmt );
					el->scaler = 
						yuv_init_cached_swscaler( el->scaler, 	
										src1,
										dst1,
										&tmpl,
										yuv_sws_get_cpu_flags() );
	
					yuv_convert_any3( el->scaler, src1,d->frame->linesize,dst1,src1->format,dst1->format);

					free(src1);
					free(dst1);

				}
				else
				{
					VJFrame *src1 = yuv_yuv_template( d->frame->data[0],
								d->frame->data[1], d->frame->data[2],
								d->frame->width,d->frame->height,
								src_fmt );
					VJFrame *dst1 = yuv_yuv_template( dst[0],dst[1],dst[2],
								el->video_width,el->video_height,
								dst_fmt );
					el->scaler = 
						yuv_init_cached_swscaler( el->scaler, 	
										src1,
										dst1,
										&tmpl,
										yuv_sws_get_cpu_flags() );

					yuv_convert_any3( el->scaler, src1,d->frame->linesize,dst1,src1->format,dst1->format);
					free(src1);
					free(dst1);
				}
			}
			else
			{
				dst[0] = d->frame->data[0];
				dst[1] = d->frame->data[1];
				dst[2] = d->frame->data[2];
			}
			return 1;
			break;
	}

	veejay_msg(VEEJAY_MSG_ERROR, "Error decoding frame %ld", nframe);
	return 0;  
}

int	detect_pixel_format_with_ffmpeg( const char *filename )
{
	char errbuf[512];
	AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVFormatContext *avformat_ctx = NULL;
	AVPacket pkt;
#if LIBAVCODEC_BUILD > 5400
	int err = avformat_open_input( &avformat_ctx, filename, NULL, NULL );
#else
	int err = av_open_input_file( &avformat_ctx,filename,NULL,0,NULL );
#endif

	if(err < 0 ) {
		av_strerror( err, errbuf, sizeof(errbuf));
		veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s", filename,errbuf );
		return -1;
	}

#if LIBAVCODEC_BUILD > 5400
	err = avformat_find_stream_info( avformat_ctx, NULL );
#else
	err = av_find_stream_info( avformat_ctx );
#endif

#ifdef STRICT_CHECKING
#if  (LIBAVFORMAT_VERSION_MAJOR <= 53)
	av_dump_format( avformat_ctx,0,filename,0 );
#endif
#endif

	if( err < 0 ) {
		av_strerror( err, errbuf, sizeof(errbuf));
		veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s" ,filename,errbuf );
	}

	if(err < 0 ) {
		vj_el_av_close_input_file( avformat_ctx );
		return -1;
	}
	
	unsigned int i,j;
	unsigned int n = avformat_ctx->nb_streams;
	int vi = -1;
	int pix_fmt = -1;
	veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: File has %d %s", n, ( n == 1 ? "stream" : "streams") );

	if( n > 65535 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "FFmpeg: Probably doesn't work, got garbage from open_input." );
		veejay_msg(VEEJAY_MSG_WARNING, "Build veejay with an older ffmpeg (for example, FFmpeg 0.8.15 \"Love\")");
		return -1;
	}


	for( i=0; i < n; i ++ )
	{
		if( avformat_ctx->streams[i]->codec )
		{
			if( avformat_ctx->streams[i]->codec->codec_type < CODEC_ID_FIRST_AUDIO )
			{
				int sup_codec = 0;
				for( j = 0; _supported_codecs[j].name != NULL; j ++ ) {
					if( avformat_ctx->streams[i]->codec->codec_id == _supported_codecs[j].id ) {
						sup_codec = 1;
						goto further;
					}
				}	
further:
				if( !sup_codec ) {
					veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: Unrecognized file %s",		
						avformat_ctx->streams[i]->codec->codec_name );
					vj_el_av_close_input_file( avformat_ctx );
					return -1;
				}
				codec = avcodec_find_decoder( avformat_ctx->streams[i]->codec->codec_id );
				if( codec == NULL ) 
				{
					veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: Unable to find decoder for codec %s", 
						avformat_ctx->streams[i]->codec->codec_name);	
					vj_el_av_close_input_file( avformat_ctx );
					return -1;
				}
				vi = i;
				break;
			}
		}
	}

	if( vi == -1 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: No video streams found");
		vj_el_av_close_input_file( avformat_ctx );
		return -1;
	}

	codec_ctx = avformat_ctx->streams[vi]->codec;
#if LIBAVCODEC_BUILD > 5400
	if ( avcodec_open2( codec_ctx, codec, NULL ) < 0 )
#else
	if ( avcodec_open( codec_ctx, codec ) < 0 ) 
#endif
	{
		veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: Unable to open %s decoder (codec %x)",
				codec_ctx->codec_name, codec_ctx->codec_id);
		return -1;
	}

	veejay_memset( &pkt, 0, sizeof(AVPacket));
	AVFrame *f = avcodec_alloc_frame();

	int got_picture = 0;
	while( (av_read_frame(avformat_ctx, &pkt) >= 0 ) ) {
		avcodec_decode_video( codec_ctx,f,&got_picture, pkt.data, pkt.size );

		if( got_picture ) {
			break;
		}
	}
	
	if(!got_picture) {
		veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: Error while reading %s", filename );
		av_free(f);
		free_av_packet(&pkt); 
		avcodec_close( codec_ctx );
		vj_el_av_close_input_file( avformat_ctx );
		return -1;
	}

	pix_fmt = codec_ctx->pix_fmt;
		
	veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg reports Video [%s] %dx%d. Pixel format: %s Has B frames: %s (%s)",
		codec_ctx->codec_name, codec_ctx->width,codec_ctx->height, el_pixfmt_str(codec_ctx->pix_fmt), 
		(codec_ctx->has_b_frames ? "Yes" : "No"), filename );

	free_av_packet(&pkt); 
	avcodec_close( codec_ctx );
	vj_el_av_close_input_file( avformat_ctx );
	av_free(f);

	return pix_fmt;
}

int	vj_el_pixfmt_to_veejay(int pix_fmt ) {
	int input_pix_fmt = -1;
	switch( pix_fmt ) {	
		case PIX_FMT_YUV420P:	input_pix_fmt = FMT_420;	break;	
		case PIX_FMT_YUV422P:	input_pix_fmt = FMT_422;	break;
		case PIX_FMT_YUVJ420P:	input_pix_fmt = FMT_420F;	break;
		case PIX_FMT_YUVJ422P:	input_pix_fmt = FMT_422F;	break;
	}
	
	return input_pix_fmt;	
}
 
int	test_video_frame( lav_file_t *lav,int out_pix_fmt)
{
	int in_pix_fmt  = 0;

	int res = lav_set_video_position( lav,  0);
	if( res < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error setting frame 0: %s", lav_strerror());
		return -1;
	}
	
    	int decoder_id = lav_video_compressor_type( lav );

	if( decoder_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot play that file, unsupported codec");
		return -1;
	}

	if(lav_filetype( lav ) == 'x')
	{
		return out_pix_fmt;
	}

	switch( lav->MJPG_chroma )
	{
		case CHROMA420F:
			in_pix_fmt = FMT_420F;break;
		case CHROMA422F:
			in_pix_fmt = FMT_422F;break;
		case CHROMA420:
			in_pix_fmt = FMT_420; break;
		case CHROMA422:
		case CHROMA411:
			in_pix_fmt = FMT_422; break;
		default:
			veejay_msg(0 ,"Unsupported pixel format");
			break;			
	}
	long max_frame_size = get_max_frame_size( lav );

	vj_decoder *d = _el_new_decoder(
				decoder_id,
				lav_video_width( lav),
				lav_video_height( lav),
			   	(float) lav_frame_rate( lav ),
				in_pix_fmt,
				out_pix_fmt,
				max_frame_size );

	if(!d)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Choked on decoder %x", decoder_id);
		return -1;
	}

	res = lav_read_frame( lav, d->tmp_buffer);

	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error reading frame: %s", lav_strerror());
		_el_free_decoder( d );
		return -1;
	}
	
	int got_picture = 0;
	int ret = -1;
	int len = 0;
	switch( decoder_id )
	{
		case CODEC_ID_YUV420F:
			ret = FMT_420F;
			break;
		case CODEC_ID_YUV422F:
			ret = FMT_422F;
			break;
		case CODEC_ID_YUV420:
			ret = FMT_420;
			break;
		case CODEC_ID_YUV422:
			ret = FMT_422;
			break;
		case CODEC_ID_DVVIDEO:
#ifdef SUPPORT_READ_DV2
			ret = vj_dv_scan_frame( d->dv_decoder, d->tmp_buffer );//FIXME
			if( ret == FMT_420 || ret == FMT_420F )
				lav->MJPG_chroma = CHROMA420;
			else
				lav->MJPG_chroma = CHROMA422;
#endif
			break;
		case CODEC_ID_YUVLZO:
			ret = FMT_422;
			if ( in_pix_fmt != ret )	
			{
				//@ correct chroma 
				if( ret == FMT_420 || ret == FMT_420F )
					lav->MJPG_chroma = CHROMA420;
				else
					lav->MJPG_chroma = CHROMA422;
			}

			break;
		default:

			len = avcodec_decode_video(
				d->context,
				d->frame,
				&got_picture,
				d->tmp_buffer,
				res
			);

			if(!got_picture || len <= 0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to get whole picture");
				ret = -1;
			}
			else
				switch( d->context->pix_fmt )
				{
					case PIX_FMT_YUV420P: ret = FMT_420; break;
					case PIX_FMT_YUV422P: ret = FMT_422; break;
					case PIX_FMT_YUVJ420P:ret = FMT_420F;break;
					case PIX_FMT_YUVJ422P:ret = FMT_422F;break;
					default:
						ret= d->context->pix_fmt;
						break;
				}
			
				break;	

	}

//	_el_free_decoder( d );
	
	return ret;  
}



int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst)
{
    int ret = 0;
    uint64_t n;	
	int ns0, ns1;

	if(el->is_empty)
	{
		int ns = el->audio_rate / el->video_fps;
		veejay_memset( dst, 0, sizeof(uint8_t) * ns * el->audio_bps );
		return 1;
	}

    if (!el->has_audio)
		return 0;
    
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    if (ret < 0)
    {
	    veejay_msg(0,"Unable to seek to frame position %ld", ns0);
		return -1;
	}

    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], dst, (ns1 - ns0));
    if (ret < 0) {
	    veejay_msg(0, "Error reading audio data at frame position %ld", ns0);
		int ns = el->audio_rate / el->video_fps;
		veejay_memset( dst, 0, sizeof(uint8_t) * ns * el->audio_bps );
		return 1;
	}
    
	return (ns1 - ns0);
}

int	vj_el_init_420_frame(editlist *el, VJFrame *frame)
{
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height>>1);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height >> 1;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 1;
	frame->shift_h = 1;
	frame->width = el->video_width;
	frame->height = el->video_height;
	frame->ssm = 0;
	frame->stride[0] = el->video_width;
	frame->stride[1] = frame->stride[2] = frame->stride[0]/2;
	frame->format = get_ffmpeg_pixfmt(el_pixel_format_);
	return 1;
}


int	vj_el_init_422_frame(editlist *el, VJFrame *frame)
{
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 0;
	frame->shift_h = 1;
	frame->width = el->video_width;
	frame->height = el->video_height;
	frame->ssm = 0;
	frame->stride[0] = el->video_width;
	frame->stride[1] = frame->stride[2] = frame->stride[0]/2;
	frame->format = get_ffmpeg_pixfmt( el_pixel_format_ );
	return 1;
}

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int num )
{
	// get audio from current frame + n frames
    int ret = 0;
    uint64_t n;	
    int ns0, ns1;

    if (!el->has_audio)
	return 0;

    if  (!el->has_video)
	{
		int size = el->audio_rate / el->video_fps * el->audio_bps;
		veejay_memset(dst,0,size);
		return size;
	}
	if (nframe < 0)
		nframe = 0;

	if (nframe > el->total_frames)
		nframe = el->total_frames;

    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + num) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    if (ret < 0)
		return -1;

    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], dst, (ns1 - ns0));
    if (ret < 0)
		return -1;

    return (ns1 - ns0);

}


editlist *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt)
{
	editlist *el = vj_calloc(sizeof(editlist));
	if(!el) {
		return NULL;
	}
	el->MJPG_chroma = chroma;
	el->video_norm = norm;
	el->is_empty = 1;
	el->is_clone = 1;
	el->has_audio = 0;
	el->audio_rate = 0;
	el->audio_bits = 0;
	el->audio_bps = 0;
	el->audio_chans = 0;
	el->num_video_files = 0;
	el->video_width = width;
	el->video_height = height;
	el->video_frames = DUMMY_FRAMES; 
	el->total_frames = el->video_frames - 1;
	el->video_fps = fps;
	el->video_inter = LAV_NOT_INTERLACED;

	/* output pixel format */
	if( fmt == -1 )
		el->pixel_format = el_pixel_format_;
	
	el->pixel_format = fmt;

	el->auto_deinter = deinterlace;
	el->max_frame_size = width * height * 3;
	el->last_afile = -1;
	el->last_apos = 0;
	el->frame_list = NULL;
	el->has_video = 0;
	el->cache = NULL;
	el_len_ = el->video_width * el->video_height;
	el_uv_len_ = el_len_;

	switch(fmt)
	{
		case FMT_422:
		case FMT_422F:
			el_uv_len_ = el_len_ / 2;
			el_uv_wid_ = el->video_width;
			break;
	}

	return el;
}

editlist *vj_el_init_with_args(char **filename, int num_files, int flags, int deinterlace, int force	,char norm , int fmt)
{
	editlist *el = vj_calloc(sizeof(editlist));
	FILE *fd;
	char line[1024];
	uint64_t	index_list[MAX_EDIT_LIST_FILES];
	int	num_list_files;
	long i,nf=0;
	int n1=0;
	int n2=0;
	long nl=0;
	uint64_t n =0;
	veejay_memset(line,0,sizeof(line));
	if(!el) return NULL;

	el->has_video = 1; //assume we get it   
	el->MJPG_chroma = CHROMA420;
	el->is_empty  = 0;
	if(!filename[0] || filename == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tInvalid filename given");
		vj_el_free(el);
		return NULL;	
	}

	if(strcmp(filename[0], "+p" ) == 0 || strcmp(filename[0], "+n") == 0 ) {
		el->video_norm = filename[0][1];
		nf = 1;
	}


	if(force)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Forcing load on interlacing and gop_size");
	}

	for (; nf < num_files; nf++)
	{
		/* Check if file really exists, is mounted etc... */
		struct stat fileinfo;
		if(stat( filename[nf], &fileinfo)!= 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to access file '%s'",filename[nf] );
			//vj_el_free(el);
			return NULL;
		} 
			fd = fopen(filename[nf], "r");
			if (fd <= 0)
			{
			   	 veejay_msg(VEEJAY_MSG_ERROR,"Error opening %s:", filename[nf]);
			 	 vj_el_free(el);
	 			 return NULL;
			}
		
			fgets(line, 1024, fd);
			if (strcmp(line, "LAV Edit List\n") == 0)
			{
				   	/* Ok, it is a edit list */
			    	veejay_msg(VEEJAY_MSG_DEBUG, "Edit list %s opened", filename[nf]);
			    	/* Read second line: Video norm */
			    	fgets(line, 1024, fd);
			    	if (line[0] != 'N' && line[0] != 'n' && line[0] != 'P' && line[0] != 'p')
				{
					veejay_msg(VEEJAY_MSG_ERROR,"Edit list second line is not NTSC/PAL");
					vj_el_free(el);
					return NULL;
				}
				veejay_msg(VEEJAY_MSG_DEBUG,"Edit list norm is %s", line[0] =='N' || line[0] == 'n' ? "NTSC" : "PAL" );
			    	if (line[0] == 'N' || line[0] == 'n')
					{
						if (el->video_norm == 'p')
						{	
							veejay_msg(VEEJAY_MSG_WARNING, "Norm already set to PAL, ignoring new norm 'NTSC'");
						}
						else el->video_norm = 'n';
					}
		    		else
				{
					if (el->video_norm == 'n')
					{
					    	veejay_msg(VEEJAY_MSG_WARNING,"Norm already set to NTSC, ignoring new norm PAL");
					}
					else
						el->video_norm = 'p';
			    	}
		   	 	/* read third line: Number of files */
		    		fgets(line, 1024, fd);
		    		sscanf(line, "%d", &num_list_files);

		   	 	veejay_msg(VEEJAY_MSG_DEBUG, "Edit list contains %d files", num_list_files);
		   		/* read files */

			    	for (i = 0; i < num_list_files; i++)
				{
					fgets(line, 1024, fd);
					n = strlen(line);

					if (line[n - 1] != '\n')
					{
					    veejay_msg(VEEJAY_MSG_ERROR, "Filename in edit list too long");
						vj_el_free(el);
						return NULL;
					}

					line[n - 1] = 0;	/* Get rid of \n at end */
	
					index_list[i] =
					    open_video_file(line, el, flags, deinterlace,force,norm);
	
					if(index_list[i]< 0)
					{
						vj_el_free(el);
						return NULL;
					}

				/*	el->frame_list = (uint64_t *) realloc(el->frame_list,
					      (el->video_frames +
					       el->num_frames[i]) *
					      sizeof(uint64_t));
					if (el->frame_list==NULL)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
						vj_el_free(el);
						return NULL;
					}
					
					long x = el->num_frames[i] + el->total_frames;	
					long j;
	    				for (j = el->video_frames; j < x; j++)
					{
						el->frame_list[el->video_frames] = EL_ENTRY(n, j);
						el->video_frames++;
					}*/


		   		 }
	
				    /* Read edit list entries */
			
				    while (fgets(line, 1024, fd))
				   {
						if (line[0] != ':')
						{	/* ignore lines starting with a : */
						    sscanf(line, "%ld %d %d", &nl, &n1, &n2);
			   
						    if (nl < 0 || nl >= num_list_files)
							{
					    		veejay_msg(VEEJAY_MSG_ERROR,"Wrong file number in edit list entry");
								vj_el_free(el);
								return NULL;
			    			}

						    if (n1 < 0)
								n1 = 0;
			  	    		if (n2 >= el->num_frames[index_list[nl]])
								n2 = el->num_frames[index_list[nl]];
			    			if (n2 < n1)
								continue;
	
							el->frame_list = (uint64_t *) realloc(el->frame_list,
								      (el->video_frames +
								       n2 - n1 +
								       1) * sizeof(uint64_t));
	
				   			if (el->frame_list==NULL)
							{
								veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
								vj_el_free(el);
								return NULL;
							}
	
               		     				for (i = n1; i <= n2; i++)
							{
								el->frame_list[el->video_frames++] =  EL_ENTRY( index_list[ nl], i);
							}
						}
		    		} /* done reading editlist entries */
			fclose(fd);
			}
			else
			{
	    		/* Not an edit list - should be a ordinary video file */
	    			fclose(fd);

		     		n = open_video_file(filename[nf], el, flags, deinterlace,force,norm);
				if(n >= 0 )
				{
			       		el->frame_list = (uint64_t *) realloc(el->frame_list,
					      (el->video_frames +
					       el->num_frames[n]) *
					      sizeof(uint64_t));
					if (el->frame_list==NULL)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
						vj_el_free(el);
						return NULL;
					}

	    				for (i = 0; i < el->num_frames[n]; i++)
					{
						el->frame_list[el->video_frames++] = EL_ENTRY(n, i);
					}
				}
			}
	}

	if( el->num_video_files == 0 || 
		el->video_width == 0 || el->video_height == 0 || el->video_frames < 1)
	{
		if( el->video_frames < 1 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "\tFile has no video frames", el->video_frames );
			vj_el_free(el); return NULL;
		}
		if( el->num_video_files == 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "\tNo videofiles in EDL");
			vj_el_free(el); return NULL;
		}
		if( el->video_height == 0 || el->video_width == 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "\tImage dimensions unknown");
			vj_el_free(el); return NULL;
		}
		vj_el_free(el);
		return NULL;
	}


	long cur_max_frame_size = 0;
	for ( i = 0; i < el->num_video_files; i ++ ) {
		if( el->max_frame_sizes[i] > cur_max_frame_size )
		 cur_max_frame_size = el->max_frame_sizes[i];
	}

	if( cur_max_frame_size == 0 ) {
		for( i = 0; i < el->num_video_files; i ++ ) {
			long tmp = get_max_frame_size( el->lav_fd[i] );
			if( tmp > cur_max_frame_size )
				cur_max_frame_size = tmp;
		}
	}

	el->max_frame_size = cur_max_frame_size;
	
	/* Pick a pixel format */
	el->pixel_format = el_pixel_format_;
	el->total_frames = el->video_frames-1;
	/* Help for audio positioning */

	el->last_afile = -1;
	veejay_msg(VEEJAY_MSG_DEBUG, "\tThere are %" PRIu64 " video frames", el->total_frames );

    //el->auto_deinter = auto_deinter;
    //if(el->video_inter != 0 ) el->auto_deinter = 0;
	el->auto_deinter = 0;

	el_len_ = el->video_width * el->video_height;
	el_uv_len_ = el_len_;

	switch( el_pixel_format_ )
	{
		case FMT_422:
		case FMT_422F:
			el_uv_len_ = el_len_ / 2;
			el_uv_wid_ = el->video_width;
			break;
	}

	return el;
}


void	vj_el_free(editlist *el)
{
	if(!el)
		return;

	int i;
	if(el->is_clone)
	{
		for( i = 0; i < el->num_video_files; i ++ )
		{
			if( el->video_file_list[i]) {
				free(el->video_file_list[i] );
				el->video_file_list[i] = NULL;
			}
		}
	}
	else
	{
		for ( i = 0; i < el->num_video_files; i ++ )
		{
			if( el->lav_fd[i] ) 
			{
				lav_close( el->lav_fd[i] );
				el->lav_fd[i] = NULL;
			}
			if( el->video_file_list[i]) {
				free(el->video_file_list[i]);
				el->video_file_list[i] = NULL;
			}
		}
	}

	if( el->cache ) {
		free_cache( el->cache );
		el->cache = NULL;
	}
	if( el->frame_list ) {
		free(el->frame_list );
		el->frame_list = NULL;
	}
	if( el->scaler )
		yuv_free_swscaler( el->scaler );
	
	free(el);

	el = NULL;
}

void	vj_el_print(editlist *el)
{
	int i;
	char timecode[64];
	char interlacing[64];
	MPEG_timecode_t ttc;
	veejay_msg(VEEJAY_MSG_INFO,"EditList settings: Video:%dx%d@%2.2f %s\tAudio:%d Hz/%d channels/%d bits",
		el->video_width,el->video_height,el->video_fps,(el->video_norm=='p' ? "PAL" :"NTSC"),
		el->audio_rate, el->audio_chans, el->audio_bits);
	for(i=0; i < el->num_video_files ; i++)
	{
		long num_frames = lav_video_frames(el->lav_fd[i]);
		MPEG_timecode_t tc;
		switch( lav_video_interlacing(el->lav_fd[i]))
		{
			case LAV_NOT_INTERLACED:
				sprintf(interlacing, "Not interlaced"); break;
			case LAV_INTER_TOP_FIRST:
				sprintf(interlacing,"Top field first"); break;
			case LAV_INTER_BOTTOM_FIRST:
				sprintf(interlacing, "Bottom field first"); break;
			default:
				sprintf(interlacing, "Unknown !"); break;
		} 

		mpeg_timecode(&tc, num_frames,
				mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
				el->video_fps );

		sprintf( timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f );

		veejay_msg(VEEJAY_MSG_INFO, "\tFile %s (%s) with %ld frames (total duration %s)",
			el->video_file_list[i],
			interlacing,
			num_frames,
			timecode );
			
	}

	mpeg_timecode(&ttc, el->video_frames,
			mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
			el->video_fps );

	sprintf( timecode, "%2d:%2.2d:%2.2d:%2.2d", ttc.h, ttc.m, ttc.s, ttc.f );


	veejay_msg(VEEJAY_MSG_INFO, "\tDuration: %s (%2d hours, %2d minutes)(%ld frames)", timecode,ttc.h,ttc.m,el->video_frames);
}

MPEG_timecode_t get_timecode(editlist *el, long num_frames)
{
	MPEG_timecode_t tc;
	veejay_memset(&tc,0,sizeof(tc));
	mpeg_timecode(&tc, num_frames,
			mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
			el->video_fps );
	return tc;
}

int	vj_el_get_file_entry(editlist *el, long *start_pos, long *end_pos, long entry )
{
	if(entry >= el->num_video_files)
		return 0;

	int64_t	n = (int64_t) entry;
	int64_t i = 0;

	if( el->video_file_list[ n ] == NULL )
		return 0;

	*start_pos = 0;

	for( i = 0;i < n ; i ++ )
		*start_pos += el->num_frames[i];

	*end_pos = (*start_pos + el->num_frames[n] - 1);

	return 1;
}



char *vj_el_write_line_ascii( editlist *el, int *bytes_written )
{
	if(el == NULL || el->is_empty)
		return NULL;

	int num_files	= 0;
	int64_t oldfile, oldframe, of1,ofr;
	int64_t index[MAX_EDIT_LIST_FILES]; //@ TRACE
	int64_t n,n1=0;
	char 	*result = NULL;
	int64_t j = 0;
	int64_t n2 = el->total_frames;
	char	filename[2048];		    //@ TRACE
	char	fourcc[6];                  //@ TRACE

#ifdef STRICT_CHECKING
	int	dbg_buflen = 0;
#endif
	/* get which files are actually referenced in the edit list entries */

	int est_len = 0;
   	for (j = 0; j < MAX_EDIT_LIST_FILES; j++)
		index[j] = -1;

   	for (j = n1; j <= n2; j++)
	{
		n = el->frame_list[j];
		index[N_EL_FILE(n)] = 1;
	}
   
	num_files = 0;
	int nnf   = 0;
	long len  = 0;

   	for (j = 0; j < MAX_EDIT_LIST_FILES; j++)
	{
		if (index[j] >= 0 && el->video_file_list[j] != NULL )
		{
			index[j] = (int64_t)num_files;
			nnf ++;
			len     += (strlen(el->video_file_list[j])) + 25 + 20;
			num_files ++;
		}
	}


	n = el->frame_list[n1];
	oldfile = index[ N_EL_FILE(n) ];
   	oldframe = N_EL_FRAME(n);

	n1 = n;
	of1 = oldfile;
	ofr = oldframe;

	for (j = n1+1; j <= n2; j++)
	{
		n = el->frame_list[j];
		if ( index[ N_EL_FILE(n) ] != oldfile ||
			N_EL_FRAME(n) != oldframe + 1 )	{
				len += (3*16);
			}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
	}

	n = n1;
	oldfile = of1;
	oldframe = ofr;
	
	n1 = 0;
 
	est_len = 64 + len;

#ifdef STRICT_CHECKING
	dbg_buflen = est_len;
#endif
	result = (char*) vj_calloc(sizeof(char) * est_len );
	sprintf(result, "%04d",nnf );

	for (j = 0; j < MAX_EDIT_LIST_FILES; j++)
	{
		if (index[j] >= 0 && el->video_file_list[j] != NULL)
		{
			snprintf(fourcc,sizeof(fourcc),"%s", "????");
			vj_el_get_file_fourcc( el, j, fourcc );
			snprintf(filename,sizeof(filename),"%03zu%s%04lu%010lu%02zu%s",
				strlen( el->video_file_list[j]  ),
				el->video_file_list[j],
				(long unsigned int) j,
				el->num_frames[j],
				strlen(fourcc),
				fourcc 
			);
#ifdef STRICT_CHECKING
			dbg_buflen -= strlen(filename);
			assert( dbg_buflen > 0);
#endif
			veejay_strncat ( result, filename, strlen(filename));
		}
	}


	char first[256];
	char tmpbuf[256];
	snprintf(first,sizeof(first), "%016" PRId64 "%016" PRId64 ,oldfile, oldframe);
#ifdef STRICT_CHECKING
	dbg_buflen -= strlen(first);
	assert( dbg_buflen > 0 );
#endif
	veejay_strncat( result, first, strlen(first) );

  	for (j = n1+1; j <= n2; j++)
	{
		n = el->frame_list[j];
		if ( index[ N_EL_FILE(n) ] != oldfile ||
			N_EL_FRAME(n) != oldframe + 1 )	
		{
			snprintf( tmpbuf,sizeof(tmpbuf), "%016" PRId64 "%016" PRId64 "%016llu",
				 oldframe,
				 index[N_EL_FILE(n)],
				 N_EL_FRAME(n) );
#ifdef STRICT_CHECKING
			dbg_buflen -= strlen(tmpbuf);
			assert( dbg_buflen > 0 );
#endif
			veejay_strncat( result, tmpbuf, strlen(tmpbuf) );
		}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
    	}

	char last_word[64];
	sprintf(last_word,"%016" PRId64, oldframe);
#ifdef STRICT_CHECKING
	dbg_buflen -= 16;
	assert( dbg_buflen > 0 );
#endif
	veejay_strncat( result, last_word, 16 );

	int datalen = strlen(result);
	*bytes_written =  datalen;
#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "EL estimated %d bytes, %d remaining bytes, %d used bytes",est_len,dbg_buflen,datalen);
#endif	
	return result;
}

int	vj_el_write_editlist( char *name, long _n1, long _n2, editlist *el )
{
	FILE *fd;
    	int num_files;
	int64_t oldfile, oldframe;
	int64_t index[MAX_EDIT_LIST_FILES];
	int64_t n;
	int64_t n1 = (uint64_t) _n1;
	int64_t n2 = (uint64_t) _n2;
	int64_t i;

	if(!el) 
		return 0;

	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EditList" );
		return 0;
	}

    	if (n1 < 0)
		n1 = 0;

    	if (n2 > el->total_frames || n2 < n1)
		n2 = el->total_frames;

    	fd = fopen(name, "w");

	if (!fd)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Can not open %s - no edit list written!", name);
		return 0;
    	}

	fprintf(fd, "LAV Edit List\n");
	fprintf(fd, "%s\n", el->video_norm == 'n' ? "NTSC" : "PAL");

	/* get which files are actually referenced in the edit list entries */

	for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
		index[i] = -1;

	for (i = n1; i <= n2; i++)
		index[N_EL_FILE(el->frame_list[i])] = 1;

	num_files = 0;
	for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
		if (index[i] > 0) index[i] = (int64_t)num_files++;

	fprintf(fd, "%d\n", num_files);
	for (i = 0; i < MAX_EDIT_LIST_FILES; i++)
		if (index[i] >= 0 && el->video_file_list[i] != NULL)
		{
			 fprintf(fd, "%s\n", el->video_file_list[i]);
		}

	n = el->frame_list[ n1 ];
	oldfile = index[N_EL_FILE(n)];
	oldframe = N_EL_FRAME(n);

    	fprintf(fd, "%" PRId64 " %" PRId64 " ", oldfile, oldframe);
    	for (i = n1 + 1; i <= n2; i++)
	{
		n = el->frame_list[i];
		if (index[N_EL_FILE(n)] != oldfile
		    || N_EL_FRAME(n) != oldframe + 1)
		{
		    fprintf(fd, "%" PRId64 "\n", oldframe);
	    	fprintf(fd, "%" PRId64 " %llu ",index[N_EL_FILE(n)], N_EL_FRAME(n));
		}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
    	}
    	n = fprintf(fd, "%" PRId64 "\n", oldframe);

    	/* We did not check if all our prints succeeded, so check at least the last one */
    	if (n <= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error writing edit list: ");
		return 0;
    	}
    	fclose(fd);

	return 1;
}


editlist	*vj_el_soft_clone(editlist *el)
{
	editlist *clone = (editlist*) vj_calloc(sizeof(editlist));
	if(!clone)
		return NULL;
	clone->is_empty = el->is_empty;
	clone->has_video = el->has_video;
	clone->video_width = el->video_width;
	clone->video_height = el->video_height;
	clone->video_fps = el->video_fps;
	clone->video_sar_width = el->video_sar_width;
	clone->video_sar_height = el->video_sar_height;
	clone->video_norm = el->video_norm;
	clone->has_audio = el->has_audio;
	clone->audio_rate = el->audio_rate;
	clone->audio_chans = el->audio_chans;
	clone->audio_bits = el->audio_bits;
	clone->audio_bps = el->audio_bps;
	clone->video_frames = el->video_frames;
	clone->total_frames = el->video_frames - 1;
	clone->num_video_files = el->num_video_files;
	clone->max_frame_size = el->max_frame_size;
	clone->MJPG_chroma = el->MJPG_chroma;
	clone->frame_list = NULL;
	clone->last_afile = el->last_afile;
	clone->last_apos  = el->last_apos;
	clone->auto_deinter = el->auto_deinter;
	clone->pixel_format = el->pixel_format;
	clone->is_clone = 1;
	int i;
	for( i = 0; i < MAX_EDIT_LIST_FILES; i ++ )
	{
		clone->video_file_list[i] = NULL;
		clone->lav_fd[i] = NULL;
		clone->num_frames[i] = 0;
		clone->yuv_taste[i] = 0;
		if( el->lav_fd[i] && el->video_file_list[i])
		{
			clone->video_file_list[i] = strdup( el->video_file_list[i] );
			clone->lav_fd[i] = el->lav_fd[i];
			clone->num_frames[i] = el->num_frames[i];
			clone->yuv_taste[i] =el->yuv_taste[i];
		}
	}

	return clone;
}

int		vj_el_framelist_clone( editlist *src, editlist *dst)
{
	if(!src || !dst) return 0;
	if(dst->frame_list)
		return 0;
	dst->frame_list = (uint64_t*) vj_malloc(sizeof(uint64_t) * src->video_frames );
	if(!dst->frame_list)
		return 0;
	
	veejay_memcpy(
		dst->frame_list,
		src->frame_list,
		(sizeof(uint64_t) * src->video_frames )
	); 
	
	return 1;
}

editlist	*vj_el_clone(editlist *el)
{
	editlist *clone = (editlist*) vj_el_soft_clone(el);
	if(!clone)
		return NULL;

	if( vj_el_framelist_clone( el, clone ) )
		return clone;
	else
	{
		if(clone) vj_el_free(clone);
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot clone: Memory error?!");
	}	
	
	return clone;
}
