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
#include <malloc.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <libvje/vje.h>
#include <veejay/vj-misc.h>
#include <veejay/vj-lib.h>
#include "veejay/jpegutils.h"
#include <libvjmsg/vj-common.h>


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

int vj_perform_screenshot2(veejay_t * info, uint8_t ** src)
{
    FILE *frame;
    int res = 0;
    char filename[15];
    uint8_t *jpeg_buff;
    int jpeg_size;

    video_playback_setup *settings = info->settings;

    jpeg_buff = (uint8_t *) malloc( 65535 * 4);
    if (!jpeg_buff)
	return -1;

    //vj_perform_get_primary_frame(info, src, settings->currently_processed_frame);

    sprintf(filename, "screenshot-%08d.jpg", settings->current_frame_num);

    frame = fopen(filename, "wb");

    if (!frame)
	return -1;

    jpeg_size = encode_jpeg_raw(jpeg_buff, (65535*4), 100,
				settings->dct_method,  
				info->edit_list->video_inter, 0,
				info->edit_list->video_width,
				info->edit_list->video_height, src[0],
				src[1], src[2]);

    res = fwrite(jpeg_buff, jpeg_size, 1, frame);
    fclose(frame);
    if(res) veejay_msg(VEEJAY_MSG_INFO, "Dumped frame to %s", filename);
    if (jpeg_buff)
	free(jpeg_buff);

    return res;
}
