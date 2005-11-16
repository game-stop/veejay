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
/*


	This file contains code-snippets from the mjpegtools' EditList
	(C) The Mjpegtools project

	http://mjpeg.sourceforge.net
*/
#include <config.h>
#include <string.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vj-global.h>
#include <libel/lav_io.h>
#include <libel/vj-el.h>
#include <libvje/vje.h>
#include <libel/vj-avcodec.h>
#include <limits.h>
#include <utils/mpegconsts.h>
#include <utils/mpegtimecode.h>
#include <libvjmem/vjmem.h>
#include "avcodec.h"
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef SUPPORT_READ_DV2
#include "rawdv.h"
#endif
#define MAX_CODECS 12
#define CODEC_ID_YUV420 999
#define CODEC_ID_YUV422 998

#define DUMMY_FRAMES 2

static struct
{
	const char *name;
} _chroma_str[] = 
{
	{	"Unknown"	},
	{	"4:2:0"		},
	{	"4:2:2"		},
	{	"4:4:4"		},
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
	{ "pict",	0xffff	}, /* invalid fourcc */
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
        AVCodec *codec[2]; // veejay supports only 2 yuv formats internally
        AVFrame *frame[2];
        AVCodecContext  *context[2];
        uint8_t *tmp_buffer;
        uint8_t *deinterlace_buffer[3];
        int fmt;
        int ref;
} vj_decoder;

static	vj_decoder *el_codecs[MAX_CODECS];


static	_el_get_codec(int id )
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

static void	_el_free_decoder( vj_decoder *d )
{
	if(d)
	{
		int i;
		if(d->tmp_buffer)
			free( d->tmp_buffer );
		for( i = 0; i < 3 ; i ++ )
			if(d->deinterlace_buffer[i]) free(d->deinterlace_buffer[i]);

		for ( i = 0; i < 2 ; i ++ )
		{
			if(d->context[i])
			{	avcodec_close( d->context[i] ); 
				free( d->context[i] );
				d->context[i] = NULL;
			}
			if(d->frame[i]) av_free(d->frame[i]);
		}
		free(d);
	}
	d = NULL;
}

void	vj_el_init()
{
	int i;
	for( i = 0; i < MAX_CODECS ;i ++ )
		el_codecs[i] = NULL;

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

vj_decoder *_el_new_decoder( int id , int width, int height, float fps, int pixel_format)
{
        vj_decoder *d = (vj_decoder*) vj_malloc(sizeof(vj_decoder));
        
        if(!d) return NULL;
	memset( d, 0, sizeof(vj_decoder));

        if( id != CODEC_ID_YUV422 && id != CODEC_ID_YUV420)
        {
		int i;
		for( i = 0; i < 2; i ++ )
		{
               		d->codec[i] = avcodec_find_decoder( id );
               		if(!d->codec[i])
                	        return NULL;
			d->codec[i] = avcodec_find_decoder( id );
			if(!d->codec[i])
				return NULL;
               		d->context[i] = avcodec_alloc_context();
               		d->context[i]->width = width;
              		d->context[i]->height= height;
                	d->context[i]->frame_rate = fps;
                	d->context[i]->pix_fmt = ( i == 0 ? PIX_FMT_YUV420P : PIX_FMT_YUV422P);
               		d->frame[i] = avcodec_alloc_frame();
		}
        }       



        d->tmp_buffer = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height * 4 );
        if(!d->tmp_buffer)
                return NULL;

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

	int i;
	for(i = 0;i < 2 ; i ++ )
	{
       	 if(d->codec[i] != NULL)
       	 {
                if ( avcodec_open( d->context[i], d->codec[i] ) < 0 )
                {
                        veejay_msg(VEEJAY_MSG_ERROR, "Error initializing decoder %d",id); 
                        if(d) free(d);
                        return NULL;
                }
         }
 	}        
        d->ref = 1;
        return d;
}

void	vj_el_set_image_output_size(editlist *el)
{
	lav_set_project(
		el->video_width, el->video_height, el->video_fps ,
			el->pixel_format == FMT_420 ? 1 :0);
}

int open_video_file(char *filename, editlist * el, int preserve_pathname, int deinter, int force,
		char norm);

static int _el_probe_for_pixel_fmt( lav_file_t *fd )
{
	if(!fd) return -1;
	if( fd->MJPG_chroma == CHROMA420 )
		return FMT_420;
	if( fd->MJPG_chroma == CHROMA422 )
		return FMT_422;
	if( fd->MJPG_chroma == CHROMA444 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "YUV 4:4:4 is not supported yet. ");
		return FMT_422;
	}
	if( fd->MJPG_chroma == CHROMAUNKNOWN )
		return FMT_420;

	return -1;
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

	veejay_msg(VEEJAY_MSG_DEBUG, "Opening file '%s'", filename );

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
	      	return i;
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

    el->num_video_files++;

    el->lav_fd[n] = lav_open_input_file(filename,mmap_size);

    if (el->lav_fd[n] == NULL)
	{
		el->num_video_files--;	
	veejay_msg(VEEJAY_MSG_ERROR,"Error loading '%s' :%s",
		realname,lav_strerror());
	 	if(realname) free(realname);
		return -1;
	}

    _fc = lav_video_MJPG_chroma(el->lav_fd[n]);

    if( !(_fc == CHROMA422 || _fc == CHROMA420 || _fc == CHROMA444 || _fc == CHROMAUNKNOWN ))
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Input file %s is not in a valid format (%d)",filename,_fc);
		el->num_video_files --;
	    if(realname) free(realname);
		if( el->lav_fd[n] ) lav_close(el->lav_fd[n]);
		return -1;

	}

    if(chroma == CHROMAUNKNOWN)
	{ /* set chroma */
  	  el->MJPG_chroma = _fc;
	  chroma = _fc;
	}


	pix_fmt = _el_probe_for_pixel_fmt( el->lav_fd[n] );

	if(pix_fmt < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to determine pixel format");
		el->num_video_files--;	
		if( el->lav_fd[n] ) lav_close( el->lav_fd[n] );
		if(realname) free(realname);
		return -1;
	}

	el->yuv_taste[n] = pix_fmt;


	if(lav_video_frames(el->lav_fd[n]) < 2)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to load video files that contain less than 2 frames");
		if( el->lav_fd[n] ) lav_close(el->lav_fd[n]);
		if(realname) free(realname);
		el->num_video_files --;
		return -1;
	}

    el->num_frames[n] = lav_video_frames(el->lav_fd[n]);
    el->video_file_list[n] = strndup(realname, strlen(realname));
    /* Debug Output */
	if(n == 0 )
	{
    veejay_msg(VEEJAY_MSG_DEBUG,"\tFull name:       %s", filename, realname);
    veejay_msg(VEEJAY_MSG_DEBUG,"\tFrames:          %ld", lav_video_frames(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tWidth:           %d", lav_video_width(el->lav_fd[n]));
    veejay_msg(VEEJAY_MSG_DEBUG,"\tHeight:          %d", lav_video_height(el->lav_fd[n]));
    {
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

	 if( deinter == 1 && ( (lav_video_interlacing(el->lav_fd[n]) == LAV_INTER_TOP_FIRST ) || (lav_video_interlacing(el->lav_fd[n])==LAV_INTER_BOTTOM_FIRST)))
		el->auto_deinter = 1;

    	veejay_msg(VEEJAY_MSG_DEBUG,"\tInterlacing:      %s", int_msg);
    }

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
#ifdef USE_GDK_PIXBUF
		}

#endif
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
		  	  el->num_video_files --;
		  	  veejay_msg(VEEJAY_MSG_ERROR, "File %s has %d audio channels - cant play that!",
			              filename,el->audio_chans);
			    nerr++;
			}
	
			el->has_audio = (el->audio_chans == 0 ? 0: 1);
			el->audio_bits = lav_audio_bits(el->lav_fd[n]);
			el->play_rate = el->audio_rate = lav_audio_rate(el->lav_fd[n]);
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
		    veejay_msg(VEEJAY_MSG_ERROR,"File %s: Geometry %dx%d does not match %dx%d.",
				filename, lav_video_width(el->lav_fd[n]),
				lav_video_height(el->lav_fd[n]), el->video_width,
				el->video_height);
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
	    veejay_msg(VEEJAY_MSG_WARNING,"(Ignoring) File %s: fps is %3.2f should be %3.2f", filename,
		       lav_frame_rate(el->lav_fd[n]), el->video_fps);
	}
	/* If first file has no audio, we don't care about audio */

	if (el->has_audio) {
	    if (el->audio_chans != lav_audio_channels(el->lav_fd[n]) ||
		el->audio_bits != lav_audio_bits(el->lav_fd[n]) ||
		el->audio_rate != lav_audio_rate(el->lav_fd[n])) {
		veejay_msg(VEEJAY_MSG_WARNING,"File %s: Audio is %d chans %d bit %ld Hz,"
			   " should be %d chans %d bit %ld Hz",
			   filename, lav_audio_channels(el->lav_fd[n]),
			   lav_audio_bits(el->lav_fd[n]),
			   lav_audio_rate(el->lav_fd[n]), el->audio_chans,
			   el->audio_bits, el->audio_rate);
	//	nerr++;
	    }
	}

	if (nerr) {
	    el->num_video_files --;
		if(el->lav_fd[n]) lav_close( el->lav_fd[n] );
		if(realname) free(realname);
	    if(el->video_file_list[n]) free(el->video_file_list[n]);
	    return -1;
        }
    }
    compr_type = (const char*) lav_video_compressor(el->lav_fd[n]);
	if(!compr_type)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot get codec information from lav file");
		if(el->lav_fd[n]) lav_close( el->lav_fd[n] );
		if(realname) free(realname);
		if(el->video_file_list[n]) free(el->video_file_list[n]);
		return -1;
	}

     // initialze a decoder if needed
	decoder_id = _el_get_codec_id( compr_type );

	if(decoder_id > 0 && decoder_id != 0xffff)
	{
		int c_i = _el_get_codec(decoder_id);
		if(c_i == -1)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported codec %s",compr_type);
			if( el->lav_fd[n] ) lav_close( el->lav_fd[n] );
			if( realname ) free(realname );
			if( el->video_file_list[n]) free(el->video_file_list[n]);
			return -1;
		}
		if( el_codecs[c_i] == NULL )
		{
		//	el_codecs[c_i] = _el_new_decoder( decoder_id, el->video_width, el->video_height, el->video_fps, pix_fmt );
			el_codecs[c_i] = _el_new_decoder( decoder_id, el->video_width, el->video_height, el->video_fps, el->yuv_taste[ n ] );
			if(!el_codecs[c_i])
			{
				veejay_msg(VEEJAY_MSG_ERROR,"Cannot initialize %s codec", compr_type);
				if( el->lav_fd[n] ) lav_close( el->lav_fd[n] );
			    if(realname) free(realname);
				if( el->video_file_list[n]) free(el->video_file_list[n]);
				return -1;
			}
		}
	}

	if(decoder_id == 0)	
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Dont know how to handle %s (fmt %d) %x", compr_type, pix_fmt,decoder_id);
		if(realname) free(realname);
		if( el->video_file_list[n]) free( el->video_file_list[n] );
		if( el->lav_fd[n] ) lav_close( el->lav_fd[n]);
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
	memset( dst[0], 16, len );
	memset( dst[1],128, uv_len );
	memset( dst[2],128, uv_len );
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

int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[3])
{
	int res;
   	uint64_t n;
	int decoder_id =0;
	int c_i = 0;
	vj_decoder *d = NULL;

	int out_pix_fmt = el->pixel_format;
	int in_pix_fmt  = out_pix_fmt;
    if( el->has_video == 0 || el->is_empty )
	{ vj_el_dummy_frame( dst, el, out_pix_fmt ); return 2; }

    if (nframe < 0)
		nframe = 0;

    if (nframe > el->video_frames)
		nframe = el->video_frames;

	n = el->frame_list[nframe];

	in_pix_fmt = el->yuv_taste[N_EL_FILE(n)];

    	res = lav_set_video_position(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));
	decoder_id = lav_video_compressor_type( el->lav_fd[N_EL_FILE(n)] );

	if (res < 0)
	{
		veejay_msg(VEEJAY_MSG_ERROR,"Error setting video position: %s",
			  lav_strerror());
 	}
	int len = el->video_width * el->video_height;
	int uv_len = 0;

	int have_picture = 0;
	int inter = lav_video_interlacing(el->lav_fd[N_EL_FILE(n)]);
	int buf_len = 0;

/*	switch(decoder_id)
	{
		case 0xffff: 
			{uint8_t *ptr = lav_get_frame_ptr( el->lav_fd[N_EL_FILE(n)] );
			if(!ptr) return 0;
			uv_len = len / ( out_pix_fmt == FMT_420 ? 4: 2 );
			veejay_memcpy( dst[0], ptr, len);
			veejay_memcpy( dst[1], ptr+len,uv_len);
			veejay_memcpy( dst[2], ptr+(len+uv_len), uv_len);
			return 1;}
			break;
		case CODEC_ID_YUV420: 
			if( out_pix_fmt == FMT_420 )
			{
				uv_len = len / 4;
				veejay_memcpy( dst[0], d->tmp_buffer, len);
				veejay_memcpy( dst[1], d->tmp_buffer+len,uv_len);
				veejay_memcpy( dst[2], d->tmp_buffer+(len+uv_len), uv_len);
			}	
			else
			{
				uv_len = len / 4;
				return (yuv420p_to_yuv422p( d->tmp_buffer,
					    d->tmp_buffer+len,
					    d->tmp_buffer+len+uv_len,
					    dst,
					    el->video_width, el->video_height));

			}
			return 1;
		break;
		case CODEC_ID_YUV422: 
			if(out_pix_fmt == FMT_422 )
			{
				uv_len = len / 2;
				veejay_memcpy( dst[0], d->tmp_buffer, len);
				veejay_memcpy( dst[1], d->tmp_buffer+len,uv_len);
				veejay_memcpy( dst[2], d->tmp_buffer+len+uv_len, uv_len);
			}
			else
			{
				uint8_t *src[3];
				uv_len = len / 2;
				src[0] =  d->tmp_buffer;
				src[1] =  d->tmp_buffer + len;
				src[2] =  d->tmp_buffer + len + uv_len;
				yuv422p_to_yuv420p2( src, dst, el->video_width,el->video_height );
			}	
			return 1;
		break;
		default:

			buf_len = avcodec_decode_video( d->context,
							d->frame,
							&have_picture,
							d->tmp_buffer,
							res); 	
			if(buf_len>0)
			{
				AVPicture pict,pict2;
				int res = 0;
				int src_fmt = ( in_pix_fmt == FMT_420 ? PIX_FMT_YUV420P : PIX_FMT_YUV422P );
				int dst_fmt = ( out_pix_fmt == FMT_420 ? PIX_FMT_YUV420P : PIX_FMT_YUV422P );
				pict.data[0] = dst[0];
				pict.data[1] = dst[1];
				pict.data[2] = dst[2];

				pict.linesize[0] = el->video_width;
				pict.linesize[1] = el->video_width >> 1;
				pict.linesize[2] = el->video_width >> 1;

				if( el->auto_deinter && inter != LAV_NOT_INTERLACED)
				{
					pict2.data[0] = d->deinterlace_buffer[0];
					pict2.data[1] = d->deinterlace_buffer[1];
					pict2.data[2] = d->deinterlace_buffer[2];
					pict2.linesize[1] = el->video_width >> 1;
					pict2.linesize[2] = el->video_width >> 1;
					pict2.linesize[0] = el->video_width;

					res = avpicture_deinterlace( &pict2, (const AVPicture*) d->frame, src_fmt,
						el->video_width, el->video_height);

					img_convert( &pict, dst_fmt, (const AVPicture*) &pict2, src_fmt,
						el->video_width,el->video_height);
				}
				else
				{
					img_convert( &pict, dst_fmt, (const AVPicture*) d->frame, src_fmt,
							el->video_width, el->video_height );
				}
				return 1;
			}
		break;
	}
	return 0;
}
*/
	if( decoder_id == 0xffff )
	{
		uint8_t *p = lav_get_frame_ptr( el->lav_fd[N_EL_FILE(n)] );
		if(!p) return -1;
		int len = el->video_width * el->video_height;
		int uv_len = (el->video_width >> 1) * (el->video_height >> (out_pix_fmt == FMT_420 ? 1:0)); 
		veejay_memcpy( dst[0], p, len );
		veejay_memcpy( dst[1], p + len, uv_len );
		veejay_memcpy( dst[2], p + len + uv_len, uv_len );
		return 1;	
	}

	c_i = _el_get_codec( decoder_id );
 	if(c_i >= 0 && c_i < MAX_CODECS)
		d = el_codecs[c_i];
	else
	{	
		veejay_msg(VEEJAY_MSG_DEBUG, "Choking on decoder ID %d (%d)", decoder_id,c_i );
		return -1;
	}
	if(lav_filetype( el->lav_fd[N_EL_FILE(n)] ) != 'x')
		res = lav_read_frame(el->lav_fd[N_EL_FILE(n)], d->tmp_buffer);


	if( decoder_id == CODEC_ID_YUV420 )
	{	
		int len = el->video_width * el->video_height;
		int uv_len = len / 4;
		if(out_pix_fmt == FMT_420)
		{
			veejay_memcpy( dst[0], d->tmp_buffer, len);
			veejay_memcpy( dst[1], d->tmp_buffer+len,uv_len);
			veejay_memcpy( dst[2], d->tmp_buffer+(len+uv_len), uv_len);
		}
		else
		{
			return (yuv420p_to_yuv422p( d->tmp_buffer,
					    d->tmp_buffer+len,
					    d->tmp_buffer+len+uv_len,
						dst, el->video_width, el->video_height));
		}
	}
	else
	{
	if( decoder_id == CODEC_ID_YUV422)
	{
		int len = el->video_width * el->video_height;
		int uv_len = len /2;	
		veejay_memcpy( dst[0], d->tmp_buffer, len);
		veejay_memcpy( dst[1], d->tmp_buffer+len,uv_len);
		veejay_memcpy( dst[2], d->tmp_buffer+len+uv_len, uv_len);

	}
	else
	{
		int len;
		int got_picture = 0;
		int inter = lav_video_interlacing(el->lav_fd[N_EL_FILE(n)]);
		len = avcodec_decode_video(d->context[in_pix_fmt], d->frame[in_pix_fmt], &got_picture, d->tmp_buffer, res); 	
		if(len>0 && got_picture)
		{
			AVPicture pict,pict2;
		//	int dst_pix_fmt = (pix_fmt == FMT_422 ? PIX_FMT_YUV422P : PIX_FMT_YUV420P);
		//	int dst_pix_fmt = el->pixel_format;
			int src_fmt = ( in_pix_fmt == FMT_420 ? PIX_FMT_YUV420P: PIX_FMT_YUV422P );
			int dst_fmt = ( out_pix_fmt== FMT_420 ? PIX_FMT_YUV420P: PIX_FMT_YUV422P) ;

			int res = 0;
			pict.data[0] = dst[0];
			pict.data[1] = dst[1];
			pict.data[2] = dst[2];

			pict.linesize[0] = el->video_width;
			pict.linesize[1] = el->video_width / 2;
			pict.linesize[2] = el->video_width / 2;

			memset(dst[0], 255, len );
			memset(dst[1], 255, uv_len );
			memset(dst[2], 255,uv_len);
			if( el->auto_deinter && inter != LAV_NOT_INTERLACED)
			{
				pict2.data[0] = d->deinterlace_buffer[0];
				pict2.data[1] = d->deinterlace_buffer[1];
				pict2.data[2] = d->deinterlace_buffer[2];
				pict2.linesize[1] = el->video_width >> 1;
				pict2.linesize[2] = el->video_width >> 1;

				pict2.linesize[0] = el->video_width;
				res = avpicture_deinterlace( &pict2, (const AVPicture*) d->frame[in_pix_fmt], src_fmt,
					el->video_width, el->video_height);

				img_convert( &pict, dst_fmt, (const AVPicture*) &pict2, src_fmt,
					el->video_width,el->video_height);
			}
			else
			{
				img_convert( &pict, dst_fmt, (const AVPicture*) d->frame[in_pix_fmt], src_fmt,
						el->video_width, el->video_height );
			}
			
			return 1;
		}
		veejay_msg(VEEJAY_MSG_WARNING, "Error decoding frame %ld - %d ", nframe,len);
		return 0;  
	}
	}
	
    return 1;
}

int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst)
{
    long pos, asize;
    int ret = 0;
    uint64_t n;	
	int ns0, ns1;

	if(el->is_empty)
	{
		int ns = el->audio_rate / el->video_fps;
		memset( dst, 0, sizeof(uint8_t) * ns * el->audio_bps );
		return 1;
	}

    if (!el->has_audio)
	return 0;

    if (nframe < 0)
		nframe = 0;

    if (nframe > el->video_frames)
		nframe = el->video_frames;

    n = el->frame_list[nframe];

    /*if( lav_is_DV( el->lav_fd[N_EL_FILE(n)] ) )
    {
	lav_set_video_position( el->lav_fd[N_EL_FILE(n)] , nframe );
	return lav_read_audio( el->lav_fd[N_EL_FILE(n)], dst, 0  );
    }*/

    ns1 = (double) (N_EL_FRAME(n) + 1) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    //asize = el->audio_rate / el->video_fps;
    pos = nframe * asize;

    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    if (ret < 0)
		return -1;

    //mlt need int16_t
    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], dst, (ns1 - ns0));
    if (ret < 0)
		return -1;

    return (ns1 - ns0);

}

int	vj_el_init_420_frame(editlist *el, VJFrame *frame)
{
	if(!el) return 0;
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height>>1);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height >> 1;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 1;
	frame->shift_h = 1;
	return 1;
}


int	vj_el_init_422_frame(editlist *el, VJFrame *frame)
{
	if(!el) return 0;
	frame->data[0] = NULL;
	frame->data[1] = NULL;
	frame->data[2] = NULL;
	frame->uv_len = (el->video_width>>1) * (el->video_height);
	frame->uv_width = el->video_width >> 1;
	frame->uv_height = el->video_height;
	frame->len = el->video_width * el->video_height;
	frame->shift_v = 0;
	frame->shift_h = 1;
	return 1;
}

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int num )
{
	// get audio from current frame + n frames
    long pos, asize;
    int ret = 0;
    uint64_t n;	
    int ns0, ns1;

    if (!el->has_audio)
	return 0;

    if  (!el->has_video)
	{
		int size = el->audio_rate / el->video_fps * el->audio_bps;
		memset(dst,0,size);
		return size;
	}

    if (nframe < 0)
		nframe = 0;

    if (nframe > el->video_frames)
		nframe = el->video_frames - num;

    n = el->frame_list[nframe];

    ns1 = (double) (N_EL_FRAME(n) + num) * el->audio_rate / el->video_fps;
    ns0 = (double) N_EL_FRAME(n) * el->audio_rate / el->video_fps;

    //asize = el->audio_rate / el->video_fps;
    pos = nframe * asize;
    ret = lav_set_audio_position(el->lav_fd[N_EL_FILE(n)], ns0);

    if (ret < 0)
		return -1;

    //mlt need int16_t
    ret = lav_read_audio(el->lav_fd[N_EL_FILE(n)], dst, (ns1 - ns0));
    if (ret < 0)
		return -1;

    return (ns1 - ns0);

}


editlist *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt)
{
	editlist *el = vj_malloc(sizeof(editlist));
	if(!el) return NULL;
	memset( el, 0, sizeof(editlist));
	el->MJPG_chroma = chroma;
	el->video_norm = norm;
	el->is_empty = 1;
	el->has_audio = 0;
	el->audio_rate = 0;
	el->audio_bits = 0;
	el->audio_bps = 0;
	el->audio_chans = 0;
	el->play_rate = 0;
	el->num_video_files = 0;
	el->video_width = width;
	el->video_height = height;
	el->video_frames = DUMMY_FRAMES; /* veejay needs at least 2 frames (play and queue next)*/
	el->video_fps = fps;
	el->video_inter = LAV_NOT_INTERLACED;

	/* output pixel format */
	if( fmt == -1 )
		el->pixel_format = FMT_422;
	else
		el->pixel_format = (fmt == 0 ? FMT_420: FMT_422 );
	el->auto_deinter = deinterlace;
	el->max_frame_size = width*height*3;
	el->last_afile = -1;
	el->last_apos = 0;
	el->frame_list = NULL;
	el->has_video = 0;
	return el;
}

void	vj_el_close( editlist *el )
{
	int i;
	for ( i = 0; i < el->num_video_files; i ++ )
	{
		if(!el->ref[i])
		{
			if( el->lav_fd[i] ) lav_close( el->lav_fd[i] );
		}
		if( el->video_file_list[i]) free(el->video_file_list[i]);
	}
	if( el->frame_list )
		free(el->frame_list );
	free(el);
}

editlist *vj_el_init_with_args(char **filename, int num_files, int flags, int deinterlace, int force
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
    /* Check if a norm parameter is present */
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
		/* Check if file really exists, is mounted etc... */
		struct stat fileinfo;
		if(stat( filename[nf], &fileinfo)!= -1) 
		{	/* Check if filename[nf] is a edit list */
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
				else
				{
					veejay_msg(VEEJAY_MSG_WARNING,
						"Cant put %s in EditList", filename[nf] );
				}
			}
    		}
		else
		{
			/* file is not there anymore ? */
			veejay_msg(VEEJAY_MSG_WARNING,
				"File %s is not acceessible (anymore?)", filename[nf]);
			vj_el_free(el);
			return NULL;
		}
	}

	/* Calculate maximum frame size */

	for (i = 0; i < el->video_frames; i++)
	{
		n = el->frame_list[i];
		if(!el->lav_fd[N_EL_FILE(n)] )
			{ vj_el_free(el); return NULL; }
		if (lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n)) >
		    el->max_frame_size)
		    el->max_frame_size =
			lav_frame_size(el->lav_fd[N_EL_FILE(n)], N_EL_FRAME(n));


   	}

	/* Pick a pixel format */
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

	/* Help for audio positioning */

	el->last_afile = -1;


    //el->auto_deinter = auto_deinter;
    //if(el->video_inter != 0 ) el->auto_deinter = 0;
	el->auto_deinter = 0;

	// FIXME
	veejay_msg(VEEJAY_MSG_WARNING, "Editlist is using %s", (el->pixel_format == FMT_420 ? "yuv420p" : "yuv422p"));


	/* Now, check if we have loaded any file 
           (because we skip the files that fail) */


	if( el->num_video_files == 0 || 
		el->video_width == 0 || el->video_height == 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Failed");
		vj_el_free(el);
		return NULL;
	}

	return el;
}


void	vj_el_free(editlist *el)
{
	if(el)
	{
		int n = el->num_video_files;
		int i;
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
		free(el);   
		el = NULL;
	}
}

void	vj_el_ref(editlist *el, int n)
{
	el->ref[n]++;
}
void	vj_el_unref(editlist *el, int n)
{
	if(el->ref[n])
		el->ref[n]--;
}

void	vj_el_print(editlist *el)
{
	int i;
	char timecode[64];
	char interlacing[64];
	MPEG_timecode_t ttc;
	veejay_msg(VEEJAY_MSG_INFO,"EditList settings: Video:%s %dx%d@%2.2f %s\tAudio:%d Hz/%d channels/%d bits",
		(el->pixel_format == FMT_420 ? "420" : "422"),el->video_width,el->video_height,el->video_fps,(el->video_norm=='p' ? "PAL" :"NTSC"),
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
	memset(&tc,0,sizeof(tc));
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
				(unsigned long) j,
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

editlist	*vj_el_soft_clone(editlist *el)
{
	editlist *clone = (editlist*) vj_malloc(sizeof(editlist));
	memset( clone, 0, sizeof(editlist));
	if(!clone)
		return 0;
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
	clone->play_rate = el->play_rate;
	clone->video_frames = el->video_frames;
	clone->num_video_files = el->num_video_files;
	clone->max_frame_size = el->max_frame_size;
	clone->MJPG_chroma = el->MJPG_chroma;

	clone->frame_list = NULL;
	clone->last_afile = el->last_afile;
	clone->last_apos  = el->last_apos;
	clone->auto_deinter = el->auto_deinter;
	clone->pixel_format = el->pixel_format;

//	memset( clone->video_file_list, 0 , sizeof(char*) * MAX_EDIT_LIST_FILES );
//	memset( clone->lav_fd , 0, sizeof(lav_file_t*) * MAX_EDIT_LIST_FILES );
//	memset( clone->num_frames, 0, sizeof(long) * MAX_EDIT_LIST_FILES);
//	memset( clone->yuv_taste, 0, sizeof(int) * MAX_EDIT_LIST_FILES);
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
			clone->ref[i] = 1; // clone starts with ref count of 1
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
