#ifndef VJ_VLOOPBACK_H
#define VJ_VLOOPBACK_H

#define VLOOPBACK_HAS_READER 0
#define VLOOPBACK_FREE_PIPE 1
#define VLOOPBACK_USED_PIPE 3
#define VLOOPBACK_AVAIL_OUTPUT 2
#define VLOOPBACK_NONE 4 

typedef struct {
    int video_pipe;
    char pipepath[255];
    int mode;
    struct video_capability vid_caps;
    struct video_window vid_win;
    struct video_picture vid_pic;
    struct video_channel channel;
    int width;
    int height;
    uint8_t *dst;
    uint8_t *rgb24;
    int pipe_id;
    int preferred_palette;
} vj_vlb;

/* initialize containers */
vj_vlb *vj_vloopback_alloc();
int vj_vloopback_verify_pipe(int mode, int pipe_id, char *filename);
void vj_vloopback_print_status();
int vj_vloopback_read_proc(vj_vlb * vlb, int mode, int pipe_id);
int vj_vloopback_open(vj_vlb * vlb, int width, int height, int norm);
int vj_vloopback_close(vj_vlb * vlb);
int vj_vloopback_read(vj_vlb * vlb, uint8_t ** frame);
int vj_vloopback_write(vj_vlb * vlb, uint8_t ** frame);
int vj_vloopback_get_status(char *filename);
#endif
