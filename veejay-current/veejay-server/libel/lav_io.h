/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef LAV_IO_H
#define LAV_IO_H

#include <config.h>
#include <libel/avilib.h>
#include <libvje/vje.h>
#ifdef SUPPORT_READ_DV2
#include <libel/rawdv.h>
#endif
// play with mlt here, avformat producer/several consumers (we can write to)
// for normalized output (720x576/480) , dv1394 capture and playback
#ifdef HAVE_MJPEGTOOLS
#include <mjpegtools/yuv4mpeg.h>
#else
#include <yuv4mpeg.h>
#endif

#define LAV_INTER_UNKNOWN       Y4M_UNKNOWN
#define LAV_NOT_INTERLACED      Y4M_ILACE_NONE
#define LAV_INTER_TOP_FIRST     Y4M_ILACE_TOP_FIRST
#define LAV_INTER_BOTTOM_FIRST  Y4M_ILACE_BOTTOM_FIRST


/* chroma_format */
#define CHROMAUNKNOWN 0
#define CHROMA420 1
#define CHROMA422 2
#define CHROMA444 3
#define CHROMA411 4
#define CHROMA420F 5
#define CHROMA422F 6

/* raw data format of a single frame */
#define DATAFORMAT_MJPG     0
#define DATAFORMAT_DV2      1
#define DATAFORMAT_YUV420   2
#define DATAFORMAT_YUV422   3
#define DATAFORMAT_DIVX    4	
#define	DATAFORMAT_MPEG4	5	

typedef struct
{
   avi_t       *avi_fd;
#ifdef SUPPORT_READ_DV2
   dv_t		*dv_fd;
#endif
	void	*qt_fd;
   int         jpeg_fd;
   char        *jpeg_filename;
#ifdef USE_GDK_PIXBUF
   void	       *picture;
#endif
   int         format;
   int         interlacing;
   int         sar_w;  /* "clip aspect ratio" width  */
   int         sar_h;  /* "clip aspect ratio" height */
   int         has_audio;
   int         bps;
   int         is_MJPG;
   int         MJPG_chroma;
   long	       mmap_size;
   int	       bogus_len;
} lav_file_t;


int lav_detect_endian (void);
int  lav_query_APP_marker(char format);
int  lav_query_APP_length(char format);
int  lav_query_polarity(char format);
lav_file_t *lav_open_output_file(char *filename, char format,
                    int width, int height, int interlaced, double fps,
                    int asize, int achans, long arate);
int  lav_close(lav_file_t *lav_file);
int  lav_write_frame(lav_file_t *lav_file, uint8_t *buff, long size, long count);
int  lav_write_audio(lav_file_t *lav_file, uint8_t *buff, long samps);
long lav_video_frames(lav_file_t *lav_file);
int  lav_video_width(lav_file_t *lav_file);
int  lav_video_height(lav_file_t *lav_file);
double lav_frame_rate(lav_file_t *lav_file);
int  lav_video_interlacing(lav_file_t *lav_file);
void lav_video_clipaspect(lav_file_t *lav_file, int *sar_w, int *sar_h);
int  lav_video_is_MJPG(lav_file_t *lav_file);
int lav_is_DV(lav_file_t *lav_file);
int  lav_video_MJPG_chroma(lav_file_t *lav_file);
const char *lav_video_compressor(lav_file_t *lav_file);
int	lav_video_compressor_type(lav_file_t *lav_file);
int  lav_audio_channels(lav_file_t *lav_file);
int  lav_audio_bits(lav_file_t *lav_file);
long lav_audio_rate(lav_file_t *lav_file);
long lav_audio_clips(lav_file_t *lav_file);
long lav_frame_size(lav_file_t *lav_file, long frame);
int  lav_seek_start(lav_file_t *lav_file);
int  lav_set_video_position(lav_file_t *lav_file, long frame);
int  lav_read_frame(lav_file_t *lav_file, uint8_t *vidbuf);
int  lav_set_audio_position(lav_file_t *lav_file, long clip);
int  lav_read_audio(lav_file_t *lav_file, uint8_t *audbuf, long samps);
int  lav_filetype(lav_file_t *lav_file);
lav_file_t *lav_open_input_file(char *filename, long mmap_size);
int  lav_get_field_size(uint8_t * jpegdata, long jpeglen);
const char *lav_strerror(void);
int  lav_fileno( lav_file_t *lav_file );
void lav_set_default_chroma(int c);
long	lav_bytes_remain( lav_file_t *lav_file );
int	lav_bogus_video_length( lav_file_t *lav_file );
void    lav_bogus_set_length( lav_file_t *lav_file , int len );
#ifdef USE_GDK_PIXBUF
VJFrame *lav_get_frame_ptr( lav_file_t *lav_file );
void	lav_set_project( int w, int h, float fps, int shift );
#endif
#endif
