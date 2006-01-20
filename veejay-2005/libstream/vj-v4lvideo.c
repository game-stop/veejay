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
#ifdef HAVE_V4L
#include <stdio.h>
#include <stdlib.h>
#include <libstream/vj-v4lvideo.h>
#include <libstream/v4lutils.h>
#include <veejay/vj-global.h>
#include <string.h>
#include <libvjmsg/vj-common.h>
//#include <veejay/vj-lib.h>
#include <libstream/vj-tag.h>
#include <libvjmem/vjmem.h>

#define VJ_V4L_DEBUG


v4l_video *vj_v4lvideo_alloc()
{
    v4l_video *v4l = (v4l_video *) malloc(sizeof(v4l_video));
    return v4l;
}

void vj_v4lvideo_free(v4l_video * v4l)
{
    if (v4l) {
	if (v4l->framebuffer)
	    free(v4l->framebuffer);
	//if (v4l->device)
	  //  free(v4l->device);
	free(v4l);
	v4l = NULL;
    }
}

int vj_v4lvideo_init(v4l_video * v4l, char *filename, int channel,
		     int norm, int freq, int width, int height,
		     int palette )
{

	veejay_msg(VEEJAY_MSG_WARNING,
		   "\tInput Channel\t %s",
		(channel==1 ? "Composite" : ( channel==0 ? "TV Tuner" : "S-Video") ));
    
    v4l->vloopback = 0;

    if (filename == NULL)
	    sprintf(filename, "%s", "/dev/video0");

    v4l->device = (v4ldevice *) malloc(sizeof(v4ldevice));
    if (v4l->device == NULL)
		return -1;

 	if (v4lopen(filename, v4l->device))
	    return -1;

	v4lsetdefaultnorm(v4l->device, norm);
	v4lgetcapability(v4l->device);

	if (v4lcancapture(v4l->device) != 0) {
	    veejay_msg(VEEJAY_MSG_ERROR,
		       "This device (%s) seems not to support capturing\n",
		       filename);
		return -1;
	}

	v4l->width = width;
    v4l->height = height;
    v4l->area = width * height * 3;
    v4l->device->norm = norm;
    v4l->framebuffer = (uint8_t *) vj_malloc(v4l->area * sizeof(uint8_t));

	if(!v4l->framebuffer )
		return -1;

	if (v4lmaxchannel(v4l->device)) {
	    if (v4lsetchannel(v4l->device, channel) != 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot open channel %d",channel);
			return -1;
		}
	}

	if (v4lmmap(v4l->device) != 0) {
	    veejay_msg(VEEJAY_MSG_ERROR,
		       "Mmap() not supported by driver of device [%s]\n",
		       filename);
	    return -1;
	}

	if (v4lgrabinit(v4l->device, width, height) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize grabbing!");
	    return -1;
	}

	if (v4lhasdoublebuffer(v4l->device) != 0) {
	    veejay_msg(VEEJAY_MSG_ERROR,
		       "Mmap() double buffering capture not supported by driver of device [%s]\n",
		       filename);
	    return -1;
	}
	if (vj_v4l_video_palette_ok(v4l, palette) == -1) {
	}

	v4lgetpicture(v4l->device);

	v4l->brightness = v4lgetbrightness(v4l->device);
	v4l->contrast 	= v4lgetcontrast(v4l->device);
	v4l->color 		= v4lgetcolor(v4l->device);
	v4l->hue 		= v4lgethue(v4l->device);

    v4l->palette = palette;

    return 0;
}

int vj_v4l_video_dealloc(v4l_video * v4l)
{
	v4lmunmap(v4l->device);
    v4lclose(v4l->device);
    return 1;
}

int vj_v4l_video_palette_ok(v4l_video * v4l, int palette)
{
    int ret;
    switch (palette) {
	case VIDEO_PALETTE_YUV422P:
		veejay_msg(VEEJAY_MSG_INFO, "Using video palette YUV 4:2:2\n");
		break;
    case VIDEO_PALETTE_YUV420P:
		veejay_msg(VEEJAY_MSG_INFO, "Using video palette YUV 4:2:0\n");
	break;
    default:
		veejay_msg(VEEJAY_MSG_INFO, "Unknown video palette\n");
		return -1;
    }

	if (vj_v4l_video_set_palette(v4l, palette) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot set video palette!");
	    return -1;
	}

	if (v4lgrabstart(v4l->device, 0) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot start grabbing!"); 
	    ret = -1;
	} else {
	    ret = v4lsync(v4l->device, 0);
	    if(ret !=0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot sync frame");
		return -1;
	}
	}

    return ret;
}

/* return 0 if type matches type in proc (hardware or other)*/
int vj_v4l_video_get_proc(int match_type, char *filename)
{
    FILE *proc_info;
    char buffer[255];
    char path[255];
    char *hardware, *type;
    sprintf(path, "/proc/video/dev/%s", filename);
    proc_info = fopen(path, "r");
    if (!proc_info) {
	sprintf(path, "/proc/video/%s", filename);
	proc_info = fopen(path,"r");
	if(!proc_info) {
	  sprintf(path, "/proc/video/%s",filename);
	  proc_info = fopen(path, "r");
  	  if(!proc_info) {
		veejay_msg(VEEJAY_MSG_ERROR,
		   "Is V4L information in /proc/video or in /proc/video/dev ? Did you load all necessary modules?\n");
		return -1;
	}
	}
    }



    if (!fgets(buffer, 255, proc_info)) {
	veejay_msg(VEEJAY_MSG_ERROR, "Error reading from proc!\n");
	return -1;		/* name */
    }
    if (!fgets(buffer, 255, proc_info)) {
	veejay_msg(VEEJAY_MSG_ERROR, "Error reading from proc!\n");
	return -1;		/* type */
    }
    if (!fgets(buffer, 255, proc_info)) {	/* hardware */
	veejay_msg(VEEJAY_MSG_ERROR, "Error reading from proc!\n");
    }
    buffer[strlen(buffer) - 1] = 0;
    hardware = strtok(buffer, ":");
    type = strtok(NULL, "\t");
    if (strcmp(type, " 0x1") == 0 || strcmp(type, "0x1") == 0) {
	if (match_type == VJ_TAG_TYPE_V4L) {
	    fclose(proc_info);
	    return 0;
	}
	veejay_msg(VEEJAY_MSG_INFO,
		   "Device %s is a video4linux device\n",
		   filename);
    }
    if (strcmp(type, " 0x0") == 0 || strcmp(type, "0x0") == 0) {
	if (match_type == VJ_TAG_TYPE_VLOOPBACK) {
	    fclose(proc_info);
	    return 0;
	}
	veejay_msg(VEEJAY_MSG_INFO,
		   "Device %s is a vloopback device\n",
		   filename);
    }
    fclose(proc_info);
    return -1;
}

int vj_v4l_video_get_palette(v4l_video * v4l)
{
    return v4l->palette;
}

int vj_v4l_video_set_palette(v4l_video * v4l, int palette)
{
    return v4lsetpalette(v4l->device, palette);
}

int vj_v4l_video_grab_start(v4l_video * v4l)
{
    if (v4l->vloopback == 1)
	return -1;
    return v4lsetcontinuous(v4l->device);
}

int vj_v4l_video_grab_stop(v4l_video * v4l)
{
    if (v4l->vloopback == 1)
	return -1;
    return v4lstopcontinuous(v4l->device);
}

int vj_v4l_video_sync_frame(v4l_video * v4l)
{
    if (v4l->vloopback == 1)
	return -1;
    return v4lsyncf(v4l->device);
}

int vj_v4l_video_grab_frame(v4l_video * v4l)
{
    return v4lgrabf(v4l->device);
}

uint8_t *vj_v4l_video_get_address(v4l_video * v4l)
{
    return v4lgetaddress(v4l->device);
}

int vj_vloopback_get_frame(v4l_video * v4l, uint8_t ** dst)
{
    return -1;
}

int vj_v4l_video_change_size(v4l_video * v4l, int w, int h)
{
    if (!w || !h)
	return -1;
    v4l->width = w;
    v4l->height = h;
    return v4lgrabinit(v4l->device, w, h);
}

int vj_v4l_video_set_freq(v4l_video * v4l, int v)
{
    if (v4l->tuner == 1 && v4l->frequency_table >= 0) {
	v4l->TVchannel += v;
	/*
	   while(v4l->TVchannel < 0) {
	   v4l->TVchannel += 
	   }
	 */
	//return v4lsetfreq( v4l->device, );
    }
    return 0;
}

void vj_v4l_video_set_brightness(v4l_video * v4l, int v)
{
    v4l->brightness += v;
    if (v4l->brightness < 0)
	v4l->brightness = 0;
    if (v4l->brightness > 65535)
	v4l->brightness = 65535;
    v4lsetpicture(v4l->device, v4l->brightness, -1, -1, -1, -1);
}

void vj_v4l_video_set_hue(v4l_video * v4l, int v)
{
    v4l->hue += v;
    if (v4l->hue < 0)
	v4l->hue = 0;
    if (v4l->hue > 65535)
	v4l->hue = 65535;
    v4lsetpicture(v4l->device, -1, v4l->hue, -1, -1, -1);
}

void vj_v4l_video_set_color(v4l_video * v4l, int v)
{
    v4l->color += v;
    if (v4l->color < 0)
	v4l->color = 0;
    if (v4l->color > 65535)
	v4l->color = 65535;
    v4lsetpicture(v4l->device, -1, -1, v4l->color, -1, -1);
}

void vj_v4l_video_set_contrast(v4l_video * v4l, int v)
{
    v4l->contrast += v;
    if (v4l->contrast < 0)
	v4l->contrast = 0;
    if (v4l->color > 65535)
	v4l->contrast = 65535;
    v4lsetpicture(v4l->device, -1, -1, -1, v4l->contrast, -1);
}

int vj_v4l_video_get_norm(v4l_video * v4l, const char *name)
{
    return -1;
}

int vj_v4l_video_get_freq(v4l_video * v4l, const char *name)
{
    return -1;
}

void vj_v4l_print_info(v4l_video * v4l)
{
	v4lprint(v4l->device);
}
#endif
