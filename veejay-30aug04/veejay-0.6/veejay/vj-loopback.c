


#ifndef __VLOOPBACK_UTILS_H__
#define __VLOOPBACK_UTILS_H__

#include <sys/types.h>
#include <sys/ioctl.h>

typedef struct {
    int fd;
    struct video_capability capability;
    struct video_window window;
    struct video_picture picture;
    struct video_channel channel;
} loop_v4ldevice;

int vj_lb_v4l_open(char *, loop_v4ldevice *);
int vj_lb_v4l_close(loop_v4ldevice *);
int vj_lb_v4l_mmap(loop_v4ldevice *);
int vj_lb_v4l_munmap(loop_v4ldevice *);
int vj_lb_v4l_grabinit(loop_v4ldevice *);
int vj_lb_v4l_grabstart(loop_v4ldevice *);
;
