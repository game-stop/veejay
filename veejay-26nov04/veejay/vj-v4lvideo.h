#ifndef VJ_V4LUTILS
#define VJ_V4LUTILS
#include <config.h>
#include <stdint.h>
#include "v4lutils.h"

typedef struct {
    int brightness;
    int hue;
    int color;
    int contrast;
    int width;
    int height;
    int area;
    int palette;
    int frequency_table;
    int TVchannel;
    int tuner;
    int vloopback;
    uint8_t *framebuffer;
    v4ldevice *device;

} v4l_video;

/* allocate memory to hold v4l_video object */
v4l_video *vj_v4lvideo_alloc();

void vj_v4lvideo_free(v4l_video * v4l);

int vj_v4l_video_get_palette(v4l_video * v4l);
/* free memory in use by object */
int vj_v4l_video_dealloc(v4l_video * v4l);

/* open the video device and set channel,norm etc. */
int vj_v4lvideo_init(v4l_video * v4l, char *filename, int channel,
		     int norm, int freq, int width, int height,
		     int palette);

/* check to see if the palette is supported */
int vj_v4l_video_palette_ok(v4l_video * v4l, int palette);

/* return 0 if type matches type in proc (hardware or other)*/
int vj_v4l_video_get_proc(int match_type, char *filename);

/* set a palette */
int vj_v4l_video_set_palette(v4l_video * v4l, int palette);

/* set continuous grabbing */
int vj_v4l_video_grab_start(v4l_video * v4l);

/* stop continuous grabbing */
int vj_v4l_video_grab_stop(v4l_video * v4l);

/* wait until frame is captured */
int vj_v4l_video_sync_frame(v4l_video * v4l);

/* start capturing next frame */
int vj_v4l_video_grab_frame(v4l_video * v4l);

/* buffer is a pointer to a linear buffer */
uint8_t *vj_v4l_video_get_address(v4l_video * v4l);

/* buffer is a pointer to a 2D buffer ([0][1] and [2]) */
//int vj_v4l_video_get_planar(v4l_video *v4l, void **buffer);

int vj_v4l_video_change_size(v4l_video * v4l, int w, int h);

int vj_v4l_video_change_size(v4l_video * v4l, int w, int h);

int vj_v4l_video_set_freq(v4l_video * v4l, int v);

void vj_v4l_video_set_brightness(v4l_video * v4l, int v);

void vj_v4l_video_set_hue(v4l_video * v4l, int v);

void vj_v4l_video_set_color(v4l_video * v4l, int v);

void vj_v4l_video_set_contrast(v4l_video * v4l, int v);

int vj_v4l_video_get_norm(v4l_video * v4l, const char *name);

int vj_v4l_video_get_freq(v4l_video * v4l, const char *name);

void vj_v4l_print_info(v4l_video * v4l);
#endif
