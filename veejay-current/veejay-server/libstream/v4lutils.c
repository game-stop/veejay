/*
 * v4lutils - utility library for Video4Linux
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * v4lutils.c: utility functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#ifdef HAVE_V4L
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev.h>
#include <pthread.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#
#include <libvjmsg/vj-msg.h>
#include <libstream/v4lutils.h>
#define DEFAULT_DEVICE "/dev/video"

#define STRBUF_LENGTH 1024

static int v4l_debug = 0;
extern int errno;
static int v4lperror_level = V4L_PERROR_ALL;

/*
 * v4lperror - inhouse perror.
 *
 */

/*
 * v4lopen - open the v4l device.
 *
 * name: device file
 * vd: v4l device object
 */
int v4lopen(char *name, v4ldevice *vd)
{
	int i;

	if(name == NULL)
		name = DEFAULT_DEVICE;

//	if(v4l_debug) veejay_msg(0, "v4lopen:open...");
	if((vd->fd = open(name,O_RDWR)) < 0) {
	//	veejay_msg(0, "v4lopen: failed to open %s: %s", name, strerror(errno));
		return -1;
	}
	if(v4lgetcapability(vd))
		return -1;

//	if(v4l_debug) veejay_msg(0, "v4lopen:VIDIOCGCHAN...");
	for(i=0;i<vd->capability.channels;i++) {
		vd->channel[i].channel = i;
		if(ioctl(vd->fd, VIDIOCGCHAN, &(vd->channel[i])) < 0) {
			veejay_msg(0,"v4lopen:VIDIOCGCHAN %s", strerror(errno));
			return -1;
		}
	}
	v4lgetpicture(vd);
	pthread_mutex_init(&vd->mutex, NULL);
//	if(v4l_debug) veejay_msg(0, "v4lopen:quit");
	return 0;
}

/*
 * v4lclose - close v4l device
 *
 * vd: v4l device object
 */
int v4lclose(v4ldevice *vd)
{
//	if(v4l_debug) veejay_msg(0, "v4lclose:close...");
	close(vd->fd);
//	if(v4l_debug) veejay_msg(0, "v4lclose:quit");
	return 0;
}

/*
 * v4lgetcapability - get the capability of v4l device
 *
 * vd: v4l device object
 */
int v4lgetcapability(v4ldevice *vd)
{
//	if(v4l_debug) veejay_msg(0, "v4lgetcapability:VIDIOCGCAP...");
	if(ioctl(vd->fd, VIDIOCGCAP, &(vd->capability)) < 0) {
		veejay_msg(0, "v4lopen:VIDIOCGCAP %s", strerror(errno));
		return -1;
	}
//	if(v4l_debug) veejay_msg(0, "v4lgetcapability:quit");
	return 0;
}

/*
 * v4lsetdefaultnorm - set default norm and reset parameters
 *
 * vd: v4l device object
 * norm: default norm
 */
int v4lsetdefaultnorm(v4ldevice *vd, int norm)
{
	int i;

	for(i=0;i<vd->capability.channels;i++) {
		v4lsetchannelnorm(vd, i, norm);
	}
	if(v4lgetcapability(vd))
		return -1;
	if(v4lgetpicture(vd))
		return -1;
	return 0;
}

/*
 * v4lgetsubcapture - get current status of subfield capturing
 *
 * vd: v4l device object
 */
int v4lgetsubcapture(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCGCAPTURE, &(vd->capture)) < 0) {
		veejay_msg(0, "v4lgetsubcapture:VIDIOCGCAPTURE %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lsetsubcapture - set parameters for subfield capturing
 *
 * vd: v4l device object
 * x,y: coordinate of source rectangle to grab
 * width: width of source rectangle to grab
 * height: height of source rectangle to grab
 * decimation: decimation to apply
 * flags: flag setting for grabbing odd/even frames
 */
int v4lsetsubcapture(v4ldevice *vd, int x, int y, int width, int height, int decimation, int flags)
{
	vd->capture.x = x;
	vd->capture.y = y;
	vd->capture.width = width;
	vd->capture.height = height;
	vd->capture.decimation = decimation;
	vd->capture.flags = flags;
	if(ioctl(vd->fd, VIDIOCGCAPTURE, &(vd->capture)) < 0) {
		veejay_msg(0, "v4lsetsubcapture:VIDIOCSCAPTURE %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lgetframebuffer - get current status of frame buffer
 *
 * vd: v4l device object
 */
int v4lgetframebuffer(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCGFBUF, &(vd->buffer)) < 0) {
		veejay_msg(0, "v4lgetframebuffer:VIDIOCGFBUF %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lsetframebuffer - set parameters of frame buffer
 *
 * vd: v4l device object
 * base: base PHYSICAL address of the frame buffer
 * width: width of the frame buffer
 * height: height of the frame buffer
 * depth: color depth of the frame buffer
 * bpl: number of bytes of memory between the start of two adjacent lines
 */
int v4lsetframebuffer(v4ldevice *vd, void *base, int width, int height, int depth, int bpl)
{
	vd->buffer.base = base;
	vd->buffer.width = width;
	vd->buffer.height = height;
	vd->buffer.depth = depth;
	vd->buffer.bytesperline = bpl;
	if(ioctl(vd->fd, VIDIOCSFBUF, &(vd->buffer)) < 0) {
		veejay_msg(0, "v4lsetframebuffer:VIDIOCSFBUF %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4loverlaystart - activate overlay capturing
 *
 * vd: v4l device object
 */
int v4loverlaystart(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCCAPTURE, 1) < 0) {
		veejay_msg(0, "v4loverlaystart:VIDIOCCAPTURE %s", strerror(errno));
		return -1;
	}
	vd->overlay = 1;
	return 0;
}

/*
 * v4loverlaystop - stop overlay capturing
 *
 * vd: v4l device object
 */
int v4loverlaystop(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCCAPTURE, 0) < 0) {
		veejay_msg(0,"v4loverlaystop:VIDIOCCAPTURE %s", strerror(errno));
		return -1;
	}
	vd->overlay = 0;
	return 0;
}

/*
 * v4lsetchannel - select the video source
 *
 * vd: v4l device object
 * ch: the channel number
 */
int v4lsetchannel(v4ldevice *vd, int ch)
{
	if(ioctl(vd->fd, VIDIOCSCHAN, &(vd->channel[ch])) < 0) {
		return -1;
	}
	return 0;
}

int v4lmaxchannel(v4ldevice *vd)
{
	return vd->capability.channels;
}

/*
 * v4lsetfreq - set the frequency of tuner
 *
 * vd: v4l device object
 * ch: frequency in KHz
 */
int v4lsetfreq(v4ldevice *vd, int freq)
{
	unsigned long longfreq=(freq*16)/1000;
	if(ioctl(vd->fd, VIDIOCSFREQ, &longfreq) < 0) {
		veejay_msg(0, "v4lsetfreq:VIDIOCSFREQ %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lsetchannelnorm - set the norm of channel
 *
 * vd: v4l device object
 * ch: the channel number
 * norm: PAL/NTSC/OTHER (see videodev.h)
 */
int v4lsetchannelnorm(v4ldevice *vd, int ch, int norm)
{
	vd->channel[ch].norm = norm;
	return 0;
}

/*
 * v4lgetpicture - get current properties of the picture
 *
 * vd: v4l device object
 */
int v4lgetpicture(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCGPICT, &(vd->picture)) < 0) {
		veejay_msg(0, "v4lgetpicture:VIDIOCGPICT %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lsetpicture - set the image properties of the picture
 *
 * vd: v4l device object
 * br: picture brightness
 * hue: picture hue
 * col: picture color
 * cont: picture contrast
 * white: picture whiteness
 */
int v4lsetpicture(v4ldevice *vd, int br, int hue, int col, int cont, int white)
{
	if(br>=0)
		vd->picture.brightness = br;
	if(hue>=0)
		vd->picture.hue = hue;
	if(col>=0)
		vd->picture.colour = col;
	if(cont>=0)
		vd->picture.contrast = cont;
	if(white>=0)
		vd->picture.whiteness = white;
	if(ioctl(vd->fd, VIDIOCSPICT, &(vd->picture)) < 0) {
		veejay_msg(0, "v4lsetpicture:VIDIOCSPICT %s", strerror(errno));
		return -1;
	}
	return 0;
}
 
/*
 * v4lsetpalette - set the palette for the images
 *
 * vd: v4l device object
 * palette: palette
 */
int v4lsetpalette(v4ldevice *vd, int palette)
{
	vd->picture.palette = palette;
	vd->mmap.format = palette;
	if(ioctl(vd->fd, VIDIOCSPICT, &(vd->picture)) < 0) {
//		veejay_msg(0, "v4lsetpalette:VIDIOCSPICT %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lgetmbuf - get the size of the buffer to mmap
 *
 * vd: v4l device object
 */
int v4lgetmbuf(v4ldevice *vd)
{
	if(ioctl(vd->fd, VIDIOCGMBUF, &(vd->mbuf))<0) {
		return -1;
	}
	return 0;
}

/*
 * v4lmmap - initialize mmap interface
 *
 * vd: v4l device object
 */
int v4lmmap(v4ldevice *vd)
{
	if(v4lgetmbuf(vd)<0)
		return -1;
	if((vd->map = mmap(0, vd->mbuf.size, PROT_READ|PROT_WRITE, MAP_SHARED, vd->fd, 0)) < 0) {
		veejay_msg(0, "v4lmmap:mmap %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lmunmap - free memory area for mmap interface
 *
 * vd: v4l device object
 */
int v4lmunmap(v4ldevice *vd)
{
	if(munmap(vd->map, vd->mbuf.size) < 0) {
		veejay_msg(0,"v4lmunmap:munmap %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * v4lgrabinit - set parameters for mmap interface
 *
 * vd: v4l device object
 * width: width of the buffer
 * height: height of the buffer
 */
int v4lgrabinit(v4ldevice *vd, int width, int height)
{
	vd->mmap.width = width;
	vd->mmap.height = height;	
	vd->mmap.format = vd->picture.palette;
	vd->frame = 0;
	vd->framestat[0] = 0;
	vd->framestat[1] = 0;
	return 0;
}

/*
 * v4lgrabstart - activate mmap capturing
 *
 * vd: v4l device object
 * frame: frame number for storing captured image
 */
int v4lgrabstart(v4ldevice *vd, int frame)
{
//	if(v4l_debug)
//		veejay_msg(VEEJAY_MSG_DEBUG, "v4lgrabstart: grab frame %d.",frame);
	if(vd->framestat[frame]) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4lgrabstart: frame %d is already used to grab.", frame);
	}
	vd->mmap.frame = frame;
	if(ioctl(vd->fd, VIDIOCMCAPTURE, &(vd->mmap)) < 0) {
		veejay_msg(VEEJAY_MSG_DEBUG,"v4lgrabstart:VIDIOCMCAPTURE %s", strerror(errno));
		return -1;
	}
	vd->framestat[frame] = 1;
	return 0;
}

/*
 * v4lsync - wait until mmap capturing of the frame is finished
 *
 * vd: v4l device object
 * frame: frame number
 */
int v4lsync(v4ldevice *vd, int frame)
{
//	if(v4l_debug) veejay_msg(VEEJAY_MSG_DEBUG, "v4lsync: sync frame %d.",frame);
	if(vd->framestat[frame] == 0) {
		veejay_msg(VEEJAY_MSG_DEBUG, "v4lsync: grabbing to frame %d is not started.", frame);
	}
	if(ioctl(vd->fd, VIDIOCSYNC, &frame) < 0) {
		veejay_msg(0,"v4lsync:VIDIOCSYNC %s", strerror(errno));
		return -1;
	}
	vd->framestat[frame] = 0;
	return 0;
}

/*
 * v4llock - lock the Video4Linux device object
 *
 * vd: v4l device object
 */
int v4llock(v4ldevice *vd)
{
	return pthread_mutex_lock(&vd->mutex);
}

/*
 * v4lunlock - unlock the Video4Linux device object
 *
 * vd: v4l device object
 */
int v4lunlock(v4ldevice *vd)
{
	return pthread_mutex_unlock(&vd->mutex);
}

/*
 * v4ltrylock - lock the Video4Linux device object (non-blocking mode)
 *
 * vd: v4l device object
 */
int v4ltrylock(v4ldevice *vd)
{
	return pthread_mutex_trylock(&vd->mutex);
}

/*
 * v4lsyncf - flip-flop sync
 *
 * vd: v4l device object
 */
int v4lsyncf(v4ldevice *vd)
{
	return v4lsync(vd, vd->frame);
}

/*
 * v4lgrabf - flip-flop grabbing
 *
 * vd: v4l device object
 */
int v4lgrabf(v4ldevice *vd)
{
	int f;

	f = vd->frame;
	vd->frame = vd->frame ^ 1;
	return v4lgrabstart(vd, f);
}

/*
 * v4lgetaddress - returns a offset addres of buffer for mmap capturing
 *
 * vd: v4l device object
 */
unsigned char *v4lgetaddress(v4ldevice *vd)
{
	return (vd->map + vd->mbuf.offsets[vd->frame]);
}

/*
 * v4lreadframe - grab one frame by calling read system call
 * vd: v4l device object
 * buf: buffer where a grabbed imaged is stored
 */

int v4lreadframe(v4ldevice *vd, unsigned char *buf)
{
	/* to do */
	return -1;
}

char	*v4lgetdevicename( v4ldevice *vd ) {
	return vd->capability.name;
}


/*
 * v4lprint - print v4l device object
 *
 * vd: v4l device object
 */
void v4lprint(v4ldevice *vd)
{
	veejay_msg(VEEJAY_MSG_INFO,"Capture device: %s in %dx%d - %x",vd->capability.name,
		vd->mmap.width,vd->mmap.height,vd->picture.palette);
	veejay_msg(VEEJAY_MSG_DEBUG,"Video channels: %d",vd->capability.channels);
	veejay_msg(VEEJAY_MSG_DEBUG,"Max resolution: %dx%d",vd->capability.maxwidth,vd->capability.maxheight);
	veejay_msg(VEEJAY_MSG_DEBUG,"Min resolution: %dx%d",vd->capability.minwidth, vd->capability.minheight);
	veejay_msg(VEEJAY_MSG_DEBUG,"Device type:");
	if(vd->capability.type & VID_TYPE_CAPTURE) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_CAPTURE,");
	if(vd->capability.type & VID_TYPE_OVERLAY) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_OVERLAY,");
	if(vd->capability.type & VID_TYPE_CLIPPING) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_CLIPPING,");
	if(vd->capability.type & VID_TYPE_FRAMERAM) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_FRAMERAM,");
	if(vd->capability.type & VID_TYPE_SCALES) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_SCALES,");
	if(vd->capability.type & VID_TYPE_MONOCHROME) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_MONOCHROME,");
	if(vd->capability.type & VID_TYPE_SUBCAPTURE) veejay_msg(VEEJAY_MSG_DEBUG,"VID_TYPE_SUBCAPTURE,");
	veejay_msg(VEEJAY_MSG_DEBUG,"Device Status:");
	veejay_msg(VEEJAY_MSG_DEBUG,"\tDepth: %d",vd->picture.depth);
	veejay_msg(VEEJAY_MSG_DEBUG,"\tPalette:%x",vd->picture.palette);
	veejay_msg(VEEJAY_MSG_DEBUG,"\tWidth:%d",vd->mmap.width);
	veejay_msg(VEEJAY_MSG_DEBUG,"\tHeight:%d",vd->mmap.height);
	veejay_msg(VEEJAY_MSG_DEBUG,"\tNorm: %s", v4lvideo_templ_get_norm_str( 	vd->channel[0].norm ));
	veejay_msg(VEEJAY_MSG_DEBUG,"\tBuffer size: %08x",vd->mbuf.size);
	veejay_msg(VEEJAY_MSG_DEBUG,"\tNum buffers: %d",vd->mbuf.frames);
	veejay_msg(VEEJAY_MSG_DEBUG,"\t  offsets[0]: %08x",vd->mbuf.offsets[0]);
	veejay_msg(VEEJAY_MSG_DEBUG,"\t  offsets[1]: %08x",vd->mbuf.offsets[1]);
}

/*
 * v4lseterrorlevel - enable/disable perror message output
 *
 * flag: V4L_PERROR_NONE or V4L_PERROR_ALL(default)
 */
void v4lseterrorlevel(int flag)
{
	v4lperror_level = flag;
}

/*
 * v4ldebug - enable/disable debug message output
 *
 * flag: 0 = disable / 1 = enable
 */
void v4ldebug(int flag)
{
	veejay_msg(0, "debug: %d",flag);
	v4l_debug = flag;
}
#endif
