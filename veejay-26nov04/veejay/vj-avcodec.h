#ifndef VJ_AVCODEC_H
#define VJ_AVCODEC_H
#include "avcodec.h"
#include "vj-el.h"

#define ENCODER_MJPEG 0
#define ENCODER_DVVIDEO 1
#define ENCODER_DIVX 2
#define ENCODER_MPEG4 3
#define ENCODER_YUV420 4
#define ENCODER_YUV422 5
#define NUM_ENCODERS 6

typedef struct
{
	AVCodec *codec;
	AVCodec *audiocodec;
	AVFrame *frame;
	AVCodecContext	*context;
	int fmt;
	int uv_len;
	int len;
	int sub_sample;	
	int super_sample;
	int width;
	int height;
} vj_encoder;

int		vj_avcodec_init(editlist *el, int pix);

int		vj_avcodec_encode_frame( int format, uint8_t *src[3], uint8_t *dst, int dst_len);


/* color space conversion routines, should go somewhere else someday
   together with subsample.c/colorspace.c into some lib
 */


// from yuv 4:2:2 packed to yuv 4:2:0 planar
void 	yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input, int w, int h);

// from yuv 4:2:2 planar to yuv 4:2:2 packed
void 	yuv422p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int w, int h);

// from yuv 4:2:0 planar to yuv 4:2:2 packed
void 	yuv420p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int w, int h );

// from yuv 4:2:2 planar to yuv 4:2:0 planar
int	yuv422p_to_yuv420p( uint8_t *src[3], uint8_t *dst, int w, int h);

// from yuv 4:2:0 planar to yuv 4:2:2 planar
int	yuv420p_to_yuv422p( uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *dst[3], int w, int h );

// from yuv 4:2:0 planar to YUYV
void yuv22_to_yuyv( uint8_t *yuv422[3], uint8_t *pixels, int w, int h );

#endif
