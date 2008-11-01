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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <sys/statfs.h>

#include <veejay/vj-misc.h>
#include <libvjmsg/vj-common.h>
#include <libyuv/yuvconv.h>
#include <ffmpeg/avcodec.h>
#include <unistd.h>
#include <stdio.h>
#include <linux/rtc.h>
#include <asm/ioctl.h>
typedef struct
{
	int rtc;
	char *dev;
} rtc_clock;

static unsigned long vj_get_timer()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((long) (tv.tv_sec * 1000000) + tv.tv_usec);
}

void	*vj_new_rtc_clock(void)
{
	rtc_clock *rtc = (rtc_clock*) malloc(sizeof(rtc_clock));
	memset(rtc,0,sizeof( rtc_clock ));

	rtc->dev = strdup( "/dev/rtc" );
	rtc->rtc = open( rtc->dev,
			0 );
	if(rtc->rtc < 0 )
	{
		if(rtc->dev) free(rtc->dev);
		if(rtc) free(rtc);
		return NULL;
	}
	unsigned long irqp = 1024;
	if( ioctl( rtc->rtc, RTC_IRQP_SET, irqp ) < 0 )
	{
		close( rtc->rtc );
		if(rtc->dev) free(rtc->dev);
		if(rtc) free(rtc);
		return NULL;
	}
	else if ( ioctl( rtc->rtc, RTC_PIE_ON, 0 ) < 0 )
	{
		close( rtc->rtc );
		if(rtc->dev) free(rtc->dev);
		if(rtc) free(rtc);
		return NULL;
	}
	return (void*) rtc;	
}
void	vj_rtc_wait(void *rclock, long time_stamp )
{
	rtc_clock *rtc = (rtc_clock*) rclock;
	while( time_stamp > 0)
	{
		unsigned long rtc_ts;
		if( read( rtc->rtc, &rtc_ts, sizeof(rtc_ts) ) <= 0 )
			veejay_msg(0, "Error reading RTC");
		time_stamp -= vj_get_relative_time();
	}
}

void	vj_rtc_close(void *rclock)
{
	rtc_clock *rtc = (rtc_clock*) rclock;
	close(rtc->rtc);
	free(rtc->dev);
	free(rtc);
}

static unsigned long vj_relative_time = 0;
unsigned long vj_get_relative_time()
{
    unsigned long time = vj_get_timer();
    unsigned long res = time - vj_relative_time;
    vj_relative_time = time;
    if(vj_relative_time < 0 )
	    vj_relative_time = 0;
    return res; 	
}
/*
#ifdef HAVE_JPEG
int vj_perform_screenshot2(veejay_t * info, uint8_t ** src)
{
    FILE *frame;
    int res = 0;
    uint8_t *jpeg_buff;
    VJFrame tmp;
    int jpeg_size;

    video_playback_setup *settings = info->settings;

    jpeg_buff = (uint8_t *) malloc( 65535 * 4);
    if (!jpeg_buff)
		return -1;

    vj_get_yuv_template( &tmp,
				info->video_output_width,
				info->video_output_height,
				info->pixel_format );

    if( tmp.shift_v == 0 )
    {
	tmp.data[0] = (uint8_t*) malloc(sizeof(uint8_t) * tmp.len );
	tmp.data[1] = (uint8_t*) malloc(sizeof(uint8_t) * tmp.uv_len );
	tmp.data[2] = (uint8_t*) malloc(sizeof(uint8_t) * tmp.uv_len );
	yuv422p_to_yuv420p2( src, tmp.data, tmp.width, tmp.height );
    }
    else
    {
	tmp.data[0] = src[0];
	tmp.data[1] = src[1];
	tmp.data[2] = src[2];
    }	

	if(info->uc->filename == NULL) 
	{
		info->uc->filename = (char*) malloc(sizeof(char) * 12); 
		sprintf(info->uc->filename, "%06d.jpg", info->settings->current_frame_num );
	}
    frame = fopen(info->uc->filename, "wb");

    if (frame)
    {	
    	jpeg_size = encode_jpeg_raw(jpeg_buff, (65535*4), 100,
				settings->dct_method,  
				info->edit_list->video_inter, 0,
				info->video_output_width,
				info->video_output_height,
				tmp.data[0],
				tmp.data[1], tmp.data[2]);

   	 res = fwrite(jpeg_buff, jpeg_size, 1, frame);
   	 fclose(frame);
    	 if(res) 
		veejay_msg(VEEJAY_MSG_INFO, "Dumped frame to %s", info->uc->filename);
    }

    if (jpeg_buff)
	free(jpeg_buff);
    if( tmp.shift_v == 0 )
    {
	free(tmp.data[0]);
	free(tmp.data[1]);
	free(tmp.data[2]);
    }

    return res;
}*/

int	veejay_create_temp_file(const char *prefix, char *dst)
{
	time_t today_time_;
	struct tm *today;

	today_time_ = time(NULL);
	today = localtime( &today_time_ );
	/* time:
		prefix_01-01-04-hh:mm:ss
           put time in filename, on cp a.o. the creation date
	   will be set to localtime (annoying for users who
	   copy arround files)
	*/
	sprintf(dst,
		"%s_[%02d-%02d-%02d]_[%02d:%02d:%02d]",		
		prefix,
		today->tm_mday,
		today->tm_mon,
		(today->tm_year%100),
		today->tm_hour,
		today->tm_min,
		today->tm_sec);
	return 1;
}

void	vj_get_rgb_template(VJFrame *src, int w, int h )
{
	src->width = w;
	src->height = h;
	src->uv_width = 0;
	src->uv_height = 0;
//	src->format = IMGFMT_BGR24;
	src->len = w * h * 3;
	src->uv_len = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
	src->data[3] = NULL;
}

void	vj_get_yuv_template(VJFrame *src, int w, int h, int fmt)
{
	src->width = w;
	src->height = h;
	src->uv_width = w >> 1;
	src->format = fmt;
	src->sampling = 0;

	if(fmt == 0)
	{
		//src->format = IMGFMT_YV12;
		src->uv_height = h >> 1;
		src->shift_v = 1;
	}
	if(fmt == 1)
	{
	//	src->format = IMGFMT_422P;
		src->uv_height = h;
		src->shift_v = 0;
	}
	src->len = w * h;
	src->uv_len = src->uv_width * src->uv_height;
	src->shift_h = 1;

	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
	src->data[3] = NULL;
}

void	vj_get_yuv444_template(VJFrame *src, int w, int h)
{
	src->width = w;
	src->uv_width = w;
	src->height = h;
	//src->format = IMGFMT_444P;
	src->uv_height = h;
	src->shift_v = 0;
	src->len = w * h;
	src->uv_len = src->uv_width * src->uv_height;
	src->shift_h = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
	src->data[3] = NULL;
}
int	available_diskspace(void)
{
	const char *point = ".";
	struct statfs s;
	if(statfs(point,&s) != 0)
		return 0;
	uint64_t avail = 0;
	avail = ((uint64_t) s.f_bavail) * ( (uint64_t) s.f_bsize);

	if( avail <= 0 )
	{
		veejay_msg(VEEJAY_MSG_WARNING, "No available diskspace in CWD");
		return 0;
	}
	if( avail <= 1048576)
	{
		veejay_msg(VEEJAY_MSG_WARNING, "Almost no available diskspace in CWD");
		return 0;
	}
	return 1;
}
int	prepare_cache_line(int perc, int n_slots)
{
	int total = 0; 
	char line[128];

	FILE *file = fopen( "/proc/meminfo","r");
	
	if(!file)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cant open proc, memory size cannot be determined");
		veejay_msg(VEEJAY_MSG_ERROR, "Cache disabled");
		return 1;
	}

//	fgets( line,128, file );
	fgets( line,128, file );
	fclose( file );
	int max_memory = 0;
	if(sscanf( line, "%*s %i", &total )==1)
	{
		if( perc > 0)
		{
			float	k = (float) perc / 100.0;
			int	threshold = total / 1024;
			max_memory = (int)( k * threshold );
		}
	}
	else
	{
		veejay_msg(0, "Dont understand layout of /proc/meminfo");
		return 1;
	}
	
	if( n_slots <= 0)
	 n_slots = 1;

	int chunk_size = (max_memory <= 0 ? 0: max_memory / n_slots ); 
	if(chunk_size > 0 )
	{
		veejay_msg(VEEJAY_MSG_INFO,"EDL Cache:");
		veejay_msg(VEEJAY_MSG_INFO, "\tTotal memory:       %2.2f MB", (float)total / 1024);
		veejay_msg(VEEJAY_MSG_INFO, "\tLimit per EDL:      %d MB", chunk_size);
		veejay_msg(VEEJAY_MSG_INFO, "\tMaximum EDL slots:  %d", n_slots );
		vj_el_init_chunk( chunk_size );
	}
	return 0;
}

