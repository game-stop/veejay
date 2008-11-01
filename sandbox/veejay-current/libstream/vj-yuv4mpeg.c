/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <libvjmsg/vj-common.h>
#include <libstream/vj-yuv4mpeg.h>
#include <string.h>

/* the audio routines are placed here 
   because its a feature i need. needs to be removed or put elsewhere.
*/


#define L_FOURCC(a,b,c,d) ( (d<<24) | ((c&0xff)<<16) | ((b&0xff)<<8) | (a&0xff) )

#define L_FOURCC_RIFF     L_FOURCC ('R', 'I', 'F', 'F')
#define L_FOURCC_WAVE     L_FOURCC ('W', 'A', 'V', 'E')
#define L_FOURCC_FMT      L_FOURCC ('f', 'm', 't', ' ')
#define L_FOURCC_DATA     L_FOURCC ('d', 'a', 't', 'a')

typedef struct {
    unsigned long rifftag;
    unsigned long rifflen;
    unsigned long wavetag;
    unsigned long fmt_tag;
    unsigned long fmt_len;
    unsigned short wFormatTag;
    unsigned short nChannels;
    unsigned long nSamplesPerSec;
    unsigned long nAvgBytesPerSec;
    unsigned short nBlockAlign;
    unsigned short wBitsPerSample;
    unsigned long datatag;
    unsigned long datalen;
} t_wave_hdr;

t_wave_hdr *wave_hdr;

int bytecount = 0;

vj_yuv *vj_yuv4mpeg_alloc(editlist * el, int w, int h)
{
    vj_yuv *yuv4mpeg = (vj_yuv *) malloc(sizeof(vj_yuv));
    if(!yuv4mpeg) return NULL;
    yuv4mpeg->sar = y4m_sar_UNKNOWN;
    yuv4mpeg->dar = y4m_dar_4_3;
    y4m_init_stream_info(&(yuv4mpeg->streaminfo));
    y4m_init_frame_info(&(yuv4mpeg->frameinfo));
    yuv4mpeg->width = w;
    yuv4mpeg->height = h;
    yuv4mpeg->audio_rate = el->audio_rate;
    yuv4mpeg->video_fps = el->video_fps;
    yuv4mpeg->has_audio = el->has_audio;
    yuv4mpeg->audio_bits = el->audio_bps;
    return yuv4mpeg;
}

void vj_yuv4mpeg_free(vj_yuv *v) {
}


int vj_yuv_stream_start_read(vj_yuv * yuv4mpeg, char *filename, int width,
			     int height)
{
    int i, w, h;
 
    yuv4mpeg->fd = open(filename,O_RDONLY);
 
   

    if (!yuv4mpeg->fd) {
	veejay_msg(VEEJAY_MSG_ERROR, "Unable to open video stream %s\n",
		   filename);
	return -1;
    }
    i = y4m_read_stream_header(yuv4mpeg->fd, &(yuv4mpeg->streaminfo));
    if (i != Y4M_OK) {
	veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg: %s", y4m_strerr(i));
	return -1;
    }
    w = y4m_si_get_width(&(yuv4mpeg->streaminfo));
    h = y4m_si_get_height(&(yuv4mpeg->streaminfo));

	if( w != width || h != height )
	{
    veejay_msg(VEEJAY_MSG_ERROR,
	       "Video dimensions: %d x %d must match %d x %d. Stream cannot be opened", w, h,
	       width, height);
		return -1;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "YUV4MPEG: stream header ok");
    return 0;
}

int vj_yuv_stream_write_header(vj_yuv * yuv4mpeg, editlist * el)
{
    int i = 0;
    y4m_si_set_width(&(yuv4mpeg->streaminfo), yuv4mpeg->width);
    y4m_si_set_height(&(yuv4mpeg->streaminfo), yuv4mpeg->height);
    y4m_si_set_interlace(&(yuv4mpeg->streaminfo), el->video_inter);
    y4m_si_set_framerate(&(yuv4mpeg->streaminfo),
			 mpeg_conform_framerate(el->video_fps));

    if (!Y4M_RATIO_EQL(yuv4mpeg->sar, y4m_sar_UNKNOWN)) {
	y4m_si_set_sampleaspect(&(yuv4mpeg->streaminfo), yuv4mpeg->sar);
	yuv4mpeg->sar.n = el->video_sar_width;
	yuv4mpeg->sar.d = el->video_sar_height;
	y4m_si_set_sampleaspect(&(yuv4mpeg->streaminfo), yuv4mpeg->sar);
    } else {
	y4m_ratio_t dar2 = y4m_guess_sar(yuv4mpeg->width,
					 yuv4mpeg->height,
					 yuv4mpeg->dar);
	y4m_si_set_sampleaspect(&(yuv4mpeg->streaminfo), dar2);
    }

    i = y4m_write_stream_header(yuv4mpeg->fd, &(yuv4mpeg->streaminfo));

    if (i != Y4M_OK)
	return -1;

    y4m_log_stream_info(LOG_INFO, "vj-yuv4mpeg", &(yuv4mpeg->streaminfo));

    return 0;
}

int vj_yuv_stream_open_pipe(vj_yuv *yuv4mpeg, char *filename,editlist *el)
{
	yuv4mpeg->fd = open(filename,O_WRONLY,0600);
        if(!yuv4mpeg->fd) return 0;
	return 1;
}
int vj_yuv_stream_header_pipe(vj_yuv *yuv4mpeg,editlist *el)
{	
    yuv4mpeg->has_audio = el->has_audio;
    vj_yuv_stream_write_header(yuv4mpeg, el);

    //if (el->has_audio) {
//	if (!vj_yuv_write_wave_header(el, filename))
//	    return 0;
  //  }
    return 1;	
}

int vj_yuv_stream_start_write(vj_yuv * yuv4mpeg, char *filename,
			      editlist * el)
{

    //if(mkfifo( filename, 0600)!=0) return -1;
    /* if the file exists gamble and simply append,
       if it does not exist write header 

     */
    struct stat sstat;
 
    if(strncasecmp( filename, "stdout", 6) == 0)
    {
		yuv4mpeg->fd = 1;
    }  
    else 
    {
       if(strncasecmp(filename, "stderr", 6) == 0)
	{
		yuv4mpeg->fd = 2;
	}
        else
	{
	    if (stat(filename, &sstat) == 0)
	    {
		if (S_ISREG(sstat.st_mode))
		{
		    	/* the file is a regular file */
		    	yuv4mpeg->fd = open(filename, O_APPEND | O_WRONLY, 0600);
		  	if (!yuv4mpeg->fd)
				return -1;
		}
	    	else
	    	{
			if (S_ISFIFO(sstat.st_mode))
			  veejay_msg(VEEJAY_MSG_INFO, "Destination file is a FIFO"); 
			return 1; // pipe needs handling
		}
	    }
	    else
	    {
		veejay_msg(VEEJAY_MSG_INFO, "Creating YUV4MPEG regular file %s\n",
			   filename);
		yuv4mpeg->fd = open(filename, O_CREAT | O_WRONLY, 0600);
  		if (!yuv4mpeg->fd)
 			return -1;
   	     }
	}

    }

    vj_yuv_stream_write_header(yuv4mpeg, el);

    yuv4mpeg->has_audio = el->has_audio;


   
    return 0;
}

void vj_yuv_stream_stop_write(vj_yuv * yuv4mpeg)
{
    y4m_fini_stream_info(&(yuv4mpeg->streaminfo));
    y4m_fini_frame_info(&(yuv4mpeg->frameinfo));
    close(yuv4mpeg->fd);

}

void vj_yuv_stream_stop_read(vj_yuv * yuv4mpeg)
{
    y4m_fini_stream_info(&(yuv4mpeg->streaminfo));
    y4m_fini_frame_info(&(yuv4mpeg->frameinfo));
    close(yuv4mpeg->fd);
    yuv4mpeg->sar = y4m_sar_UNKNOWN;
    yuv4mpeg->dar = y4m_dar_4_3;
}

int vj_yuv_get_frame(vj_yuv * yuv4mpeg, uint8_t * dst[3])
{
    int i;
    i = y4m_read_frame(yuv4mpeg->fd, &(yuv4mpeg->streaminfo),
		       &(yuv4mpeg->frameinfo), dst);
    
    if (i != Y4M_OK)
    {
	veejay_msg(VEEJAY_MSG_ERROR, "yuv4mpeg %s", y4m_strerr(i));
	return -1;
	}
    return 0;
}

int vj_yuv_get_aframe(vj_yuv * yuv4mpeg, uint8_t * audio)
{
    return 0;			/*un used */

}

int vj_yuv_put_frame(vj_yuv * vjyuv, uint8_t ** src)
{
    int i;
    if (!vjyuv->fd) {
	veejay_msg(VEEJAY_MSG_ERROR, "Invalid file descriptor for y4m stream");
	return -1;
	}
    i = y4m_write_frame(vjyuv->fd, &(vjyuv->streaminfo),
			&(vjyuv->frameinfo), src);
    if (i != Y4M_OK) {
	veejay_msg(VEEJAY_MSG_ERROR, "Yuv4Mpeg : [%s]", y4m_strerr(i));
	return -1;
    }
    return 0;
}
int vj_yuv_put_aframe(uint8_t * audio, editlist * el, int len)
{
    int i = 0;
    return i;
}
