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
 * GNU General Public License for more details//.
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
#include <ctype.h>
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
#include <libel/avhelper.h>
#include <libel/av.h>
#include <veejay/vj-task.h>
#include <liblzo/lzo.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#define    RUP8(num)(((num)+8)&~8)

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


static	long mmap_size = 0;

typedef struct
{
        AVCodec *codec;
        AVCodecContext *codec_ctx;
        AVFormatContext *avformat_ctx;
        AVPacket pkt;
        int pixfmt;
        int codec_id;
} el_decoder_t;

extern void sample_new_simple( void *el, long start, long end );

void	vj_el_set_mmap_size( long size )
{
	mmap_size = size;
}

typedef struct
{
        AVCodec *codec;
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


char	vj_el_get_default_norm( float fps )
{
	if( fps == 25.0f )
		return 'p';
	if( fps > 23.0f && fps < 24.0f )
		return 's';
	if( fps > 29.0f && fps <= 30.0f )
		return 'n';
	return 'p';
}

float	vj_el_get_default_framerate( int norm )
{
	switch( norm ) {
		case VIDEO_MODE_PAL:
			return 25.0f;
		case VIDEO_MODE_SECAM:
			return 23.976f;
		case VIDEO_MODE_NTSC:
			return 29.97f;
		default:
			veejay_msg(VEEJAY_MSG_WARNING, "Unknown video norm! Use 'p' (PAL), 'n' (NTSC) or 's' (SECAM)");
	}
	return 30.0f;
}

int	vj_el_get_usec_per_frame( float video_fps ) 
{
//			norm_usec_per_frame = 1001000 / 30;	/* 30ish Hz */
	return (int)(1000000 / video_fps);
}

int		vj_el_get_decoder_from_fourcc( const char *fourcc )
{
	return avhelper_get_codec_by_name( fourcc );
}

static void	_el_free_decoder( vj_decoder *d )
{
	if(d)
	{
		if(d->tmp_buffer)
			free( d->tmp_buffer );
		if(d->deinterlace_buffer[0])
			free(d->deinterlace_buffer[0]);
#ifdef SUPPORT_READ_DV2
		if( d->dv_decoder ) {
			vj_dv_free_decoder( d->dv_decoder );
		}
#endif
		if(d->frame) {
			av_free(d->frame);
		}

		if(d->img)
			free(d->img);

		if( d->lzo_decoder )
			lzo_free(d->lzo_decoder);

		free(d);
	}
	d = NULL;
}
#define LARGE_NUM (256*256*256*64)

static int el_pixel_format_org = 1;
static int el_pixel_format_ = 1;
static int el_width_ = 0;
static float el_fps_ = 30;
static int el_height_ = 0;
static long mem_chunk_ = 0;
static int el_switch_jpeg_ = 0;

static VJFrame *el_out_ = NULL;

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
	el_pixel_format_org = pf;
	el_pixel_format_ = get_ffmpeg_pixfmt( pf );
	el_width_ = dw;
	el_height_ = dh;
	el_fps_ = fps;

	el_switch_jpeg_ = switch_jpeg;

	lav_set_project( dw,dh, fps, pf );

	el_out_ = yuv_yuv_template( NULL,NULL,NULL, dw,dh, get_ffmpeg_pixfmt(pf) );

	char *maxFileSize = getenv( "VEEJAY_MAX_FILESIZE" );
	if( maxFileSize != NULL ) {
		uint64_t mfs = atol( maxFileSize );
		if( mfs > AVI_get_MAX_LEN() )
			mfs = AVI_get_MAX_LEN();
		if( mfs > 0 ) {
			AVI_set_MAX_LEN( mfs );
			veejay_msg(VEEJAY_MSG_INFO, "Changed maximum file size" );
		}
	}

	if( has_env_setting( "VEEJAY_RUN_MODE", "CLASSIC" ) )
	{
		require_same_resolution = 1;
	}


	veejay_msg(VEEJAY_MSG_DEBUG,"Initialized EDL, processing video in %d x %d", el_width_, el_height_ );
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
		if( el->video_frames < n_slots)
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "Good, can load this sample entirely into memory... (%d slots, chunk=%d, framesize=%d)", n_slots, mem_chunk_, el->max_frame_size ); 
			el->cache = init_cache( n_slots );
		}
	}
}

void	vj_el_clear_cache( editlist *el )
{
	if( el != NULL ) {
		if(el->cache) {
			reset_cache(el->cache);
		}
	}
}

void	vj_el_deinit()
{
}

int	vj_el_cache_size()
{
	return cache_avail_mb();
}

#ifndef GREMLIN_GUARDIAN
#define GREMLIN_GUARDIAN (128*1024)-1
#endif

vj_decoder *_el_new_decoder( void *ctx, int id , int width, int height, float fps, int pixel_format, int out_fmt, long max_frame_size)
{
        vj_decoder *d = (vj_decoder*) vj_calloc(sizeof(vj_decoder));
        if(!d)
	  return NULL;

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
		d->dv_decoder = vj_dv_decoder_init(1, width, height, out_fmt );
#endif	

	if( id == CODEC_ID_YUVLZO )
	{
		d->lzo_decoder = lzo_new();
	}
	else if ( id == CODEC_ID_YUV422 || id == CODEC_ID_YUV420 || id == CODEC_ID_YUV420F || id == CODEC_ID_YUV422F ) {
		
	}
	else if( ctx )
        {
		d->codec = avhelper_get_codec(ctx);
		d->context = avhelper_get_codec_ctx(ctx);
		d->frame = avcodec_alloc_frame();
		d->img = (VJFrame*) vj_calloc(sizeof(VJFrame));
		d->img->width = width;
		d->img->height = height;
	}

	size_t safe_max_frame_size = (max_frame_size < GREMLIN_GUARDIAN) ? 128 * 1024: RUP8(max_frame_size);

	d->tmp_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * safe_max_frame_size );
        d->fmt = id;

	return d;
}

int	get_ffmpeg_pixfmt( int pf )
{
	switch( pf )
	{
		case FMT_420:
			return PIX_FMT_YUV420P;
		case FMT_420F:
			return PIX_FMT_YUVJ420P;
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

int open_video_file(char *filename, editlist * el, int preserve_pathname, int deinter, int force, char override_norm, int out_format, int width, int height )
{
	int i, n, nerr;
	int chroma=0;
	int _fc;
	int decoder_id = 0;
	const char *compr_type;
	char *realname = NULL;	

 	if( filename == NULL ) 
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No files to open!");
		return -1;
	}

	if (preserve_pathname)
		realname = vj_strdup(filename);
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
      
        n = el->num_video_files;	
	
	int pixfmt = -1;

	lav_file_t *elfd = lav_open_input_file(filename,mmap_size );
	el->lav_fd[n] = NULL;
	if (elfd == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error loading videofile '%s'", realname);
	        veejay_msg(VEEJAY_MSG_ERROR,"%s",lav_strerror());
	 	if(realname) free(realname);
		return -1;
	}


	el->ctx[n] = avhelper_get_decoder( filename, out_format, width, height );

	if( el->ctx[n] == NULL ) {
		pixfmt = test_video_frame( el, n, elfd, el_pixel_format_ );
		if( pixfmt == -1 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to determine video format" );
			lav_close(elfd);
			if(realname) free(realname);
			return -1;
		}
		el->pixfmt[n] = pixfmt;
	}
	else {
		el_decoder_t *x = (el_decoder_t*) el->ctx[n];
		el->pixfmt[n] = x->pixfmt;
	}


	if(lav_video_frames(elfd) < 1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to load empty video files");
		if(realname) free(realname);
		lav_close(elfd);
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
		return -1;
	}

	
	_fc = lav_video_MJPG_chroma(elfd);

	if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN || _fc == CHROMA411 || _fc == CHROMA422F || _fc == CHROMA420F))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Input file %s is not in a valid format (%d)",filename,_fc);
	   	if(realname) free(realname);
		lav_close( elfd );
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
		return -1;

	}

	if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  	  el->MJPG_chroma = _fc;
	  chroma = _fc; //FIXME
	}

	el->lav_fd[n] = elfd;
    	el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
    	el->video_file_list[n] = vj_strndup(realname, strlen(realname));
	
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
		veejay_msg(VEEJAY_MSG_DEBUG,"\tVideo compressor: %s",lav_video_compressor(el->lav_fd[n]));
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
		el->video_height = lav_video_height(el->lav_fd[n]);
		el->video_width = lav_video_width(el->lav_fd[n]);
		el->video_inter = lav_video_interlacing(el->lav_fd[n]);
		el->video_fps = lav_frame_rate(el->lav_fd[n]);
	
		lav_video_clipaspect(el->lav_fd[n],
				       &el->video_sar_width,
				       &el->video_sar_height);

		if (!el->video_norm)
		{
			el->video_norm = vj_el_get_default_norm( el->video_fps );
		}

		if (!el->video_norm)
		{
			if(override_norm == 'p' || override_norm == 'n' || override_norm == 's')
				el->video_norm = override_norm;
		}

		if( !el->video_norm ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to detect video norm, using PAL" );
			el->video_norm = 'p';
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
			   lav_audio_bits(el->lav_fd[n]) != el->audio_bits ) {
				veejay_msg(VEEJAY_MSG_ERROR,"File %s has different audio properties - cant play that!");
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio rate %ld, source is %ld", el->audio_rate, lav_audio_rate(el->lav_fd[n]));
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio bits %d, source is %d", el->audio_bits, lav_audio_bits(el->lav_fd[n]));
				veejay_msg(VEEJAY_MSG_DEBUG,"Audio channels %d, source is %d", el->audio_chans, lav_audio_channels(el->lav_fd[n]) );
				nerr++;
			} else
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
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );
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
		if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );

		return -1;
	}

	if( el->decoders[n] == NULL ) {
		long max_frame_size = get_max_frame_size( el->lav_fd[n] );
		decoder_id = avhelper_get_codec_by_name( compr_type );
		el->decoders[n] = 
			_el_new_decoder( el->ctx[n], decoder_id, el->video_width, el->video_height, el->video_fps, el->pixfmt[ n ],el_pixel_format_, max_frame_size );
		if( el->decoders[n] == NULL ) {
			veejay_msg(VEEJAY_MSG_ERROR,"Unsupported video compression type: %s", compr_type );
			if( el->lav_fd[n] ) 
				lav_close( el->lav_fd[n] );
			el->lav_fd[n] = NULL;
			if( realname ) free(realname );
			if( el->video_file_list[n]) 
				free(el->video_file_list[n]);
			el->video_file_list[n] = NULL;
			if( el->ctx[n] ) avhelper_close_decoder( el->ctx[n] );

			return -1;
		}
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
    
	if( el_width_ == 0 && el_height_ == 0 ) {
		el_width_ = el->video_width;
		el_height_ = el->video_height;
		el_fps_ = el->video_fps;

		veejay_msg(VEEJAY_MSG_WARNING, "Initialized video project settings from first file (%s)" , filename );
	}

	return n;
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

int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[4])
{
	if( el->has_video == 0 || el->is_empty )
	{
		vj_el_dummy_frame( dst, el, el->pixel_format );
		return 2;
	}

	int res = 0;
   	uint64_t n;
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

	in_pix_fmt = el->pixfmt[N_EL_FILE(n)];

	uint8_t *in_cache = NULL;
	if(el->cache)
		in_cache = get_cached_frame( el->cache, nframe, &res, &in_pix_fmt );


	int decoder_id = lav_video_compressor_type( el->lav_fd[N_EL_FILE(n)] );
		


	if(! in_cache )	
	{
		res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
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
		int strides[4] = { el_out_->len, el_out_->uv_len, el_out_->uv_len,0 };
		vj_frame_copy( srci->data, dst, strides );
                return 1;     
	}

	vj_decoder *d = (vj_decoder*) el->decoders[ N_EL_FILE(n) ];

	if(!in_cache)
	{
		if(lav_filetype( el->lav_fd[N_EL_FILE(n)] ) != 'x')
		{
		    res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);
		    if(res > 0 && el->cache)
			cache_frame( el->cache, d->tmp_buffer, res, nframe, decoder_id );
		}
	}

	uint8_t *data = ( in_cache == NULL ? d->tmp_buffer: in_cache );
	int inter = 0;
	uint8_t *in[3] = { NULL,NULL,NULL };
	int strides[4] = { el_out_->len, el_out_->uv_len, el_out_->uv_len ,0};
	uint8_t *dataplanes[3] = { data , data + el_out_->len, data + el_out_->len + el_out_->uv_len };
	switch( decoder_id )
	{
		case CODEC_ID_YUV420:
			vj_frame_copy1( data,dst[0], el_out_->len );
			in[0] = data; 
			in[1] = data+el_out_->len; 
			in[2] = data+el_out_->len + (el_out_->len/4);
			if( el_pixel_format_ == PIX_FMT_YUVJ422P ) {
				yuv_scale_pixels_from_ycbcr( in[0],16.0f,235.0f, el_out_->len );
				yuv_scale_pixels_from_ycbcr( in[1],16.0f,240.0f, el_out_->len/4); 
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;	
		case CODEC_ID_YUV420F:
			vj_frame_copy1( data, dst[0], el_out_->len);
			in[0] = data;
			in[1] = data + el_out_->len;
			in[2] = data + el_out_->len+(el_out_->len/4);
			if( el_pixel_format_ == PIX_FMT_YUV422P ) {
				yuv_scale_pixels_from_y( dst[0], el_out_->len );
				yuv_scale_pixels_from_uv( dst[1], el_out_->len/4);
			}
			yuv420to422planar( in , dst, el->video_width,el->video_height );
			return 1;
			break;
		case CODEC_ID_YUV422:
			vj_frame_copy( dataplanes,dst,strides );
			if( el_pixel_format_ == PIX_FMT_YUVJ422P ) {
				yuv_scale_pixels_from_ycbcr( dst[0],16.0f,235.0f, el_out_->len );
				yuv_scale_pixels_from_ycbcr( dst[1],16.0f,240.0f, el_out_->len/2);
			}	
			return 1;
			break;
		case CODEC_ID_YUV422F:
			vj_frame_copy( dataplanes, dst, strides );
			if( el_pixel_format_ == PIX_FMT_YUV422P ) {
				yuv_scale_pixels_from_y( dst[0], el_out_->len );
				yuv_scale_pixels_from_uv( dst[1], el_out_->len/2);
			}
			return 1;
			break;
		case CODEC_ID_YUVLZO:
			if(  ( in_pix_fmt == PIX_FMT_YUVJ420P || in_pix_fmt == PIX_FMT_YUV420P  ) ) {
				inter = lzo_decompress420into422(d->lzo_decoder, data,res,dst, el->video_width,el->video_height );
			} 
			else { 
				inter = lzo_decompress_el( d->lzo_decoder, data,res, dst,el->video_width*el->video_height);
			}

			return inter;

			break;			
		default:

			return avhelper_decode_video( el->ctx[ N_EL_FILE(n) ], data, res, dst );

			break;
	}

	return 0;  
}


int	test_video_frame( editlist *el, int n, lav_file_t *lav,int out_pix_fmt)
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
			in_pix_fmt = PIX_FMT_YUVJ420P;break;
		case CHROMA422F:
			in_pix_fmt = PIX_FMT_YUVJ422P;break;
		case CHROMA420:
			in_pix_fmt = PIX_FMT_YUV420P; break;
		case CHROMA422:
		case CHROMA411:
			in_pix_fmt = PIX_FMT_YUV422P; break;
		default:
			veejay_msg(0 ,"Unsupported pixel format");
			break;			
	}
	long max_frame_size = get_max_frame_size( lav );

	vj_decoder *d  = _el_new_decoder(
				NULL,
				decoder_id,
				lav_video_width( lav),
				lav_video_height( lav),
			   	(float) lav_frame_rate( lav ),
				in_pix_fmt,
				out_pix_fmt,
				max_frame_size );

	if(!d)
	{
		return -1;
	} 

	res = lav_read_frame( lav, d->tmp_buffer);

	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Error reading frame: %s", lav_strerror());
		_el_free_decoder( d );
		return -1;
	}


	int ret = -1;
	switch( decoder_id )
	{
		case CODEC_ID_YUV420F:
			ret = PIX_FMT_YUVJ420P;
			break;
		case CODEC_ID_YUV422F:
			ret = PIX_FMT_YUVJ422P;
			break;
		case CODEC_ID_YUV420:
			ret = PIX_FMT_YUV420P;
			break;
		case CODEC_ID_YUV422:
			ret = PIX_FMT_YUV422P;
			break;
		case CODEC_ID_DVVIDEO:
#ifdef SUPPORT_READ_DV2
			ret = vj_dv_scan_frame( d->dv_decoder, d->tmp_buffer );
			if( ret == PIX_FMT_YUV420P || ret == PIX_FMT_YUVJ420P )
				lav->MJPG_chroma = CHROMA420;
			else
				lav->MJPG_chroma = CHROMA422;
#endif
			break;
		case CODEC_ID_YUVLZO:
			ret = PIX_FMT_YUVJ422P;
			if ( in_pix_fmt != ret )	
			{
				//@ correct chroma 
				if( ret == PIX_FMT_YUV420P || ret == PIX_FMT_YUVJ420P )
					lav->MJPG_chroma = CHROMA420;
				else
					lav->MJPG_chroma = CHROMA422;
			}

			break;
		default:
			_el_free_decoder( d );
			return -1;
			break;	
	}

	el->decoders[n] = (void*) d;

	
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
	frame->format = el_pixel_format_;
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
	frame->format = el_pixel_format_;
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
	el->pixel_format = get_ffmpeg_pixfmt(fmt);
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

	return el;
}

void	vj_el_scan_video_file( char *filename,  int *dw, int *dh, float *dfps, long *arate )
{
	void *tmp = avhelper_get_decoder( filename, PIX_FMT_YUVJ422P, -1, -1 );
	if( tmp ) {
		AVCodecContext *c = avhelper_get_codec_ctx( tmp );
		*dw = c->width;
		*dh = c->height;
		*dfps = (float) c->time_base.den / c->time_base.num;
		*arate = c->sample_rate;
		avhelper_close_decoder(tmp);
	} else {
		lav_file_t *fd = lav_open_input_file( filename, mmap_size );
		if( fd ) {
			*dw = lav_video_width( fd );
			*dh = lav_video_height( fd );
			*dfps = lav_frame_rate( fd );
			*arate = lav_audio_rate( fd );
			lav_close(fd);
		}
		
	}
	
	veejay_msg(VEEJAY_MSG_DEBUG, "Using video settings from first loaded video %s: %dx%d@%2.2f", filename,*dw,*dh,*dfps);
}


int	vj_el_auto_detect_scenes( editlist *el, uint8_t *tmp[4], int w, int h, int dl_threshold )
{
	long n1 = 0;
	long n2 = el->total_frames;
	long n;
	int dl = 0;
	int last_lm = 0;
	int index = 0;
	long prev = 0;

	if( el == NULL || el->is_empty || el->total_frames < 2 )
		return 0;

	for( n = n1; n < n2; n ++ ) {
		vj_el_get_video_frame(el, n, tmp );
		int lm = luminance_mean( tmp, w, h );
		if( n == 0 ) {
			dl = 0;
		}
		else {
			dl = abs( lm - last_lm );
		}
		last_lm = lm;

		veejay_msg(VEEJAY_MSG_DEBUG,"frame %ld/%ld luminance mean %d, delta %d ", n, n2, lm, dl );

		if( dl > dl_threshold ) {

			if( prev == 0 ) {
				sample_new_simple(el,0,n);
				veejay_msg(VEEJAY_MSG_INFO,"sampled frames %ld - %ld", 0,n);
			} else {
				sample_new_simple(el,prev,n);
				veejay_msg(VEEJAY_MSG_INFO,"sampled frames %ld - %ld", prev, n );
			}

			prev = n;
			index ++;
		}	
	}
	return index;
}

editlist *vj_el_init_with_args(char **filename, int num_files, int flags, int deinterlace, int force,char norm , int out_format, int width, int height)
{
	editlist *el = vj_calloc(sizeof(editlist));
	FILE *fd;
	char line[1024];
	uint64_t index_list[MAX_EDIT_LIST_FILES];
	int	num_list_files;
	long i,nf=0;
	int n1=0;
	int n2=0;
	long nl=0;
	uint64_t n =0;
	
	int av_pixfmt = get_ffmpeg_pixfmt( out_format );
	
	veejay_memset(line,0,sizeof(line));
	
	if(!el) return NULL;

	el->has_video = 1; //assume we get it   
	el->MJPG_chroma = CHROMA420;
	el->is_empty  = 0;
	if(!filename[0] || filename == NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"\tInvalid filename given");
		free(el);
		return NULL;	
	}

	if(strcmp(filename[0], "+p" ) == 0 || strcmp(filename[0], "+n") == 0 || strcmp(filename[0],"+s") == 0 ) {
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
			vj_el_free(el);
			return NULL;
		} 
			fd = fopen(filename[nf], "r");
			if (fd <= 0)
			{
			   	 veejay_msg(VEEJAY_MSG_ERROR,"Error opening %s:", filename[nf]);
				 fclose(fd);
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
			    	if (line[0] != 'N' && line[0] != 'n' && line[0] != 'P' && line[0] != 'p' && line[0] != 's' && line[0] != 'S')
				{
					veejay_msg(VEEJAY_MSG_ERROR,"Edit list second line is not NTSC/PAL/SECAM");
					fclose(fd);
					vj_el_free(el);
					return NULL;
				}
				
				if( el->video_norm != '\0' ) {
					veejay_msg(VEEJAY_MSG_WARNING,"Norm already set to, ignoring new norm");
				}
				else {
					el->video_norm = tolower(line[0]);
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
						fclose(fd);
						vj_el_free(el);
						return NULL;
					}

					line[n - 1] = 0;	/* Get rid of \n at end */
	
					index_list[i] =
					    open_video_file(line, el, flags, deinterlace,force,norm, av_pixfmt, width, height);
	
					if(index_list[i]< 0)
					{
						fclose(fd);
						vj_el_free(el);
						return NULL;
					}

					el->frame_list = (uint64_t *) realloc(el->frame_list, (el->video_frames + el->num_frames[n]) * sizeof(uint64_t));
					if (el->frame_list==NULL)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
						fclose(fd);
						vj_el_free(el);
						return NULL;
					}

	    			for (i = 0; i < el->num_frames[n]; i++)
					{
						el->frame_list[el->video_frames++] = EL_ENTRY(n, i);
					}


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
								fclose(fd);
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
								fclose(fd);
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

		     		n = open_video_file(filename[nf], el, flags, deinterlace,force,norm, av_pixfmt, width, height);
				if(n >= 0 )
				{
			       		el->frame_list = (uint64_t *) realloc(el->frame_list, (RUP8(el->video_frames + el->num_frames[n])) * sizeof(uint64_t));
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
	el->pixel_format = el_pixel_format_org;
	el->total_frames = el->video_frames-1;
	/* Help for audio positioning */

	el->last_afile = -1;
	veejay_msg(VEEJAY_MSG_DEBUG, "\tThere are %" PRIu64 " video frames", el->total_frames );

	el->auto_deinter = 0;

	return el;
}


void	vj_el_free(editlist *el)
{
	if(!el)
		return;

	int i;
	for ( i = 0; i < el->num_video_files; i ++ )
	{
		if( el->video_file_list[i]) {
			free(el->video_file_list[i]);
			el->video_file_list[i] = NULL;
		}

		if( el->is_clone )
			continue;

		if( el->ctx[i] ) {
			avhelper_close_decoder( el->ctx[i] );
		}
		if( el->decoders[i] ) {
			_el_free_decoder( el->decoders[i] );	
		}
		if( el->lav_fd[i] ) 
		{
			lav_close( el->lav_fd[i] );
			el->lav_fd[i] = NULL;
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
				len += 64;
			}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
	}

	n = n1;
	oldfile = of1;
	oldframe = ofr;
	
	n1 = 0;
 
	est_len = 2 * len;

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
			strncat ( result, filename, strlen(filename));
		}
	}


	char first[128];
	char tmpbuf[128];
	snprintf(first,sizeof(first), "%016" PRId64 "%016" PRId64 ,oldfile, oldframe);
	strncat( result, first, strlen(first) );

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
			strncat( result, tmpbuf, strlen(tmpbuf) );
		}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
    	}

	char last_word[64];
	snprintf(last_word,sizeof(last_word),"%016" PRId64, oldframe);
	strncat( result, last_word, strlen(last_word) );

	int datalen = strlen(result);
	*bytes_written =  datalen;
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
		clone->pixfmt[i] = 0;
		if( el->lav_fd[i] && el->video_file_list[i])
		{
			clone->video_file_list[i] = vj_strdup( el->video_file_list[i] );
			clone->lav_fd[i] = el->lav_fd[i];
			clone->num_frames[i] = el->num_frames[i];
			clone->pixfmt[i] =el->pixfmt[i];
		}
		clone->decoders[i] = el->decoders[i]; 
		clone->ctx[i] = el->ctx[i];
	}

	return clone;
}

int		vj_el_framelist_clone( editlist *src, editlist *dst)
{
	if(!src || !dst) return 0;
	if(dst->frame_list)
		return 0;
	dst->frame_list = (uint64_t*) vj_malloc(sizeof(uint64_t) * RUP8(src->video_frames) );
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
