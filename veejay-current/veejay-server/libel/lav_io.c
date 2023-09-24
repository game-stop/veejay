/*
 *  Some routines for handling I/O from/to different video
 *  file formats (currently AVI, Quicktime and movtar).
 *
 *  These routines are isolated here in an extra file
 *  in order to be able to handle more formats in the future.
 *
 *  Copyright (C) 2000 Rainer Johanni <Rainer@Johanni.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#include <libel/lav_io.h>
//#include <veejay/vj-lib.h>
#include <veejaycore/vj-msg.h>
#ifdef USE_GDK_PIXBUF
#include <gmodule.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libel/pixbuf.h>
#endif
#include <libavcodec/avcodec.h>
#ifdef HAVE_LIBQUICKTIME
#include <quicktime.h>
#include <lqt.h>
#include <lqt_version.h>
#endif
#include <veejaycore/vims.h>
#include <veejaycore/lzo.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/avcommon.h>
#include <veejaycore/avhelper.h>

#define QUICKTIME_MJPG_TAG 0x6d6a7067
extern int get_ffmpeg_pixfmt(int p);

extern int AVI_errno;
static int _lav_io_default_chroma = CHROMAUNKNOWN;
static char video_format=' ';
static int  internal_error=0;

#define ERROR_JPEG      1
#define ERROR_MALLOC    2
#define ERROR_FORMAT    3
#define ERROR_NOAUDIO   4

#ifdef USE_GDK_PIXBUF
static int      output_scale_width = 0;
static int      output_scale_height = 0;
static float        output_fps = 25.0;
static int      output_yuv = 1; // 422


char *get_filename_ext(char *filename) {
	char *dot = strrchr(filename, '.');
	if(!dot || dot == filename) 
		return NULL;
	return dot + 1;
}

int	lav_is_supported_image_file(char *filename)
{
	GSList *list = gdk_pixbuf_get_formats();
	GSList *iter;
	int i;

	char *ext = get_filename_ext(filename);

	for( iter = list; iter->next != NULL; iter = iter->next ) {
		gchar **extensions = gdk_pixbuf_format_get_extensions (iter->data);
		for( i = 0; extensions[i] != NULL; i ++ ) {
			if( strncasecmp(ext, extensions[i], strlen(ext)) == 0 ) {
				g_strfreev(extensions);
				g_slist_free(list);
				return 1;	
			}
		}	
		g_strfreev (extensions);
	}
	g_slist_free(list);
	return 0;
}

void    lav_set_project(int w, int h, float f, int fmt)
{
    output_scale_width = w;
    output_scale_height = h;
    output_fps = f;
    output_yuv = fmt;
}
#else
void    lav_set_project(int w, int h, float f, int fmt)
{
}
#endif

#define M_SOF0  0xC0
#define M_SOF1  0xC1
#define M_DHT   0xC4
#define M_SOI   0xD8        /* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9        /* End Of Image (end of datastream) */
#define M_SOS   0xDA        /* Start Of Scan (begins compressed data) */
#define M_DQT   0xDB
#define M_APP0  0xE0
#define M_APP1  0xE1
#define TMP_EXTENSION ".tmp"

void set_fourcc(lav_file_t *lav_file, char *fourcc)
{
	/* ensure fourcc is in lowercase */
	char fourcc_lc[5];
	int i;
	for( i = 0; i < 4; i ++ ) {
		fourcc_lc[i] = tolower(fourcc[i]);
	}
	fourcc_lc[4] = 0;
	char *ptr = fourcc_lc;

	/* hash the string */
	int hash = 5381;
    	int c;
    	while( (c = (int) *ptr++) != 0)
    		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */\

	/* look up codec_id in hashtable by key (hash) */
	lav_file->codec_id = avhelper_get_codec_by_key( hash );
}

/* The query routines about the format */
int lav_query_APP_marker(char format)
{
   /* AVI needs the APP0 marker, Quicktime APP1 */

   switch(format)
   {
      case 'Q':
      case 'q': return 1;
      default:  return 0;
   }
}

int lav_query_APP_length(char format)
{
   /* AVI: APP0 14 bytes, Quicktime APP1: 40 */

   switch(format)
   {
      case 'Q':
      case 'q': return 40;
      case 'm': return 0;
      default:  return 14;
   }
}

int lav_query_polarity(char format)
{
   switch(format)
   {
      default:  return LAV_NOT_INTERLACED;
   }
}

void lav_set_default_chroma(int _chroma)
{
    if(_chroma == CHROMAUNKNOWN || _chroma == CHROMA420 ||
        _chroma == CHROMA422 || _chroma == CHROMA444 ||
        _chroma == CHROMA422F || _chroma == CHROMA420F)
        _lav_io_default_chroma = _chroma;
}


lav_file_t *lav_open_output_file(char *filename, char format,
                    int width, int height, int interlaced, double fps,
                    int asize, int achans, long arate)
{
   lav_file_t *lav_fd = (lav_file_t*) vj_malloc(sizeof(lav_file_t));

   if(lav_fd==0) { internal_error=ERROR_MALLOC; return NULL; }

   /* Set lav_fd */

   lav_fd->avi_fd      = 0;
   lav_fd->format      = format;
   lav_fd->interlacing = interlaced ? lav_query_polarity(format):LAV_NOT_INTERLACED;
   lav_fd->has_audio   = (asize>0 && achans>0);
   lav_fd->bps         = (asize*achans+7)/8;
   lav_fd->MJPG_chroma = _lav_io_default_chroma;
  
   char fourcc[16];

   int is_avi = 1;

   switch(format)
   {
    case 'a':
    case 'A':
             /* Open AVI output file */
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI MJPEG");
        sprintf(fourcc, "MJPG" );
        break;
    case 'H':
        veejay_msg(VEEJAY_MSG_DEBUG,"\tWriting output file in AVI HFYU");
        sprintf(fourcc, "HFYU");
        break;
    case 'c':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI MJPEG-b");
        sprintf(fourcc, "MJPB" );
        break;
    case 'l':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI LJPEG");
        sprintf(fourcc, "JPGL");
        break;
    case 'L':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI LZO (veejay's fourcc)");
        sprintf(fourcc, "MLZO" );
        break;
    case 'o':
    case 'O':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI QOI");
        sprintf(fourcc, "QOIY");
        break;
    case 'v':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI VJ20 (veejay's fourcc)");
        sprintf(fourcc,"VJ20");
        break;  
    case 'V':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI VJ22 (veejay's fourcc)");
        sprintf(fourcc,"VJ22");
        break;
    case 'Y':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI IYUV");
        sprintf(fourcc, "IYUV" );
        break;
    case 'P':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI YV16");  
        sprintf(fourcc, "YV16");
        break;
    case 'D':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI DIV3");
        sprintf(fourcc, "DIV3");
        break;
    case 'M':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI MP4V");
        sprintf(fourcc,"MP4V");
        break;
    case 'b':
    case 'd':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in AVI DVSD");
        sprintf(fourcc, "DVSD");
        break;

    case 'q':
    case 'Q':
        veejay_msg(VEEJAY_MSG_DEBUG, "\tWriting output file in Quicktime MJPA/JPEG");
        is_avi = 0;
        break;
    case 'x':
        is_avi = 0;
        break;
    }

    if( is_avi )    
    {       
        lav_fd->avi_fd = AVI_open_output_file(filename);
            if(!lav_fd->avi_fd)
        {
            free(lav_fd);
            return NULL;
        }
            AVI_set_video(lav_fd->avi_fd, width, height, fps, fourcc );
            if (asize)
        {
            if(AVI_set_audio(lav_fd->avi_fd, achans, arate, asize, WAVE_FORMAT_PCM)==-1)
            {
                veejay_msg(0, "Too many channels or invalid AVI file");
                lav_close( lav_fd );
                return NULL;
            }
        }
            return lav_fd;
    } else {
#ifdef HAVE_LIBQUICKTIME
          /* open quicktime output file */

             /* since the documentation says that the file should be empty,
                    we try to remove it first */
            remove(filename);

            lav_fd->qt_fd = quicktime_open(filename, 0, 1);
            if(!lav_fd->qt_fd)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "\tCannot open '%s' for writing", filename);
            free(lav_fd);
            return NULL;
        }
        if(format=='q')
                quicktime_set_video(lav_fd->qt_fd, 1, width, height, fps,
                             (interlaced ? QUICKTIME_MJPA : QUICKTIME_JPEG));
        else
            quicktime_set_video(lav_fd->qt_fd,1, width,height,fps,
                QUICKTIME_DV );
        
     
	if (asize)
            quicktime_set_audio(lav_fd->qt_fd, achans, arate, asize, QUICKTIME_TWOS);

        int has_kf = quicktime_has_keyframes( lav_fd->qt_fd, 0 );
        char *copyright = quicktime_get_copyright( lav_fd->qt_fd );
        char *name      = quicktime_get_name( lav_fd->qt_fd );
        char *info      = quicktime_get_info( lav_fd->qt_fd );
        
        veejay_msg(VEEJAY_MSG_DEBUG,
                "(C) %s by %s, %s, has keyframes = %d", copyright,name,info,has_kf );
                
        return lav_fd;
#else
        veejay_msg(0,"Quicktime not compiled in, cannot use Quicktime");
        internal_error = ERROR_FORMAT;
        return NULL;
#endif
          
    }
    if(lav_fd) free(lav_fd);
    return NULL;
}

int lav_close(lav_file_t *lav_file)
{
    int ret = 0;
        video_format = lav_file->format; internal_error = 0; /* for error messages */
    switch(video_format)
    {
#ifdef SUPPORT_READ_DV2
        case 'b':
            if( lav_file->dv_fd )
            {
                ret = rawdv_close(lav_file->dv_fd); 
            }
            break;
#endif
        case 'x':
#ifdef USE_GDK_PIXBUF
            vj_picture_cleanup( lav_file->picture );
#endif
            ret = 1;
            break;
#ifdef HAVE_LIBQUICKTIME
        case 'q':
            if( lav_file->qt_fd )
            {
                    ret = quicktime_close( lav_file->qt_fd );
            }
            break;
#endif          
        default:
            if( lav_file->avi_fd )
            {
                ret = AVI_close(lav_file->avi_fd);
            }
            break;
    }

    if(lav_file) free(lav_file);
    
    lav_file = NULL;

    return ret;
}

long    lav_bytes_remain( lav_file_t *lav_file )
{
    switch( lav_file->format )
    {
        case 'a':
        case 'A':
        case 'M':
        case 'P':
        case 'D':
        case 'v':
        case 'V':
        case 'Y':
        case 'L':   
        case 'l':
        case 'd':
        case 'H':
        case 'o':
        case 'O':
          return AVI_bytes_remain( lav_file->avi_fd );
        default:
         return -1;
    } 
    return -1;
}


int lav_write_frame(lav_file_t *lav_file, uint8_t *buff, long size, long count)
{
   int res=0, n;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return -1; //rawdv, no stream writing support yet
#endif
   /* For interlaced video insert the apropriate APPn markers */
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return -1;//picture
#endif
   
    for(n=0;n<count;n++)
   {
      switch(lav_file->format)
      {
        case 'a':
        case 'A':
        case 'M':
        case 'P':
        case 'D':
        case 'v':
        case 'V':
        case 'Y':
        case 'L':   
        case 'l':
        case 'o':
        case 'O':
        case 'd':
        case 'H':
            if(n==0) {
                res = AVI_write_frame( lav_file->avi_fd, buff, size );
            }   
            else
            {     
                res = AVI_dup_frame( lav_file->avi_fd );
            }
        break;
        
#ifdef HAVE_LIBQUICKTIME
      case 'q':
      case 'Q':
            res = quicktime_write_frame( lav_file->qt_fd, buff, size, 0 );
            break;
#endif
     default:
        res = -1;
        break;

      }
   }
   return res;
}

int lav_write_audio(lav_file_t *lav_file, uint8_t *buff, long samps)
{
#ifdef HAVE_LIBQUICKTIME
   int i, j;
   int16_t *qt_audio = (int16_t *)buff, **qt_audion;
   int channels = lav_audio_channels(lav_file);
   int bits = lav_audio_bits(lav_file);
   int res=0;
#endif

   switch(lav_file->format )
    {
#ifdef HAVE_LIBQUICKTIME
      case 'q':
      case 'Q':
    if (bits != 16 || channels > 1)
     {
	qt_audion = (int16_t**) malloc(channels * sizeof (int16_t*));
    /* Deinterleave the audio into the two channels and/or convert
     * bits per sample to the required format.
     */
    for (i = 0; i < channels; i++)
      qt_audion[i] = malloc(samps * lav_file->bps * sizeof(int16_t));

    if (bits == 16)
      for (i = 0; i < samps; i++)
            for (j = 0; j < channels; j++)
              qt_audion[j][i] = qt_audio[channels * i + j];
   else if (bits == 8)
        for (i = 0; i < samps; i++)
          for (j = 0; j < channels; j++)
            qt_audion[j][i] = ((int16_t)(buff[channels * i + j]) << 8) ^ 0x8000;

   if (bits == 8 || bits == 16)
      res = lqt_encode_audio_track(lav_file->qt_fd, qt_audion, NULL, samps, 0);

   for (i = 0; i < channels; i++)
       free(qt_audion[i]);

   free(qt_audion);
   } 
   else 
   {
       qt_audion = &qt_audio;
       res = lqt_encode_audio_track(lav_file->qt_fd, qt_audion, NULL, samps, 0);
   }

   return res;
        break;
#endif
#ifdef SUPPORT_READ_DV2
    case 'b':
        return 0;
#endif
#ifdef USE_GDK_PIXBUF
    case 'x':
        return 0;
#endif
    default:
         return AVI_write_audio( lav_file->avi_fd, buff, samps*lav_file->bps);
    }
    return 0;
}

void    lav_bogus_set_length( lav_file_t *lav_file , int len )
{
    lav_file->bogus_len = len;
}

int lav_bogus_video_length( lav_file_t *lav_file )
{
    video_format = lav_file->format;
    if( lav_file->format == 'x' )
        return lav_file->bogus_len;
    return 0;
}

long lav_video_frames(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
#ifdef SUPPORT_READ_DV2
    case 'b':
        return rawdv_video_frames(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
    case 'x':
        return lav_file->bogus_len;
#endif
#ifdef HAVE_LIBQUICKTIME
      case 'q':
      case 'Q':
         return quicktime_video_length(lav_file->qt_fd,0);
#endif

      default:
        return AVI_video_frames( lav_file->avi_fd );
   }
   return -1;
}

int lav_video_width(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
    switch(lav_file->format)
    {
#ifdef SUPPORT_READ_DV2
        case 'b': return rawdv_width(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
        case 'x': return output_scale_width;
#endif
#ifdef HAVE_LIBQUICKTIME
        case 'q': case 'Q': return quicktime_video_width(lav_file->qt_fd,0);
#endif          
        default:
             return AVI_video_width( lav_file->avi_fd);
    }
    return -1;
}

int lav_video_height(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch( lav_file->format )
   {
#ifdef SUPPORT_READ_DV2
    case 'b': return rawdv_height( lav_file->dv_fd );
#endif
#ifdef USE_GDK_PIXBUF   
    case 'x': return output_scale_height;
#endif
#ifdef HAVE_LIBQUICKTIME
    case 'q': case 'Q': return quicktime_video_height(lav_file->qt_fd,0);
#endif
    default:
          return AVI_video_height(lav_file->avi_fd);
   }
    return -1;
}

double lav_frame_rate(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
#ifdef SUPPORT_READ_DV2
        case 'b':
           return rawdv_fps(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
        case 'x':
           return output_fps;
#endif
#ifdef HAVE_LIBQUICKTIME
        case 'q': case 'Q':
              return quicktime_frame_rate(lav_file->qt_fd,0);
#endif     
       default:
        return AVI_frame_rate( lav_file->avi_fd ); 
  }
   return -1;
}

int lav_video_interlacing(lav_file_t *lav_file)
{
#ifdef SUPPORT_READ_DV2
    if(video_format == 'b')
        return rawdv_interlacing(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return LAV_NOT_INTERLACED;
#endif
   return lav_file->interlacing;
}

void lav_video_clipaspect(lav_file_t *lav_file, int *sar_w, int *sar_h)
{
  *sar_w = lav_file->sar_w;
  *sar_h = lav_file->sar_h;
  return;
}

int lav_video_MJPG_chroma(lav_file_t *lav_file)
{
    return lav_file->MJPG_chroma;
}

int lav_video_compressor_type(lav_file_t *lav_file)
{
    return lav_file->codec_id;
}

#define FOURCC_DV "dvsd"
#define FOURCC_PIC "pict"
#define FOURCC_LZO "mlzo" 
#define FOURCC_HUFFYUV "hfyu"

const char *lav_video_compressor(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if( video_format == 'b' )
   {
    return FOURCC_DV;
   }
#endif
#ifdef USE_GDK_PIXBUF
   if( video_format == 'x')
   {
    return FOURCC_PIC;
   }
#endif
   if( video_format == 'L' )
   {
    return FOURCC_LZO;
   }
#ifdef HAVE_LIBQUICKTIME
   if(lav_file->format == 'q' || lav_file->format == 'Q')
    return quicktime_video_compressor(lav_file->qt_fd,0);
#endif

   return AVI_video_compressor(lav_file->avi_fd);
}

int lav_audio_channels(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return rawdv_audio_channels(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
   if(video_format == 'x')
    return 0;
#endif
#ifdef HAVE_LIBQUICKTIME
    if(video_format == 'q' || video_format =='Q')
         return quicktime_track_channels(lav_file->qt_fd,0);
#endif
   return AVI_audio_channels(lav_file->avi_fd);
}

int lav_audio_bits(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return rawdv_audio_bits(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x' )
        return 0;
#endif
#ifdef HAVE_LIBQUICKTIME
      if(video_format == 'q'|| video_format =='Q')
         return quicktime_audio_bits(lav_file->qt_fd,0);
#endif  
   return (AVI_audio_bits(lav_file->avi_fd));
}

long lav_audio_rate(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
    if(video_format=='b')
     return rawdv_audio_rate(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return 0;
#endif
#ifdef HAVE_LIBQUICKTIME
    if( video_format == 'q'|| video_format =='Q')
        return quicktime_sample_rate(lav_file->qt_fd,0);
#endif  
   return (AVI_audio_rate(lav_file->avi_fd));
}

long lav_audio_clips(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format=='b')
    return rawdv_audio_bps(lav_file->dv_fd);
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return 0;
#endif
#ifdef HAVE_LIBQUICKTIME
    if(video_format == 'q'|| video_format == 'Q')
        return quicktime_audio_length(lav_file->qt_fd,0);
#endif  
   return (AVI_audio_bytes(lav_file->avi_fd)/lav_file->bps);
}

long lav_frame_size(lav_file_t *lav_file, long frame)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return rawdv_frame_size( lav_file->dv_fd );
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return output_scale_width * output_scale_height * 3;
#endif
#ifdef HAVE_LIBQUICKTIME
    if( video_format == 'q' || video_format == 'Q')
        return quicktime_frame_size(lav_file->qt_fd,frame,0);
#endif  
   return (AVI_frame_size(lav_file->avi_fd,frame));
}

int lav_seek_start(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return rawdv_set_position( lav_file->dv_fd, 0 );
#endif
#ifdef USE_GDK_PIXBUF
   if(video_format == 'x')
    return 1;
#endif
#ifdef HAVE_LIBQUICKTIME
  return quicktime_seek_start(lav_file->qt_fd);
#endif
   return (AVI_seek_start(lav_file->avi_fd));
}

int lav_set_video_position(lav_file_t *lav_file, long frame)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return rawdv_set_position( lav_file->dv_fd, frame );
#endif
#ifdef USE_GDK_PIXBUF
   if(video_format == 'x')
    return 1;
#endif
#ifdef HAVE_LIBQUICKTIME
    if(video_format == 'q' || video_format == 'Q')
        return quicktime_set_video_position(lav_file->qt_fd,(int64_t)frame,0);
#endif   
   return (AVI_set_video_position(lav_file->avi_fd,frame));
}

int lav_read_frame(lav_file_t *lav_file, uint8_t *vidbuf)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
    if(lav_file->format == 'b')
    {
        return rawdv_read_frame( lav_file->dv_fd, vidbuf );
    }
#endif
#ifdef USE_GDK_PIXBUF
    if(lav_file->format == 'x')
    return -1;
#endif
#ifdef HAVE_LIBQUICKTIME
    if(lav_file->format == 'q'|| lav_file->format == 'Q')
        return quicktime_read_frame(lav_file->qt_fd,vidbuf,0);
#endif
   int kf = 1;  
   int ret = (AVI_read_frame(lav_file->avi_fd,vidbuf,&kf));

#ifdef STRICT_CHECKING
   if(!kf)
   {
    veejay_msg(VEEJAY_MSG_DEBUG, "Requested frame is not a keyframe");
    return ret;
   }
#endif
   return ret;

}

#ifdef USE_GDK_PIXBUF
VJFrame *lav_get_frame_ptr( lav_file_t *lav_file )
{
    if(lav_file->format == 'x')
        return vj_picture_get( lav_file->picture );
    return NULL;
}
#else
uint8_t *lav_get_frame_ptr( lav_file_t *lav_file)
{
    return NULL;
}
#endif

int lav_is_DV(lav_file_t *lav_file)
{
#ifdef SUPPORT_READ_DV2
    if(lav_file->format == 'b')
        return 1;
#endif
    return 0;
}

int lav_set_audio_position(lav_file_t *lav_file, long clip)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef SUPPORT_READ_DV2
   if(video_format == 'b')
    return 0;
#endif
#ifdef USE_GDK_PIXBUF
   if(video_format == 'x')
    return 0;
#endif
#ifdef HAVE_LIBQUICKTIME
    if(video_format =='q'|| video_format == 'Q' ) {
        quicktime_set_audio_position(lav_file->qt_fd,clip,0);
        return 1;
    }
#endif
   return (AVI_set_audio_position(lav_file->avi_fd,clip*lav_file->bps));
}

int lav_read_audio(lav_file_t *lav_file, uint8_t *audbuf, long samps)
{
    if(!lav_file->has_audio)
    {
            internal_error = ERROR_NOAUDIO;
        return -1;
    }
#ifdef SUPPORT_READ_DV2
    if(video_format == 'b')
        return rawdv_read_audio_frame( lav_file->dv_fd, audbuf );
#endif
#ifdef USE_GDK_PIXBUF
    if(video_format == 'x')
        return 0;
#endif
    video_format = lav_file->format; internal_error = 0; /* for error messages */
#ifdef HAVE_LIBQUICKTIME
    if( video_format == 'q' || video_format == 'Q')
    {
        int64_t last_pos, start_pos;
        int res, i, j;
        int16_t *qt_audio = (int16_t *)audbuf, **qt_audion;
        int channels = lav_audio_channels(lav_file);
        uint8_t b0, b1;
        qt_audion = (int16_t**) malloc(channels * sizeof (int16_t*));
        for (i = 0; i < channels; i++)
            qt_audion[i] = (int16_t *)malloc(samps * lav_file->bps * sizeof(int16_t));

        start_pos = quicktime_audio_position(lav_file->qt_fd, 0);
        lqt_decode_audio_track(lav_file->qt_fd, qt_audion, NULL, samps, 0);
        last_pos = lqt_last_audio_position(lav_file->qt_fd, 0);
        res = last_pos - start_pos;
        if (res <= 0)
           goto out;
        /* Interleave the channels of audio into the one buffer provided */
        for (i =0; i < res; i++)
            {
                for (j = 0; j < channels; j++)
                qt_audio[(channels*i) + j] = qt_audion[j][i];
            }

            if (lav_detect_endian())
            {
                i= 0;
                while (i < (2*res) )
                {
                    b0 = 0;
                        b1 = 0; 
                        b0 = (qt_audio[i] & 0x00FF);
                        b1 =  (qt_audio[i] & 0xFF00) >> 8;
                        qt_audio[i] = (b0 <<8) + b1;
                        i = i +1;
                    } 
                }
out:
        for (j = 0; j < channels; j++)
                    free(qt_audion[j]);
        free(qt_audion);
            return(res);
    }
#endif

    int res = AVI_read_audio( lav_file->avi_fd, audbuf, 
            (samps * lav_file->bps) );
    return res;
}

int lav_filetype(lav_file_t *lav_file)
{
   return lav_file->format;
}

lav_file_t *lav_open_input_file(char *filename, long mmap_size)
{
   static char pict[5] = "PICT\0";
   char *video_comp = NULL;
   unsigned char *frame = NULL; 
   int ierr;

   lav_file_t *lav_fd = (lav_file_t*) vj_calloc(sizeof(lav_file_t));

   if(lav_fd==0) { internal_error=ERROR_MALLOC; return 0; }

   /* Set lav_fd */

#ifdef  HAVE_LIBQUICKTIME
   char *audio_comp;
#endif
   lav_fd->avi_fd      = 0;
#ifdef SUPPORT_READ_DV2
   lav_fd->dv_fd    = 0;
#endif
#ifdef USE_GDK_PIXBUF
   lav_fd->picture  = NULL;
#endif
   lav_fd->format      = 0;
   lav_fd->interlacing = LAV_INTER_UNKNOWN;
   lav_fd->sar_w       = 0; /* (0,0) == unknown */
   lav_fd->sar_h       = 0; 
   lav_fd->has_audio   = 0;
   lav_fd->bps         = 0;
   lav_fd->MJPG_chroma = CHROMAUNKNOWN;
   lav_fd->mmap_size   = mmap_size;

    int ret = 0;

    /* open file, check if file is a file */
    struct stat s;
    if( stat(filename, &s ) != 0 )
    {
        if(lav_fd) free(lav_fd);
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid file '%s'. %s",filename, strerror(errno));
        return NULL;
    }

    if(!S_ISREG( s.st_mode) )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "'%s' is not a regular file",filename);
        if(lav_fd) free(lav_fd);
        return NULL;
    }

#ifdef USE_GDK_PIXBUF
    int is_picture = lav_is_supported_image_file( filename );
#else
    int is_picture = 0;
#endif

    if( is_picture ) {
#ifdef USE_GDK_PIXBUF
    	lav_fd->picture = vj_picture_open( (const char*) filename, output_scale_width, output_scale_height, get_ffmpeg_pixfmt(output_yuv) );
   	if(lav_fd->picture)
    	{
        	lav_fd->format = 'x';
        	lav_fd->bogus_len = (int) output_fps;
        	video_comp = pict;
        	veejay_msg(VEEJAY_MSG_DEBUG,"\tLoaded image file");
        	return lav_fd;
    	}
#endif
    }
    else
    {
        lav_fd->avi_fd = AVI_open_input_file(filename,1,mmap_size);
        
        if( lav_fd->avi_fd && AVI_errno == AVI_ERR_EMPTY )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Empty AVI file");
            if(lav_fd) free(lav_fd);
            return NULL;
        }
        else if ( lav_fd->avi_fd && AVI_errno == 0 )
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"\tFile is AVI" );
            ret =1;
        }
    }
   
    int alt = 0;
    
    if(lav_fd->avi_fd)
    {
        ret = 1;
        alt = 1;
        lav_fd->format = 'a';
        lav_fd->has_audio = (AVI_audio_bits(lav_fd->avi_fd)>0 &&
                           AVI_audio_format(lav_fd->avi_fd)==WAVE_FORMAT_PCM);
        video_comp = AVI_video_compressor(lav_fd->avi_fd);
        if(video_comp == NULL || strlen(video_comp) <= 0)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to read FOURCC from AVI");
                if(lav_fd) free(lav_fd);
                return NULL;
        }
    }
    else if( AVI_errno==AVI_ERR_NO_AVI || (!lav_fd->avi_fd && !ret) )
    {
#ifdef HAVE_LIBQUICKTIME
        if(quicktime_check_sig(filename))
        {
            veejay_msg(VEEJAY_MSG_DEBUG, "Opening quicktime file ...");
            quicktime_pasp_t pasp;
            int nfields, detail;
            lav_fd->qt_fd = quicktime_open(filename,1,0);
            video_format = 'q'; /* for error messages */
            if (!lav_fd->qt_fd)
                {
                veejay_msg(VEEJAY_MSG_ERROR, "Unable to open quicktime file");
                free(lav_fd);
                    return NULL;
                }
            else
                veejay_msg(VEEJAY_MSG_DEBUG, "\tOpening Quicktime file");
            lav_fd->avi_fd = NULL;
            lav_fd->format = 'q';
            video_comp = quicktime_video_compressor(lav_fd->qt_fd,0);
            /* We want at least one video track */
            if (quicktime_video_tracks(lav_fd->qt_fd) < 1)
                {
                veejay_msg(VEEJAY_MSG_ERROR, "At least one video track required");
                    lav_close(lav_fd);
                    internal_error = ERROR_FORMAT;
                    return NULL;
                }
            /*
            * If the quicktime file has the sample aspect atom then use it to set
            * the sar values in the lav_fd structure.  Hardwired (like everywhere else)
            * to only look at track 0.
            */
     
            if (lqt_get_pasp(lav_fd->qt_fd, 0, &pasp) != 0)
                {
                lav_fd->sar_w = pasp.hSpacing;
                lav_fd->sar_h = pasp.vSpacing;
                }
            /*
             * If a 'fiel' atom is present (not guaranteed) then use it to set the
             * interlacing type.
             */
        
            if (lqt_get_fiel(lav_fd->qt_fd, 0, &nfields, &detail) != 0)
                {
                    if (nfields == 2)
                    {
                    if (detail == 14 || detail == 6)
                        lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST;
                    else if (detail == 9 || detail == 1)
                        lav_fd->interlacing = LAV_INTER_TOP_FIRST;
                    else
                        veejay_msg(VEEJAY_MSG_DEBUG, "Unknown 'detail' in 'fiel' atom: %d", detail);
                    }
                    else
                        lav_fd->interlacing = LAV_NOT_INTERLACED;
                 }
            /* Check for audio tracks */
            lav_fd->has_audio = 0;
            if (quicktime_audio_tracks(lav_fd->qt_fd))
                {
                audio_comp = quicktime_audio_compressor(lav_fd->qt_fd,0);
                if (strncasecmp(audio_comp, QUICKTIME_TWOS,4)==0)
                    lav_fd->has_audio = 1;
                else
                    veejay_msg(VEEJAY_MSG_WARNING, "Audio compressor '%s' not supported",
                            audio_comp );
                }

            alt = 1;
            ret = 1;
        }
        else
            veejay_msg(VEEJAY_MSG_DEBUG, "\tNot a Quicktime file");
#endif


#ifdef SUPPORT_READ_DV2
        if(!alt)
        {
            ret = 0;
            lav_fd->dv_fd = rawdv_open_input_file(filename,mmap_size);
            if(lav_fd->dv_fd > 0)
            {
                lav_fd->MJPG_chroma = rawdv_sampling( lav_fd->dv_fd );
                video_comp = rawdv_video_compressor( lav_fd->dv_fd );
                lav_fd->format = 'b'; 
                lav_fd->has_audio = 0;
                ret = 1;
                alt = 1;
                veejay_msg(VEEJAY_MSG_DEBUG,
                        "RAW DV file '%s'",
                        video_comp );
            }
            else
                veejay_msg(VEEJAY_MSG_DEBUG, "\tNot a raw dv file");
        }
#endif
    }

    if(ret == 0 || video_comp == NULL || alt == 0)
    {
        free(lav_fd);
        internal_error = ERROR_FORMAT;
        return NULL;
    }

	set_fourcc(lav_fd, video_comp);
    
   lav_fd->bps = (lav_audio_channels(lav_fd)*lav_audio_bits(lav_fd)+7)/8;

   if(lav_fd->bps==0) lav_fd->bps=1; /* make it save since we will divide by that value */
 /*     if(strlen(video_comp) == 1 ) {
       lav_fd->MJPG_chroma = CHROMA422;
        lav_fd->format = 'V';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd;
    }
*/

#ifdef USE_GDK_PIXBUF
    if(strncasecmp(video_comp, "PICT",4) == 0 )
    {
        switch(output_yuv)
        {
            case FMT_420:
            case FMT_420F:
                lav_fd->MJPG_chroma = CHROMA420;
                break;
            case FMT_422:
            case FMT_422F:
                lav_fd->MJPG_chroma = CHROMA422;
                break;
            default:
                lav_fd->MJPG_chroma = CHROMAUNKNOWN;
                break;
        }
        lav_fd->format = 'x';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd;
    }
#endif
   
    if ( strncasecmp(video_comp,"iyuv",4)==0 ||
        strncasecmp(video_comp,"yv12",4)==0 ||
        strncasecmp(video_comp,"i420",4)==0)
    {
        lav_fd->MJPG_chroma = CHROMA420;
        lav_fd->format = 'Y';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd;
    }

    if(strncasecmp(video_comp,"vj22",4)==0 || strncasecmp(video_comp, "y42b",4) == 0)
    {
        lav_fd->MJPG_chroma = CHROMA422F;
        lav_fd->format = 'V';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd; 
    }
    if(strncasecmp(video_comp,"vj20",4)==0)
    {
        lav_fd->MJPG_chroma = CHROMA420F;
        lav_fd->format = 'v';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd; 
    }

    if (strncasecmp(video_comp,"yv16",4)==0 ||
        strncasecmp(video_comp,"i422",4)==0)
    {
        lav_fd->MJPG_chroma = CHROMA422;
        lav_fd->format = 'P';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd; 
    }

    if (strncasecmp(video_comp,"hfyu",4)==0)
    {
        lav_fd->MJPG_chroma = CHROMA422;
        lav_fd->format = 'H';
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd; 
    }

    if( strncasecmp( video_comp, "mlzo", 4 ) == 0 )
    {
        lav_fd->MJPG_chroma = CHROMA422;
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd;
    }
    
    if( strncasecmp( video_comp, "qoiy", 4 ) == 0 )
    {
        lav_fd->MJPG_chroma = CHROMA422;
        lav_fd->interlacing = LAV_NOT_INTERLACED;
        return lav_fd;
    }

    if (strncasecmp(video_comp,"dvsd",4)==0 ||
        strncasecmp(video_comp,"dvcp",4) ==0 ||
        strncasecmp(video_comp,"dxsd",4) == 0 ||
        strncasecmp(video_comp,"dvp",3) == 0 ||
        strncasecmp(video_comp,"dvhd",4) == 0 ||
        strncasecmp(video_comp,"dv",2 ) == 0)
    { 
        int gw = lav_video_height( lav_fd );
        if( gw == 480 )
             lav_fd->MJPG_chroma = CHROMA411;
        else
            lav_fd->MJPG_chroma = CHROMA422;

        lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST;
        return lav_fd; 
    }

    if( strncasecmp(video_comp, "png", 3 ) == 0 ||
        strncasecmp(video_comp, "mpng",4) == 0 )
    {
        lav_fd->MJPG_chroma = CHROMA420;
        lav_fd->interlacing = LAV_INTER_UNKNOWN;
        return lav_fd;
    }

    if( strncasecmp(video_comp, "svq1", 4 ) == 0 ||
        strncasecmp(video_comp, "svq3", 4 ) == 0 ||
        strncasecmp(video_comp, "rpza", 4 ) == 0 ||
        strncasecmp(video_comp, "cyuv", 4 ) == 0 )
    {
        lav_fd->MJPG_chroma = CHROMA420;
        lav_fd->interlacing = LAV_INTER_UNKNOWN;
        return lav_fd;
    }
    
    if (strncasecmp(video_comp,"mjpg", 4) == 0 ||
        strncasecmp(video_comp,"mjpa", 4) == 0 ||
        strncasecmp(video_comp,"jpeg", 4) == 0 ||
        strncasecmp(video_comp,"mjpb" ,4) == 0 ||
        strncasecmp(video_comp,"ljpg", 4) == 0 ||
        strncasecmp(video_comp,"sp5x", 4) == 0 ||
        strncasecmp(video_comp,"jpgl", 4) == 0 ||
        strncasecmp(video_comp,"jfif", 4 ) == 0 ||
        strncasecmp(video_comp,"dmb1", 4)==0 )
    {
        lav_fd->MJPG_chroma = CHROMA420;
        lav_fd->interlacing = LAV_INTER_UNKNOWN;
        if ( lav_set_video_position(lav_fd,0) < 0 ) goto ERREXIT;
        lav_fd->MJPG_chroma = CHROMAUNKNOWN;
        lav_fd->interlacing = LAV_INTER_UNKNOWN;
        return lav_fd;
    }
    
    ierr = ERROR_FORMAT;
    veejay_msg(VEEJAY_MSG_ERROR, "Unrecognized format '%s' in %s", video_comp, filename);

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   internal_error = ierr;
    veejay_msg(VEEJAY_MSG_ERROR, "%s", lav_strerror());
   return 0;
}

const char *lav_strerror(void)
{
   static char error_string[1024];

   switch(internal_error)
   {
      case ERROR_JPEG:
         sprintf(error_string,"Internal: broken JPEG format");
         internal_error = 0;
         return error_string;
      case ERROR_MALLOC:
         sprintf(error_string,"Internal: Out of memory");
         internal_error = 0;
         return error_string;
      case ERROR_FORMAT:
         sprintf(error_string,"Input file format not recognized");
         internal_error = 0;
         return error_string;
      case ERROR_NOAUDIO:
         sprintf(error_string,"Trying to read audio from a video only file");
         internal_error = 0;
         return error_string;
   }

   switch(video_format)
   {
      case 'a':
      case 'A':
      case 'Y':
      case 'v':
      case 'V':
      case 'M':
      case 'P':
      case 'L':
      case 'o':
      case 'O':
      case 'D':
      case 'H':
         return AVI_strerror();
      default:
         /* No or unknown video format */
         snprintf(error_string,sizeof(error_string),"No or unknown video format");
         return error_string;
   }
}


/*
static int check_DV2_input(lav_file_t *lav_fd)
{
   int ierr = 0;
   double len = 0;
   unsigned char *frame = NULL;

   lav_fd->is_MJPG = 0;


   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   if ( (len = lav_frame_size(lav_fd,0)) <=0 ) goto ERREXIT;
   if ( (frame = (unsigned char*) malloc(len)) == 0 ) { ierr=ERROR_MALLOC; goto ERREXIT; }

   if ( lav_read_frame(lav_fd,frame) <= 0 ) goto ERREXIT;
   {
     dv_decoder_t *decoder = dv_decoder_new(0,0,0);
     dv_parse_header(decoder, frame);

     switch (decoder->system) {
     case e_dv_system_525_60:
       if (dv_format_wide(decoder)) {
     lav_fd->sar_w = 40;
     lav_fd->sar_h = 33;
       } else {
     lav_fd->sar_w = 10;
     lav_fd->sar_h = 11;
       } 
       break;
     case e_dv_system_625_50:
       if (dv_format_wide(decoder)) {
     lav_fd->sar_w = 118;
     lav_fd->sar_h = 81;
       } else {
     lav_fd->sar_w = 59;
     lav_fd->sar_h = 54;
       } 
       break;
     default:
       lav_fd->sar_w = 0; 
       lav_fd->sar_h = 0;
       break;
     }
     veejay_msg(VEEJAY_MSG_DEBUG, "DV System %s (sar w %d sar h %d)",
    (decoder->system == e_dv_system_525_60 ? "525-60" : ( decoder->system == e_dv_system_625_50 ? "625-50" : "unknown!")),lav_fd->sar_w,lav_fd->sar_h);
     dv_decoder_free(decoder);
   }

   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   return 0;

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   if (ierr) internal_error = ierr;
   return 1;
}

*/

int lav_fileno(lav_file_t *lav_file)
{
   int res;

   video_format = lav_file->format; 

   switch(lav_file->format)
   {
#ifdef HAVE_LIBQUICKTIME
      case 'q':
      case 'Q':
     {
#if ( LQT_CODEC_API_VERSION & 0xffff ) > 6
        res = lqt_fileno( (quicktime_t*) lav_file->qt_fd );
#else
        quicktime_t *q = lav_file->qt_fd;
        res = (int) fileno( (quicktime_t*) q->stream );
#endif
     }
        break;
#endif       
      default:
         res = AVI_fileno(lav_file->avi_fd);
   }

   return res;
}

int lav_detect_endian (void)
{
    unsigned int fred;
    char     *pfred;

  fred = 2 | (1 << (sizeof(int)*8-8));
  pfred = (char *)&fred;

  if  (*pfred == 1)
      return 1;
  else if(*pfred == 2)
      return 0;
  else
      return -1;
}










