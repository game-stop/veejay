/*
 * v4lutils - utility library for Video4Linux
 * Copyright (C) 2001 FUKUCHI Kentarou
 *
 * v4lutils.h: header file
 *
 */

#ifndef __V4LUTILS_H__
#define __V4LUTILS_H__

#include <sys/types.h>
#include <stdint.h>
#include <linux/videodev.h>
#include <pthread.h>

/*
 * Error message displaying level
 */
#define V4L_PERROR_NONE (0)
#define V4L_PERROR_ALL (1)
//#define V4L_DEBUG 0
/*
 * Video4Linux Device Structure
 */


#define DEFAULT_VIDEO_DEVICE "/dev/video"

typedef struct {
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
    int norm;
    int frame;
    int framestat[2];
    int overlay;
    int preferred_palette;
} v4ldevice;



int v4lopen(char *, v4ldevice *);
int v4lopenvloopback(char *name, v4ldevice * vd, int palette);
int v4lclose(v4ldevice *);
int v4lgetcapability(v4ldevice *);
int v4lsetdefaultnorm(v4ldevice *, int);
int v4lgetsubcapture(v4ldevice *);
int v4lsetsubcapture(v4ldevice *, int, int, int, int, int, int);
int v4lgetframebuffer(v4ldevice *);
int v4lsetframebuffer(v4ldevice *, void *, int, int, int, int);
int v4loverlaystart(v4ldevice *);
int v4loverlaystop(v4ldevice *);
int v4lsetchannel(v4ldevice *, int);
int v4lmaxchannel(v4ldevice *);
int v4lcancapture(v4ldevice *);
int v4lhastuner(v4ldevice *);
int v4lhasdoublebuffer(v4ldevice *);
int v4lgetbrightness(v4ldevice *);
int v4lgethue(v4ldevice *);
int v4lgetcolor(v4ldevice *);
int v4lgetcontrast(v4ldevice *);
int v4lsetcontinuous(v4ldevice *);
int v4lstopcontinuous(v4ldevice *);
int v4lsetfreq(v4ldevice *, int);
int v4lsetchannelnorm(v4ldevice * vd, int, int);
int v4lgetpicture(v4ldevice *);
int v4lsetpicture(v4ldevice *, int, int, int, int, int);
int v4lsetpalette(v4ldevice *, int);
int v4lgetmbuf(v4ldevice *);
int v4lmmap(v4ldevice *);
int v4lmunmap(v4ldevice *);
int v4lgrabinit(v4ldevice *, int, int);
int v4lgrabstart(v4ldevice *, int);
int v4lsync(v4ldevice *, int);
int v4llock(v4ldevice *);
int v4ltrylock(v4ldevice *);
int v4lunlock(v4ldevice *);
int v4lsyncf(v4ldevice *);
int v4lgrabf(v4ldevice *);
uint8_t *v4lgetaddress(v4ldevice *);
int v4lreadframe(v4ldevice *, uint8_t *, int, int);
void v4lprint(v4ldevice *);

#endif				/* __V4LUTILS_H__ */
