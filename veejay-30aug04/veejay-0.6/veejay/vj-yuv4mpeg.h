#ifndef VJ_YUV4MPEG_H
#define VJ_YUV4MPEG_H
#include "editlist.h"
#include "mpegconsts.h"
typedef struct {
    y4m_stream_info_t streaminfo;
    y4m_frame_info_t frameinfo;
    y4m_ratio_t sar;
    y4m_ratio_t dar;
    int width;
    int height;
    int fd;
    int has_audio;
    int audio_bits;
    float video_fps;
    long audio_rate;
} vj_yuv;

vj_yuv *vj_yuv4mpeg_alloc(EditList * el);


void vj_yuv4mpeg_free(vj_yuv *v) ;

int vj_yuv_stream_start_read(vj_yuv *, char *, int width, int height);

int vj_yuv_stream_write_header(vj_yuv * yuv4mpeg, EditList * el);

int vj_yuv_stream_start_write(vj_yuv *, char *, EditList *);

void vj_yuv_stream_stop_read(vj_yuv * yuv4mpeg);

void vj_yuv_stream_stop_write(vj_yuv * yuv4mpeg);

int vj_yuv_get_frame(vj_yuv *, uint8_t **);

int vj_yuv_put_frame(vj_yuv * vjyuv, uint8_t **);

int vj_yuv_get_aframe(vj_yuv * vjyuv, uint8_t * audio);

int vj_yuv_put_aframe(uint8_t * audio, EditList * el, int len);

int vj_yuv_write_wave_header(EditList * el, char *outfile);

int vj_yuv_stream_open_pipe(vj_yuv *, char *, EditList *el);

int vj_yuv_stream_header_pipe( vj_yuv *, EditList *el );
#endif
