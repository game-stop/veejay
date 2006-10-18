/*
 *  Some utilities for writing and reading AVI files.
 *  These are not intended to serve for a full blown
 *  AVI handling software (this would be much too complex)
 *  The only intention is to write out MJPEG encoded
 *  AVIs with sound and to be able to read them back again.
 *  These utilities should work with other types of codecs too, however.
 *
 *  Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "avilib.h"
#include "mjpeg_logging.h"
#include <ffmpeg/avcodec.h>
/* There is an experimental kernel patch available at 
 *   http://www.tech9.net/rml/linux/
 * that adds the O_STREAMING flag for open().  Files opened this way will
 * bypass the linux buffer cache entirely, so that writing multi-gigabyte files
 * with lavrec will not cause everything in memory to get swapped to disk.
 * This is highly desirable, hopefuly it will be merged with the mainstream
 * kernel.
 *
 * we leave it out if it's unknown, since its value differs per arch...
 */
#ifndef O_STREAMING
#define O_STREAMING 0
#endif

/* The following variable indicates the kind of error */

long AVI_errno = 0;

/*******************************************************************
 *                                                                 *
 *    Utilities for writing an AVI File                            *
 *                                                                 *
 *******************************************************************/

/* AVI_MAX_LEN: The maximum length of an AVI file, we stay a bit below
    the 2GB limit (Remember: 2*10^9 is smaller than 2 GB) */

#define AVI_MAX_LEN 2000000000

/* HEADERBYTES: The number of bytes to reserve for the header */

#define HEADERBYTES 2048

#define PAD_EVEN(x) ( ((x)+1) & ~1 )


/* Copy n into dst as a 4 byte, little endian number.
   Should also work on big endian machines */

static void long2str(uint8_t *dst, int n)
{
   dst[0] = (n    )&0xff;
   dst[1] = (n>> 8)&0xff;
   dst[2] = (n>>16)&0xff;
   dst[3] = (n>>24)&0xff;
}

/* Convert a string of 4 or 2 bytes to a number,
   also working on big endian machines */

static unsigned long str2ulong(uint8_t *str)
{
   return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}
static unsigned long str2ushort(uint8_t *str)
{
   return ( str[0] | (str[1]<<8) );
}

/* Calculate audio clip size from number of bits and number of channels.
   This may have to be adjusted for eg. 12 bits and stereo */

static int avi_sampsize(avi_t *AVI)
{
   int s;
   s = ((AVI->a_bits+7)/8)*AVI->a_chans;
   if(s==0) s=1; /* avoid possible zero divisions */
   return s;
}

/* Add a chunk (=tag and data) to the AVI file,
   returns -1 on write error, 0 on success */

static int avi_add_chunk(avi_t *AVI, const uint8_t *tag, uint8_t *data, int length)
{
   uint8_t c[8];

   /* Copy tag and length int c, so that we need only 1 write system call
      for these two values */

   memcpy(c,tag,4);
   long2str(c+4,length);

   /* Output tag, length and data, restore previous position
      if the write fails */

   length = PAD_EVEN(length);

   if( write(AVI->fdes,c,8) != 8 ||
       write(AVI->fdes,data,length) != length )
   {
      lseek(AVI->fdes,AVI->pos,SEEK_SET);
      AVI_errno = AVI_ERR_WRITE;
      return -1;
   }

   /* Update file position */

   AVI->pos += 8 + length;

   return 0;
}

static int avi_add_index_entry(avi_t *AVI, const uint8_t *tag, long flags, long pos, long len)
{
   void *ptr;

   if(AVI->n_idx>=AVI->max_idx)
   {
      ptr = realloc((void *)AVI->idx,(AVI->max_idx+4096)*16);
      if(ptr == 0)
      {
         AVI_errno = AVI_ERR_NO_MEM;
         return -1;
      }
      AVI->max_idx += 4096;
      AVI->idx = (uint8_t((*)[16]) ) ptr;
   }

   /* Add index entry */

   memcpy(AVI->idx[AVI->n_idx],tag,4);
   long2str(AVI->idx[AVI->n_idx]+ 4,flags);
   long2str(AVI->idx[AVI->n_idx]+ 8,pos);
   long2str(AVI->idx[AVI->n_idx]+12,len);

   /* Update counter */

   AVI->n_idx++;

   return 0;
}

/*
   AVI_open_output_file: Open an AVI File and write a bunch
                         of zero bytes as space for the header.

   returns a pointer to avi_t on success, a zero pointer on error
*/

avi_t* AVI_open_output_file(char * filename)
{
   avi_t *AVI;
   int i;
   uint8_t AVI_header[HEADERBYTES];

   /* Allocate the avi_t struct and zero it */

   AVI = (avi_t *) malloc(sizeof(avi_t));
   if(AVI==0)
   {
      AVI_errno = AVI_ERR_NO_MEM;
      return 0;
   }
   memset((void *)AVI,0,sizeof(avi_t));

   /* Since Linux needs a long time when deleting big files,
      we do not truncate the file when we open it.
      Instead it is truncated when the AVI file is closed */

   AVI->fdes = open(filename,O_RDWR|O_CREAT|O_STREAMING,0644);
   if (AVI->fdes < 0)
   {
      AVI_errno = AVI_ERR_OPEN;
      free(AVI);
      return 0;
   }

   /* Write out HEADERBYTES bytes, the header will go here
      when we are finished with writing */

   for (i=0;i<HEADERBYTES;i++) AVI_header[i] = 0;
   i = write(AVI->fdes,AVI_header,HEADERBYTES);
   if (i != HEADERBYTES)
   {
      close(AVI->fdes);
      AVI_errno = AVI_ERR_WRITE;
      free(AVI);
      return 0;
   }

   AVI->pos  = HEADERBYTES;
   AVI->mode = AVI_MODE_WRITE; /* open for writing */

   return AVI;
}

void AVI_set_video(avi_t *AVI, int width, int height, double fps, const char *compressor)
{
   /* may only be called if file is open for writing */

   if(AVI->mode==AVI_MODE_READ) return;

   AVI->width  = width;
   AVI->height = height;
   AVI->fps    = fps;
   memcpy(AVI->compressor,compressor,4);
   AVI->compressor[4] = 0;

}

void AVI_set_audio(avi_t *AVI, int channels, long rate, int bits, int format)
{
   /* may only be called if file is open for writing */

   if(AVI->mode==AVI_MODE_READ) return;

   AVI->a_chans = channels;
   AVI->a_rate  = rate;
   AVI->a_bits  = bits;
   AVI->a_fmt   = format;

}

#define OUT4CC(s) \
   if(nhb<=HEADERBYTES-4) memcpy(AVI_header+nhb,s,4); nhb += 4

#define OUTLONG(n) \
   if(nhb<=HEADERBYTES-4) long2str(AVI_header+nhb,n); nhb += 4

#define OUTSHRT(n) \
   if(nhb<=HEADERBYTES-2) { \
      AVI_header[nhb  ] = (n   )&0xff; \
      AVI_header[nhb+1] = (n>>8)&0xff; \
   } \
   nhb += 2

/*
  Write the header of an AVI file and close it.
  returns 0 on success, -1 on write error.
*/

static int avi_close_output_file(avi_t *AVI)
{

   int ret, njunk, sampsize, hasIndex, ms_per_frame, idxerror, flag;
   int movi_len, hdrl_start, strl_start;
   uint8_t AVI_header[HEADERBYTES];
   long nhb;

   /* Calculate length of movi list */

   movi_len = AVI->pos - HEADERBYTES + 4;

   /* Try to ouput the index entries. This may fail e.g. if no space
      is left on device. We will report this as an error, but we still
      try to write the header correctly (so that the file still may be
      readable in the most cases */

   idxerror = 0;
   ret = avi_add_chunk(AVI,(const uint8_t*)"idx1",(void*)AVI->idx,AVI->n_idx*16);
   hasIndex = (ret==0);
   if(ret)
   {
      idxerror = 1;
      AVI_errno = AVI_ERR_WRITE_INDEX;
   }

   /* Calculate Microseconds per frame */

   if(AVI->fps < 0.001)
      ms_per_frame = 0;
   else
      ms_per_frame = 1000000./AVI->fps + 0.5;

   /* Prepare the file header */

   nhb = 0;

   /* The RIFF header */

   OUT4CC ("RIFF");
   OUTLONG(AVI->pos - 8);    /* # of bytes to follow */
   OUT4CC ("AVI ");

   /* Start the header list */

   OUT4CC ("LIST");
   OUTLONG(0);        /* Length of list in bytes, don't know yet */
   hdrl_start = nhb;  /* Store start position */
   OUT4CC ("hdrl");

   /* The main AVI header */

   /* The Flags in AVI File header */

#define AVIF_HASINDEX           0x00000010      /* Index at end of file */
#define AVIF_MUSTUSEINDEX       0x00000020
#define AVIF_ISINTERLEAVED      0x00000100
#define AVIF_TRUSTCKTYPE        0x00000800      /* Use CKType to find key frames */
#define AVIF_WASCAPTUREFILE     0x00010000
#define AVIF_COPYRIGHTED        0x00020000

   OUT4CC ("avih");
   OUTLONG(56);                 /* # of bytes to follow */
   OUTLONG(ms_per_frame);       /* Microseconds per frame */
   OUTLONG(10000000);           /* MaxBytesPerSec, I hope this will never be used */
   OUTLONG(0);                  /* PaddingGranularity (whatever that might be) */
                                /* Other sources call it 'reserved' */
   flag = AVIF_WASCAPTUREFILE;
   if(hasIndex) flag |= AVIF_HASINDEX;
   if(hasIndex && AVI->must_use_index) flag |= AVIF_MUSTUSEINDEX;
   OUTLONG(flag);               /* Flags */
   OUTLONG(AVI->video_frames);  /* TotalFrames */
   OUTLONG(0);                  /* InitialFrames */
   if (AVI->audio_bytes)
      { OUTLONG(2); }           /* Streams */
   else
      { OUTLONG(1); }           /* Streams */
   OUTLONG(0);                  /* SuggestedBufferSize */
   OUTLONG(AVI->width);         /* Width */
   OUTLONG(AVI->height);        /* Height */
                                /* MS calls the following 'reserved': */
   OUTLONG(0);                  /* TimeScale:  Unit used to measure time */
   OUTLONG(0);                  /* DataRate:   Data rate of playback     */
   OUTLONG(0);                  /* StartTime:  Starting time of AVI data */
   OUTLONG(0);                  /* DataLength: Size of AVI data chunk    */


   /* Start the video stream list ---------------------------------- */

   OUT4CC ("LIST");
   OUTLONG(0);        /* Length of list in bytes, don't know yet */
   strl_start = nhb;  /* Store start position */
   OUT4CC ("strl");

   /* The video stream header */

   OUT4CC ("strh");
   OUTLONG(64);                 /* # of bytes to follow */
   OUT4CC ("vids");             /* Type */
   OUT4CC (AVI->compressor);    /* Handler */
   OUTLONG(0);                  /* Flags */
   OUTLONG(0);                  /* Reserved, MS says: wPriority, wLanguage */
   OUTLONG(0);                  /* InitialFrames */
   OUTLONG(ms_per_frame);       /* Scale */
   OUTLONG(1000000);            /* Rate: Rate/Scale == clips/second */
   OUTLONG(0);                  /* Start */
   OUTLONG(AVI->video_frames);  /* Length */
   OUTLONG(0);                  /* SuggestedBufferSize */
   OUTLONG(-1);                 /* Quality */
   OUTLONG(0);                  /* ClipSize */
   OUTLONG(0);                  /* Frame */
   OUTLONG(0);                  /* Frame */
   OUTLONG(0);                  /* Frame */
   OUTLONG(0);                  /* Frame */

   /* The video stream format */

   OUT4CC ("strf");
   OUTLONG(40);                 /* # of bytes to follow */
   OUTLONG(40);                 /* Size */
   OUTLONG(AVI->width);         /* Width */
   OUTLONG(AVI->height);        /* Height */
   OUTSHRT(1); OUTSHRT(24);     /* Planes, Count */
   OUT4CC (AVI->compressor);    /* Compression */
   OUTLONG(AVI->width*AVI->height);  /* SizeImage (in bytes?) */
   OUTLONG(0);                  /* XPelsPerMeter */
   OUTLONG(0);                  /* YPelsPerMeter */
   OUTLONG(0);                  /* ClrUsed: Number of colors used */
   OUTLONG(0);                  /* ClrImportant: Number of colors important */

   /* Finish stream list, i.e. put number of bytes in the list to proper pos */

   long2str(AVI_header+strl_start-4,nhb-strl_start);

   if (AVI->a_chans && AVI->audio_bytes)
   {

   sampsize = avi_sampsize(AVI);

   /* Start the audio stream list ---------------------------------- */

   OUT4CC ("LIST");
   OUTLONG(0);        /* Length of list in bytes, don't know yet */
   strl_start = nhb;  /* Store start position */
   OUT4CC ("strl");

   /* The audio stream header */

   OUT4CC ("strh");
   OUTLONG(64);            /* # of bytes to follow */
   OUT4CC ("auds");
   OUT4CC ("\0\0\0\0");
   OUTLONG(0);             /* Flags */
   OUTLONG(0);             /* Reserved, MS says: wPriority, wLanguage */
   OUTLONG(0);             /* InitialFrames */
   OUTLONG(sampsize);      /* Scale */
   OUTLONG(sampsize*AVI->a_rate); /* Rate: Rate/Scale == clips/second */
   OUTLONG(0);             /* Start */
   OUTLONG(AVI->audio_bytes/sampsize);   /* Length */
   OUTLONG(0);             /* SuggestedBufferSize */
   OUTLONG(-1);            /* Quality */
   OUTLONG(sampsize);      /* ClipSize */
   OUTLONG(0);             /* Frame */
   OUTLONG(0);             /* Frame */
   OUTLONG(0);             /* Frame */
   OUTLONG(0);             /* Frame */

   /* The audio stream format */

   OUT4CC ("strf");
   OUTLONG(16);                   /* # of bytes to follow */
   OUTSHRT(AVI->a_fmt);           /* Format */
   OUTSHRT(AVI->a_chans);         /* Number of channels */
   OUTLONG(AVI->a_rate);          /* ClipsPerSec */
   OUTLONG(sampsize*AVI->a_rate); /* AvgBytesPerSec */
   OUTSHRT(sampsize);             /* BlockAlign */
   OUTSHRT(AVI->a_bits);          /* BitsPerClip */

   /* Finish stream list, i.e. put number of bytes in the list to proper pos */

   long2str(AVI_header+strl_start-4,nhb-strl_start);

   }

   /* Finish header list */

   long2str(AVI_header+hdrl_start-4,nhb-hdrl_start);

   /* Calculate the needed amount of junk bytes, output junk */

   njunk = HEADERBYTES - nhb - 8 - 12;

   /* Safety first: if njunk <= 0, somebody has played with
      HEADERBYTES without knowing what (s)he did.
      This is a fatal error */

   if(njunk<=0)
   {
	   mjpeg_error_exit1("AVI_close_output_file: # of header bytes too small");
   }

   OUT4CC ("JUNK");
   OUTLONG(njunk);
   memset(AVI_header+nhb,0,njunk);
   nhb += njunk;

   /* Start the movi list */

   OUT4CC ("LIST");
   OUTLONG(movi_len); /* Length of list in bytes */
   OUT4CC ("movi");

   /* Output the header, truncate the file to the number of bytes
      actually written, report an error if someting goes wrong */

   if ( lseek(AVI->fdes,0,SEEK_SET)<0 ||
        write(AVI->fdes,AVI_header,HEADERBYTES)!=HEADERBYTES ||
        ftruncate(AVI->fdes,AVI->pos)<0 )
   {
      AVI_errno = AVI_ERR_CLOSE;
      return -1;
   }

   if(idxerror) return -1;

   return 0;
}
/*
   AVI_write_data:
   Add video or audio data to the file;

   Return values:
    0    No error;
   -1    Error, AVI_errno is set appropriatly;

*/

static int avi_write_data(avi_t *AVI, uint8_t *data, long length, int audio)
{
   int n;

   /* Check for maximum file length */

   if ( (AVI->pos + 8 + length + 8 + (AVI->n_idx+1)*16) > AVI_MAX_LEN )
   {
      AVI_errno = AVI_ERR_SIZELIM;
      return -1;
   }

   /* Add index entry */

   if(audio)
      n = avi_add_index_entry(AVI,(const uint8_t*)"01wb",0x00,AVI->pos,length);
   else
      n = avi_add_index_entry(AVI,(const uint8_t*)"00db",0x10,AVI->pos,length);

   if(n) return -1;

   /* Output tag and data */

   if(audio)
      n = avi_add_chunk(AVI,(const uint8_t*)"01wb",data,length);
   else
      n = avi_add_chunk(AVI,(const uint8_t*)"00db",data,length);

   if (n) return -1;

   return 0;
}

int AVI_write_frame(avi_t *AVI, uint8_t *data, long bytes)
{
   long pos;

   if(AVI->mode==AVI_MODE_READ) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }

   pos = AVI->pos;
   if( avi_write_data(AVI,data,bytes,0) ) return -1;
   AVI->last_pos = pos;
   AVI->last_len = bytes;
   AVI->video_frames++;
   return 0;
}

int AVI_dup_frame(avi_t *AVI)
{
   if(AVI->mode==AVI_MODE_READ) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }

   if(AVI->last_pos==0) return 0; /* No previous real frame */
   if(avi_add_index_entry(AVI,(const uint8_t*)"00db",0x10,AVI->last_pos,AVI->last_len)) return -1;
   AVI->video_frames++;
   AVI->must_use_index = 1;
   return 0;
}

int AVI_write_audio(avi_t *AVI, uint8_t *data, long bytes)
{
   if(AVI->mode==AVI_MODE_READ) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }

   if( avi_write_data(AVI,data,bytes,1) ) return -1;
   AVI->audio_bytes += bytes;
   return 0;
}

long AVI_bytes_remain(avi_t *AVI)
{
   if(AVI->mode==AVI_MODE_READ) return 0;

   return ( AVI_MAX_LEN - (AVI->pos + 8 + 16*AVI->n_idx));
}

/*******************************************************************
 *                                                                 *
 *    Utilities for reading video and audio from an AVI File       *
 *                                                                 *
 *******************************************************************/

int AVI_close(avi_t *AVI)
{
   int ret;

   /* If the file was open for writing, the header and index still have
      to be written */

   if(AVI->mode == AVI_MODE_WRITE)
      ret = avi_close_output_file(AVI);
   else
      ret = 0;

   /* Even if there happened a error, we first clean up */
   
   mmap_free( AVI->mmap_region );

   close(AVI->fdes);
   if(AVI->idx) free(AVI->idx);
   if(AVI->video_index) free(AVI->video_index);
   if(AVI->audio_index) free(AVI->audio_index);
   free(AVI);

   return ret;
}


int AVI_fileno(avi_t *AVI)
{
	return AVI->fdes;
}


#define ERR_EXIT(x) \
{ \
   AVI_close(AVI); \
   AVI_errno = x; \
   return 0; \
}

avi_t *AVI_open_input_file(char *filename, int getIndex, int mmap_size)
{
   avi_t *AVI;
   long i, n, rate, scale, idx_type;
   uint8_t *hdrl_data;
   long hdrl_len = 0;
   long nvi, nai, ioff;
   long tot;
   int lasttag = 0;
   int vids_strh_seen = 0;
   int vids_strf_seen = 0;
   int auds_strh_seen = 0;
   int auds_strf_seen = 0;
   int num_stream = 0;
   uint8_t data[256];
   struct stat s;

   /* Create avi_t structure */

   AVI = (avi_t *) malloc(sizeof(avi_t));
   if(AVI==NULL)
   {
      AVI_errno = AVI_ERR_NO_MEM;
      return NULL;
   }
   memset((void *)AVI,0,sizeof(avi_t));

   AVI->mode = AVI_MODE_READ; /* open for reading */

   /* Open the file */

   AVI->fdes = open(filename,O_RDONLY|O_STREAMING);
   if(AVI->fdes < 0)
   {
      AVI_errno = AVI_ERR_OPEN;
      free(AVI);
      return NULL;
   }

   off_t len = lseek( AVI->fdes, 0, SEEK_END );
   if( len <= (HEADERBYTES+16))
   {
	AVI_errno = AVI_ERR_EMPTY;
  	free(AVI);
	return NULL;
   }
   lseek(AVI->fdes,0,SEEK_SET);

   /* Read first 12 bytes and check that this is an AVI file */

   if( read(AVI->fdes,data,12) != 12 ) ERR_EXIT(AVI_ERR_READ)

   if( strncasecmp((char*)data  ,"RIFF",4) !=0 ||
       strncasecmp((char*)data+8,"AVI ",4) !=0 ) {
	   ERR_EXIT(AVI_ERR_NO_AVI)
	}
   /* Go through the AVI file and extract the header list,
      the start position of the 'movi' list and an optionally
      present idx1 tag */

   hdrl_data = 0;

   while(1)
   {
      if( read(AVI->fdes,data,8) != 8 ) break; /* We assume it's EOF */

      n = str2ulong(data+4);
      n = PAD_EVEN(n);

      if(strncasecmp((char*)data,"LIST",4) == 0)
      {
         if( read(AVI->fdes,data,4) != 4 ) ERR_EXIT(AVI_ERR_READ)
         n -= 4;
         if(strncasecmp((char*)data,"hdrl",4) == 0)
         {
            hdrl_len = n;
            hdrl_data = (uint8_t *) malloc(n);
            if(hdrl_data==0) ERR_EXIT(AVI_ERR_NO_MEM)
            if( read(AVI->fdes,hdrl_data,n) != n ) ERR_EXIT(AVI_ERR_READ)
         }
         else if(strncasecmp((char*)data,"movi",4) == 0)
         {
            AVI->movi_start = lseek(AVI->fdes,0,SEEK_CUR);
            lseek(AVI->fdes,n,SEEK_CUR);
         }
         else
            lseek(AVI->fdes,n,SEEK_CUR);
      }
      else if(strncasecmp((char*)data,"idx1",4) == 0)
      {
         /* n must be a multiple of 16, but the reading does not
            break if this is not the case */

         AVI->n_idx = AVI->max_idx = n/16;
         AVI->idx = (uint8_t((*)[16]) ) malloc(n);
         if(AVI->idx==0) ERR_EXIT(AVI_ERR_NO_MEM)
         if( read(AVI->fdes,AVI->idx,n) != n ) ERR_EXIT(AVI_ERR_READ)
      }
      else
         lseek(AVI->fdes,n,SEEK_CUR);
   }

   if(!hdrl_data      ) ERR_EXIT(AVI_ERR_NO_HDRL)
   if(!AVI->movi_start) ERR_EXIT(AVI_ERR_NO_MOVI)

   /* Interpret the header list */

   for(i=0;i<hdrl_len;)
   {
      /* List tags are completly ignored */

      if(strncasecmp((char*)hdrl_data+i,"LIST",4)==0) { i+= 12; continue; }

      n = str2ulong(hdrl_data+i+4);
      n = PAD_EVEN(n);

      /* Interpret the tag and its args */

      if(strncasecmp((char*)hdrl_data+i,"strh",4)==0)
      {
         i += 8;
         if(strncasecmp((char*)hdrl_data+i,"vids",4) == 0 && !vids_strh_seen)
         {
            memcpy(AVI->compressor,hdrl_data+i+4,4);
            AVI->compressor[4] = 0;
            scale = str2ulong(hdrl_data+i+20);
            rate  = str2ulong(hdrl_data+i+24);
            if(scale!=0) AVI->fps = (double)rate/(double)(scale);
	    /* kludge to get ntsc 29.97 correct */
	    if (AVI->fps > 29.95 && AVI->fps < 29.99)
		AVI->fps = 30000.0/1001.0; /* ntsc frame rate */
            AVI->video_frames = str2ulong(hdrl_data+i+32);
            AVI->video_strn = num_stream;
            vids_strh_seen = 1;


	    AVI->ffmpeg_codec_id =
		    vj_el_get_decoder_from_fourcc( AVI->compressor );
            lasttag = 1; /* vids */
         }
         else if (strncasecmp ((char*)hdrl_data+i,"auds",4) ==0 && ! auds_strh_seen)
         {
            AVI->audio_bytes = str2ulong(hdrl_data+i+32)*avi_sampsize(AVI);
            AVI->audio_strn = num_stream;
            auds_strh_seen = 1;
            lasttag = 2; /* auds */
         }
         else
            lasttag = 0;
         num_stream++;
      }
      else if(strncasecmp((char*)hdrl_data+i,"strf",4)==0)
      {
         i += 8;
         if(lasttag == 1)
         {
            AVI->width  = str2ulong(hdrl_data+i+4);
            AVI->height = str2ulong(hdrl_data+i+8);
            vids_strf_seen = 1;
         }
         else if(lasttag == 2)
         {
            AVI->a_fmt   = str2ushort(hdrl_data+i  );
            AVI->a_chans = str2ushort(hdrl_data+i+2);
            AVI->a_rate  = str2ulong (hdrl_data+i+4);
            AVI->a_bits  = str2ushort(hdrl_data+i+14);
            auds_strf_seen = 1;
         }
         lasttag = 0;
      }
      else
      {
         i += 8;
         lasttag = 0;
      }

      i += n;
   }

   free(hdrl_data);

   if(!vids_strh_seen || !vids_strf_seen || AVI->video_frames==0) ERR_EXIT(AVI_ERR_NO_VIDS)

   AVI->video_tag[0] = AVI->video_strn/10 + '0';
   AVI->video_tag[1] = AVI->video_strn%10 + '0';
   AVI->video_tag[2] = 'd';
   AVI->video_tag[3] = 'b';

   /* Audio tag is set to "99wb" if no audio present */
   if(!AVI->a_chans) AVI->audio_strn = 99;

   AVI->audio_tag[0] = AVI->audio_strn/10 + '0';
   AVI->audio_tag[1] = AVI->audio_strn%10 + '0';
   AVI->audio_tag[2] = 'w';
   AVI->audio_tag[3] = 'b';

   lseek(AVI->fdes,AVI->movi_start,SEEK_SET);

   /* get index if wanted */

   if(!getIndex) return AVI;

   /* if the file has an idx1, check if this is relative
      to the start of the file or to the start of the movi list */

   idx_type = 0;

   if(AVI->idx)
   {
      long pos, len;

      /* Search the first videoframe in the idx1 and look where
         it is in the file */

      for(i=0;i<AVI->n_idx;i++)
         if( strncasecmp((char*)AVI->idx[i],(char*)AVI->video_tag,3)==0 ) break;
      if(i>=AVI->n_idx) ERR_EXIT(AVI_ERR_NO_VIDS)

      pos = str2ulong(AVI->idx[i]+ 8);
      len = str2ulong(AVI->idx[i]+12);

      lseek(AVI->fdes,pos,SEEK_SET);
      if(read(AVI->fdes,data,8)!=8) ERR_EXIT(AVI_ERR_READ)
      if( strncasecmp((char*)data,(char*)AVI->idx[i],4)==0 && str2ulong(data+4)==len )
      {
         idx_type = 1; /* Index from start of file */
      }
      else
      {
         lseek(AVI->fdes,pos+AVI->movi_start-4,SEEK_SET);
         if(read(AVI->fdes,data,8)!=8) ERR_EXIT(AVI_ERR_READ)
         if( strncasecmp((char*)data,(char*)AVI->idx[i],4)==0 && str2ulong(data+4)==len )
         {
            idx_type = 2; /* Index from start of movi list */
         }
      }
      /* idx_type remains 0 if neither of the two tests above succeeds */
   }

   if(idx_type == 0)
   {
      /* we must search through the file to get the index */

      lseek(AVI->fdes, AVI->movi_start, SEEK_SET);

      AVI->n_idx = 0;

      while(1)
      {
         if( read(AVI->fdes,data,8) != 8 ) break;
         n = str2ulong(data+4);

         /* The movi list may contain sub-lists, ignore them */

         if(strncasecmp((char*)data,"LIST",4)==0)
         {
            lseek(AVI->fdes,4,SEEK_CUR);
            continue;
         }

         /* Check if we got a tag ##db, ##dc or ##wb */

         if( ( (data[2]=='d' || data[2]=='D') &&
               (data[3]=='b' || data[3]=='B' || data[3]=='c' || data[3]=='C') )
          || ( (data[2]=='w' || data[2]=='W') &&
               (data[3]=='b' || data[3]=='B') ) )
         {
            avi_add_index_entry(AVI,data,0,lseek(AVI->fdes,0,SEEK_CUR)-8,n);
         }

         lseek(AVI->fdes,PAD_EVEN(n),SEEK_CUR);
      }
      idx_type = 1;
   }

   /* Now generate the video index and audio index arrays */

   nvi = 0;
   nai = 0;

   for(i=0;i<AVI->n_idx;i++)
   {
      if(strncasecmp((char*)AVI->idx[i],(char*)AVI->video_tag,3) == 0) nvi++;
      if(strncasecmp((char*)AVI->idx[i],(char*)AVI->audio_tag,4) == 0) nai++;
   }

   AVI->video_frames = nvi;
   AVI->audio_chunks = nai;

   if(AVI->video_frames==0) ERR_EXIT(AVI_ERR_NO_VIDS)
   AVI->video_index = (video_index_entry *) malloc(nvi*sizeof(video_index_entry));
   if(AVI->video_index==0) ERR_EXIT(AVI_ERR_NO_MEM)
   if(AVI->audio_chunks)
   {
      AVI->audio_index = (audio_index_entry *) malloc(nai*sizeof(audio_index_entry));
      if(AVI->audio_index==0) ERR_EXIT(AVI_ERR_NO_MEM)
   }

   nvi = 0;
   nai = 0;
   tot = 0;
   ioff = idx_type == 1 ? 8 : AVI->movi_start+4;

   for(i=0;i<AVI->n_idx;i++)
   {
      if(strncasecmp((char*)AVI->idx[i],(char*)AVI->video_tag,3) == 0)
      {
         AVI->video_index[nvi].pos = str2ulong(AVI->idx[i]+ 8)+ioff;
         AVI->video_index[nvi].len = str2ulong(AVI->idx[i]+12);
         nvi++;
      }
      if(strncasecmp((char*)AVI->idx[i],(char*)AVI->audio_tag,4) == 0)
      {
         AVI->audio_index[nai].pos = str2ulong(AVI->idx[i]+ 8)+ioff;
         AVI->audio_index[nai].len = str2ulong(AVI->idx[i]+12);
         AVI->audio_index[nai].tot = tot;
         tot += AVI->audio_index[nai].len;
         nai++;
      }
   }

   AVI->audio_bytes = tot;

   long file_size = 0;
   file_size = (long) lseek( AVI->fdes, 0, SEEK_END );

   /* Reposition the file */
   lseek(AVI->fdes,AVI->movi_start,SEEK_SET);
   AVI->video_pos = 0;

   /* map file to memory */
   AVI->mmap_region = NULL;
   AVI->mmap_size = AVI->width * AVI->height * mmap_size;
   if(AVI->mmap_size > 0)
   {
  	 AVI->mmap_region = mmap_file( AVI->fdes, 0, AVI->mmap_size, file_size );
   }

   return AVI;
}

long AVI_video_frames(avi_t *AVI)
{
   return AVI->video_frames;
}
int  AVI_video_width(avi_t *AVI)
{
   return AVI->width;
}
int  AVI_video_height(avi_t *AVI)
{
   return AVI->height;
}
double AVI_frame_rate(avi_t *AVI)
{
   return AVI->fps;
}
char* AVI_video_compressor(avi_t *AVI)
{
   return AVI->compressor;
}
int	AVI_video_compressor_type(avi_t *AVI)
{
	return AVI->ffmpeg_codec_id;
}


int AVI_audio_channels(avi_t *AVI)
{
   return AVI->a_chans;
}
int AVI_audio_bits(avi_t *AVI)
{
   return AVI->a_bits;
}
int AVI_audio_format(avi_t *AVI)
{
   return AVI->a_fmt;
}
long AVI_audio_rate(avi_t *AVI)
{
   return AVI->a_rate;
}
long AVI_audio_bytes(avi_t *AVI)
{
   return AVI->audio_bytes;
}

long AVI_frame_size(avi_t *AVI, long frame)
{
   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }
   if(!AVI->video_index)         { AVI_errno = AVI_ERR_NO_IDX;   return -1; }

   if(frame < 0 || frame >= AVI->video_frames) return 0;
   return(AVI->video_index[frame].len);
}

int AVI_seek_start(avi_t *AVI)
{
   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }

   lseek(AVI->fdes,AVI->movi_start,SEEK_SET);
   AVI->video_pos = 0;
   return 0;
}

int AVI_set_video_position(avi_t *AVI, long frame)
{
   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }
   if(!AVI->video_index)         { AVI_errno = AVI_ERR_NO_IDX;   return -1; }

   if (frame < 0 ) frame = 0;
   AVI->video_pos = frame;
   return 0;
}
      

long AVI_read_frame(avi_t *AVI, uint8_t *vidbuf)
{
   long n;

   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }
   if(!AVI->video_index)         { AVI_errno = AVI_ERR_NO_IDX;   return -1; }

   if(AVI->video_pos < 0 || AVI->video_pos >= AVI->video_frames) return 0;

	n = AVI->video_index[AVI->video_pos].len;
   if( AVI->mmap_region == NULL )
   {			
   lseek(AVI->fdes, AVI->video_index[AVI->video_pos].pos, SEEK_SET);
   if (read(AVI->fdes,vidbuf,n) != n)
   {
      AVI_errno = AVI_ERR_READ;
      return -1;
   }
   } 
   else
   {
   n = mmap_read( AVI->mmap_region, AVI->video_index[AVI->video_pos].pos,
 		n, vidbuf );
   }

   AVI->video_pos++;

   return n;
}

int AVI_set_audio_position(avi_t *AVI, long byte)
{
   long n0, n1, n;

   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }
   if(!AVI->audio_index)         { AVI_errno = AVI_ERR_NO_IDX;   return -1; }

   if(byte < 0) byte = 0;

   /* Binary search in the audio chunks */

   n0 = 0;
   n1 = AVI->audio_chunks;

   while(n0<n1-1)
   {
      n = (n0+n1)/2;
      if(AVI->audio_index[n].tot>byte)
         n1 = n;
      else
         n0 = n;
   }

   AVI->audio_posc = n0;
   AVI->audio_posb = byte - AVI->audio_index[n0].tot;

   return 0;
}

long AVI_read_audio(avi_t *AVI, uint8_t *audbuf, long bytes)
{
   long nr, pos, left, todo;

   if(AVI->mode==AVI_MODE_WRITE) { AVI_errno = AVI_ERR_NOT_PERM; return -1; }
   if(!AVI->audio_index)         { AVI_errno = AVI_ERR_NO_IDX;   return -1; }

   nr = 0; /* total number of bytes read */

   while(bytes>0)
   {
      left = AVI->audio_index[AVI->audio_posc].len - AVI->audio_posb;
      if(left==0)
      {
         if(AVI->audio_posc>=AVI->audio_chunks-1) return nr;
         AVI->audio_posc++;
         AVI->audio_posb = 0;
         continue;
      }
      if(bytes<left)
         todo = bytes;
      else
         todo = left;
      pos = AVI->audio_index[AVI->audio_posc].pos + AVI->audio_posb;
      lseek(AVI->fdes, pos, SEEK_SET);
      if (read(AVI->fdes,audbuf+nr,todo) != todo)
      {
         AVI_errno = AVI_ERR_READ;
         return -1;
      }
      bytes -= todo;
      nr    += todo;
      AVI->audio_posb += todo;
   }

   return nr;
}

/* AVI_read_data: Special routine for reading the next audio or video chunk
                  without having an index of the file. */

int AVI_read_data(avi_t *AVI, uint8_t *vidbuf, long max_vidbuf,
                              uint8_t *audbuf, long max_audbuf,
                              long *len)
{

/*
 * Return codes:
 *
 *    1 = video data read
 *    2 = audio data read
 *    0 = reached EOF
 *   -1 = video buffer too small
 *   -2 = audio buffer too small
 */

   int n;
   uint8_t data[8];

   if(AVI->mode==AVI_MODE_WRITE) return 0;

   while(1)
   {
      /* Read tag and length */

      if( read(AVI->fdes,data,8) != 8 ) return 0;

      /* if we got a list tag, ignore it */

      if(strncasecmp((char*)data,"LIST",4) == 0)
      {
         lseek(AVI->fdes,4,SEEK_CUR);
         continue;
      }

      n = PAD_EVEN(str2ulong(data+4));

      if(strncasecmp((char*)data,(char*)AVI->video_tag,3) == 0)
      {
         *len = n;
         AVI->video_pos++;
         if(n>max_vidbuf)
         {
            lseek(AVI->fdes,n,SEEK_CUR);
            return -1;
         }
         if(read(AVI->fdes,vidbuf,n) != n ) return 0;
         return 1;
      }
      else if(strncasecmp((char*)data,(char*)AVI->audio_tag,4) == 0)
      {
         *len = n;
         if(n>max_audbuf)
         {
            lseek(AVI->fdes,n,SEEK_CUR);
            return -2;
         }
         if(read(AVI->fdes,audbuf,n) != n ) return 0;
         return 2;
         break;
      }
      else
         if(lseek(AVI->fdes,n,SEEK_CUR)<0)  return 0;
   }
}

/* AVI_print_error: Print most recent error (similar to perror) */

const char *(avi_errors[]) =
{
  /*  0 */ "avilib - No Error",
  /*  1 */ "avilib - AVI file size limit reached",
  /*  2 */ "avilib - Error opening AVI file",
  /*  3 */ "avilib - Error reading from AVI file",
  /*  4 */ "avilib - Error writing to AVI file",
  /*  5 */ "avilib - Error writing index (file may still be useable)",
  /*  6 */ "avilib - Error closing AVI file",
  /*  7 */ "avilib - Operation (read/write) not permitted",
  /*  8 */ "avilib - Out of memory (malloc failed)",
  /*  9 */ "avilib - Not an AVI file",
  /* 10 */ "avilib - AVI file has no header list (corrupted?)",
  /* 11 */ "avilib - AVI file has no MOVI list (corrupted?)",
  /* 12 */ "avilib - AVI file has no video data",
  /* 13 */ "avilib - operation needs an index",
  /* 14 */ "avilib - Unkown Error",
  /* 15 */ "avilib - AVI file is empty"           
};
static int num_avi_errors = sizeof(avi_errors)/sizeof(char*);

static char error_string[4096];

void AVI_print_error(const char *str)
{
   int aerrno;

   aerrno = (AVI_errno>=0 && AVI_errno<num_avi_errors) ? AVI_errno : num_avi_errors-1;

   mjpeg_error("%s: %s",str,avi_errors[aerrno]);

   /* for the following errors, perror should report a more detailed reason: */

   if(AVI_errno == AVI_ERR_OPEN ||
      AVI_errno == AVI_ERR_READ ||
      AVI_errno == AVI_ERR_WRITE ||
      AVI_errno == AVI_ERR_WRITE_INDEX ||
      AVI_errno == AVI_ERR_CLOSE )
   {
      perror("REASON");
   }
}

const char *AVI_strerror(void)
{
   int aerrno;

   aerrno = (AVI_errno>=0 && AVI_errno<num_avi_errors) ? AVI_errno : num_avi_errors-1;

   if(AVI_errno == AVI_ERR_OPEN ||
      AVI_errno == AVI_ERR_READ ||
      AVI_errno == AVI_ERR_WRITE ||
      AVI_errno == AVI_ERR_WRITE_INDEX ||
      AVI_errno == AVI_ERR_CLOSE )
   {
      sprintf(error_string,"%s",avi_errors[aerrno]);
      return error_string;
   }
   else
   {
      return avi_errors[aerrno];
   }
}
