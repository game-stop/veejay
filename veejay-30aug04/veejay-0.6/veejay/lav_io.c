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
#define COMPILE_LAV_IO_C
#include <veejay/lav_io.h>
#include <veejay/vj-common.h>
#ifdef SUPPORT_READ_DV2
#include <libdv/dv.h>
#endif

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

#define M_SOF0  0xC0
#define M_SOF1  0xC1
#define M_DHT   0xC4
#define M_SOI   0xD8		/* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9		/* End Of Image (end of datastream) */
#define M_SOS   0xDA		/* Start Of Scan (begins compressed data) */
#define M_DQT   0xDB
#define M_APP0  0xE0
#define M_APP1  0xE1

#define QUICKTIME_MJPG_TAG 0x6d6a7067  /* 'mjpg' */

static int check_YUV420_input(lav_file_t *lav_fd);

#ifdef SUPPORT_READ_DV2
static int check_DV2_input(lav_file_t *lav_fd);
#endif

#define TMP_EXTENSION ".tmp"

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
      case 'a': return 0;
      case 'D': return 0;
      case 'd': return 0; 
      case 'A': return 0;
      case 'j': return 0;
      case 'q': return 1;
      case 'm': return 0;
      default:  return 0;
   }
}

int lav_query_APP_length(char format)
{
   /* AVI: APP0 14 bytes, Quicktime APP1: 40 */

   switch(format)
   {
      case 'a': return 14;
      case 'A': return 14;
	  case 'Y': return 14;
      case 'D': return 14;
	case 'M': return 14;
      case 'd': return 14;
      case 'j': return 14;
      case 'q': return 40;
      case 'm': return 0;
      default:  return 0;
   }
}

int lav_query_polarity(char format)
{
   /* Quicktime needs TOP_FIRST, for AVI we have the choice */

   switch(format)
   {
      case 'a': return LAV_INTER_TOP_FIRST;
      case 'A': return LAV_INTER_BOTTOM_FIRST;
      case 'D': return LAV_NOT_INTERLACED;
	  case 'Y': return LAV_NOT_INTERLACED;
	case 'M': return LAV_NOT_INTERLACED;
      case 'd': return LAV_INTER_UNKNOWN; 
      case 'j': return LAV_INTER_TOP_FIRST;
      case 'q': return LAV_INTER_TOP_FIRST;
      case 'm': return LAV_INTER_TOP_FIRST;
      default:  return LAV_INTER_TOP_FIRST;
   }
}

void lav_set_default_chroma(int _chroma)
{
	if(_chroma == CHROMAUNKNOWN || _chroma == CHROMA420 ||
		_chroma == CHROMA422 || _chroma == CHROMA444)
		_lav_io_default_chroma = _chroma;
}


lav_file_t *lav_open_output_file(char *filename, char format,
                    int width, int height, int interlaced, double fps,
                    int asize, int achans, long arate)
{
   lav_file_t *lav_fd = (lav_file_t*) malloc(sizeof(lav_file_t));

   if(lav_fd==0) { internal_error=ERROR_MALLOC; return 0; }

   /* Set lav_fd */

   lav_fd->avi_fd      = 0;
#ifdef HAVE_LIBQUICKTIME
   lav_fd->qt_fd       = 0;
#endif
#ifdef HAVE_LIBMOVTAR
   lav_fd->movtar_fd       = 0;
#endif
   lav_fd->format      = format;
/*
   if(rindex(filename, '.') != NULL)
   {
      if( (format == 'Y' || format=='D' || format == 'd' || format == 'a' || format == 'A') && strcmp(rindex(filename, '.')+1, "avi")) {
        internal_error = ERROR_FORMAT;
        return 0;
      }
      if(format == 'q' && (strcmp(rindex(filename, '.')+1, "qt") 
           && strcmp(rindex(filename, '.')+1, "mov") && strcmp(rindex(filename, '.')+1,"moov"))) {
        internal_error = ERROR_FORMAT;
        return 0;
      }
      if(format == 'j' && strcmp(rindex(filename, '.')+1, "jpg")
           && strcmp(rindex(filename, '.')+1, "jpeg")) {
        internal_error = ERROR_FORMAT;
        return 0;
      }
   }
*/
   lav_fd->interlacing = interlaced ? lav_query_polarity(format) :
                                      LAV_NOT_INTERLACED;
   lav_fd->has_audio   = (asize>0 && achans>0);
   lav_fd->bps         = (asize*achans+7)/8;
   lav_fd->is_MJPG     = 1;
   lav_fd->MJPG_chroma = _lav_io_default_chroma;
   switch(format)
   {
      case 'a':
      case 'A':
         /* Open AVI output file */

         lav_fd->avi_fd = AVI_open_output_file(filename);
         if(!lav_fd->avi_fd) { free(lav_fd); return 0; }
         AVI_set_video(lav_fd->avi_fd, width, height, fps, "MJPG");
         if (asize) AVI_set_audio(lav_fd->avi_fd, achans, arate, asize, WAVE_FORMAT_PCM);
         return lav_fd;
	 break;
     case 'Y':
	  lav_fd->avi_fd = AVI_open_output_file(filename);
	  if(!lav_fd->avi_fd) { free(lav_fd); return 0; }
	  AVI_set_video(lav_fd->avi_fd, width,height,fps, "iyuv");
	  if(asize) AVI_set_audio(lav_fd->avi_fd, achans,arate,asize,WAVE_FORMAT_PCM);
	  return lav_fd;
	  break;
      case 'D':
	  lav_fd->avi_fd = AVI_open_output_file(filename);
	  if(!lav_fd->avi_fd) { free(lav_fd); return 0; }
	  AVI_set_video(lav_fd->avi_fd,width,height,fps, "div3");
	  if(asize) AVI_set_audio(lav_fd->avi_fd,achans,arate,asize,WAVE_FORMAT_PCM);
	  return lav_fd;
      case 'M':
	  lav_fd->avi_fd = AVI_open_output_file(filename);
	  if(!lav_fd->avi_fd) { free(lav_fd); return 0; }
	  AVI_set_video(lav_fd->avi_fd,width,height,fps, "mp4v");
	  if(asize) AVI_set_audio(lav_fd->avi_fd,achans,arate,asize,WAVE_FORMAT_PCM);
	  return lav_fd;

      case 'd':
	  lav_fd->avi_fd = AVI_open_output_file(filename);
	  if(!lav_fd->avi_fd) { free(lav_fd); return 0; }
	  AVI_set_video(lav_fd->avi_fd,width,height,fps, "dvsd");
	  if(asize) AVI_set_audio(lav_fd->avi_fd,achans,arate,asize,WAVE_FORMAT_PCM);
	  return lav_fd;

      case 'j': {

        /* Open JPEG output file */
        char tempfile[256];

        strcpy(tempfile, filename);
        strcat(tempfile, TMP_EXTENSION);

        lav_fd->jpeg_filename = strdup(filename);
        lav_fd->jpeg_fd = open(tempfile, O_CREAT | O_TRUNC | O_WRONLY, 0644);

        return lav_fd;
      }
      case 'q':

#ifdef HAVE_LIBQUICKTIME
         /* open quicktime output file */

         /* since the documentation says that the file should be empty,
            we try to remove it first */

         remove(filename);

         lav_fd->qt_fd = quicktime_open(filename, 1, 1);
         if(!lav_fd->qt_fd) { free(lav_fd); return 0; }
         quicktime_set_video(lav_fd->qt_fd, 1, width, height, fps,
                             (interlaced ? QUICKTIME_MJPA : QUICKTIME_JPEG));

         /* The sound system wants unsigned data (QUICKTIME_RAW) for 8 bit
            and signed twos complement data (QUICKTIME_TWOS) for 16 bit! */
         if (asize) quicktime_set_audio(lav_fd->qt_fd, achans, arate,
                             asize, (asize==8) ? QUICKTIME_RAW : QUICKTIME_TWOS);
         return lav_fd;
#else
	 internal_error = ERROR_FORMAT;
	 return 0;
#endif

      case 'm':

#ifdef HAVE_LIBMOVTAR
         /* Open movtar output file */
         lav_fd->movtar_fd = movtar_open(filename, 0, 1, 0x0);
         if(!lav_fd->movtar_fd) { free(lav_fd); return 0; }
         movtar_set_video(lav_fd->movtar_fd, 1, width, height, fps, "MJPG", 0); /* BUUUUUUG !! interlaced !*/
         if (asize) movtar_set_audio(lav_fd->movtar_fd, achans, arate, asize, "LPCM");
         return lav_fd;
#else
	 internal_error = ERROR_FORMAT;
	 return 0;
#endif

      default:

         return 0;
   }
}

int lav_close(lav_file_t *lav_file)
{
   int res;

   video_format = lav_file->format; internal_error = 0; /* for error messages */

   switch(lav_file->format)
   {
      case 'a':
      case 'A':
      case 'd':
      case 'M': 
	  case 'Y':
     case 'D':
         res = AVI_close( lav_file->avi_fd );
         break;
      case 'j': {
         char tempfile[256];
         strcpy(tempfile, lav_file->jpeg_filename);
         strcat(tempfile, TMP_EXTENSION);
         res = close(lav_file->jpeg_fd);
         rename(tempfile, lav_file->jpeg_filename);
         free(lav_file->jpeg_filename);
         break;
      }
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         res = quicktime_close( lav_file->qt_fd );
         break;
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         res = movtar_close( lav_file->movtar_fd );
         break;
#endif
      default:
         res = -1;
   }

   free(lav_file);

   return res;
}

int lav_write_frame(lav_file_t *lav_file, uint8_t *buff, long size, long count)
{
   int res, n;
   uint8_t *jpgdata = NULL;
   long jpglen = 0;

   video_format = lav_file->format; internal_error = 0; /* for error messages */

   /* For interlaced video insert the apropriate APPn markers */

   if(lav_file->interlacing!=LAV_NOT_INTERLACED)
   {
      switch(lav_file->format)
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
               if (res) { internal_error=ERROR_JPEG; return -1; }

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

         case 'j':

            jpgdata = buff;
            jpglen = size;
            break;

#ifdef HAVE_LIBQUICKTIME
         case 'q':

            jpgdata = buff;
            jpglen  = size;

            /* Loop over both fields */

            for(n=0;n<2;n++)
            {
		int ncomps;
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
#ifdef HAVE_LIBMOVTAR
         case 'm':

            jpgdata = buff;
            jpglen  = size;

	    /* No APP markers needed, since movtar _requires_ the fields to be in a certain order
	       (even first, then odd) */
            break;
#endif
	case 'D':
	case 'M':
	case 'd':
		break;
      }
   }
   
   res = 0; /* Silence gcc */
   for(n=0;n<count;n++)
   {
      switch(lav_file->format)
      {
         case 'a':
         case 'A':
		 case 'Y':
	 case 'd':
	 case 'M':
	 case 'D':  
            if(n==0)
               res = AVI_write_frame( lav_file->avi_fd, buff, size );
            else
               res = AVI_dup_frame( lav_file->avi_fd );
            break;
         case 'j':
            if (n==0)
               write(lav_file->jpeg_fd, buff, size);
            break;
#ifdef HAVE_LIBQUICKTIME
         case 'q':
            res = quicktime_write_frame( lav_file->qt_fd, buff, size, 0 );
            break;
#endif
#ifdef HAVE_LIBMOVTAR
         case 'm':
            res = movtar_write_frame( lav_file->movtar_fd, buff, size);
            break;
#endif
         default:
            res = -1;
      }
      if (res) break;
   }

   return res;
}

int lav_write_audio(lav_file_t *lav_file, uint8_t *buff, long samps)
{
   int res;
#ifdef	HAVE_LIBQUICKTIME
   int	n, nb;
   uint8_t *hbuff;
#endif

   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
      case 'Y':
      case 'D':
      case 'd':
      case 'M':
         res = AVI_write_audio( lav_file->avi_fd, buff, samps*lav_file->bps);
         break;
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         if(lav_audio_bits(lav_file)==16)
         {
            nb = samps*2*lav_audio_channels(lav_file);
            hbuff = (uint8_t *) malloc(nb);
            if(!hbuff) { internal_error=ERROR_MALLOC; return -1; }
            for(n=0;n<nb;n+=2)
            { hbuff[n] = buff[n+1]; hbuff[n+1] = buff[n]; }
            res = quicktime_write_audio( lav_file->qt_fd, (char*)hbuff, samps, 0 );
            free(hbuff);
         }
         else
            res = quicktime_write_audio( lav_file->qt_fd, (char*)buff, samps, 0 );
         break;
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         res = movtar_write_audio( lav_file->movtar_fd, (char*)buff, samps);
	 break;
#endif
      default:
         res = -1;
   }

   return res;
}



long lav_video_frames(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	  case 'Y':
      case 'd':
	case 'M':
	case 'D':
         return AVI_video_frames(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_video_length(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_video_length(lav_file->movtar_fd);
#endif
   }
   return -1;
}

int lav_video_width(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
      case 'd': 
	case 'M':
	case 'Y':
	case 'D':  
         return AVI_video_width(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_video_width(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_video_width(lav_file->movtar_fd);
#endif
   }
   return -1;
}

int lav_video_height(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'M':
	case 'Y': 
	case 'D':
	case 'd': 
         return AVI_video_height(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_video_height(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_video_height(lav_file->movtar_fd);
#endif
   }
   return -1;
}

double lav_frame_rate(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'M':
	case 'Y':
	case 'D':
	case 'd': 
         return AVI_frame_rate(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_frame_rate(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_frame_rate(lav_file->movtar_fd);
#endif
   }
   return -1;
}

int lav_video_interlacing(lav_file_t *lav_file)
{
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


const char *lav_video_compressor(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
	case 'M':
	case 'D':
 	case 'Y':
	case 'd':   
   case 'A':
         return AVI_video_compressor(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_video_compressor(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return "jpeg";
#endif
   }
   return "N/A";
}

int lav_audio_channels(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
	case 'M':
	case 'D':
	case 'Y':
	case 'd':
      case 'a':
      case 'A':
         return AVI_audio_channels(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_track_channels(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_track_channels(lav_file->movtar_fd);
#endif
   }
   return -1;
}

int lav_audio_bits(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	  case 'Y':
	case 'd':
	case 'M':
	case 'D':
         return AVI_audio_bits(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_audio_bits(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_audio_bits(lav_file->movtar_fd);
#endif
   }
   return -1;
}

long lav_audio_rate(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
		case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_audio_rate(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_clip_rate(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_clip_rate(lav_file->movtar_fd);
#endif
   }
   return -1;
}

long lav_audio_clips(lav_file_t *lav_file)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	 case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_audio_bytes(lav_file->avi_fd)/lav_file->bps;
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_audio_length(lav_file->qt_fd,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_audio_length(lav_file->movtar_fd);
#endif
   }
   return -1;
}

long lav_frame_size(lav_file_t *lav_file, long frame)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'M':
	case 'Y':
	case 'D':
	case 'd':
         return AVI_frame_size(lav_file->avi_fd,frame);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_frame_size(lav_file->qt_fd,frame,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_frame_size(lav_file->movtar_fd,frame);
#endif
   }
   return -1;
}

int lav_seek_start(lav_file_t *lav_file)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_seek_start(lav_file->avi_fd);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_seek_start(lav_file->qt_fd);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_seek_start(lav_file->movtar_fd);
#endif
   }
   return -1;
}

int lav_set_video_position(lav_file_t *lav_file, long frame)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
		case 'Y':
	case 'M':	
	case 'D':
	case 'd':
         return AVI_set_video_position(lav_file->avi_fd,frame);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_set_video_position(lav_file->qt_fd,frame,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_set_video_position(lav_file->movtar_fd,frame);
#endif
   }
   return -1;
}

int lav_read_frame(lav_file_t *lav_file, uint8_t *vidbuf)
{
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_read_frame(lav_file->avi_fd,vidbuf);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_read_frame(lav_file->qt_fd,vidbuf,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_read_frame(lav_file->movtar_fd,vidbuf);
#endif
   }
   return -1;
}


int lav_set_audio_position(lav_file_t *lav_file, long clip)
{
   if(!lav_file->has_audio) return 0;
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
		case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_set_audio_position(lav_file->avi_fd,clip*lav_file->bps);
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         return quicktime_set_audio_position(lav_file->qt_fd,clip,0);
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_set_audio_position(lav_file->movtar_fd,clip);
#endif
   }
   return -1;
}

long lav_read_audio(lav_file_t *lav_file, uint8_t *audbuf, long samps)
{
#ifdef	HAVE_LIBQUICKTIME
   long res, n, t;
#endif

   if(!lav_file->has_audio)
   {
      internal_error = ERROR_NOAUDIO;
      return -1;
   }
   video_format = lav_file->format; internal_error = 0; /* for error messages */
   switch(lav_file->format)
   {
      case 'a':
      case 'A':
		case 'Y':
	case 'M':
	case 'D':
	case 'd':
         return AVI_read_audio(lav_file->avi_fd,audbuf,samps*lav_file->bps)/lav_file->bps;
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         res = quicktime_read_audio(lav_file->qt_fd,(char*)audbuf,samps,0)/lav_file->bps;
         if(res<=0) return res;
         if(lav_audio_bits(lav_file)==16)
         {
            for(n=0;n<res*2*lav_audio_channels(lav_file);n+=2)
            {
               t = audbuf[n];
               audbuf[n] = audbuf[n+1];
               audbuf[n+1] = t;
            }
         }
         return res;
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         return movtar_read_audio(lav_file->movtar_fd,(char*)audbuf,samps)/lav_file->bps;
#endif
   }
   return -1;
}

int lav_filetype(lav_file_t *lav_file)
{
   return lav_file->format;
}

lav_file_t *lav_open_input_file(char *filename)
{
   int n;
   const char *video_comp = NULL;
#ifdef	HAVE_LIBQUICKTIME
   char *audio_comp;
#endif
   unsigned char *frame = NULL; /* Make sure un-init segfaults! */
   long len;
   int jpg_height, jpg_width, ncomps, hf[3], vf[3];
   int ierr;

   lav_file_t *lav_fd = (lav_file_t*) malloc(sizeof(lav_file_t));

   if(lav_fd==0) { internal_error=ERROR_MALLOC; return 0; }

   /* Set lav_fd */

   lav_fd->avi_fd      = 0;
#ifdef HAVE_LIBQUICKTIME
   lav_fd->qt_fd       = 0;
#endif
#ifdef HAVE_LIBMOVTAR
   lav_fd->movtar_fd   = 0;
#endif
   lav_fd->format      = 0;
   lav_fd->interlacing = LAV_INTER_UNKNOWN;
   lav_fd->sar_w       = 0; /* (0,0) == unknown */
   lav_fd->sar_h       = 0; 
   lav_fd->has_audio   = 0;
   lav_fd->bps         = 0;
   lav_fd->is_MJPG     = 0;
   lav_fd->MJPG_chroma = CHROMAUNKNOWN;

   /* open video file, try AVI first */

   lav_fd->avi_fd = AVI_open_input_file(filename,1);
   video_format = 'a'; /* for error messages */

   if(lav_fd->avi_fd)
   {
      /* It is an AVI file */
#ifdef HAVE_LIBQUICKTIME
      lav_fd->qt_fd  = 0;
#endif
#ifdef HAVE_LIBMOVTAR
      lav_fd->movtar_fd  = 0;
#endif
      lav_fd->format = 'a';
      lav_fd->has_audio = (AVI_audio_bits(lav_fd->avi_fd)>0 &&
                           AVI_audio_format(lav_fd->avi_fd)==WAVE_FORMAT_PCM);
      video_comp = AVI_video_compressor(lav_fd->avi_fd);
      veejay_msg(VEEJAY_MSG_DEBUG, "Video compressor [%s]",video_comp);
   }
   else if( AVI_errno==AVI_ERR_NO_AVI )
   {
	veejay_msg(VEEJAY_MSG_ERROR, "Not an AVI file");
#ifdef HAVE_LIBQUICKTIME
      if(!quicktime_check_sig(filename))
#endif
#ifdef HAVE_LIBMOVTAR
      {
	movtar_init(FALSE, FALSE);
	if (!movtar_check_sig(filename))
#endif
	  {
	    /* None of the known formats */
            char errmsg[1024];
	    sprintf(errmsg, "Unable to identify file (not a supported format - avi");
#ifdef HAVE_LIBMOVTAR
            strcat(errmsg, ", movtar");
#endif
#ifdef HAVE_LIBQUICKTIME
            strcat(errmsg, ", quicktime");
#endif
	    strcat(errmsg, ").\n");
            fprintf(stderr, errmsg);
	    free(lav_fd);
	    internal_error = ERROR_FORMAT; /* Format not recognized */
	    return 0;
	  }
#ifdef HAVE_LIBMOVTAR
	else
	  {
	    /* It is a movtar file */
	    lav_fd->movtar_fd = movtar_open(filename, 1, 0, 0x0);
	    video_format = 'm'; /* for error messages */
	    if(!lav_fd->movtar_fd) { free(lav_fd); return 0; }
	    lav_fd->avi_fd = 0;
#ifdef HAVE_LIBQUICKTIME
	    lav_fd->qt_fd = 0;
#endif
	    lav_fd->format = 'm';
	    video_comp = "mjpg"; /* nothing else possible */
	    /* We want at least one video track */
	    if(movtar_video_tracks(lav_fd->movtar_fd) < 1)
	      {
		lav_close(lav_fd);
		internal_error = ERROR_FORMAT;
		return 0;
	      }
	    /* Check for audio tracks */
	    lav_fd->has_audio = 0;
	    if (movtar_audio_tracks(lav_fd->movtar_fd)) /* movtar audio is always readable */
		    lav_fd->has_audio = 1;
	    /* don't show us fake frames, too tedious to implement */
	    movtar_show_fake_frames(lav_fd->movtar_fd, 0); 
	  }
      }
#endif
#ifdef HAVE_LIBQUICKTIME
      else
	{
	  /* It is a quicktime file */
	  lav_fd->qt_fd = quicktime_open(filename,1,0);
	  video_format = 'q'; /* for error messages */
	  if(!lav_fd->qt_fd) { free(lav_fd); return 0; }
	  lav_fd->avi_fd = 0;
#ifdef HAVE_LIBMOVTAR
	  lav_fd->movtar_fd = 0;
#endif
	  lav_fd->format = 'q';
	  video_comp = quicktime_video_compressor(lav_fd->qt_fd,0);
	  /* We want at least one video track */
	  if(quicktime_video_tracks(lav_fd->qt_fd) < 1)
	    {
	      lav_close(lav_fd);
	      internal_error = ERROR_FORMAT;
	      return 0;
	    }
	  /* Check for audio tracks */
	  lav_fd->has_audio = 0;
	  if(quicktime_audio_tracks(lav_fd->qt_fd))
	    {
	      audio_comp = quicktime_audio_compressor(lav_fd->qt_fd,0);
	      /* in order to be able to play the audio correctly,
		 size must either be 8 bits and compressor "raw "
		 or 16 bits and compressor "twos" */
	      if( ( quicktime_audio_bits(lav_fd->qt_fd,0)==8 &&
		    strncasecmp(audio_comp,QUICKTIME_RAW,4)==0 ) ||
		  ( quicktime_audio_bits(lav_fd->qt_fd,0)==16 &&
		    strncasecmp(audio_comp,QUICKTIME_TWOS,4)==0 ) )
		{
		  lav_fd->has_audio = 1;
		}
	    }
	}
#endif
   }
   else
   {
      /* There should be an error from avilib, just return */
      free(lav_fd);
      return 0;
   }

   /* set audio bytes per clip */

   lav_fd->bps = (lav_audio_channels(lav_fd)*lav_audio_bits(lav_fd)+7)/8;
   if(lav_fd->bps==0) lav_fd->bps=1; /* make it save since we will divide by that value */
   if(strncasecmp(video_comp, "div3",4)==0) {
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->format = 'D';
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		veejay_msg(VEEJAY_MSG_DEBUG, "Playing MS MPEG4v3 DivX Video. (Every frame must be an intra frame)" );
		return lav_fd;
	} 
	if(strncasecmp(video_comp,"mp4v",4)==0)
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->format = 'M';
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		veejay_msg(VEEJAY_MSG_DEBUG, "Playing MPEG4 Video (Experimental)");
		return lav_fd;
	}
   /* Check compressor, no further action if not Motion JPEG/DV */
   if (strncasecmp(video_comp,"iyuv",4)==0)
	{
		lav_fd->MJPG_chroma = CHROMA420;
		lav_fd->format = 'Y';
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		veejay_msg(VEEJAY_MSG_DEBUG, "Playing YUV 4:2:0 uncompressed video");
		return lav_fd; 

	}

   if(strncasecmp(video_comp,"mjpg",4)!=0 &&
      strncasecmp(video_comp,"mjpa",4)!=0 &&
      strncasecmp(video_comp,"jpeg",4)!=0 )
   {
#ifdef SUPPORT_READ_DV2
   if(strncasecmp(video_comp,"dvsd",4)==0
#ifdef HAVE_LIBQUICKTIME
      || strncasecmp(video_comp,QUICKTIME_DV,4)==0
#endif
      || strncasecmp(video_comp,"dv",2)==0) {
       ierr = check_DV2_input(lav_fd);
       lav_fd->MJPG_chroma = CHROMA422;
	lav_fd->format = 'd';
       if (ierr) goto ERREXIT;
       /* DV is always interlaced, bottom first */
       lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST; 
   }
#endif

   if(strncasecmp(video_comp,"yuv",3)==0
#ifdef HAVE_LIBQUICKTIME
/*    || strncasecmp(video_comp,QUICKTIME_YUV4,4)==0 */
      || strncasecmp(video_comp,QUICKTIME_YUV420,4)==0
#endif
      ) {
       ierr = check_YUV420_input(lav_fd);
#ifdef HAVE_LIBQUICKTIME
       /* check for YUV format if quicktime file */
       if (strncasecmp(video_comp,QUICKTIME_YUV420,4)==0)
           lav_fd->MJPG_chroma = CHROMA420;
       else if (strncasecmp(video_comp,QUICKTIME_YUV4,4)==0)
           lav_fd->MJPG_chroma = CHROMA422;
#else
   lav_fd->MJPG_chroma = CHROMA420;
#endif
       if (ierr) goto ERREXIT;
   }
      return lav_fd;
   }

   lav_fd->is_MJPG = 1;

   /* Make some checks on the video source, we read the first frame for that */

   ierr  = 0;
   frame = NULL;
   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   if ( (len = lav_frame_size(lav_fd,0)) <=0 ) goto ERREXIT;
   if ( (frame = (unsigned char*) malloc(len)) == 0 ) { ierr=ERROR_MALLOC; goto ERREXIT; }

   if ( lav_read_frame(lav_fd,frame) <= 0 ) goto ERREXIT;
   /* reset video position to 0 */
   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   if( scan_jpeg(frame, len, 1) ) { ierr=ERROR_JPEG; goto ERREXIT; }

   /* We have to look to the JPEG SOF marker for further information
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
   }
   /* Check if video is interlaced */

   /* height and width are encoded in the JPEG SOF marker at offsets 5 and 7 */

   jpg_height = get_int2(frame + jpeg_image_offset + 5);
   jpg_width  = get_int2(frame + jpeg_image_offset + 7);

   /* check height */

   if( jpg_height == lav_video_height(lav_fd))
   {
      lav_fd->interlacing = LAV_NOT_INTERLACED;
   }
   else if ( jpg_height == lav_video_height(lav_fd)/2 )
   {
      /* Video is interlaced */

      switch(lav_fd->format)
      {
         case 'a':

            /* Check the APP0 Marker, if present */

            if(jpeg_app0_offset && 
               get_int2(frame + jpeg_app0_offset + 2) >= 5 &&
               strncasecmp((char*)(frame + jpeg_app0_offset + 4),"AVI1",4)==0 )
            {
                if (frame[jpeg_app0_offset+8]==1)
		{
                   lav_fd->interlacing = LAV_INTER_TOP_FIRST;
		}
	         else {
                   lav_fd->interlacing = LAV_INTER_BOTTOM_FIRST;
		}
            }
            else
            {
               /* There is no default, it really depends on the
                  application which produced the AVI */
               lav_fd->interlacing = LAV_INTER_TOP_FIRST;
            }
            lav_fd->format = lav_fd->interlacing == LAV_INTER_BOTTOM_FIRST ? 'A' : 'a';
            break;
	case 'd':
	case 'M':
		lav_fd->interlacing = LAV_NOT_INTERLACED;
		break;
         case 'q':
            lav_fd->interlacing = LAV_INTER_TOP_FIRST;
	    break;	
         case 'm':
            lav_fd->interlacing = LAV_INTER_TOP_FIRST;
	  break;
      }
   }
   else
   {
      ierr=ERROR_JPEG;
      goto ERREXIT;
   }

   free(frame);
   return lav_fd;

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   internal_error = ierr;
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

static char error_string[4096];

const char *lav_strerror(void)
{

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
      case 'd':
      case 'Y':
      case 'M':
      case 'D':
         return AVI_strerror();
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         /* The quicktime documentation doesn't say much about error codes,
            we hope that strerror may give some info */
         sprintf(error_string,"Quicktime error, possible(!) reason: %s",strerror(errno));
         return error_string;
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
         sprintf(error_string,"No detailed movtar error information available (yet) !");
         return error_string;
#endif
      default:
         /* No or unknown video format */
         if(errno) strerror(errno);
         else sprintf(error_string,"No or unknown video format");
         return error_string;
   }
}



#ifdef SUPPORT_READ_DV2
static int check_DV2_input(lav_file_t *lav_fd)
{
   int ierr = 0;
   double len = 0;
   unsigned char *frame = NULL;

   lav_fd->is_MJPG = 0;

   /* Make some checks on the video source, we read the first frame for that */

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
       lav_fd->sar_w = 0; /* ??? -> unknown */
       lav_fd->sar_h = 0;
       break;
     }
     veejay_msg(VEEJAY_MSG_DEBUG, "DV System %s (sar w %d sar h %d)",
	(decoder->system == e_dv_system_525_60 ? "525-60" : ( decoder->system == e_dv_system_625_50 ? "625-50" : "unknown!")),lav_fd->sar_w,lav_fd->sar_h);
     dv_decoder_free(decoder);
   }

   /* reset video position to 0 */
   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   return 0;

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   if (ierr) internal_error = ierr;
   return 1;
}
#endif



static int check_YUV420_input(lav_file_t *lav_fd)
{
   int ierr = 0;
   double len = 0;
   unsigned char *frame = NULL;

   lav_fd->is_MJPG = 0;

   /* Make some checks on the video source, we read the first frame for that */

   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   if ( (len = lav_frame_size(lav_fd,0)) <=0 ) goto ERREXIT;
   if ( (frame = (unsigned char*) malloc(len)) == 0 ) { ierr=ERROR_MALLOC; goto ERREXIT; }

   if ( lav_read_frame(lav_fd,frame) <= 0 ) goto ERREXIT;
   /* reset video position to 0 */
   if ( lav_set_video_position(lav_fd,0) ) goto ERREXIT;
   return 0;

ERREXIT:
   lav_close(lav_fd);
   if(frame) free(frame);
   if (ierr) internal_error = ierr;
   return 1;
}

int lav_fileno(lav_file_t *lav_file)
{
   int res;

   video_format = lav_file->format; 

   switch(lav_file->format)
   {
      case 'a':
      case 'A':
	case 'd': 
         res = AVI_fileno( lav_file->avi_fd );
         break;
#ifdef HAVE_LIBQUICKTIME
      case 'q':
         res = fileno(lav_file->qt_fd->stream);
         break;
#endif
#ifdef HAVE_LIBMOVTAR
      case 'm':
		  res = fileno( lav_file->movtar_fd->file );
         break;
#endif
      default:
         res = -1;
   }

   return res;
}














