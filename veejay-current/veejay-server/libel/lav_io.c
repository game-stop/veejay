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
#include <sys/stat.h>
#include <libel/lav_io.h>
//#include <veejay/vj-lib.h>
#include <libvjmsg/vj-msg.h>
#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#endif
#include <libavcodec/avcodec.h>
#ifdef HAVE_LIBQUICKTIME
#include <quicktime.h>
#include <lqt.h>
#include <lqt_version.h>
#endif
#include <veejay/vims.h>
#include <liblzo/lzo.h>
#include <libvjmem/vjmem.h>
#define QUICKTIME_MJPG_TAG 0x6d6a7067
extern int vj_el_get_decoder_from_fourcc( const char *fourcc );
extern int get_ffmpeg_pixfmt(int p);

extern int AVI_errno;
static int _lav_io_default_chroma = CHROMAUNKNOWN;
static char video_format=' ';
static int  internal_error=0;

#define ERROR_JPEG      1
#define ERROR_MALLOC    2
#define ERROR_FORMAT    3
#define ERROR_NOAUDIO   4

static unsigned long jpeg_field_size     = 0;
static unsigned long jpeg_quant_offset   = 0;
static unsigned long jpeg_huffman_offset = 0;
static unsigned long jpeg_image_offset   = 0;
static unsigned long jpeg_scan_offset    = 0;
static unsigned long jpeg_data_offset    = 0;
static unsigned long jpeg_padded_len     = 0;
static unsigned long jpeg_app0_offset    = 0;
static unsigned long jpeg_app1_offset    = 0;

uint16_t reorder_16(uint16_t todo, int big_endian);

#ifdef USE_GDK_PIXBUF
static int 		output_scale_width = 0;
static int		output_scale_height = 0;
static float		output_fps = 25.0;
static int 		output_yuv = 1; // 422

void	lav_set_project(int w, int h, float f, int fmt)
{
	output_scale_width = w;
	output_scale_height = h;
	output_fps = f;
	output_yuv = fmt;
}
#else
void	lav_set_project(int w, int h, float f, int fmt)
{
}
#endif

#define M_SOF0  0xC0
#define M_SOF1  0xC1
#define M_DHT   0xC4
#define M_SOI   0xD8		/* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9		/* End Of Image (end of datastream) */
#define M_SOS   0xDA		/* Start Of Scan (begins compressed data) */
#define M_DQT   0xDB
#define M_APP0  0xE0
#define M_APP1  0xE1


#ifdef HAVE_LIBQUICKTIME
/*
   put_int4:
   Put a 4 byte integer value into a character array as big endian number
*/

static void put_int4(unsigned char *buf, int val)
{
	buf[0] = (val >> 24);
	buf[1] = (val >> 16);
	buf[2] = (val >> 8 );
	buf[3] = (val      );
}
#endif

//#ifdef SUPPORT_READ_DV2
//static int check_DV2_input(lav_file_t *lav_fd);
//#endif

#define TMP_EXTENSION ".tmp"

/*
   get_int2:
   get a 2 byte integer value from a character array as big endian number
 */

static int get_int2(unsigned char *buff)
{
   return (buff[0]*256 + buff[1]);
}

/*
   scan_jpeg:
   Scan jpeg data for markers, needed for Quicktime MJPA format
   and partly for AVI files.
   Taken mostly from Adam Williams' quicktime library
 */

static int scan_jpeg(unsigned char * jpegdata, long jpeglen, int header_only)
{
   int  marker, length;
   long p;

   jpeg_field_size     = 0;
   jpeg_quant_offset   = 0;
   jpeg_huffman_offset = 0;
   jpeg_image_offset   = 0;
   jpeg_scan_offset    = 0;
   jpeg_data_offset    = 0;
   jpeg_padded_len     = 0;
   jpeg_app0_offset    = 0;
   jpeg_app1_offset    = 0;

   /* The initial marker must be SOI */

   if (jpegdata[0] != 0xFF || jpegdata[1] != M_SOI) return -1;

   /* p is the pointer within the jpeg data */

   p = 2;

   /* scan through the jpeg data */

   while(p<jpeglen)
   {
      /* get next marker */

      /* Find 0xFF byte; skip any non-FFs */
      while(jpegdata[p] != 0xFF)
      {
         p++;
         if(p>=jpeglen) return -1;
      }

      /* Get marker code byte, swallowing any duplicate FF bytes */
      while(jpegdata[p] == 0xFF)
      {
         p++;
         if(p>=jpeglen) return -1;
      }

      marker = jpegdata[p++];

      if(p<=jpeglen-2)
         length = get_int2(jpegdata+p);
      else
         length = 0;

      /* We found a marker - check it */

      if(marker == M_EOI) { jpeg_field_size = p; break; }

      switch(marker)
      {
         case M_SOF0:
         case M_SOF1:
            jpeg_image_offset = p-2;
            break;
         case M_DQT:
            if(jpeg_quant_offset==0) jpeg_quant_offset = p-2;
            break;
         case M_DHT:
            if(jpeg_huffman_offset==0) jpeg_huffman_offset = p-2;
            break;
         case M_SOS:
            jpeg_scan_offset = p-2;
            jpeg_data_offset = p+length;
            if(header_only) return 0; /* we are done with the headers */
            break;
         case M_APP0:
            if(jpeg_app0_offset==0) jpeg_app0_offset = p-2;
            break;
         case M_APP1:
            if(jpeg_app1_offset==0) jpeg_app1_offset = p-2;
            break;
      }

      /* The pseudo marker as well as the markers M_TEM (0x01)
         and M_RST0 ... M_RST7 (0xd0 ... 0xd7) have no paramters.
         M_SOI and M_EOI also have no parameters, but we should
         never come here in that case */

      if(marker == 0 || marker == 1 || (marker >= 0xd0 && marker <= 0xd7))
         continue;

      /* skip length bytes */

      if(p+length<=jpeglen)
         p += length;
      else
         return -1;
   }

   /* We are through parsing the jpeg data, we should have seen M_EOI */

   if(!jpeg_field_size) return -1;

   /* Check for trailing garbage until jpeglen is reached or a new
      M_SOI is seen */

   while(p<jpeglen)
   {
      if(p<jpeglen-1 && jpegdata[p]==0xFF && jpegdata[p+1]==M_SOI) break;
      p++;
   }

   jpeg_padded_len = p;
   return 0;

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
   /* Quicktime needs TOP_FIRST, for AVI we have the choice */

   switch(format)
   {
 /*     case 'b': // todo: must implement polarity for DV?
		return LAV_NOT_INTERLACED;

      case 'a': return LAV_INTER_TOP_FIRST;
      case 'A': return LAV_INTER_BOTTOM_FIRST;
      case 'D': return LAV_NOT_INTERLACED; //divx
      case 'Y': return LAV_NOT_INTERLACED; // planar yuv 4:2:0 (yv12)
      case 'P': return LAV_NOT_INTERLACED; // planar yuv 4:2:2 (yv16)
      case 'V': return LAV_NOT_INTERLACED; // planar yuv 4:2:0 (yv12)
      case 'v': return LAV_NOT_INTERLACED; // planar yuv 4:2:2 (yv16)

      case 'M': return LAV_NOT_INTERLACED; // mpeg4 , 
      case 'd': return LAV_INTER_BOTTOM_FIRST;  // DV, interlaced 
      case 'j': return LAV_INTER_TOP_FIRST;
      case 'q': return LAV_INTER_TOP_FIRST;
      case 'L': return LAV_NOT_INTERLACED;
      case 'l': return LAV_NOT_INTERLACED;
      case 'm': return LAV_INTER_TOP_FIRST;
      case 'x': return LAV_NOT_INTERLACED; // picture is always not interlaced*/
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
   lav_fd->is_MJPG     = 1;
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
		veejay_msg(0,"Quicktime not compiled in, cannot use Quicktime.");
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
			vj_picture_cleanup( lav_file->picture );
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

long	lav_bytes_remain( lav_file_t *lav_file )
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
 		  return AVI_bytes_remain( lav_file->avi_fd );
		default:
		 return -1;
	} 
	return -1;
}


int lav_write_frame(lav_file_t *lav_file, uint8_t *buff, long size, long count)
{
   int res, n;
   uint8_t *jpgdata = NULL;
   long jpglen = 0;
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
   if(lav_file->interlacing!=LAV_NOT_INTERLACED)
   {
	switch( lav_file->format )
	{
		case 'a':
		case 'A':			
         	   jpgdata = buff;
        	   jpglen  = size;

      	      /* Loop over both fields */

          	  for(n=0;n<2;n++)
         	  {
          		/* For first field scan entire field, for second field
        	          scan the JPEG header, put in AVI1 + polarity.
        	          Be generous on errors */

       		        res = scan_jpeg(jpgdata, size, n);
       	       		 if (res)
			 {
				 internal_error=ERROR_JPEG;
				 return -1;
			 }

         	      if(!jpeg_app0_offset) continue;

         	      /* APP0 marker should be at least 14+2 bytes */
         	      if(get_int2(jpgdata+jpeg_app0_offset+2) < 16 ) continue;

			jpgdata[jpeg_app0_offset+4] = 'A';
			jpgdata[jpeg_app0_offset+5] = 'V';
			jpgdata[jpeg_app0_offset+6] = 'I';
			jpgdata[jpeg_app0_offset+7] = '1';
			jpgdata[jpeg_app0_offset+8] = lav_file->format=='a' ? n+1 : 2-n;

	               /* Update pointer and len for second field */
	               jpgdata += jpeg_padded_len;
	               jpglen  -= jpeg_padded_len;
		}
		break;
#ifdef HAVE_LIBQUICKTIME
         case 'q':
	 case 'Q':

            jpgdata = buff;
            jpglen  = size;

            /* Loop over both fields */

            for(n=0;n<2;n++)
            {
               /* Scan the entire JPEG field data - APP1 marker MUST be present */
               res = scan_jpeg(jpgdata,jpglen,0);
               if(res || !jpeg_app1_offset) { internal_error=ERROR_JPEG; return -1; }

               /* Length of APP1 marker must be at least 40 + 2 bytes */
               if ( get_int2(jpgdata+jpeg_app1_offset+2) < 42)
               { internal_error=ERROR_JPEG; return -1; }

               /* Fill in data */
               put_int4(jpgdata+jpeg_app1_offset+ 4,0);
               put_int4(jpgdata+jpeg_app1_offset+ 8,QUICKTIME_MJPG_TAG);
               put_int4(jpgdata+jpeg_app1_offset+12,jpeg_field_size);
               put_int4(jpgdata+jpeg_app1_offset+16,jpeg_padded_len);
               put_int4(jpgdata+jpeg_app1_offset+20,n==0?jpeg_padded_len:0);
               put_int4(jpgdata+jpeg_app1_offset+24,jpeg_quant_offset);
               put_int4(jpgdata+jpeg_app1_offset+28,jpeg_huffman_offset);
               put_int4(jpgdata+jpeg_app1_offset+32,jpeg_image_offset);
               put_int4(jpgdata+jpeg_app1_offset+36,jpeg_scan_offset);
               put_int4(jpgdata+jpeg_app1_offset+40,jpeg_data_offset);

               /* Update pointer and len for second field */
               jpgdata += jpeg_padded_len;
               jpglen  -= jpeg_padded_len;
            }
            break;
#endif
	
	}
   }
   res = 0; /* Silence gcc */
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
		case 'd':
      if(n==0)
      {
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
#ifdef HAVE_LIBQUICKTIME
   int res=0;
#endif
   qt_audion = malloc(channels * sizeof (int16_t **));
   for (i = 0; i < channels; i++)
	qt_audion[i] = (int16_t *)malloc(samps * lav_file->bps);
#endif
	switch(lav_file->format )
	{
#ifdef HAVE_LIBQUICKTIME
      case 'q':
      case 'Q':
	if (bits != 16 || channels > 1)
	 {
    /* Deinterleave the audio into the two channels and/or convert
     * bits per sample to the required format.
     */
    qt_audion = malloc(channels * sizeof(*qt_audion));
    for (i = 0; i < channels; i++)
      qt_audion[i] = malloc(samps * sizeof(**qt_audion));

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

void	lav_bogus_set_length( lav_file_t *lav_file , int len )
{
	lav_file->bogus_len = len;
}

int	lav_bogus_video_length( lav_file_t *lav_file )
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
		return 2;//lav_file->bogus_len;
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

int lav_video_is_MJPG(lav_file_t *lav_file)
{
   return lav_file->is_MJPG;
}

int lav_video_MJPG_chroma(lav_file_t *lav_file)
{
	return lav_file->MJPG_chroma;
}

int	lav_is_yuv_planar( int pix_fmt )
{
	switch(pix_fmt){
		case PIX_FMT_YUVJ420P:
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUVJ444P:
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUV444P:
			return 1;
	}
	return 0;
}

int lav_video_cmodel( lav_file_t *lav_file)
{
	switch(lav_file->MJPG_chroma)
	{
		case CHROMA411:
			return PIX_FMT_YUV411P;
		case CHROMA420:
			return PIX_FMT_YUV420P;
		case CHROMA422:
			return PIX_FMT_YUV422P;
		case CHROMA420F:
			return PIX_FMT_YUVJ420P;
		case CHROMA422F:
			return PIX_FMT_YUVJ422P;
		case CHROMA444:
			return PIX_FMT_YUV444P;
		default:
			return -1;
	}

	return -1;
}

int	lav_video_is_qt( lav_file_t *lav_file)
{
#ifdef HAVE_LIBQUICK_TIME
	if( lav_file->qt_fd)
		return 1;
#endif
	return 0;
}

		

int lav_video_compressor_type(lav_file_t *lav_file)
{
#ifdef SUPPORT_READ_DV2
	if(lav_file->format == 'b')
		return rawdv_compressor( lav_file->dv_fd );
#endif
#ifdef USE_GDK_PIXBUF
	if(lav_file->format == 'x')
		return 0xffff;
#endif
#ifdef HAVE_LIBQUICKTIME
	if(lav_file->format == 'q' || lav_file->format == 'Q')
	{
		const char *compressor = quicktime_video_compressor(lav_file->qt_fd,0);
		return	vj_el_get_decoder_from_fourcc( compressor );
	}
#endif
	return vj_el_get_decoder_from_fourcc( AVI_video_compressor(lav_file->avi_fd) );

}

#define FOURCC_DV "dvsd"
#define FOURCC_PIC "pict"
#define FOURCC_LZO "mlzo" 

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
/*
   if(!kf)
   {
//	veejay_msg(0, "Requested frame is not a keyframe");
	return ret;
   }
*/
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
		qt_audion = malloc(channels * sizeof (int16_t **));
		for (i = 0; i < channels; i++)
			qt_audion[i] = (int16_t *)malloc(samps * lav_file->bps);

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
   int n;
   static char pict[5] = "PICT\0";
   char *video_comp = NULL;
   unsigned char *frame = NULL; 
   long len;
   int jpg_height, jpg_width, ncomps, hf[3], vf[3];
   int ierr;

   lav_file_t *lav_fd = (lav_file_t*) vj_malloc(sizeof(lav_file_t));

   if(lav_fd==0) { internal_error=ERROR_MALLOC; return 0; }

   /* Set lav_fd */

#ifdef	HAVE_LIBQUICKTIME
   char *audio_comp;
#endif
   lav_fd->avi_fd      = 0;
#ifdef SUPPORT_READ_DV2
   lav_fd->dv_fd	= 0;
#endif
#ifdef USE_GDK_PIXBUF
   lav_fd->picture	= NULL;
#endif
   lav_fd->format      = 0;
   lav_fd->interlacing = LAV_INTER_UNKNOWN;
   lav_fd->sar_w       = 0; /* (0,0) == unknown */
   lav_fd->sar_h       = 0; 
   lav_fd->has_audio   = 0;
   lav_fd->bps         = 0;
   lav_fd->is_MJPG     = 0;
   lav_fd->MJPG_chroma = CHROMAUNKNOWN;
   lav_fd->mmap_size   = mmap_size;

	int ret = 0;

	/* open file, check if file is a file */
	struct stat s;
	if( stat(filename, &s ) != 0 )
	{
		if(lav_fd) free(lav_fd);
		veejay_msg(VEEJAY_MSG_ERROR, "Invalid file '%s'. Proper permissions?",filename);
		return NULL;
	}

	if(!S_ISREG( s.st_mode) )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "'%s' is not a regular file",filename);
		if(lav_fd) free(lav_fd);
		return NULL;
	}


	lav_fd->avi_fd = AVI_open_input_file(filename,1,mmap_size);

	if( lav_fd->avi_fd && AVI_errno == AVI_ERR_EMPTY )
   	{
	   	veejay_msg(VEEJAY_MSG_ERROR, "Empty AVI file");
		if(lav_fd) free(lav_fd);
		return NULL;
   	}
	else if ( lav_fd->avi_fd && AVI_errno == 0 )
   	{
		veejay_msg(VEEJAY_MSG_DEBUG,
				"\tFile is AVI" );
		ret =1;
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
	       		return 0;
		}
		veejay_msg(VEEJAY_MSG_DEBUG, "\tFOURCC is %s", video_comp );
   	}
   	else if( AVI_errno==AVI_ERR_NO_AVI || !lav_fd->avi_fd)
   	{
#ifdef HAVE_LIBQUICKTIME
		if(quicktime_check_sig(filename))
		{
			quicktime_pasp_t pasp;
			int nfields, detail;
			lav_fd->qt_fd = quicktime_open(filename,1,0);
			video_format = 'q'; /* for error messages */
			if (!lav_fd->qt_fd)
		    	{
				veejay_msg(VEEJAY_MSG_ERROR, "Unable to open quicktime file");
			   	free(lav_fd);
	    			return 0;
	    		}
			else
				veejay_msg(VEEJAY_MSG_DEBUG, "\tOpening Quicktime file");
			lav_fd->avi_fd = NULL;
			lav_fd->format = 'q';
	 		video_comp = quicktime_video_compressor(lav_fd->qt_fd,0);
			veejay_msg(VEEJAY_MSG_DEBUG,"\tFile has fourcc '%s'",
					video_comp );
	  		/* We want at least one video track */
	  		if (quicktime_video_tracks(lav_fd->qt_fd) < 1)
	     		{
				veejay_msg(VEEJAY_MSG_ERROR, "At least one video track required");
	     			lav_close(lav_fd);
	     			internal_error = ERROR_FORMAT;
	     			return 0;
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
		#ifdef USE_GDK_PIXBUF
		if(!alt)
		{
			lav_fd->picture = vj_picture_open( (const char*) filename,
				output_scale_width, output_scale_height, get_ffmpeg_pixfmt(output_yuv) );
			if(lav_fd->picture)
			{
				lav_fd->format = 'x';
				lav_fd->has_audio = 0;
				lav_fd->bogus_len = (int) output_fps;
				video_comp = pict;
				ret = 1;
				alt = 1;
				veejay_msg(VEEJAY_MSG_DEBUG,"\tLoaded image file");
			}
		}
#endif
   	}

	if(ret == 0 || video_comp == NULL || alt == 0)
	{
		free(lav_fd);
		internal_error = ERROR_FORMAT;
		return 0;
	}
	
   lav_fd->bps = (lav_audio_channels(lav_fd)*lav_audio_bits(lav_fd)+7)/8;

   if(lav_fd->bps==0) lav_fd->bps=1; /* make it save since we will divide by that value */
 /*  	if(strlen(video_comp) == 1 ) {
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
   

	if(	strncasecmp(video_comp, "div3",4)==0 ||
		strncasecmp(video_comp, "mp43",4)==0 ||
  		strncasecmp(video_comp, "mp42",4)==0 )
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		veejay_msg(VEEJAY_MSG_WARNING, "Playing MS MPEG4v3 DivX Video. (Every frame should be an intra frame)" );
		return lav_fd;
	} 

	if(	strncasecmp(video_comp,"mp4v",4 )==0 ||
		strncasecmp(video_comp,"fmp4",4 )==0 ||
		strncasecmp(video_comp,"divx",4 ) == 0 ||
		strncasecmp(video_comp,"xvid",4 ) == 0 ||
		strncasecmp(video_comp,"dxsd",4 ) == 0 ||
		strncasecmp(video_comp,"mp4s",4 ) == 0 ||
		strncasecmp(video_comp,"m4s2",4 ) == 0 )
	{
		lav_fd->format = 'D';
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		veejay_msg(VEEJAY_MSG_WARNING, "Playing MPEG4 Video (Every frame should be an intra frame)");
		return lav_fd;
	}
		
    	if (	strncasecmp(video_comp,"iyuv",4)==0 ||
		strncasecmp(video_comp,"yv12",4)==0 ||
		strncasecmp(video_comp,"i420",4)==0)
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->format = 'Y';
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		return lav_fd;
	}

	if(strncasecmp(video_comp,"vj22",4)==0)
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

    	if (	strncasecmp(video_comp,"yv16",4)==0 ||
		strncasecmp(video_comp,"i422",4)==0 ||
		strncasecmp(video_comp,"hfyu",4)==0)
	{
		lav_fd->MJPG_chroma = CHROMA422;
		lav_fd->format = 'P';
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		return lav_fd; 
	}

	if( 	strncasecmp(video_comp, "avc1", 4 ) == 0 ||
		strncasecmp(video_comp, "h264", 4 ) == 0 ||
		strncasecmp(video_comp, "x264", 4 ) == 0 ||
		strncasecmp(video_comp, "davc", 4 ) == 0 )
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		return lav_fd;
	}

	if(	strncasecmp( video_comp, "mlzo", 4 ) == 0 )
	{
		lav_fd->MJPG_chroma = CHROMA422;
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		return lav_fd;
	}
	
	if (	strncasecmp(video_comp,"dvsd",4)==0 ||
		strncasecmp(video_comp,"dvcp",4) ==0 ||
		strncasecmp(video_comp,"dxsd",4) == 0 ||
		strncasecmp(video_comp, "dvp",3) == 0 ||
		strncasecmp(video_comp, "dvhd",4) == 0 ||
		strncasecmp(video_comp, "dv",2 ) == 0)
	{ 
		int gw = lav_video_height( lav_fd );
		if( gw == 480 )
			 lav_fd->MJPG_chroma = CHROMA411;
		else
			lav_fd->MJPG_chroma = CHROMA422;

		lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST;
		return lav_fd; 
	}

	if(	strncasecmp(video_comp, "png", 3 ) == 0 ||
		strncasecmp(video_comp, "mpng",4) == 0 )
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_INTER_UNKNOWN;
		return lav_fd;
	}

	if(	strncasecmp(video_comp, "svq1", 4 ) == 0 ||
		strncasecmp(video_comp, "svq3", 4 ) == 0 ||
		strncasecmp(video_comp, "rpza", 4 ) == 0 ||
		strncasecmp(video_comp, "cyuv", 4 ) == 0 )
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_INTER_UNKNOWN;
		return lav_fd;
	}
	
	if (	strncasecmp(video_comp,"mjpg", 4) == 0 ||
		strncasecmp(video_comp,"mjpa", 4) == 0 ||
		strncasecmp(video_comp,"jpeg", 4) == 0 ||
		strncasecmp(video_comp,"mjpb" ,4) == 0 ||
		strncasecmp(video_comp,"sp5x", 4) == 0 ||
		strncasecmp(video_comp,"jpgl", 4) == 0 ||
		strncasecmp(video_comp , "jfif", 4 ) == 0 ||
		strncasecmp(video_comp, "dmb1", 4)==0 )
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->interlacing = LAV_INTER_UNKNOWN;
		lav_fd->is_MJPG = 1;
	
		/* Make some checks on the video source, we read the first frame for that */

		ierr  = 0;
		frame = NULL;


		int rolls = 5; // try to survive loading broken AVI
		int pos   = 0;
		int success = 0;
		while( pos < rolls ) {
			if( frame != NULL ) {
				free(frame);
				frame = NULL;
			}	

			if( lav_set_video_position(lav_fd, pos ) ) {
				pos++;
				continue;
			}
			if( (len = lav_frame_size(lav_fd, pos )) <= 0 ) {
				pos++;
				continue;
			}
			
			if( (frame = (unsigned char*) malloc(len)) == 0 ) {
			       ierr = ERROR_MALLOC;
			       break;
			}
	 		if( (lav_read_frame( lav_fd, frame ) <= 0 ) ) {
				pos ++;
				if( frame ) { free(frame);frame=NULL;}
				continue;
			}
			if( scan_jpeg(frame,len,1) ) {
				ierr = ERROR_JPEG;
				if( frame ) { free(frame);frame=NULL;}
				break;
			}

			success = 1;
			break;
		}

		if(!success) {
			goto ERREXIT;
		} else {
			lav_set_video_position( lav_fd, pos );
		}

	/*	if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
		if ( (len = lav_frame_size(lav_fd,0)) <=0 ) goto ERREXIT;
		if ( (frame = (unsigned char*) malloc(len)) == 0 ) { ierr=ERROR_MALLOC; goto ERREXIT; }

		if ( lav_read_frame(lav_fd,frame) <= 0 ) goto ERREXIT;
	
		if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
		if( scan_jpeg(frame, len, 1) ) { ierr=ERROR_JPEG; goto ERREXIT; }

		// We have to look to the JPEG SOF marker for further information
    	  The SOF marker has the following format:

		FF
		C0
		len_hi
		len_lo
		data_precision
		height_hi
		height_lo
		width_hi
		width_lo
		num_components

		And then 3 bytes for each component:

		Component id
		H, V sampling factors (as nibbles)
		Quantization table number
	    */

	   /* Check if the JPEG has the special 4:2:2 format needed for
    	  some HW JPEG decompressors (the Iomega Buz, for example) */

	   ncomps = frame[jpeg_image_offset + 9];
	   if(ncomps==3)
   	   {
	    for(n=0;n<3;n++)
   		{
   	     hf[n] = frame[jpeg_image_offset + 10 + 3*n + 1]>>4;
         vf[n] = frame[jpeg_image_offset + 10 + 3*n + 1]&0xf;
        }

	  /* Identify chroma sub-sampling format only 420 and 422 supported
	   at present...*/
	  

		if( hf[0] == 2*hf[1] && hf[0] == 2*hf[2] )
		{
		 if( vf[0] == vf[1] && vf[0] == vf[2] )
		 {
			 lav_fd->MJPG_chroma = CHROMA422;
		 }
		 else if( vf[0] == 2*vf[1] && vf[0] == 2*vf[2] )
			{
			 lav_fd->MJPG_chroma = CHROMA420;
			}
		 	else		
			{	 lav_fd->MJPG_chroma = CHROMAUNKNOWN;
			}
	  	}
	  	else
		{
		  lav_fd->MJPG_chroma = CHROMAUNKNOWN;
		}
   	    } // ncomps
	   /* Check if video is interlaced */

	   /* height and width are encoded in the JPEG SOF marker at offsets 5 and 7 */

	   jpg_height = get_int2(frame + jpeg_image_offset + 5);
	   jpg_width  = get_int2(frame + jpeg_image_offset + 7);

	/*   if( strncasecmp( frame + 6, "LAVC", 4 ) == 0 ) {
		   int pf = detect_pixel_format_with_ffmpeg( filename );
		   switch(pf) {
			case PIX_FMT_YUV422P: lav_fd->MJPG_chroma = CHROMA422;break;
			case PIX_FMT_YUVJ422P:lav_fd->MJPG_chroma = CHROMA422F;break;
			case PIX_FMT_YUV420P: lav_fd->MJPG_chroma = CHROMA420;break;
			case PIX_FMT_YUVJ420P: lav_fd->MJPG_chroma = CHROMA420F;break;
			case PIX_FMT_YUV444P: lav_fd->MJPG_chroma = CHROMA444;break;
			default:
			    pf = -1;
			    break;
	           }
		   if( pf >= 0 ) {
	          	 lav_fd->interlacing = LAV_NOT_INTERLACED;
			 if(frame) free(frame);
		  	 return lav_fd;
		   }
	   } */

	   /* check height */
	
	   if( jpg_height == lav_video_height(lav_fd))
  	 	{
   		   lav_fd->interlacing = LAV_NOT_INTERLACED;
		}
		else if ( jpg_height == lav_video_height(lav_fd)/2 )
		{
	
 	     /* Video is interlaced */
		  if(lav_fd->format == 'a')
		  {
            /* Check the APP0 Marker, if present */

            if(jpeg_app0_offset && 
               get_int2(frame + jpeg_app0_offset + 2) >= 5 &&
               strncasecmp((char*)(frame + jpeg_app0_offset + 4),"AVI1",4)==0 )
            {
                if (frame[jpeg_app0_offset+8]==1)
				{
                   lav_fd->interlacing = LAV_INTER_TOP_FIRST;
				}
	         	else
				{
                   lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST;
				}
            }
            else
            {
               /* There is no default, it really depends on the
                  application which produced the AVI */
               lav_fd->interlacing = LAV_INTER_UNKNOWN;
            }
            lav_fd->format = lav_fd->interlacing == LAV_INTER_BOTTOM_FIRST ? 'A' : 'a';
      	}  // end of interlaced
   	   }
       else
   		{
      		ierr=ERROR_JPEG;
      		goto ERREXIT;
   		}
    
   		if(frame) free(frame);

   		return lav_fd;
	}

	ierr = ERROR_FORMAT;
	veejay_msg(VEEJAY_MSG_ERROR, "Unrecognized format '%s'", video_comp);

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   internal_error = ierr;
	veejay_msg(VEEJAY_MSG_ERROR, "%s", lav_strerror());
   return 0;
}

/* Get size of first field of for a data array containing
   (possibly) two jpeg fields */

int lav_get_field_size(uint8_t * jpegdata, long jpeglen)
{
   int res;

   res = scan_jpeg(jpegdata,jpeglen,0);
   if(res<0) return jpeglen; /* Better than nothing */

   /* we return jpeg_padded len since this routine is used
      for field exchange where alignment might be important */

   return jpeg_padded_len;
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
      case 'D':
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

/* We need this to reorder the 32 bit values for big endian systems */
uint32_t reorder_32(uint32_t todo, int big_endian)
{
  unsigned char b0, b1, b2, b3;
  unsigned long reversed; 

  if( big_endian )
    {
    b0 = (todo & 0x000000FF);
    b1 = (todo & 0x0000FF00) >> 8;
    b2 = (todo & 0x00FF0000) >> 16;
    b3 = (todo & 0xFF000000) >> 24;

    reversed = (b0 << 24) + (b1 << 16) + (b2 << 8) +b3;
    return reversed;
    }
  return todo;
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













