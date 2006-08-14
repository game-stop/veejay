/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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

/** \defgroup edl Edit Decision List / Media files
 *
 * This is almost the same as the EditList used in the MjpegTools 
 * (http://mjpeg.sourceforge.net)
 * Veejay's EDL supports up to 4096 avi files to be added to a single  
 * Edit Descision List.
 *
 * Basic operations include:
 *   -# cut , copy, paste, crop and delete linear selections of the EDL
 *   -# add a video file to the edit list 
 *   
 * This module keeps an internal cache to avoid disk-usage when re-using
 * a frame in the performer chain.  
 *
 * Todo: reduce globals in  editlist implementation 
 */

/*


	This file contains code-snippets from the mjpegtools' EditList
	(C) The Mjpegtools project

	http://mjpeg.sourceforge.net
*/
#include <config.h>
#include <string.h>
#include <libvjmsg/vj-common.h>
#include <libel/lav_io.h>
#include <libel/vj-el.h>
//#include <libel/vj-avcodec.h>
#include <libel/elcache.h>
#include <limits.h>
#include <mjpegtools/mpegconsts.h>
#include <mjpegtools/mpegtimecode.h>
#include <libvjmem/vjmem.h>
#include <ffmpeg/avcodec.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libyuv/yuvconv.h>
#ifdef SUPPORT_READ_DV2
#include "rawdv.h"
#include "vj-dv.h"
#endif
#define MAX_CODECS 12
#define CODEC_ID_YUV420 997
#define CODEC_ID_YUV422 998
#define CODEC_ID_YUV444 999
#define CODEC_ID_YUV_PLANAR 996
#define MAX_EDIT_LIST_FILES 4096

#define DUMMY_FRAMES 2
#include <config.h>
#include <libel/lav_io.h>
#include <veejay/defs.h>
#include <vevosample/vevosample.h>


#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define N_EL_FRAME(x)  ( (x)&0xfffffffffffffLLU  )
#define N_EL_FILE(x) (int)  ( ((x)>>52)&0xfff ) 
/* ((file)&0xfff<<52) */
#define EL_ENTRY(file,frame) ( ((file)<<52) | ((frame)& 0xfffffffffffffLLU) )


#define	YUV_SAMPLING_420(p)	( p == PIX_FMT_YUVJ420P  ? 1 : (p == PIX_FMT_YUV420P ? 1: 0))
#define YUV_SAMPLING_422(p)	( p == PIX_FMT_YUVJ422P  ? 1 : (p == PIX_FMT_YUV422P ? 1: 0))
#define YUV_SAMPLING_444(p)	( p == PIX_FMT_YUVJ444P	 ? 1 : (p == PIX_FMT_YUV444P ? 1: 0))

typedef struct 
{
	int 	has_video;
	int	is_empty;
	int	video_width;  
	int	video_height;
	int	video_inter;
	float	video_fps;
	int	video_sar_width;	
	int 	video_sar_height;	
	char 	video_norm;	

	int	has_audio; 
	long	audio_rate;
	int	audio_chans;
	int	audio_bits;
	int	audio_bps;
	int	play_rate;

	uint64_t 	video_frames; 

	long	num_video_files;

	long	max_frame_size;
	int	MJPG_chroma;

	char		*(video_file_list[MAX_EDIT_LIST_FILES]);
	lav_file_t	*(lav_fd[MAX_EDIT_LIST_FILES]);
	int		ref[MAX_EDIT_LIST_FILES];
	int		yuv_taste[MAX_EDIT_LIST_FILES];

	long 		num_frames[MAX_EDIT_LIST_FILES];
	uint64_t 	*frame_list;

	int 		last_afile;
	long 		last_apos;
	int		auto_deinter;
	int		itu601;	
	void		*cache;
} editlist;  

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
};


static struct
{
        const char *name;
        int  id;
	int  pix_fmt;
} _supported_codecs[] = 
{
        { "mjpeg" , CODEC_ID_MJPEG  },
	{ "mjpegb", CODEC_ID_MJPEGB },
#if LIBAVCODEC_BUILD > 4680
	{ "sp5x",   CODEC_ID_SP5X }, /* sunplus motion jpeg video */
#endif
#if LIBAVCODEC_BUILD >= 4685
	{ "theora", CODEC_ID_THEORA },
#endif
	{ "huffyuv", CODEC_ID_HUFFYUV },
	{ "cyuv",	CODEC_ID_CYUV },
        { "dv"    , CODEC_ID_DVVIDEO },
        { "msmpeg4",CODEC_ID_MPEG4 },
        { "divx"   ,CODEC_ID_MSMPEG4V3 },
        { "i420",   CODEC_ID_YUV420 },
        { "i422",   CODEC_ID_YUV422 },
	{ "i444",   CODEC_ID_YUV444 },
        { NULL  , 0 },
};

static struct
{
	const char *name;
	int id;
} _supported_fourcc[] =
{
	{ "mjpg",	CODEC_ID_MJPEG	},
	{ "mjpb",	CODEC_ID_MJPEGB },
	{ "jpeg",	CODEC_ID_MJPEG	},
	{ "mjpa",	CODEC_ID_MJPEG  },
	{ "jfif",	CODEC_ID_MJPEG  },
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
	{ "div3",	CODEC_ID_MSMPEG4V3 },
	{ "mp43",	CODEC_ID_MSMPEG4V3 },
	{ "mp42",	CODEC_ID_MSMPEG4V2 },
	{ "mpg4",	CODEC_ID_MSMPEG4V1 },
	{ "yuv",	CODEC_ID_YUV420 },
	{ "iyuv",	CODEC_ID_YUV420 },
	{ "i420",	CODEC_ID_YUV420 },
	{ "yv16",	CODEC_ID_YUV422 },
	{ "i444",	CODEC_ID_YUV444 },
	{ "hfyu",	CODEC_ID_HUFFYUV},
	{ "cyuv",	CODEC_ID_CYUV   },
#if LIBAVCODEC_BUILD > 4680
	{ "spsx",	CODEC_ID_SP5X	},
#endif
	{ NULL, 0 }
};

static	int mmap_size = 0;

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
	void *sampler;
} vj_decoder;


#ifdef SUPPORT_READ_DV2
static	vj_dv_decoder *dv_decoder_ = NULL;
#endif

static	int	itu601_clamp_ = 0;

static	vj_decoder *el_codecs[MAX_CODECS];
struct pixmap_name_struct {
  int value;
  char *name;
};
#undef MAP
#define MAP(a) {a, #a}
static const struct pixmap_name_struct pixmap_name_map[] = {
  // internal format
	MAP(PIX_FMT_YUV420P), 
	MAP(PIX_FMT_YUV422),
	MAP(PIX_FMT_RGB24),
	MAP(PIX_FMT_BGR24),
	MAP(PIX_FMT_YUV422P),
	MAP(PIX_FMT_YUV444P),
	MAP(PIX_FMT_RGBA32),
	MAP(PIX_FMT_YUV410P),
	MAP(PIX_FMT_RGB565),
	MAP(PIX_FMT_RGB555),
	MAP(PIX_FMT_GRAY8),
	MAP(PIX_FMT_PAL8),
	MAP(PIX_FMT_YUVJ420P),
	MAP(PIX_FMT_YUVJ422P),
	MAP(PIX_FMT_YUVJ444P),
	MAP(PIX_FMT_UYVY422),
	MAP(PIX_FMT_UYVY411),
	MAP(PIX_FMT_MONOBLACK),
	MAP(PIX_FMT_MONOWHITE),
  	{0, 0}
};
#undef MAP

static const char *pixfmt_name(int value)
{
  int i = 0;
  while (pixmap_name_map[i].name) {
    if (pixmap_name_map[i].value == value)
      return pixmap_name_map[i].name;
    i++;
  }
  return "Unknown format!";
}

#define LARGE_NUM (256*256*256*64)
static int get_buffer(AVCodecContext *context, AVFrame *av_frame){
	vj_decoder *this = (vj_decoder *)context->opaque;
	VJFrame *img;
	int width  = context->width;
	int height = context->height;

	avcodec_align_dimensions(context, &width, &height);
  
	img = this->img;

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
#ifdef STRICT_CHECKING  
  assert(av_frame->type == FF_BUFFER_TYPE_USER);
  assert(av_frame->opaque);  
#endif
  av_frame->opaque = NULL;
}

static	int _el_get_codec(int id )
{
	int i;
	for( i = 0; _supported_codecs[i].name != NULL ; i ++ )
	{
		if( _supported_codecs[i].id == id )
			return i;
	}
	return -1;
}
static	int	_el_get_codec_id( const char *fourcc )
{
	int i;
	for( i = 0; _supported_fourcc[i].name != NULL ; i ++ )
		if( strncasecmp( fourcc, _supported_fourcc[i].name, strlen(_supported_fourcc[i].name) ) == 0 )
			return _supported_fourcc[i].id;
	return -1;
}

void		vj_el_set_itu601_preference( int status )
{
	itu601_clamp_ = status;
}

static void	_el_free_decoder( vj_decoder *d )
{
	if(d)
	{
		int i;
		if(d->tmp_buffer)
			free( d->tmp_buffer );
		if(d->sampler )
			subsample_free( d->sampler );

		for( i = 0; i < 3 ; i ++ )
			if(d->deinterlace_buffer[i]) free(d->deinterlace_buffer[i]);

		if(d->context)
		{
			avcodec_close( d->context ); 
			free( d->context );
			d->context = NULL;
		}
		if(d->frame) av_free(d->frame);
		
		free(d);
	}
	d = NULL;
}

static int mem_chunk_ = 0;
void	vj_el_init_chunk(int size)
{
//@@ chunk size per editlist
	mem_chunk_ = 1024 * 1024 * size;
}

int	vj_el_match( void *sv, void *edl)
{
	sample_video_info_t *v = (sample_video_info_t*) sv;
	editlist *el = (editlist*) edl;
	if(!el) return 0;
#ifdef STRICT_CHECKING
	assert( v != NULL );
	assert( v->w > 0 );
	assert( v->h > 0 );
	assert( el != NULL );
#endif
	if ( v->w != el->video_width)
		return 0;
	if ( v->h != el->video_height)
		return 0;
	return 1;	
}

void	vj_el_init()
{
	int i;
	for( i = 0; i < MAX_CODECS ;i ++ )
		el_codecs[i] = NULL;
#ifdef USE_GDK_PIXBUF
	vj_picture_init();
#endif
}

int	vj_el_is_dv(editlist *el)
{
	return is_dv_resolution(el->video_width, el->video_height);
}


void	vj_el_prepare()
{
//	reset_cache( el->cache );
}

//@ iterateovers over sample fx chain
void	vj_el_setup_cache( editlist *el )
{	
	if(!el->cache)
	{
		int n_slots = mem_chunk_ / el->max_frame_size;
		veejay_msg(VEEJAY_MSG_INFO, "\tCache limit:      %d frames", n_slots ); 
		el->cache = init_cache( n_slots );
	}
#ifdef STRICT_CHECKING
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Internal error: cache already setup");
		assert(0);
	}
#endif
}

void	vj_el_clear_cache( editlist *el )
{
	if(el->cache)
		reset_cache(el->cache);
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

vj_decoder *_el_new_decoder( int id , int width, int height, float fps, int dri)
{
        vj_decoder *d = (vj_decoder*) vj_malloc(sizeof(vj_decoder));
        if(!d)
	  return NULL;
	memset( d, 0, sizeof(vj_decoder));

	if( id < CODEC_ID_YUV_PLANAR)
        {
		d->codec = avcodec_find_decoder( id );
		if(!d->codec)
		{
			veejay_msg(0, "Cannot find decoder '%d'", id );
			if(d) free(d);
			return NULL;
		}
		d->context = avcodec_alloc_context();
		d->context->width = width;
		d->context->height = height;
                d->context->time_base.den = fps;
		d->context->time_base.num = 1;
		d->frame = avcodec_alloc_frame();
		d->context->opaque = d;
		d->context->palctrl = NULL;
		if ( avcodec_open( d->context, d->codec ) < 0 )
       		{
			if(d) free(d);
      		        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open codec %s", d->codec->name); 
       		       return NULL;
      		}
		
		if(dri)
		{
			if( d->codec->capabilities & CODEC_CAP_DR1)
			{
				d->context->get_buffer = get_buffer;
				d->context->release_buffer = release_buffer;
			}
		}
			
        }
	d->sampler = subsample_init( width );
	       

        d->tmp_buffer = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height * 4 );
        if(!d->tmp_buffer)
	{
		free(d);
                return NULL;
	}
        d->fmt = id;
        memset( d->tmp_buffer, 0, width * height * 4 );

        d->deinterlace_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height);
        if(!d->deinterlace_buffer[0]) { if(d) free(d); return NULL; }
        d->deinterlace_buffer[1] = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height);
        if(!d->deinterlace_buffer[1]) { if(d) free(d); return NULL; }
        d->deinterlace_buffer[2] = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height);
        if(!d->deinterlace_buffer[2]) { if(d) free(d); return NULL; }

        memset( d->deinterlace_buffer[0], 0, width * height );
        memset( d->deinterlace_buffer[1], 0, width * height );
        memset( d->deinterlace_buffer[2], 0, width * height );
        
       	d->ref = 1;
        return d;
}

void	vj_el_set_image_output_size(editlist *el)
{
#ifdef STRICT_CHECKING
	assert(0);
#endif
}

int open_video_file(const char *filename, editlist * el );

//@@ should identify sampling format, 411,420,444
static int _el_probe_for_pixel_fmt( lav_file_t *fd )
{
	switch( fd->MJPG_chroma )
	{
		case CHROMA411:
			return FMT_411;
		case CHROMA420:
			return FMT_420;
		case CHROMA422:
			return FMT_422;
		case CHROMA444:
			return FMT_444;
	}
	return FMT_444;
}

int 	vj_el_scan_video_frame(void *edl)
{
	editlist *el = (editlist*) edl;
	int nframe = 0;
	uint64_t n = 0;

	if(! el->lav_fd || !el->lav_fd[N_EL_FILE(n)])
		return 0;
	
	int decoder_id = lav_video_compressor_type( el->lav_fd[N_EL_FILE(n)] );

	if(decoder_id <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to identify codec");
		return 0;
	}
	
	int res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
	
	if (res < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error setting video position: %s",
			  lav_strerror());
 		return 0;
	}

	uint8_t *data = vj_malloc( sizeof(uint8_t*) * el->video_width * el->video_height * 4 );

    	res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], data);
	if( res <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot read frame 0");
		return 0;
	}	
	
	int c_i = _el_get_codec(decoder_id);
	if(c_i == -1)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unsupported codec");
		return 0;
	}
	vj_decoder *d = _el_new_decoder( decoder_id, el->video_width, el->video_height, el->video_fps,0);
	if(!d)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Unable to initialize codec %d",decoder_id);
		return 0;
	}
	if( decoder_id < CODEC_ID_YUV_PLANAR )
	{
		int	got_picture = 1;
		int len = avcodec_decode_video(
			d->context,
			d->frame,
			&got_picture,
			data,
			res
		);
		if( len <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot decode frame 0");
			return 0;
		}
 		 
		 
		el->yuv_taste[ N_EL_FILE(n) ] = d->context->pix_fmt;
	}
	else
	{
		switch(decoder_id)
		{
			case CODEC_ID_YUV420:
				el->yuv_taste[ N_EL_FILE(n) ] = PIX_FMT_YUV420P;
				break;
			case CODEC_ID_YUV422:
				el->yuv_taste[ N_EL_FILE(n) ] = PIX_FMT_YUV422P;
				break;
			case CODEC_ID_YUV444:
				el->yuv_taste[ N_EL_FILE(n) ] = PIX_FMT_YUV444P;
				break;
	
		}
	}

	veejay_msg(VEEJAY_MSG_INFO, "\tPixel format:     %s", pixfmt_name( el->yuv_taste[N_EL_FILE(n)] ));
	
	_el_free_decoder(d);	

	free(data);
	
	return 1;  
}

//@ realname is full path , or preserved path.
int open_video_file(const char *realname, editlist * el )
{
	int i, n, nerr;
	int chroma=0;
	int _fc;
	int decoder_id = 0;
	const char *compr_type;
	int pix_fmt = -1;

	for (i = 0; i < el->num_video_files; i++)
	{
		if( el->video_file_list[i] && strncmp(realname, el->video_file_list[i], strlen( el->video_file_list[i])) == 0)
		{
		    veejay_msg(VEEJAY_MSG_ERROR, "File %s already in editlist", realname);
		    return i;
		}
	}

    	if (el->num_video_files >= MAX_EDIT_LIST_FILES)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Maximum number of video files exceeded\n");
		return -1; 
    	}

    	if (el->num_video_files >= 1)
		chroma = el->MJPG_chroma;
         
    	n = el->num_video_files;

    	el->num_video_files++;

    	el->lav_fd[n] = lav_open_input_file(realname,mmap_size);

    	if (el->lav_fd[n] == NULL)
	{
		el->num_video_files--;	
		veejay_msg(VEEJAY_MSG_ERROR,"Error loading '%s' :%s",
		realname,lav_strerror());
		return -1;
	}

    	_fc = lav_video_MJPG_chroma(el->lav_fd[n]);

    	if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN || _fc == CHROMA411 ))
	{
		el->num_video_files --;
		return -1;
	}

    	if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  		el->MJPG_chroma = _fc;
		chroma = _fc;
	}

	if(lav_video_frames(el->lav_fd[n]) < 2)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to load video files that contain less than 2 frames");
		el->num_video_files --;
		return -1;
	}

	el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
	el->video_file_list[n] = strndup(realname, strlen(realname));
    /* Debug Output */
	if(n == 0 )
	{
		veejay_msg(2,"\tFull name:       %s", realname);
		veejay_msg(2,"\tFrames:          %ld", lav_video_frames(el->lav_fd[n]));
		veejay_msg(2,"\tWidth:           %d", lav_video_width(el->lav_fd[n]));
		veejay_msg(2,"\tHeight:          %d", lav_video_height(el->lav_fd[n]));
    		veejay_msg(2,"\tFrames/sec:       %f", lav_frame_rate(el->lav_fd[n]));
   		veejay_msg(2,"\tSampling format:  %s", _chroma_str[ lav_video_MJPG_chroma(el->lav_fd[n])].name);
		veejay_msg(2,"\tFOURCC:           %s",lav_video_compressor(el->lav_fd[n]));
		veejay_msg(2,"\tAudio samps:      %ld", lav_audio_clips(el->lav_fd[n]));
		veejay_msg(2,"\tAudio chans:      %d", lav_audio_channels(el->lav_fd[n]));
		veejay_msg(2,"\tAudio bits:       %d", lav_audio_bits(el->lav_fd[n]));
		veejay_msg(2,"\tAudio rate:       %ld", lav_audio_rate(el->lav_fd[n]));
	}
	else
	{
		veejay_msg(2, "\tFull name	%s",realname);	
		veejay_msg(2, "\tFrames	        %d", lav_video_frames(el->lav_fd[n]));
		veejay_msg(2, "\tDecodes into    %s", _chroma_str[ lav_video_MJPG_chroma( el->lav_fd[n]) ]);
	}

	nerr = 0;
	if (n == 0)
	{
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
		    /* TODO: This guessing here is a bit dubious but it can be over-ridden */
		    if (el->video_fps > 24.95 && el->video_fps < 25.05)
			el->video_norm = 'p';
		    else if (el->video_fps > 29.92 && el->video_fps <= 30.02)
			el->video_norm = 'n';
		}

		el->audio_chans = lav_audio_channels(el->lav_fd[n]);
		if (el->audio_chans > 2) {
		 	  el->num_video_files --;
		 	  veejay_msg(VEEJAY_MSG_ERROR, "File %s has %d audio channels - cant play that!",
			              realname,el->audio_chans);
			    nerr++;
		}
	
		el->has_audio = (el->audio_chans == 0 ? 0: 1);
		el->audio_bits = lav_audio_bits(el->lav_fd[n]);
		el->play_rate = el->audio_rate = lav_audio_rate(el->lav_fd[n]);
		el->audio_bps = (el->audio_bits * el->audio_chans + 7) / 8;
   	}
       	else
	{
		/* All files after first have to match the paramters of the first */
	
		if (el->video_height != lav_video_height(el->lav_fd[n]) ||
		    el->video_width != lav_video_width(el->lav_fd[n]))
		{
		    veejay_msg(VEEJAY_MSG_ERROR,"File %s: Geometry %dx%d does not match %dx%d.",
				realname, lav_video_width(el->lav_fd[n]),
				lav_video_height(el->lav_fd[n]), el->video_width,
				el->video_height);
		    nerr++;
	 	}
	}
		
	/* give a warning on different fps instead of error , this is better 
	   for live performances */
	if (fabs(el->video_fps - lav_frame_rate(el->lav_fd[n])) >
	    0.0000001) 
	    veejay_msg(VEEJAY_MSG_WARNING,"(Ignoring) File %s: fps is %3.2f should be %3.2f", realname,
		       lav_frame_rate(el->lav_fd[n]), el->video_fps);
	
	/* If first file has no audio, we don't care about audio */

	if (el->has_audio) {
	    if (el->audio_chans != lav_audio_channels(el->lav_fd[n]) ||
		el->audio_bits != lav_audio_bits(el->lav_fd[n]) ||
		el->audio_rate != lav_audio_rate(el->lav_fd[n])) {
		veejay_msg(VEEJAY_MSG_WARNING,"File %s: Audio is %d chans %d bit %ld Hz,"
			   " should be %d chans %d bit %ld Hz",
			   realname, lav_audio_channels(el->lav_fd[n]),
			   lav_audio_bits(el->lav_fd[n]),
			   lav_audio_rate(el->lav_fd[n]), el->audio_chans,
			   el->audio_bits, el->audio_rate);
	//	nerr++;
	    }
	}

	if (nerr)
	{
		el->num_video_files --;
		if(el->lav_fd[n]) lav_close( el->lav_fd[n] );
	    	if(el->video_file_list[n]) free(el->video_file_list[n]);
		return -1;
        }
    
	compr_type = (const char*) lav_video_compressor(el->lav_fd[n]);
	if(!compr_type)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get codec information from lav file");
		if(el->lav_fd[n]) lav_close( el->lav_fd[n] );
		if(el->video_file_list[n]) free(el->video_file_list[n]);
		return -1;
	}

/*
	if(el->is_empty)
	{
		el->video_frames = el->num_frames[0];
		el->video_frames -= DUMMY_FRAMES;
	}*/
	return n;
}

void		vj_el_show_formats(void)
{
#ifdef SUPPORT_READ_DV2
		veejay_msg(VEEJAY_MSG_INFO,
			"Video formats: AVI and  RAW DV");
		veejay_msg(VEEJAY_MSG_INFO, 
			"\t[dvsd|dv] DV Video (Quasar DV codec)");
#else
		veejay_msg(VEEJAY_MSG_INFO,
			"Video format: AVI");	
#endif
		veejay_msg(VEEJAY_MSG_INFO,
			"\t[yv16] Planer YUV 4:2:2");
		veejay_msg(VEEJAY_MSG_INFO,
			"\t[iyuv] Planer YUV 4:2:0");
		veejay_msg(VEEJAY_MSG_INFO,
			"\t[mjpg|mjpa] Motion JPEG");
		veejay_msg(VEEJAY_MSG_INFO,
			"Limited support for:");
		veejay_msg(VEEJAY_MSG_INFO,
			"\t[div3] MS MPEG4v3 Divx Video");
		veejay_msg(VEEJAY_MSG_INFO,
			"\t[mp4v] MPEG4 Video (ffmpeg experimental)");  		
#ifdef USE_GDK_PIXBUF
		veejay_msg(VEEJAY_MSG_INFO,
			"Image types supported:");
		vj_picture_display_formats();
#endif

}


static int	vj_el_dummy_frame( uint8_t *dst[3], editlist *el ,int pix_fmt)
{
	const int uv_len = (el->video_width * el->video_height) / ( (pix_fmt==FMT_422 ? 2 : 4));
	const int len = el->video_width * el->video_height;
veejay_msg(0,"Cannot handle 444");
	memset( dst[0], 16, len );
	memset( dst[1],128, uv_len );
	memset( dst[2],128, uv_len );
	return 1;
}

int	vj_el_get_file_fourcc(void *edl, int num, char *fourcc)
{
	editlist *el = (editlist*) edl;
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

static int	vj_el_can_use_dri(editlist *el, uint64_t n, VJFrame *dst)
{
	int vf = el->yuv_taste[ N_EL_FILE(n) ];
	int df = dst->pixfmt;

	if( YUV_SAMPLING_420(vf) && YUV_SAMPLING_420(df ))
		return 1;
	if( YUV_SAMPLING_422(vf) && YUV_SAMPLING_422(df ))
		return 1;
	if( YUV_SAMPLING_444(vf) && YUV_SAMPLING_444(df ))
		return 1;
	return 0;
}

int	vj_el_get_video_frame(void *edl, long nframe, void *fdst)
{
	VJFrame *dst = (VJFrame*) fdst;
#ifdef STRICT_CHECKING
	assert(fdst != NULL );
	assert( dst->width > 0 );
	assert( dst->height > 0 );
#endif
	editlist *el = (editlist*) edl;

	int res = 0;
   	uint64_t n;
	int decoder_id =0;
	int c_i = 0;
	vj_decoder *d = NULL;

#ifdef STRICT_CHECKING
	assert( el->has_video == 1);
	assert( el->is_empty == 0 );
#endif
	
/*	if( el->has_video == 0 || el->is_empty )
	{
		vj_el_dummy_frame( dst->data, el, dst->format );
		return 2;
	}*/

	if (nframe < 0)
		nframe = 0;

	if (nframe > el->video_frames)
		nframe = el->video_frames;

	n = el->frame_list[nframe];

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
 		}
		if(decoder_id > 0 )
		{
			c_i = _el_get_codec(decoder_id);
			if(c_i == -1)
			{
				veejay_msg(VEEJAY_MSG_ERROR ,"Unknown codec!");
				return -1;
			}
		
			if( el_codecs[c_i] == NULL )
			{
				int dri = vj_el_can_use_dri(el, n, dst);
				el_codecs[c_i] = _el_new_decoder( decoder_id, el->video_width, el->video_height, el->video_fps,dri);
				if(!el_codecs[c_i])
				{
					veejay_msg(VEEJAY_MSG_ERROR,"Failed to initialize codec");
					return -1;
				}
			}
			el_codecs[c_i]->img = dst;
			
		}

		if(decoder_id == 0)	
		{
			veejay_msg(0, "Unable to find decoder. Abort");
			return -1;
		}

		if(decoder_id == 0xffff)
		{
			//@ decode images from pixbuf.c . vj_get_picture must be modified to support dst->format
			//@ need file to test first
		}
	}

	c_i = _el_get_codec( decoder_id );
        if(c_i >= 0 && c_i < MAX_CODECS)
                d = el_codecs[c_i];
	else
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Choked on decoder" );
		return -1;
	}

	//@ edl cache only keeps raw buffers to reduce disk access
	if(!in_cache )
	{
		if(lav_filetype( el->lav_fd[N_EL_FILE(n)] ) != 'x')
            	{
		    res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);
		    if( res <= 0 )
		    {
			    veejay_msg(VEEJAY_MSG_WARNING, "Error while getting frame %ld ", nframe);
			return 0;  
		    }
		    cache_frame( el->cache, d->tmp_buffer, res, nframe, decoder_id );
		}
	}

	int len = el->video_width * el->video_height;
	uint8_t *data = ( in_cache == NULL ? d->tmp_buffer: in_cache );
	int inter = 0;
	int got_picture = 0;

	//@ problem while getting frames: Format of FRAME may change any time.
	
	//@ start to deliver in YUV planar performer needs
	if(decoder_id > CODEC_ID_YUV_PLANAR )
	{
		yuv_1plane_to_planar( (decoder_id - CODEC_ID_YUV_PLANAR - 1),
					data,
					dst,
					d->sampler );
		return 1;
	}
	else
	{
		inter = lav_video_interlacing(el->lav_fd[N_EL_FILE(n)]);
		int src_fmt = d->context->pix_fmt;
		int dst_fmt = dst->pixfmt;
		AVPicture pict,pict2;
		memset(&pict,0,sizeof(AVPicture));
		pict.data[0] = dst->data[0];
		pict.data[1] = dst->data[1];
		pict.data[2] = dst->data[2];
		pict.linesize[0] = dst->width;
		pict.linesize[1] = dst->width >> dst->shift_v;
		pict.linesize[2] = dst->width >> dst->shift_v;

		AVFrame test_f;
		memset(&test_f,0,sizeof(AVFrame));
		len = avcodec_decode_video(
			d->context,
			d->frame,
			&got_picture,
			data,
			res
		);

		if( len <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
			 "Frame %ld broken, fix your videofiles",
				nframe);
			return 0;
		}
		if( !d->frame->opaque )	//@ indirect
		{
			if( el->auto_deinter && inter != LAV_NOT_INTERLACED)
			{
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
				img_convert( &pict, dst_fmt, (const AVPicture*) &pict2, src_fmt,
					el->video_width,el->video_height);
			}
			else
			{
				__builtin_prefetch( pict.data[0],1,3 );
				__builtin_prefetch( pict.data[1],1,3 );
				__builtin_prefetch( pict.data[2],1,3 );
				__builtin_prefetch( d->frame->data[0],0,3);

				img_convert( &pict, dst_fmt, (const AVPicture*) d->frame, src_fmt,
					el->video_width, el->video_height );
			}
		}
		else
		{
			//@ dri
			dst = d->frame->opaque;
			if(el->itu601)
				subsample_ycbcr_itu601(d->sampler,dst);
		}
		return 1;
	}

	veejay_msg(VEEJAY_MSG_WARNING, "Error decoding frame %ld - %d ", nframe,len);
	return 0;  
}

void	vj_el_set_itu601( void *edl , int status )
{
#ifdef STRICT_CHECKING
	assert ( edl != NULL );
#endif
	editlist *el = (editlist*) edl;
	el->itu601 = status;
	if(el->itu601)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Clamping EDL frames to ITU601");
	}
}


int	vj_el_get_audio_frame(void *edl, uint32_t nframe, void *dav, int n_packets)
{
	AFrame *av = (AFrame*) dav;
	editlist *el = (editlist*) edl;
    long pos, asize;
    int ret = 0;
    uint64_t n;	
    uint64_t n2;
	unsigned long ns0, ns1, nswap;

	if(el->is_empty)
	{
		int ns = el->audio_rate / el->video_fps * n_packets;
		memset( av->data, 0, sizeof(uint8_t) * ns * el->audio_bps );
		av->samples = ns;
		return 1;
	}

    if (!el->has_audio)
	return 0;

    if (nframe < 0)
		nframe = 0;

    if (nframe > el->video_frames)
		nframe = el->video_frames;

    n = el->frame_list[nframe];

    n2 = n + n_packets;

    /*if( lav_is_DV( el->lav_fd[N_EL_FILE(n)] ) )
    {
	lav_set_video_position( el->lav_fd[N_EL_FILE(n)] , nframe );
	return lav_read_audio( el->lav_fd[N_EL_FILE(n)], dst, 0  );
    }*/

	av->rate = el->audio_rate;
	av->bits = el->audio_bits;
	av->bps  = el->audio_bps;
	av->num_chans = el->audio_chans;
    
    ns1 = (double) N_EL_FRAME(n2) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);
    if (ret < 0)
    {
	    veejay_msg(0, "Error seeking to %d",ns0 );
		return -1;
	}
    //mlt need int16_t
    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], av->data, (ns1 - ns0));
    if (ret < 0)
    {
	    veejay_msg(0, "Unable to read audio data %ld ",ns1-ns0);
		return -1;
    }
    av->samples = ns1 - ns0;
    return av->samples;

}


void *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt)
{
	editlist *el = vj_malloc(sizeof(editlist));
	if(!el) return NULL;
	memset( el, 0, sizeof(editlist));
	el->MJPG_chroma = chroma;
	el->video_norm = norm;
	el->is_empty = 1;
	el->video_width = width;
	el->video_height = height;
	el->video_frames = DUMMY_FRAMES; /* veejay needs at least 2 frames (play and queue next)*/
	el->video_fps = fps;
	el->video_inter = LAV_NOT_INTERLACED;
#ifdef STRICT_CHECKING
	assert( fmt == FMT_420 || FMT_422 || FMT_444 );
#endif
	
	/* output pixel format */
	el->auto_deinter = deinterlace;
	el->max_frame_size = width*height*3;
	el->last_afile = -1;
	el->frame_list = NULL;
	el->cache = NULL;
	return (void*)el;
}


void	vj_el_close( void *edl )
{
	editlist *el = (editlist*) edl;
	int i;
	for ( i = 0; i < el->num_video_files; i ++ )
	{
		if(!el->ref[i])
		{
			if( el->lav_fd[i] ) lav_close( el->lav_fd[i] );
		}
		if( el->video_file_list[i]) free(el->video_file_list[i]);
	}
	if( el->cache )
		free_cache( el->cache );

	if( el->frame_list )
		free(el->frame_list );
	free(el);
}
/*
void *vj_el_init_with_args(char **filename, int num_files, int flags, int deinterlace, int force
	,char norm, int out_fmt)
{
	editlist *el = vj_malloc(sizeof(editlist));
	memset(el, 0, sizeof(editlist));
	FILE *fd;
	char line[1024];
	uint64_t	index_list[MAX_EDIT_LIST_FILES];
	int	num_list_files;
	long i,nf=0;
	int n1=0;
	int n2=0;
	long nl=0;
	uint64_t n =0;
	bzero(line,1024);
	if(!el) return NULL;
#ifdef USE_GDK_PIXBUF
	vj_picture_init();
#endif
	memset( el, 0, sizeof(editlist) );  

	el->has_video = 1; //assume we get it   
	el->MJPG_chroma = CHROMA420;
	if(!filename[0] || filename == NULL)
	{
		vj_el_free(el);
		return NULL;	
	}

    if (strcmp(filename[0], "+p") == 0 || strcmp(filename[0], "+n") == 0)
	{
		el->video_norm = filename[0][1];
		nf = 1;
		veejay_msg(VEEJAY_MSG_DEBUG,"Norm set to %s",  el->video_norm == 'n' ? "NTSC" : "PAL");
    }

	if(force)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Forcing load on interlacing and gop_size");
	}

	for (; nf < num_files; nf++)
	{
		struct stat fileinfo;
		if(stat( filename[nf], &fileinfo)== 0) 
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
			    	veejay_msg(VEEJAY_MSG_DEBUG, "Edit list %s opened", filename[nf]);
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
							veejay_msg(VEEJAY_MSG_ERROR, "Norm already set to PAL");
							vj_el_free(el);
							return NULL;
						}
						el->video_norm = 'n';
					}
		    		else
				{
					if (el->video_norm == 'n')
					{
			    		veejay_msg(VEEJAY_MSG_ERROR,"Norm allready set to NTSC");
						vj_el_free(el);
						return NULL;
					}
					el->video_norm = 'p';
			    	}
		    		fgets(line, 1024, fd);
		    		sscanf(line, "%d", &num_list_files);

		   	 	veejay_msg(VEEJAY_MSG_DEBUG, "Edit list contains %d files", num_list_files);

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

					line[n - 1] = 0;
	
					index_list[i] =
					    open_video_file(line, el, flags, deinterlace,force,norm);
	
					if(index_list[i]< 0)
					{
						vj_el_free(el);
						return NULL;
					}
		   		 }
	
			
				    while (fgets(line, 1024, fd))
				   {
						if (line[0] != ':')
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
								el->frame_list[el->video_frames] =  EL_ENTRY( index_list[ nl], i);

								el->video_frames++;
							}
						}
			fclose(fd);
			}
			else
			{
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
						el->frame_list[el->video_frames] = EL_ENTRY(n, i);
						el->video_frames++;
					}
				}
			}
    		}
	}

	if( el->num_video_files == 0 || 
		el->video_width == 0 || el->video_height == 0 || el->video_frames <= 2)
	{
		vj_el_free(el);
		return NULL;
	}



	for (i = 0; i < el->video_frames; i++)
	{
		n = el->frame_list[i];
		if(!el->lav_fd[N_EL_FILE(n)] )
		{
			vj_el_free(el);
			return NULL;
		}
		if (lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n)) >
		    el->max_frame_size)
		    el->max_frame_size =
			lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));


   	}

	if(out_fmt == -1)
	{
		int lowest = FMT_420;
		for( i = 0 ; i < el->num_video_files; i ++ )
		{
			if( lav_video_MJPG_chroma( el->lav_fd[ i ] ) == CHROMA422 )
				lowest = FMT_422;
		}	
		out_fmt = lowest;
	}
	
	el->pixel_format = out_fmt;


	el->last_afile = -1;


    //el->auto_deinter = auto_deinter;
    //if(el->video_inter != 0 ) el->auto_deinter = 0;
	el->auto_deinter = 0;

	// FIXME
	veejay_msg(VEEJAY_MSG_WARNING, "Editlist is using %s", (el->pixel_format == FMT_420 ? "yuv420p" : "yuv422p"));




	return (void*) el;
}
*/


void *vj_el_open_video_file(const char *filename )
{
	editlist *el = vj_malloc(sizeof(editlist));
	memset(el, 0, sizeof(editlist));

	FILE *fd;
	char line[1024];
	uint64_t	index_list[MAX_EDIT_LIST_FILES];
	int		num_list_files;

	long i,nf=0;
	int n1=0;
	int n2=0;
	long nl=0;
	uint64_t n =0;

	bzero(line,1024);

	el->itu601 = itu601_clamp_;
	el->has_video = 1; //assume we get it   
	el->MJPG_chroma = CHROMA420;
	/* Check if file really exists, is mounted etc... */
	struct stat fileinfo;

	if(stat( filename, &fileinfo)== 0) 
	{	/* Check if filename is a edit list */
		fd = fopen(filename, "r");
		if (fd <= 0)
		{
		   	 veejay_msg(VEEJAY_MSG_ERROR,"Error opening %s:", filename);
		 	 vj_el_free(el);
			 return NULL;
		}
		fgets(line, 1024, fd);
		if (strcmp(line, "LAV Edit List\n") == 0)
		{
		   	/* Ok, it is a edit list */
		    	veejay_msg(VEEJAY_MSG_DEBUG, "Edit list %s opened", filename);
		    	/* Read second line: Video norm */
		    	fgets(line, 1024, fd);
		    	if (line[0] != 'N' && line[0] != 'n' && line[0] != 'P' && line[0] != 'p')
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Edit list second line is not NTSC/PAL");
				vj_el_free(el);
				return NULL;
			}
			veejay_msg(VEEJAY_MSG_DEBUG,"Edit list norm is %s", line[0] =='N' || line[0] == 'n' ? "NTSC" : "PAL" );
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
				    open_video_file(line, el );
	
				if(index_list[i]< 0)
				{
					vj_el_free(el);
					return NULL;
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
						el->frame_list[el->video_frames] =  EL_ENTRY( index_list[ nl], i);
						el->video_frames++;
					}
				}
		 	} /* done reading editlist entries */
			fclose(fd);
		}
		else
		{
	    	/* Not an edit list - should be a ordinary video file */
	    		fclose(fd);
		 	n = open_video_file(filename, el );
			if(n >= 0 )
			{
				if(!vj_el_scan_video_frame(el))
				{
					veejay_msg(0, "Failed to scan video file '%s'",filename);
				}
				else
				{	
					el->frame_list = (uint64_t *) realloc(el->frame_list,
				      (el->video_frames +
					       el->num_frames[n]) *
					      sizeof(uint64_t));
					if (el->frame_list==NULL)
					{
						veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
						return NULL;
					}
	 				for (i = 0; i < el->num_frames[n]; i++)
					{
						el->frame_list[el->video_frames] = EL_ENTRY(n, i);
						el->video_frames++;
					}
			
				}
			}
		}
		if( el->video_frames <= 0 )
		{
			vj_el_free(el);
			return NULL;				
		}
		
    	}

	if( el->num_video_files == 0 || 
		el->video_width == 0 || el->video_height == 0 || el->video_frames <= 2)
	{
		veejay_msg(0,
				"Invalid parameters: nf=%d, w=%d,h=%d, vf=%d",
				el->num_video_files,
				el->video_width,
				el->video_height,
				el->video_frames );
		vj_el_free(el);
		return NULL;
	}

	/* do we have anything? */

	/* Calculate maximum frame size */

	for (i = 0; i < el->video_frames; i++)
	{
		n = el->frame_list[i];
		if(!el->lav_fd[N_EL_FILE(n)] )
		{
			vj_el_free(el);
			return NULL;
		}
		if (lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n)) >
		    el->max_frame_size)
		    el->max_frame_size =
			lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
   	}

	/* Help for audio positioning */
	el->last_afile = -1;
	el->auto_deinter = 1;

	return (void*) el;
}



void	vj_el_free(void *edl)
{
	editlist *el = (editlist*) edl;
	if(el)
	{
		int n = el->num_video_files;
		int i;
		vj_el_clear_cache( el );

		for( i = 0; i < MAX_EDIT_LIST_FILES ; i++ )
		{
			if( el->video_file_list[i] && el->lav_fd[i])
				free(el->video_file_list[i]);
			/* close fd if ref counter is zero */
			if(el->lav_fd[i])
				lav_close( el->lav_fd[i]);
		}
		if(el->frame_list)
			free(el->frame_list);
		if(el->cache)
			free_cache(el->cache);
		free(el);   
		el = NULL;
	}
}

void	vj_el_ref(void *data, int n)
{
	editlist *el = (editlist*) data;
	el->ref[n]++;
}
void	vj_el_unref(void *data, int n)
{
	editlist *el = (editlist*) data;
	if(el->ref[n])
		el->ref[n]--;
}

void	vj_el_print(void *edl)
{
	int i;
	char timecode[64];
	char interlacing[64];
	MPEG_timecode_t ttc;
	editlist *el = (editlist*) edl;
	veejay_msg(VEEJAY_MSG_INFO,"EditList settings: %dx%d@%2.2f %s\tAudio:%d Hz/%d channels/%d bits",
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

MPEG_timecode_t get_timecode(void *edl, long num_frames)
{
	editlist *el = (editlist*) edl;
	MPEG_timecode_t tc;
	memset(&tc,0,sizeof(tc));
	mpeg_timecode(&tc, num_frames,
			mpeg_framerate_code( mpeg_conform_framerate( el->video_fps )),
			el->video_fps );
	return tc;
}

int	vj_el_get_file_entry(void *edl, long *start_pos, long *end_pos, long entry )
{
	editlist *el = (editlist*) edl;

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



char *vj_el_write_line_ascii( void *edl, int *bytes_written )
{
	
	editlist *el = (editlist*) edl;

	if(el->is_empty)
		return NULL;

	int num_files;
	int64_t oldfile, oldframe;
	int64_t index[MAX_EDIT_LIST_FILES];
	int64_t n;
	char *result = NULL;
	int64_t n1 = 0;
	int64_t j = 0;
	int64_t n2 = el->video_frames-1;
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
   	for (j = 0; j < MAX_EDIT_LIST_FILES; j++)
	{
		if (index[j] > 0 )
			index[j] = (int64_t)num_files++;
	}
	int nnf = 0;
	for ( j = 0; j < MAX_EDIT_LIST_FILES ; j ++ )
		if(index[j] >= 0 && el->video_file_list[j] != NULL)
		{
			nnf ++;
		}
	n = el->frame_list[n1];
	oldfile = index[ N_EL_FILE(n) ];
   	oldframe = N_EL_FRAME(n);
 
	
	est_len = nnf * 1024;
	result = (char*) vj_malloc(sizeof(char) * est_len );
	bzero( result, est_len );
	sprintf(result, "%04d",nnf );

	for (j = 0; j < MAX_EDIT_LIST_FILES; j++)
	{
		if (index[j] >= 0 && el->video_file_list[j] != NULL)
		{
			char filename[400];
			char fourcc[6];
			bzero(filename,400);
			bzero(fourcc,6);
			sprintf(fourcc, "%s", "????");
			vj_el_get_file_fourcc( el, j, fourcc );
			sprintf(filename ,"%03d%s%04d%010ld%02d%s",
				strlen( el->video_file_list[j]  ),
				el->video_file_list[j],
				(int) j,
				el->num_frames[j],
				strlen(fourcc),
				fourcc 
			);
			sprintf(fourcc, "%04d", strlen( filename ));
			strncat( result, fourcc, strlen(fourcc ));
			strncat ( result, filename, strlen(filename));
		}
	}

	char first[33];
	bzero(first,33);
	sprintf(first, "%016lld%016lld",oldfile, oldframe);
	strncat( result, first, strlen(first) );

  	for (j = n1+1; j <= n2; j++)
	{
		n = el->frame_list[j];
		if ( index[ N_EL_FILE(n) ] != oldfile ||
			N_EL_FRAME(n) != oldframe + 1 )	
		{
			int len = 20 + (16 * 3 ) + strlen( el->video_file_list[ index[N_EL_FILE(n)] ] );
			char *tmp = (char*) vj_malloc(sizeof(char) * len );
			bzero(tmp,len);	
			sprintf( tmp, "%016lld%016lld%016lld",
				 oldframe,
				 index[N_EL_FILE(n)],
				 N_EL_FRAME(n) );
			strncat( result, tmp, strlen(tmp) );
			free(tmp);
		}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
    	}

	char last_word[16];
	sprintf(last_word,"%016lld", oldframe);
	strncat( result, last_word, 16 );
	*bytes_written = strlen( result );

	return result;
}

int	vj_el_write_editlist( char *name, long _n1, long _n2, void *edl )
{
	FILE *fd;
    	int num_files;
	int64_t oldfile, oldframe;
	int64_t index[MAX_EDIT_LIST_FILES];
	int64_t n;
	int64_t n1 = (uint64_t) _n1;
	int64_t n2 = (uint64_t) _n2;
	int64_t i;

	editlist *el = (editlist*) edl;

	if(el->is_empty)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames in EditList" );
		return 0;
	}

    	if (n1 < 0)
		n1 = 0;

    	if (n2 >= el->video_frames || n2 < n1)
		n2 = el->video_frames - 1;

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

    	fprintf(fd, "%lld %lld ", oldfile, oldframe);
    	for (i = n1 + 1; i <= n2; i++)
	{
		n = el->frame_list[i];
		if (index[N_EL_FILE(n)] != oldfile
		    || N_EL_FRAME(n) != oldframe + 1)
		{
		    fprintf(fd, "%lld\n", oldframe);
	    	fprintf(fd, "%lld %lld ",index[N_EL_FILE(n)], N_EL_FRAME(n));
		}
		oldfile = index[N_EL_FILE(n)];
		oldframe = N_EL_FRAME(n);
    	}
    	n = fprintf(fd, "%lld\n", oldframe);

    	/* We did not check if all our prints succeeded, so check at least the last one */
    	if (n <= 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error writing edit list: ");
		return 0;
    	}

    	fclose(fd);

	return 1;
}

void	vj_el_frame_cache(int n )
{
	if(n > 1  || n < 1000)
	{
		mmap_size = n;
	}
}

uint64_t	*vj_el_edit_copy( void *edl, uint64_t start, uint64_t end, uint64_t *len )
{
	editlist *el = (editlist*) edl;
	uint64_t *res = NULL;
	
	if(el->is_empty)
		return NULL;

	uint64_t i;
	uint64_t n1 = (uint64_t) start;
	uint64_t n2 = (uint64_t) end;

	res = (uint64_t *) vj_malloc((n2 - n1 + 1) * sizeof(uint64_t));

	if (!res)
		return NULL;
    
    	uint64_t k = 0;

 	for (i = n1; i <= n2; i++)
		res[k++] = el->frame_list[i];
  
	*len = k;
	
	return res;
}

int		vj_el_edit_del( void *edl, uint64_t start, uint64_t end )
{
	editlist *el = (editlist*) edl;

	if(el->is_empty)
		return 0;
	uint64_t i;
	uint64_t n1 =  (uint64_t) start;
	uint64_t n2 =  (uint64_t) end;

	if( end < start || start > el->video_frames ||
			end >= el->video_frames || end < 0 || start < 0 )
		return 0;
	
	for( i = n2 + 1 ; i < el->video_frames; i ++ )
		el->frame_list[i - (n2 - n1 + 1 ) ] =
			el->frame_list[i];
	el->video_frames -= (end - start + 1);
	return 1;
}

int		vj_el_edit_paste( void *edl, uint64_t destination, uint64_t *frame_list, uint64_t len)
{
	editlist *el = (editlist*) edl;

	if( destination < 0 || destination >= el->video_frames )
		return 0;
	el->frame_list = (uint64_t*) realloc( el->frame_list,
			el->video_frames + len * sizeof(uint64_t));
	if(!el->frame_list)
		return 0;
	uint64_t i,k = len;
	for( i = el->video_frames - 1; i >= destination &&  i > 0 ; i -- )
		el->frame_list[ i + k ] = el->frame_list[i];
	k = destination;
	for( i = 0; i < len; i ++ )
		el->frame_list[k++] = frame_list[i];
	return 1;
}

uint64_t		vj_el_get_num_frames(void *edl)
{
	editlist *el = (editlist*) edl;
	return el->video_frames - 1;
}

int		vj_el_get_width( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->video_width;
}
int		vj_el_get_height( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->video_height;
}
int		vj_el_get_inter( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->video_inter;
}

int		vj_el_get_audio_rate( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->audio_rate;
}
int		vj_el_get_audio_bps( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->audio_bps;
}
int		vj_el_get_audio_bits( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->audio_bits;
}
int		vj_el_get_audio_chans( void *edl )
{
	editlist *el = (editlist*) edl;
	return el->audio_chans;
}

char	vj_el_get_norm(void *edl)
{
	editlist *el = (editlist*) edl;
	return el->video_norm;
}

float	vj_el_get_fps(void *edl)
{
	editlist *el = (editlist*) edl;
	return el->video_fps;
}
