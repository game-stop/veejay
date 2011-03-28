/*
 * v4lutils - utility library for Video4Linux
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * v4lutils.h: header file
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

#ifndef __V4LUTILS_H__
#define __V4LUTILS_H__
#include <linux/videodev.h>
/*
 * Error message displaying level
 */
#define V4L_PERROR_NONE (0)
#define V4L_PERROR_ALL (1)

/*
 * Video4Linux Device Structure
 */
struct _v4ldevice
{
	int fd;
	struct video_capability capability;
	struct video_channel channel[10];
	struct video_picture picture;
	struct video_clip clip;
	struct video_window window;
	struct video_capture capture;
	struct video_buffer buffer;
	struct video_mmap mmap;
	struct video_mbuf mbuf;
	struct video_unit unit;
	unsigned char *map;
	pthread_mutex_t mutex;
	int frame;
	int framestat[2];
	int overlay;
};

typedef struct _v4ldevice v4ldevice;

extern int v4lopen(char *, v4ldevice *);
extern int v4lclose(v4ldevice *);
extern int v4lgetcapability(v4ldevice *);
extern int v4lsetdefaultnorm(v4ldevice *, int);
extern int v4lgetsubcapture(v4ldevice *);
extern int v4lsetsubcapture(v4ldevice *, int, int, int, int, int, int);
extern int v4lgetframebuffer(v4ldevice *);
extern int v4lsetframebuffer(v4ldevice *, void *, int, int, int, int);
extern int v4loverlaystart(v4ldevice *);
extern int v4loverlaystop(v4ldevice *);
extern int v4lsetchannel(v4ldevice *, int);
extern int v4lmaxchannel(v4ldevice *);
extern int v4lsetfreq(v4ldevice *,int);
extern int v4lsetchannelnorm(v4ldevice *vd, int, int);
extern int v4lgetpicture(v4ldevice *);
extern int v4lsetpicture(v4ldevice *, int, int, int, int, int);
extern int v4lsetpalette(v4ldevice *, int);
extern int v4lgetmbuf(v4ldevice *);
extern int v4lmmap(v4ldevice *);
extern int v4lmunmap(v4ldevice *);
extern int v4lgrabinit(v4ldevice *, int, int);
extern int v4lgrabstart(v4ldevice *, int);
extern int v4lsync(v4ldevice *, int);
extern int v4llock(v4ldevice *);
extern int v4ltrylock(v4ldevice *);
extern int v4lunlock(v4ldevice *);
extern int v4lsyncf(v4ldevice *);
extern int v4lgrabf(v4ldevice *);
extern unsigned char *v4lgetaddress(v4ldevice *);
extern int v4lreadframe(v4ldevice *, unsigned char *);
extern void v4lprint(v4ldevice *);
extern void v4lseterrorlevel(int);
extern void v4ldebug(int);
extern char *v4lgetdevicename(v4ldevice *);

#endif /* __V4LUTILS_H__ */
