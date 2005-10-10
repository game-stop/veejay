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
#include <libvje/vje.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#ifdef HAVE_JPEG
#include <liblavjpeg/jpegutils.h>
#endif
#include <libvjmsg/vj-common.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <libpostproc/img_format.h>

static unsigned int vj_relative_time = 0;

static unsigned int vj_get_timer()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return ((tv.tv_sec & 1000000) + tv.tv_usec);
}


float vj_get_relative_time()
{
    long int time, relative;
    time = vj_get_timer();
    relative = time - vj_relative_time;
    vj_relative_time = time;
    //fprintf(stderr, "relative_time: %d, %d, %f\n",
    //      time,relative,(relative*0.000001F));
    if (relative < 0)
	relative = 0;		/* cant keep counter increasing forever */
    return (float) relative *0.000001F;
}

int vj_perform_take_bg(veejay_t *info, uint8_t **src)
{
	VJFrame frame;
	VJFrameInfo tag;
	char *descr = "Difference Overlay";
	memset(&frame, 0, sizeof(VJFrame));
	memset(&tag, 0, sizeof(VJFrameInfo));
	frame.data[0] = src[0];
	frame.data[1] = src[1];
	frame.data[2] = src[2];
	tag.width = info->edit_list->video_width;
	tag.height = info->edit_list->video_height;	
	veejay_msg(VEEJAY_MSG_INFO, "Warning: taking current frame %d as static bg (%p)",info->settings->current_frame_num, src[0]);

	vj_effect_prepare( &frame, &tag, (int) vj_effect_get_by_name( descr ) );

	return 1;
}
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
}

#endif

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
	src->format = IMGFMT_BGR24;
	src->len = w * h * 3;
	src->uv_len = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
}

void	vj_get_yuv_template(VJFrame *src, int w, int h, int fmt)
{
	src->width = w;
	src->uv_width = w >> 1;
	src->height = h;
	
	if(fmt == 0)
	{
		src->format = IMGFMT_YV12;
		src->uv_height = h >> 1;
		src->shift_v = 1;
	}
	if(fmt == 1)
	{
		src->format = IMGFMT_422P;
		src->uv_height = h;
		src->shift_v = 0;
	}
	src->len = w * h;
	src->uv_len = src->uv_width * src->uv_height;
	src->shift_h = 1;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;

}

void	vj_get_yuv444_template(VJFrame *src, int w, int h)
{
	src->width = w;
	src->uv_width = w;
	src->height = h;
	src->format = IMGFMT_444P;
	src->uv_height = h;
	src->shift_v = 0;
	src->len = w * h;
	src->uv_len = src->uv_width * src->uv_height;
	src->shift_h = 0;
	src->data[0] = NULL;
	src->data[1] = NULL;
	src->data[2] = NULL;
}

