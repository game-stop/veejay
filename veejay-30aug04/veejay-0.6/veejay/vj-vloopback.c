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
#include <stdint.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev.h>
#include <string.h>
#include "vj-vloopback.h"
#include "hash.h"
#include "../ccvt/ccvt.h"
#include "colorspace.h"
#include "vj-common.h"


extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;



vj_vlb *vj_vloopback_alloc()
{
    vj_vlb *vlb = (vj_vlb *) malloc(sizeof(vj_vlb));
    if (!vlb)
	return NULL;
    vlb->width = 0;
    vlb->height = 0;
    vlb->mode = 1;
    vlb->pipe_id = -1;
    vlb->preferred_palette = VIDEO_PALETTE_YUV420P;
    return vlb;
}

void vj_vloopback_print_status()
{
    char buffer[255];
    char *loop, *input, *istatus, *ostatus, *output;
    FILE *vloopback;
    vloopback = fopen("/proc/video/vloopback/vloopbacks", "r");

    if (!vloopback) {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "Unable to open /proc/video/vloopback/vloopbacks.\nDo you have v4l information in /proc ?");

	return;
    }
    fgets(buffer, 255, vloopback);
    veejay_msg(VEEJAY_MSG_DEBUG, "You are using %s", buffer);	/* version information */
    fgets(buffer, 255, vloopback);	/* table */
    while (fgets(buffer, 255, vloopback)) {
	int loop_id = 0;
	/* parse tokens in proc information */
	buffer[strlen(buffer) - 1] = 0;
	loop = strtok(buffer, "\t");
	input = strtok(NULL, "\t");
	istatus = strtok(NULL, "\t");
	output = strtok(NULL, "\t");
	ostatus = strtok(NULL, "\t");
	veejay_msg(VEEJAY_MSG_DEBUG,
		   "Pipe [%s] Input = [%s] Status = [%s] Output = [%s] Status = [%s]",
		   loop, input, istatus, output, ostatus);
	sscanf(loop, "%d", &loop_id);

    }
    fclose(vloopback);
}

/* scan for proc information */
int vj_vloopback_verify_pipe(int mode, int pipe_id, char *filename)
{
    char buffer[255];
    char *loop, *input, *istatus, *ostatus, *output;
    FILE *vloopback;
    vloopback = fopen("/proc/video/vloopback/vloopbacks", "r");
    if (!vloopback)
	return -1;
    fgets(buffer, 255, vloopback);
    fgets(buffer, 255, vloopback);	/* table */
    while (fgets(buffer, 255, vloopback)) {
	int loop_id = 0;
	/* parse tokens in proc information */
	buffer[strlen(buffer) - 1] = 0;
	loop = strtok(buffer, "\t");
	input = strtok(NULL, "\t");
	istatus = strtok(NULL, "\t");
	output = strtok(NULL, "\t");
	ostatus = strtok(NULL, "\t");
	sscanf(loop, "%d", &loop_id);

	if (istatus[0] == '-' && mode == 1 && loop_id == pipe_id) {
	    veejay_msg(VEEJAY_MSG_INFO,
		       "Using input pipe %s (status %s) for Video Output",
		       input, istatus);
	    sprintf(filename, "/dev/%s", input);
	    fclose(vloopback);
	    return 0;
	}
	if (istatus[0] == 'W' && mode == 0 && ostatus[0] == '-' && loop_id == pipe_id) {	/* veejay will read the pipe */
	    veejay_msg(VEEJAY_MSG_INFO,
		       "Using output pipe %s (status %s) for Video Input",
		       output, ostatus);
	    sprintf(filename, "/dev/%s", output);
	    fclose(vloopback);
	    return 0;
	}
    }
    fclose(vloopback);
    return -1;
}

int vj_vloopback_get_status(char *filename) {
	FILE *fd;
	char buffer[255];
	char *loop, *input, *istatus, *output , *ostatus;
	fd = fopen("/proc/video/vloopback/vloopbacks","r");
	if(!fd) return VLOOPBACK_NONE;
	fgets(buffer,255,fd);
	fgets(buffer,255,fd);
	while(fgets(buffer,255,fd)) {
		buffer[strlen(buffer)-1]=0;
		loop = strtok(buffer,"\t");
		input = strtok(NULL,"\t");	
		istatus = strtok(NULL,"\t");
		output = strtok(NULL,"\t");
		ostatus = strtok(NULL,"\t");
		if(strcmp( input, filename )==0) { /* right pipe ? */
			if(ostatus[0] == 'R' ) {
				return VLOOPBACK_HAS_READER; /* busy */
			}
			if(istatus[0] == 'W' && ostatus[0]== '-' ) {
				return VLOOPBACK_AVAIL_OUTPUT;
			}
			if(istatus[0] == 'W' && ostatus[0] == 'R') {
				return VLOOPBACK_USED_PIPE;
			}
			if(istatus[0] == '-' && ostatus[0]== '-' ) {
				return VLOOPBACK_FREE_PIPE;
			}
		}
		
	}
	return VLOOPBACK_NONE;
}

/* veejay will read data from the pipe */
int vj_vloopback_open(vj_vlb * vlb, int width, int height, int norm)
{
    if (vlb->video_pipe != 0 || vlb->pipe_id == 1) {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "You are already using this vloopback pipe");
	return -1;		/* exist if exists */
    }
    vlb->video_pipe = open(vlb->pipepath, O_RDWR);
    if (!vlb->video_pipe) {
	veejay_msg(VEEJAY_MSG_ERROR,
		   "Cannot open file %s", vlb->pipepath);
	return -1;
    }
    if (vlb->mode == 1) {
	vlb->dst =
	    (uint8_t *) malloc(sizeof(uint8_t) * 3 * width * height);
	vlb->rgb24 =
	    (uint8_t *) malloc(sizeof(uint8_t) * 3 * width * height);
	if (!vlb->rgb24)
	    return -1;
	if (!vlb->dst)
	    return -1;

	vlb->width = width;
	vlb->height = height;
	
	if (ioctl(vlb->video_pipe, VIDIOCGCAP, &(vlb->vid_caps)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING,
		       "Warning, unable to get capabilities of [%s]",
		       vlb->pipepath);
	}
	if (ioctl(vlb->video_pipe, VIDIOCGPICT, &(vlb->vid_pic)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING,
		       "Warning, unable to get picture of [%s]",
		       vlb->pipepath);
	}
	vlb->vid_pic.palette = vlb->preferred_palette;

	if (ioctl(vlb->video_pipe, VIDIOCSPICT, &(vlb->vid_pic)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING,
		       "Warning, unable to set picture of [%s]",
		       vlb->pipepath);
	}
	if (ioctl(vlb->video_pipe, VIDIOCGWIN, &(vlb->vid_win)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING,
		       "Warning, unable to get window of [%s]",
		       vlb->pipepath);
	}

	vlb->vid_win.width = width;
	vlb->vid_win.height = height;

	if (ioctl(vlb->video_pipe, VIDIOCSWIN, &(vlb->vid_win)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING,
		       "Warning, unable to set window of [%s]",
		       vlb->pipepath);
	}

	vlb->channel.flags = 0;
	vlb->channel.tuners = 0;
	vlb->channel.norm = norm;
	vlb->channel.type = VIDEO_TYPE_CAMERA;

	if (ioctl(vlb->video_pipe, VIDIOCGCHAN, &(vlb->channel)) == -1) {
	    veejay_msg(VEEJAY_MSG_WARNING, "ioctl VIDIOCGCHAN");
	}

    }
    if (vlb->mode == 0) {
	veejay_msg(VEEJAY_MSG_WARNING, "not implemented");
	return -1;
    }

    vlb->pipe_id = 1;

    return 0;
}

int vj_vloopback_close(vj_vlb * vlb)
{
    if (!vlb)
	return 0;
    if(!vlb->video_pipe) return 0;

    close(vlb->video_pipe);

    if (vlb->dst)
	free(vlb->dst);
    if (vlb->rgb24)
	free(vlb->rgb24);
    vlb->dst = NULL;
    vlb->rgb24 = NULL;
    vlb->pipe_id = -1;
    vlb->video_pipe = 0;
    return 0;
}

int vj_vloopback_read(vj_vlb * vlb, uint8_t ** frame)
{
    /* capture data is now in v4lutil */
    return 0;
}

int vj_vloopback_write(vj_vlb * vlb, uint8_t ** frame)
{
    int n;

    /* copy frame to dst this should be fixed (not needed) */
    if(frame[0]==NULL) {
	veejay_msg(VEEJAY_MSG_ERROR, "This may never happen. vloopback buffer is NULL");
	return -1;
	}
    if(vlb->preferred_palette == VIDEO_PALETTE_RGB24)
	{
    veejay_memcpy(vlb->dst, frame[0], vlb->width * vlb->height);
    veejay_memcpy(vlb->dst + (vlb->width * vlb->height), frame[2],
	       (vlb->width * vlb->height * 5) / 4);
    veejay_memcpy(vlb->dst + (vlb->width * vlb->height * 5) / 4, frame[1]
	       , (vlb->width * vlb->height) / 4);

    /* convert rgb24 and write to address of mmap */
    ccvt_420p_rgb24(vlb->width, vlb->height, vlb->dst, vlb->rgb24);
	
	
    /* 
       convert_YCbCr_to_RGB(
       frame,
       vlb->height * vlb->width);

       ccvt gives correct colors(!) and is faster.
     */

    n = write(vlb->video_pipe, vlb->rgb24, vlb->width * vlb->height * 3);
    if (n != vlb->width * vlb->height * 3) {
	veejay_msg(VEEJAY_MSG_ERROR, "Wrote only %d bytes of %d", n,
		   (vlb->width * vlb->height * 3));
	return -1;
    }
	}
	else
	{
		unsigned int uv_len = (vlb->width * vlb->height)>>2;
  		veejay_memcpy(vlb->dst, frame[0], vlb->width * vlb->height);
    		veejay_memcpy(vlb->dst + (vlb->width * vlb->height), frame[1], uv_len );
    		veejay_memcpy(vlb->dst + (vlb->width * vlb->height * 5) / 4, frame[2], uv_len);

	n = write(vlb->video_pipe,vlb->dst, (vlb->width * vlb->height) + uv_len + uv_len );
	if(n < 0)
	{
		return -1;
	}
   }
    return 0;
}
