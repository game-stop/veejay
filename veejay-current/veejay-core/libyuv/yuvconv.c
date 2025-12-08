/* veejay - Linux VeeJay
 *       (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvjmem/vjmem.h>
#include <veejaycore/defs.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include <libavutil/pixfmt.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <veejaycore/vj-task.h>
#include <libyuv/mmx_macros.h>
#include <veejaycore/avcommon.h>
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#if defined(HAVE_ASM_SSE2) || defined(HAVE_ASM_SSE4_1) || defined(HAVE_ASM_AVX) || defined(HAVE_ASM_AVX2)
#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSSE3 (for _mm_srai_epi16)
#include <smmintrin.h> // SSE4.1
#include <immintrin.h> // AVX
#define Q_SHIFT 15
#define Q_MAX 32767
#endif

#define Y4M_CHROMA_420JPEG     0  /* 4:2:0, H/V centered, for JPEG/MPEG-1 */
#define Y4M_CHROMA_420MPEG2    1  /* 4:2:0, H cosited, for MPEG-2         */
#define Y4M_CHROMA_420PALDV    2  /* 4:2:0, alternating Cb/Cr, for PAL-DV */
#define Y4M_CHROMA_444         3  /* 4:4:4, no subsampling, phew.         */
#define Y4M_CHROMA_422         4  /* 4:2:2, H cosited                     */
#define Y4M_CHROMA_411         5  /* 4:1:1, H cosited                     */
#define Y4M_CHROMA_MONO        6  /* luma plane only                      */
#define Y4M_CHROMA_444ALPHA    7  /* 4:4:4 with an alpha channel          */

#define ru4(num)  (((num)+3)&~3)
/* this routine is the same as frame_YUV422_to_YUV420P , unpack
 * libdv's 4:2:2-packed into 4:2:0 planar 
 * See http://mjpeg.sourceforge.net/ (MJPEG Tools) (lav-common.c)
 */

typedef struct
{
    struct SwsContext *sws;
    SwsFilter     *src_filter;
    SwsFilter     *dst_filter;
    int swscale_flags;
    int     format; 
    int width;
    int height;
} vj_sws;

static  int         sws_context_flags_ = 0;
static  int         full_range_pixel_value_ = 0;

static struct {
    int i;
    const char *s;
} pixstr[] = {
    {PIX_FMT_YUV420P, "PIX_FMT_YUV420P"},
{   PIX_FMT_YUV422P, "PIX_FMT_YUV422P"},
{   PIX_FMT_YUVJ420P, "PIX_FMT_YUVJ420P"},
{   PIX_FMT_YUVJ422P, "PIX_FMT_YUVJ422P"},
{   PIX_FMT_RGB24,    "PIX_FMT_RGB24"},
{   PIX_FMT_BGR24,    "PIX_FMT_BGR24"},
{   PIX_FMT_YUV444P,  "PIX_FMT_YUV444P"},
{   PIX_FMT_YUVJ444P, "PIX_FMT_YUVJ444P"},
{   PIX_FMT_YUVA422P, "PIX_FMT_YUVA422P"},
{   PIX_FMT_YUVA420P, "PIX_FMT_YUVA420P"},
{   PIX_FMT_YUVA444P, "PIX_FMT_YUVA444P"},
{   PIX_FMT_RGB32,      "PIX_FMT_RGB32"},
{   PIX_FMT_BGR32,      "PIX_FMT_BGR32"},
{   PIX_FMT_GRAY8,      "PIX_FMT_GRAY8"},
{   PIX_FMT_RGB32_1,    "PIX_FMT_RGB32_1"},
{   PIX_FMT_YUYV422,    "PIX_FMT_YUYV422"},
{   PIX_FMT_UYVY422,    "PIX_FMT_UYVY422"},
{   PIX_FMT_RGBA,       "PIX_FMT_RGBA"},
{   PIX_FMT_BGRA,       "PIX_FMT_BGRA"},
{   PIX_FMT_ARGB,       "PIX_FMT_ARGB"},
{   PIX_FMT_ABGR,       "PIX_FMT_ABGR"},
{   0   ,       NULL}
};


const char  *yuv_get_pixfmt_description(int fmt)
{
    int i;
    for( i = 0; pixstr[i].s != NULL ; i ++ )
        if( fmt == pixstr[i].i ) 
            return pixstr[i].s;
    return "NONE";
}

static  float   jpeg_to_CCIR_tableY[256];
static  float   CCIR_to_jpeg_tableY[256];
static  float   jpeg_to_CCIR_tableUV[256];
static  float   CCIR_to_jpeg_tableUV[256];
#define round1(x) ( (int32_t)( (x>0) ? (x) + 0.5 : (x)  - 0.5 ))
#define _CLAMP(a,min,max) ( round1(a) < min ? min : ( round1(a) > max ? max : round1(a) ))

static struct {
    int id;
} ccir_pixfmts[] =
{
    { PIX_FMT_YUV420P },
    { PIX_FMT_YUYV422 },
    { PIX_FMT_YUV422P },
    { PIX_FMT_YUV444P },
    { PIX_FMT_YUVA420P},
    { PIX_FMT_YUVA422P},
    { -1 }
};
static struct {
    int id;
} jpeg_pixfmts[] = 
{
    { PIX_FMT_YUVJ420P },
    { PIX_FMT_YUVJ422P },
    { PIX_FMT_YUVJ444P },
    { -1           },
};

static  int auto_conversion_ccir_jpeg_ = 0;

static int  is_CCIR(int a) {
    int i;
    for( i = 0; ccir_pixfmts[i].id != -1; i ++ )
        if( a == ccir_pixfmts[i].id )
            return 1;
    return 0;
}
static int  is_JPEG(int a) {
    int i;
    for( i = 0; jpeg_pixfmts[i].id != -1 ; i ++ )
        if( a == jpeg_pixfmts[i].id )
            return 1;
    return 0;
}

void    verify_CCIR_auto(int a, int b, VJFrame *dst )
{
    int a_is_CCIR = is_CCIR(a);
    int a_is_JPEG = is_JPEG(a);

    int b_is_CCIR = is_CCIR(b);
    int b_is_JPEG = is_JPEG(b);

    if( a_is_JPEG && b_is_CCIR ) {
        yuv_scale_pixels_from_y( dst->data[0], dst->len );
        yuv_scale_pixels_from_uv( dst->data[1], dst->uv_len );
        yuv_scale_pixels_from_uv( dst->data[2], dst->uv_len );
    }
    else if( a_is_CCIR && b_is_JPEG ) {
        yuv_scale_pixels_from_ycbcr( dst->data[0], 16.0f, 235.0f, dst->len );
        yuv_scale_pixels_from_ycbcr( dst->data[1], 16.0f, 240.0f, dst->uv_len );
        yuv_scale_pixels_from_ycbcr( dst->data[2], 16.0f, 240.0f, dst->uv_len );
    }
}

int yuv_use_auto_ccir_jpeg()
{
    return auto_conversion_ccir_jpeg_;
}

int get_chroma_from_pixfmt(int pixfmt) {
    int chroma;
    switch(pixfmt) {
        case PIX_FMT_YUVJ420P: chroma = Y4M_CHROMA_420JPEG; break;
        case PIX_FMT_YUV420P: chroma = Y4M_CHROMA_420MPEG2; break;
        case PIX_FMT_YUV422P: chroma = Y4M_CHROMA_422; break;
        case PIX_FMT_YUV444P: chroma = Y4M_CHROMA_444; break;
        case PIX_FMT_YUVJ422P: chroma = Y4M_CHROMA_422; break; 
        case PIX_FMT_YUVJ444P: chroma = Y4M_CHROMA_444; break;
        case PIX_FMT_YUV411P: chroma = Y4M_CHROMA_411; break;
        case PIX_FMT_YUVA420P: chroma = Y4M_CHROMA_420JPEG; break;
        case PIX_FMT_YUVA422P: chroma = Y4M_CHROMA_422; break;
        case PIX_FMT_YUVA444P: chroma = Y4M_CHROMA_444; break;
        case PIX_FMT_GRAY8: chroma = PIX_FMT_GRAY8; break;
        default:
            chroma = Y4M_CHROMA_444;
            break;
    }
    return chroma;
}

int get_pixfmt_from_chroma(int chroma) {
    int src_fmt = 0;
    switch( chroma ) {
        case Y4M_CHROMA_420JPEG: 
                src_fmt = PIX_FMT_YUVJ420P; break;
        case Y4M_CHROMA_420MPEG2:
        case Y4M_CHROMA_420PALDV:
                src_fmt = PIX_FMT_YUV420P; break;
        case Y4M_CHROMA_422:
                src_fmt = PIX_FMT_YUV422P; break;
        case Y4M_CHROMA_444:
                src_fmt = PIX_FMT_YUV444P; break;
        case Y4M_CHROMA_411:
                src_fmt = PIX_FMT_YUV411P; break;
        case Y4M_CHROMA_MONO:
                src_fmt = PIX_FMT_GRAY8; break;
        default:
        break;
    }
    return src_fmt;
}

int vj_to_pixfmt(int fmt) {
    int pixfmt;
    switch(fmt) {
        case FMT_420: pixfmt = PIX_FMT_YUV420P; break;
        case FMT_420F: pixfmt = PIX_FMT_YUVJ420P; break;
        case FMT_422: pixfmt = PIX_FMT_YUV422P; break;
        case FMT_422F: pixfmt = PIX_FMT_YUVJ422P; break;
        case FMT_444: pixfmt = PIX_FMT_YUV444P;break;
        default:
            pixfmt = -1;
            break;
    }
    return pixfmt;
}

int pixfmt_to_vj(int pixfmt) {
    int fmt;
    switch(pixfmt) {
        case PIX_FMT_YUV420P: fmt = FMT_420; break;
        case PIX_FMT_YUVJ420P: fmt = FMT_420F; break;
        case PIX_FMT_YUVJ422P: fmt = FMT_422F; break;
        case PIX_FMT_YUV422P: fmt = FMT_422; break;
        default: fmt = -1; break;
    }
    return fmt;
}

int vj_is_full_range(int fmt) {
    return ( fmt == FMT_420F || fmt == FMT_422F ) ? 1: 0;
}

int pixfmt_is_full_range(int pixfmt) {
    return ( pixfmt == PIX_FMT_YUVJ420P || pixfmt == PIX_FMT_YUVJ422P || pixfmt == PIX_FMT_YUVJ444P ) ? 1:0;
}

static int  global_scaler_ = SWS_FAST_BILINEAR;
static int  full_chroma_interpolation_ = 0;
int yuv_which_scaler()
{
    return global_scaler_;
}

void    yuv_init_lib(int extra_flags, int auto_ccir_jpeg, int default_zoomer)
{
    sws_context_flags_ = yuv_sws_get_cpu_flags();
    if(extra_flags) {
        full_chroma_interpolation_ = 1;
        veejay_msg(VEEJAY_MSG_WARNING,
                "Interpolating full chroma in converter/scaler");
    }
    if( default_zoomer ) {
        if( default_zoomer == 1 ) {
            global_scaler_ = SWS_FAST_BILINEAR;
        } else if (default_zoomer == 2 ) {
            global_scaler_ = SWS_BICUBIC;
        }
    }

    auto_conversion_ccir_jpeg_ = auto_ccir_jpeg;
    if( auto_conversion_ccir_jpeg_ ) {
        veejay_msg(VEEJAY_MSG_WARNING,
                "On-the-fly conversion between CCIR 601 and JPEG color range!");
        auto_conversion_ccir_jpeg_ = 1;
    }

    // initialize tables for jpeg <-> ccir conversion
    veejay_memset( jpeg_to_CCIR_tableY, 0, sizeof( jpeg_to_CCIR_tableY ) );
    veejay_memset( CCIR_to_jpeg_tableY, 0, sizeof( CCIR_to_jpeg_tableY ) );
    veejay_memset( jpeg_to_CCIR_tableUV, 0, sizeof( jpeg_to_CCIR_tableUV ) );
    veejay_memset( CCIR_to_jpeg_tableUV, 0, sizeof( CCIR_to_jpeg_tableUV ) );

    unsigned int i;
    float    s = (235.0f - 16.0f) / 255.0f;
    float    u = (240.0f - 16.0f) / 255.0f;
    float    c = 255.0f / ( 235.0f-16.0f );
    float    d = 255.0f / ( 240.0f-16.0f );

    for( i = 0; i < 256 ; i ++ ) {
        jpeg_to_CCIR_tableY[i] = _CLAMP( (float)i * s + 16.0f , 16.0f, 235.0f );
        jpeg_to_CCIR_tableUV[i]= _CLAMP( (float)i * u + 16.0f , 16.0f, 240.0f );
        CCIR_to_jpeg_tableY[i] = _CLAMP( (float)i * c - 16.0f ,  0.0f, 255.0f );
        CCIR_to_jpeg_tableUV[i]= _CLAMP( (float)i * d - 16.0f ,  0.0f, 255.0f );
    }
}

void    yuv_plane_sizes( VJFrame *src, int *p1, int *p2, int *p3, int *p4 )
{

    switch(src->format) {
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUVJ420P:
            *p1 = src->len;
            *p2 = src->len / 4;
            *p3 = src->len / 4;
            *p4 = 0;
            break;
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUVJ422P:
        case PIX_FMT_YUVJ444P:
        case PIX_FMT_YUV444P:
            
            if(p1 != NULL) {
                *p1 = src->len;
            }
            if(p2 != NULL) {
                *p2 = src->uv_len;
            }
            if(p3 != NULL) {
                *p3 = src->uv_len;
            }

            if(p4 != NULL) {
                *p4 = 0;
            }
            break;
        case PIX_FMT_YUVA420P:
        case PIX_FMT_YUVA422P:
            if(p1 != NULL) {
                *p1 = src->len;
            }
            if(p2 != NULL) {
                *p2 = src->uv_len;
            }
            if(p3 != NULL) {
                *p3 = src->uv_len;
            }
            if(p4 != NULL) {
                *p4 = src->len;
            }
            break;
        case PIX_FMT_YUVA444P:
            if(p1 != NULL) {
                *p1 = src->len;
            }
            if(p2 != NULL) {
                *p2 = src->len;
            }
            if(p3 != NULL) {
                *p3 = src->len;
            }
            if(p4 != NULL) {
                *p4 = src->len;
            }
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            if( p1 != NULL ) 
                *p1 = src->len * 3;
            *p2 = 0;
            *p3 = 0;
            *p4 = 0;
            break;
        case PIX_FMT_RGBA:
        case PIX_FMT_BGRA:
        case PIX_FMT_ARGB:
        case PIX_FMT_ABGR:
            if( p1 != NULL )
                *p1 = src->len * 4;
            *p2 = 0;
            *p3 = 0;
            *p4 = 0;
            break;
        case PIX_FMT_YUYV422:
            if( p1 != NULL )
                *p1 = src->len * 2;
            *p2 = 0;
            *p3 = 0;
            *p4 = 0;
            break;
        default:    
            if(p1 != NULL) {
                *p1 = src->len;
            }
            if(p2 != NULL) {
                *p2 = 0;
            }
            if(p3 != NULL) {
                *p3 = 0;
            }

            if(p4 != NULL) {
                *p4 = 0;
            }

            break;
    }
}

void            yuv_set_pixel_range(int full_range)
{
    full_range_pixel_value_ = full_range;
}

int             yuv_get_pixel_range()
{
    return full_range_pixel_value_;
}

int         alpha_fmt_to_yuv(int fmt)
{
    switch(fmt) {
        case PIX_FMT_YUVA422P: 
            return (full_range_pixel_value_ ? PIX_FMT_YUVJ422P: PIX_FMT_YUV422P ); break;
        case PIX_FMT_YUVA420P:
            return (full_range_pixel_value_ ? PIX_FMT_YUVJ420P: PIX_FMT_YUV420P ); break;
        case PIX_FMT_YUVA444P:
            return (full_range_pixel_value_ ? PIX_FMT_YUVJ444P: PIX_FMT_YUV444P ); break;

    }
    return fmt;
}   

int         yuv_to_alpha_fmt(int fmt)
{
    switch(fmt) {
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUVJ422P:
            return PIX_FMT_YUVA422P;
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUVJ420P:
            return PIX_FMT_YUVA420P;
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUVJ444P:
            return PIX_FMT_YUVA444P;
    }
    return  (full_range_pixel_value_ ? PIX_FMT_YUVJ444P: PIX_FMT_YUV444P );
}


VJFrame *yuv_yuv_template( uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int fmt )
{
    VJFrame *f = (VJFrame*) vj_calloc(sizeof(VJFrame));
    f->format = fmt;
    f->data[0] = Y;
    f->data[1] = U;
    f->data[2] = V;
    f->width   = w;
    f->height  = h;
    f->out_width = w;
    f->out_height = h;
    switch(fmt)
    {
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUVJ422P:
            f->uv_width = w>>1;
            f->uv_height= f->height;        
            f->stride[0] = w;
            f->stride[1] = w>>1;
            f->stride[2] = w>>1;
            f->shift_h = 1;
            f->yuv_fmt = PIX_FMT_YUV422P;
            f->range = (fmt == PIX_FMT_YUVJ422P ? 1 : 0 );
            break;
        case PIX_FMT_YUVA422P:
            f->uv_width = w>>1;
            f->uv_height=f->height;
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = w>>1;
            f->stride[3] = w;
            f->shift_h = 1;
            f->yuv_fmt = PIX_FMT_YUVA422P;
            f->range = full_range_pixel_value_;
            break;
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUVJ444P:
            f->uv_width = w;
            f->uv_height=f->height;
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = f->stride[0];
            f->yuv_fmt = PIX_FMT_YUV444P;
            f->range = (fmt == PIX_FMT_YUVJ444P ? 1 : 0 );
            break;
        case PIX_FMT_YUVA444P:
            f->uv_width = w;
            f->uv_height=f->height;
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = f->stride[0];
            f->stride[3] = w;
            f->yuv_fmt = PIX_FMT_YUVA444P;
            f->range = full_range_pixel_value_;
            break;
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUVJ420P:
            f->uv_width = w>>1;
            f->uv_height=f->height>>1;
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = f->stride[0]>>1;
            f->shift_v = 1;
            f->shift_h = 1;
            f->yuv_fmt = PIX_FMT_YUV420P;
            f->range = (fmt == PIX_FMT_YUVJ420P ? 1 : 0 );
            break;
        case PIX_FMT_YUVA420P:
            f->uv_width = w>>1;
            f->uv_height=f->height>>1;
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = f->stride[0]>>1;
            f->stride[3] = f->stride[0];
            f->shift_v = 1;
            f->shift_h = 1;
            f->yuv_fmt = PIX_FMT_YUVA420P;
            f->range = full_range_pixel_value_;
            break;
        case PIX_FMT_GRAY8:
            f->stride[0] = w;
            f->stride[1] = f->stride[2] = 0;
            f->yuv_fmt = fmt;
            f->range = full_range_pixel_value_;
            break;
        case PIX_FMT_YUYV422:
        case PIX_FMT_UYVY422:
            f->stride[0] = w * 2;
            f->stride[1] = f->stride[2] = 0;
            f->yuv_fmt = fmt;
            f->range = full_range_pixel_value_;
            break;
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
            f->stride[0] = w * 3;
            f->uv_width = 0; f->uv_height=0;
            f->data[1] = NULL;f->data[2] = NULL;
            f->yuv_fmt = fmt;
            break;
        case PIX_FMT_BGR32:
        case PIX_FMT_RGB32:
        case PIX_FMT_ARGB:
        case PIX_FMT_ABGR:
            f->stride[0] = w * 4;
            f->uv_width = 0; f->uv_height = 0;
            f->data[1] = NULL; f->data[2] = NULL;
            f->yuv_fmt = fmt;
            break;
        default:
        break;
    }
    f->len = w*h;   
    f->uv_len = f->uv_width*f->uv_height;

    return f;
}

VJFrame *yuv_rgb_template( uint8_t *rgb_buffer, int w, int h, int fmt )
{
    VJFrame *f = (VJFrame*) vj_calloc(sizeof(VJFrame));
    f->format = fmt;
    f->data[0] = rgb_buffer;
    f->data[1] = NULL;
    f->data[2] = NULL;
    f->data[3] = NULL;
    f->width   = w;
    f->height  = h;
    f->out_width = w;
    f->out_height = h;
    switch( fmt )
    {
        case PIX_FMT_RGB24:
        case PIX_FMT_BGR24:
                f->stride[0] = w * 3;
        break;
        default:
                f->stride[0] = w * 4;
        break;
    }
    f->stride[1] = 0;
    f->stride[2] = 0;
    f->len = w * h;
    f->yuv_fmt = fmt;
    return f;
}

void    yuv_convert_any_ac( VJFrame *src, VJFrame *dst )
{
    void *ctx = yuv_fx_context_create( src, dst );
    yuv_fx_context_process( ctx, src,dst );
    yuv_fx_context_destroy( ctx );
}

void    *yuv_fx_context_create( VJFrame *src, VJFrame *dst )
{
    struct SwsContext *ctx = sws_getContext( src->width,src->height, src->yuv_fmt, dst->width,dst->height,dst->yuv_fmt,
            sws_context_flags_, NULL,NULL,NULL );
    return (void*) ctx;
}

void    yuv_fx_context_process( void *ctx, VJFrame *src, VJFrame *dst )
{
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( src->data[1] );
    check_desired_alignment( src->data[2] );
    check_desired_alignment( dst->data[0] );
    check_desired_alignment( dst->data[1] );
    check_desired_alignment( dst->data[2] );
#endif
    sws_scale( (struct SwsContext*) ctx,(const uint8_t * const*) src->data, src->stride,0,src->height,(uint8_t * const*) dst->data,dst->stride );
}

void    yuv_fx_context_destroy( void *ctx )
{
    struct SwsContext *stx = (struct SwsContext*) ctx;
    sws_freeContext( stx );
}


void    yuv_convert_any3( void *scaler, VJFrame *src, int src_stride[4], VJFrame *dst)
{
    vj_sws *s = (vj_sws*) scaler;
    int dst_stride[4] = { ru4(dst->stride[0]),ru4(dst->stride[1]),ru4(dst->stride[2]), ru4(dst->stride[3]) };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( src->data[1] );
    check_desired_alignment( src->data[2] );
    check_desired_alignment( dst->data[0] );
    check_desired_alignment( dst->data[1] );
    check_desired_alignment( dst->data[2] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data, src_stride, 0, src->height,(uint8_t * const*) dst->data, dst_stride);
}   


/* convert 4:2:0 to yuv 4:2:2 packed */
void yuv422p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
               int height)
{
    unsigned int x, y;
    uint8_t *Cb = yuv420[1];
    uint8_t *Cr = yuv420[2];
    uint8_t *Y  = yuv420[0];
#pragma omp simd
    for (y = 0; y < height; ++y) {
    for (x = 0; x < width; x +=2) {
        *(dest + 0) = Y[0];
        *(dest + 1) = Cb[0];
        *(dest + 2) = Y[1];
        *(dest + 3) = Cr[0];
        dest += 4;
        Y += 2;
        ++Cb;
        ++Cr;
    }
    Y += width;
    Cb += (width>>1);
    Cr += (height>>1);
    }
}



/* convert 4:2:0 to yuv 4:2:2 */
void yuv420p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
               int height)
{
    unsigned int x, y;
#pragma omp simd
    for (y = 0; y < height; ++y) {
    uint8_t *Y = yuv420[0] + y * width;
    uint8_t *Cb = yuv420[1] + (y >> 1) * (width >> 1);
    uint8_t *Cr = yuv420[2] + (y >> 1) * (width >> 1);
    for (x = 0; x < width; x += 2) {
        *(dest + 0) = Y[0];
        *(dest + 1) = Cb[0];
        *(dest + 2) = Y[1];
        *(dest + 3) = Cr[0];
        dest += 4;
        Y += 2;
        ++Cb;
        ++Cr;
    }
    }
}

#ifdef HAVE_ASM_MMX 
#include "mmx_macros.h"
#include "mmx.h"

/*****************************************************************

  _yuv_yuv_mmx.c

  Copyright (c) 2001-2002 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de

  http://gmerlin.sourceforge.net

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.


    - took yuy2 -> planar 422 and 422 planar -> yuy2 mmx conversion
                                 routines.
      (Niels, 02/2005)

*****************************************************************/

static mmx_t mmx_00ffw =   { 0x00ff00ff00ff00ffLL };

#ifdef HAVE_ASM_MMX2
#define MOVQ_R2M(reg,mem) movntq_r2m(reg, mem)
#else
#define MOVQ_R2M(reg,mem) movq_r2m(reg, mem)
#endif

#define PLANAR_TO_YUY2   movq_m2r(*src_y, mm0);/*   mm0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                         movd_m2r(*src_u, mm1);/*   mm1: 00 00 00 00 U6 U4 U2 U0 */\
                         movd_m2r(*src_v, mm2);/*   mm2: 00 00 00 00 V6 V4 V2 V0 */\
                         pxor_r2r(mm3, mm3);/*      Zero mm3                     */\
                         movq_r2r(mm0, mm7);/*      mm7: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                         punpcklbw_r2r(mm3, mm0);/* mm0: 00 Y3 00 Y2 00 Y1 00 Y0 */\
                         punpckhbw_r2r(mm3, mm7);/* mm7: 00 Y7 00 Y6 00 Y5 00 Y4 */\
                         pxor_r2r(mm4, mm4);     /* Zero mm4                     */\
                         punpcklbw_r2r(mm1, mm4);/* mm4: U6 00 U4 00 U2 00 U0 00 */\
                         pxor_r2r(mm5, mm5);     /* Zero mm5                     */\
                         punpcklbw_r2r(mm2, mm5);/* mm5: V6 00 V4 00 V2 00 V0 00 */\
                         movq_r2r(mm4, mm6);/*      mm6: U6 00 U4 00 U2 00 U0 00 */\
                         punpcklwd_r2r(mm3, mm6);/* mm6: 00 00 U2 00 00 00 U0 00 */\
                         por_r2r(mm6, mm0);      /* mm0: 00 Y3 U2 Y2 00 Y1 U0 Y0 */\
                         punpcklwd_r2r(mm3, mm4);/* mm4: 00 00 U6 00 00 00 U4 00 */\
                         por_r2r(mm4, mm7);      /* mm7: 00 Y7 U6 Y6 00 Y5 U4 Y4 */\
                         pxor_r2r(mm6, mm6);     /* Zero mm6                     */\
                         punpcklwd_r2r(mm5, mm6);/* mm6: V2 00 00 00 V0 00 00 00 */\
                         por_r2r(mm6, mm0);      /* mm0: V2 Y3 U2 Y2 V0 Y1 U0 Y0 */\
                         punpckhwd_r2r(mm5, mm3);/* mm3: V6 00 00 00 V4 00 00 00 */\
                         por_r2r(mm3, mm7);      /* mm7: V6 Y7 U6 Y6 V4 Y5 U4 Y4 */\
                         MOVQ_R2M(mm0, *dst);\
                         MOVQ_R2M(mm7, *(dst+8));


#define YUY2_TO_YUV_PLANAR movq_m2r(*src,mm0);\
                           movq_m2r(*(src+8),mm1);\
                           movq_r2r(mm0,mm2);/*       mm2: V2 Y3 U2 Y2 V0 Y1 U0 Y0 */\
                           pand_m2r(mmx_00ffw,mm2);/* mm2: 00 Y3 00 Y2 00 Y1 00 Y0 */\
                           pxor_r2r(mm4, mm4);/*      Zero mm4 */\
                           packuswb_r2r(mm4,mm2);/*   mm2: 00 00 00 00 Y3 Y2 Y1 Y0 */\
                           movq_r2r(mm1,mm3);/*       mm3: V6 Y7 U6 Y6 V4 Y5 U4 Y4 */\
                           pand_m2r(mmx_00ffw,mm3);/* mm3: 00 Y7 00 Y6 00 Y5 00 Y4 */\
                           pxor_r2r(mm6, mm6);/*      Zero mm6 */\
                           packuswb_r2r(mm3,mm6);/*   mm6: Y7 Y6 Y5 Y4 00 00 00 00 */\
                           por_r2r(mm2,mm6);/*        mm6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                           psrlw_i2r(8,mm0);/*        mm0: 00 V2 00 U2 00 V0 00 U0 */\
                           psrlw_i2r(8,mm1);/*        mm1: 00 V6 00 U6 00 V4 00 U4 */\
                           packuswb_r2r(mm1,mm0);/*   mm0: V6 U6 V4 U4 V2 U2 V0 U0 */\
                           movq_r2r(mm0,mm1);/*       mm1: V6 U6 V4 U4 V2 U2 V0 U0 */\
                           pand_m2r(mmx_00ffw,mm0);/* mm0: 00 U6 00 U4 00 U2 00 U0 */\
                           psrlw_i2r(8,mm1);/*        mm1: 00 V6 00 V4 00 V2 00 V0 */\
                           packuswb_r2r(mm4,mm0);/*   mm0: 00 00 00 00 U6 U4 U2 U0 */\
                           packuswb_r2r(mm4,mm1);/*   mm1: 00 00 00 00 V6 V4 V2 V0 */\
                           MOVQ_R2M(mm6, *dst_y);\
                           movd_r2m(mm0, *dst_u);\
                           movd_r2m(mm1, *dst_v);
/*
#define MMX_YUV422_YUYV "                                                 \n\
movq       (%1), %%mm0  # Load 8 Y            y7 y6 y5 y4 y3 y2 y1 y0     \n\
movd       (%2), %%mm1  # Load 4 Cb           00 00 00 00 u3 u2 u1 u0     \n\
movd       (%3), %%mm2  # Load 4 Cr           00 00 00 00 v3 v2 v1 v0     \n\
punpcklbw %%mm2, %%mm1  #                     v3 u3 v2 u2 v1 u1 v0 u0     \n\
movq      %%mm0, %%mm2  #                     y7 y6 y5 y4 y3 y2 y1 y0     \n\
punpcklbw %%mm1, %%mm2  #                     v1 y3 u1 y2 v0 y1 u0 y0     \n\
movq      %%mm2, (%0)   # Store low YUYV                                  \n\
punpckhbw %%mm1, %%mm0  #                     v3 y7 u3 y6 v2 y5 u2 y4     \n\
movq      %%mm0, 8(%0)  # Store high YUYV                                 \n\
"
*/

//inline this function from libswscale
static inline void yuvPlanartoyuy2(const uint8_t *ysrc, const uint8_t *usrc, const uint8_t *vsrc, uint8_t *dst,
                                           int width, int height,
                                           int lumStride, int chromStride, int dstStride, int vertLumPerChroma)
{
    int y;
    const x86_reg chromWidth= width>>1;

    for (y=0; y<height; y++) {
        __asm__ volatile(
            "xor                 %%"REG_a", %%"REG_a"   \n\t"
            ".p2align                    4              \n\t"
            "1:                                         \n\t"
            PREFETCH"    32(%1, %%"REG_a", 2)           \n\t"
            PREFETCH"    32(%2, %%"REG_a")              \n\t"
            PREFETCH"    32(%3, %%"REG_a")              \n\t"
            "movq          (%2, %%"REG_a"), %%mm0       \n\t" // U(0)
            "movq                    %%mm0, %%mm2       \n\t" // U(0)
            "movq          (%3, %%"REG_a"), %%mm1       \n\t" // V(0)
            "punpcklbw               %%mm1, %%mm0       \n\t" // UVUV UVUV(0)
            "punpckhbw               %%mm1, %%mm2       \n\t" // UVUV UVUV(8)

            "movq        (%1, %%"REG_a",2), %%mm3       \n\t" // Y(0)
            "movq       8(%1, %%"REG_a",2), %%mm5       \n\t" // Y(8)
            "movq                    %%mm3, %%mm4       \n\t" // Y(0)
            "movq                    %%mm5, %%mm6       \n\t" // Y(8)
            "punpcklbw               %%mm0, %%mm3       \n\t" // YUYV YUYV(0)
            "punpckhbw               %%mm0, %%mm4       \n\t" // YUYV YUYV(4)
            "punpcklbw               %%mm2, %%mm5       \n\t" // YUYV YUYV(8)
            "punpckhbw               %%mm2, %%mm6       \n\t" // YUYV YUYV(12)

            MOVNTQ"                  %%mm3,   (%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm4,  8(%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm5, 16(%0, %%"REG_a", 4)    \n\t"
            MOVNTQ"                  %%mm6, 24(%0, %%"REG_a", 4)    \n\t"

            "add                        $8, %%"REG_a"   \n\t"
            "cmp                        %4, %%"REG_a"   \n\t"
            " jb                        1b              \n\t"
            ::"r"(dst), "r"(ysrc), "r"(usrc), "r"(vsrc), "g" (chromWidth)
            : "%"REG_a
        );
        if ((y&(vertLumPerChroma-1)) == vertLumPerChroma-1) {
            usrc += chromStride;
            vsrc += chromStride;
        }
        ysrc += lumStride;
        dst  += dstStride;
    }
    __asm__(_EMMS"       \n\t"
            SFENCE"     \n\t"
            :::"memory");
}


void    yuv422_to_yuyv(uint8_t *src[3], uint8_t *dstI, int w, int h)
{
    yuvPlanartoyuy2( src[0], src[1], src[2], dstI, w, h, w, w, w * 2, 2 );
}


void    yuy2toyv16(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v, uint8_t *srcI, int w, int h )
{
    int j,jmax,imax,i;
    uint8_t *src = srcI;
    
    jmax = w / 8;
    imax = h;

    for( i = 0; i < imax ;i ++ )
    {
        for( j = 0; j < jmax ; j ++ )
        {
            YUY2_TO_YUV_PLANAR;
            src += 16;
            dst_y += 8;
            dst_u += 4;
            dst_v += 4;
        }
    }
        __asm__ __volatile__ ( _EMMS:::"memory");
}

void vj_yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
        int width, int height)
{
    int j,jmax,imax,i;
    uint8_t *src = input;
    uint8_t *dst_y = _y;
    uint8_t *dst_u = _u; 
    uint8_t *dst_v = _v;
    jmax = width / 8;
    imax = height;

    for( i = 0; i < imax ;i ++ )
    {
        for( j = 0; j < jmax ; j ++ )
        {
            YUY2_TO_YUV_PLANAR;
            src += 16;
            dst_y += 8;
            dst_u += 4;
            dst_v += 4;
        }
        dst_u += width;
        dst_v += width;
    }
    __asm__ __volatile__ ( _EMMS:::"memory");
}

#endif


// non mmx functions
#if !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
void vj_yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
        int width, int height)
{
  int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //I420
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i += 4) {
    /* top field scanline */
    for (j = 0; j < w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(y++) = *(input++);
        *(u++) = *(input++);
        *(y++) = *(input++);
        *(v++) = *(input++);
    }
    for (j = 0; j < w2; j++)
    {
        *(y++) = *(input++);
        *(u++) = *(input++);
        *(y++) = *(input++);
        *(v++) = *(input++);
    
    }

    /* next two scanlines, one frome each field , interleaved */
    for (j = 0; j < w2; j++) {
        /* skip every second line for U and V */
        *(y++) = *(input++);
        input++;
        *(y++) = *(input++);
        input++;
    }
    /* bottom field scanline*/
    for (j = 0; j < w2; j++) {
        /* skip every second line for U and V */
        *(y++) = *(input++);
        input++;
        *(y++) = *(input++);
        input++;
    }

    }
}

void yuy2toyv16(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
        int width, int height)
{

    int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //YV16
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i ++ )
    {
    for (j = 0; j < w2; j++) {
        /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
        *(y++) = *(input++);
        *(u++) = *(input++);
        *(y++) = *(input++);
        *(v++) = *(input++);
    }
    }
}

void yuv422_to_yuyv(uint8_t *yuv422[3], uint8_t *pixels, int w, int h)
{
    int x,y;
    uint8_t *Y = yuv422[0];
    uint8_t *U = yuv422[1];
    uint8_t *V = yuv422[2]; // U Y V Y
    for(y = 0; y < h; y ++ )
    {
        Y = yuv422[0] + y * w;
        U = yuv422[1] + (y>>1) * w;
        V = yuv422[2] + (y>>1) * w;
        for( x = 0 ; x < w ; x += 4 )
        {
            *(pixels + 0) = Y[0];
            *(pixels + 1) = U[0];
            *(pixels + 2) = Y[1];
            *(pixels + 3) = V[0];
            *(pixels + 4) = Y[2];
            *(pixels + 5) = U[1];
            *(pixels + 6) = Y[3];
            *(pixels + 7) = V[1];
            pixels += 8;
            Y+=4;
            U+=2;
            V+=2;
        }
    }
}
#endif

#ifdef HAVE_ARM
void yuv422_to_yuyv(uint8_t *yuv422[3], uint8_t *pixels, int w, int h) {
    int x, y;
    uint8_t *Y_plane = yuv422[0];
    uint8_t *U_plane = yuv422[1];
    uint8_t *V_plane = yuv422[2];

    for (y = 0; y < h; y++) {
        uint8_t *Y = Y_plane + y * w;
        uint8_t *U = U_plane + (y >> 1) * w;
        uint8_t *V = V_plane + (y >> 1) * w;
        uint8_t *dst = pixels + y * w * 2;

        for (x = 0; x < w; x += 16) {
            uint8x16_t y_data = vld1q_u8(Y);
            uint8x8_t u_data = vld1_u8(U);
            uint8x8_t v_data = vld1_u8(V);

            uint8x8x2_t uv_pair = vzip_u8(u_data, v_data);
            
            uint8x16x2_t yuyv_pair = vzipq_u8(y_data, vcombine_u8(uv_pair.val[0], uv_pair.val[1]));
            
            vst1q_u8(dst, yuyv_pair.val[0]);
            vst1q_u8(dst + 16, yuyv_pair.val[1]);
            
            dst += 32;
            Y += 16;
            U += 8;
            V += 8;
        }
    }
}
#endif


/* lav_common - some general utility functionality used by multiple
    lavtool utilities. */

/* Copyright (C) 2000, Rainer Johanni, Andrew Stevens */
/* - added scene change detection code 2001, pHilipp Zabel */
/* - broke some code out to lav_common.h and lav_common.c 
 *   July 2001, Shawn Sulma <lavtools@athos.cx>.  In doing this,
 *   I replaced the large number of globals with a handful of structs
 *   that are passed into the appropriate methods.  Check lav_common.h
 *   for the structs.  I'm sure some of what I've done is inefficient,
 *   subtly incorrect or just plain Wrong.  Feedback is welcome.
 */


int luminance_mean(uint8_t * frame[], int w, int h)
{
    uint8_t *p;
    uint8_t *lim;
    int sum = 0;
    int count = 0;
    p = frame[0];
    lim = frame[0] + w * (h - 1);
    while (p < lim) {
    sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
    p += 31;
    count += 4;
    }

    w = w / 2;
    h = h / 2;

    p = frame[1];
    lim = frame[1] + w * (h - 1);
    while (p < lim) {
    sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
    p += 31;
    count += 4;
    }
    p = frame[2];
    lim = frame[2] + w * (h - 1);
    while (p < lim) {
    sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
    p += 31;
    count += 4;
    }

    if(count == 0)
        return 0;

    return sum / count;
}



void*   yuv_init_swscaler(VJFrame *src, VJFrame *dst, sws_template *tmpl, int swscale_flagss)
{
    vj_sws *s = (vj_sws*) vj_calloc(sizeof(vj_sws));
    if(!s)
        return NULL;

    int     swscale_flags = 0;
    switch(tmpl->flags)
    {
        case 1:
            swscale_flags = swscale_flags|SWS_FAST_BILINEAR;
            break;
        case 2:
            swscale_flags = swscale_flags|SWS_BILINEAR;
            break;
        case 4:
            swscale_flags = swscale_flags|SWS_BICUBIC;
            break;
        case 3:
            swscale_flags = swscale_flags |SWS_POINT;
            break;
        case 5:
            swscale_flags = swscale_flags|SWS_X;
            break;
        case 6:
            swscale_flags = swscale_flags | SWS_AREA;
            break;
        case 7:
            swscale_flags = swscale_flags | SWS_BICUBLIN;
            break;
        case 8: 
            swscale_flags = swscale_flags | SWS_GAUSS;
            break;
        case 9:
            swscale_flags = swscale_flags | SWS_SINC;
            break;
        case 10:
            swscale_flags = swscale_flags |SWS_LANCZOS;
            break;
        case 11:
            swscale_flags = swscale_flags | SWS_SPLINE;
            break;
    }   

    if( full_chroma_interpolation_ ) 
        swscale_flags = swscale_flags |  SWS_FULL_CHR_H_INT;

    s->src_filter = sws_getDefaultFilter( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1 );
    s->dst_filter = sws_getDefaultFilter( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1 );

    s->sws = sws_getContext(
            src->width,
            src->height,
            src->format,
            dst->width,
            dst->height,
            dst->format,
            swscale_flags,
            s->src_filter,
            s->dst_filter,
            NULL
        );

    int dummy[4];
    int srcRange, dstRange;
    int brightness,contrast,saturation;

    sws_getColorspaceDetails( s->sws, (int**) &dummy, &srcRange, (int**) &dummy, &dstRange, &brightness,&contrast,&saturation );
    const int *coefs = sws_getCoefficients(SWS_CS_DEFAULT);

    srcRange = src->range;
    dstRange = dst->range;

    sws_setColorspaceDetails( s->sws, coefs, srcRange, coefs, dstRange, brightness, contrast, saturation );
  
#ifdef STRICT_CHECKING
    veejay_msg(VEEJAY_MSG_DEBUG, "Initialized scaler context %dx%d@%d (%s) [%d] -> %dx%d@%d (%s) [%d]",
            src->width,src->height,src->yuv_fmt,yuv_get_pixfmt_description(src->yuv_fmt), src->range,
            dst->width,dst->height,dst->yuv_fmt,yuv_get_pixfmt_description(dst->yuv_fmt), dst->range);
#endif

    if(!s->sws)
    {
        veejay_msg(VEEJAY_MSG_DEBUG,"sws_getContext failed");
        if(s)free(s);
        return NULL;
    }   
    
    return ((void*)s);

}

static void *yuv_init_sws_cached_context(vj_sws *s, VJFrame *src, VJFrame *dst, sws_template *tmpl, int swscale_flagss)
{
    int     swscale_flags = 0;
    switch(tmpl->flags)
    {
        case 1:
            swscale_flags = swscale_flags|SWS_FAST_BILINEAR;
            break;
        case 2:
            swscale_flags = swscale_flags|SWS_BILINEAR;
            break;
        case 4:
            swscale_flags = swscale_flags|SWS_BICUBIC;
            break;
        case 3:
            swscale_flags = swscale_flags |SWS_POINT;
            break;
        case 5:
            swscale_flags = swscale_flags|SWS_X;
            break;
        case 6:
            swscale_flags = swscale_flags | SWS_AREA;
            break;
        case 7:
            swscale_flags = swscale_flags | SWS_BICUBLIN;
            break;
        case 8: 
            swscale_flags = swscale_flags | SWS_GAUSS;
            break;
        case 9:
            swscale_flags = swscale_flags | SWS_SINC;
            break;
        case 10:
            swscale_flags = swscale_flags |SWS_LANCZOS;
            break;
        case 11:
            swscale_flags = swscale_flags | SWS_SPLINE;
            break;
    }   

    if( full_chroma_interpolation_ ) 
        swscale_flags = swscale_flags |  SWS_FULL_CHR_H_INT;

    if( !sws_isSupportedInput( src->format ) ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "No support for input format");
    }
    if( !sws_isSupportedOutput( dst->format ) ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "No support for output format");
    }

    if( s->sws != NULL ) {
        if( s->width != src->width || s->format != src->format || s->height != src->height ) {
            sws_freeContext( s->sws );
            s->sws = NULL;
        }
    }

    s->src_filter = sws_getDefaultFilter( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1 );
    s->dst_filter = sws_getDefaultFilter( 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1 );

    if( s->sws == NULL ) {
        s->sws = sws_getContext(
            src->width,
            src->height,
            src->format,
            dst->width,
            dst->height,
            dst->format,
            swscale_flags,
            s->src_filter,
            s->dst_filter,
            NULL
        );
        s->width = src->width;
        s->height = src->height;
        s->format = src->format;
    }

    int dummy[4];
    int srcRange, dstRange;
    int brightness,contrast,saturation;

    sws_getColorspaceDetails( s->sws, (int**) &dummy, &srcRange, (int**) &dummy, &dstRange, &brightness,&contrast,&saturation );
    const int *coefs = sws_getCoefficients(SWS_CS_DEFAULT);

    srcRange = src->range;
    dstRange = dst->range;

    sws_setColorspaceDetails( s->sws, coefs, srcRange, coefs, dstRange, brightness, contrast, saturation );
#ifdef STRICT_CHECKING
    veejay_msg(VEEJAY_MSG_DEBUG, "Initialized scaler context %dx%d@%d (%s) [%d] -> %dx%d@%d (%s) [%d]",
            src->width,src->height,src->yuv_fmt,yuv_get_pixfmt_description(src->yuv_fmt), src->range,
            dst->width,dst->height,dst->yuv_fmt,yuv_get_pixfmt_description(dst->yuv_fmt), dst->range );
#endif
    if( s->sws == NULL )
    {
        veejay_msg(VEEJAY_MSG_ERROR,"Failed to get scaler context for %dx%d in %d -> %dx%d in %d",
            src->width,src->height,src->format, dst->width,dst->height,dst->format );

        return NULL;
    }

    
    return (void*) s;
}


void*   yuv_init_cached_swscaler(void *cache,VJFrame *src, VJFrame *dst, sws_template *tmpl, int swscale_flags)
{
    vj_sws *ctx = (vj_sws*) cache;
    if( ctx == NULL )
    {
        ctx = (vj_sws*) vj_calloc(sizeof(vj_sws));
        return yuv_init_sws_cached_context(ctx,src, dst, tmpl, swscale_flags);
    }
    
    return yuv_init_sws_cached_context( ctx, src, dst, tmpl, swscale_flags);
}


void  yuv_crop(VJFrame *src, VJFrame *dst, VJRectangle *rect )
{
    int x;
    int y;
    int i = 0;

    for( i = 0 ; i < 3 ; i ++ )
    {
        int j = 0;
        uint8_t *srcPlane = src->data[i];
        uint8_t *dstPlane = dst->data[i];
        for( y = rect->top ; y < ( src->height - rect->bottom ); y ++ )
        {
            for ( x = rect->left ; x < ( src->width - rect->right ); x ++ )
            {
                dstPlane[j] = srcPlane[ y * src->width + x ];
                j++;
            }
        }
    }

}

VJFrame *yuv_allocate_crop_image( VJFrame *src, VJRectangle *rect )
{
    int w = src->width - rect->left - rect->right;
    int h = src->height - rect->top - rect->bottom;

    if( w <= 0 )
        return NULL;
    if( h <= 0 )
        return NULL;

    VJFrame *new = (VJFrame*) vj_malloc(sizeof(VJFrame));
    if(!new)    
        return NULL;

    new->width = w;
    new->height = h;    
    new->uv_len = (w >> src->shift_h) * (h >> src->shift_v );
    new->len = w * h;
    new->uv_width  = (w >> src->shift_h );
    new->uv_height = (h >> src->shift_v );
    new->shift_v = src->shift_v;
    new->shift_h = src->shift_h;

    return new;
}


void    yuv_free_swscaler(void *sws)
{
    if(sws)
    {
        vj_sws *s = (vj_sws*) sws;
        if(s->sws)
        {
            sws_freeContext( s->sws );
            s->sws = NULL;
        }
        if(s) {
            if(s->dst_filter) 
            {
                sws_freeFilter( s->dst_filter );
                s->dst_filter = NULL;
            }
            if(s->src_filter) {
                sws_freeFilter( s->src_filter );
                s->src_filter = NULL;
            }
            free(s);
        }
        sws = NULL;
    }
}

void    yuv_convert_and_scale_gray_rgb(void *sws,VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;
    const int src_stride[3] = { src->width,0,0 };
    const int dst_stride[3] = { src->width * 3, 0,0 };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( dst->data[0] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data,src_stride, 0,src->height,(uint8_t * const*)dst->data, dst_stride );
}
void    yuv_convert_and_scale_from_rgb(void *sws , VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;
    int n = 3;
    if( src->format == PIX_FMT_RGBA ||
        src->format == PIX_FMT_BGRA ||
        src->format == PIX_FMT_ARGB ||
        src->format == PIX_FMT_ABGR ||
        src->format == PIX_FMT_BGR32 ||
        src->format == PIX_FMT_RGB32 )
        n = 4;
    
    const int src_stride[4] = { src->width*n,0,0,0};
    const int dst_stride[4] = { dst->width,dst->uv_width,dst->uv_width,dst->stride[3] };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( dst->data[0] );
    check_desired_alignment( dst->data[1] );
    check_desired_alignment( dst->data[2] );
#endif

    sws_scale( s->sws,(const uint8_t * const*) src->data, src_stride, 0, src->height, (uint8_t * const*)dst->data, dst_stride );
}

void    yuv_convert_and_scale_rgb(void *sws , VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;
    int n = 3;

    if( dst->format == PIX_FMT_RGBA ||
        dst->format == PIX_FMT_BGRA ||
        dst->format == PIX_FMT_ARGB ||
        dst->format == PIX_FMT_ABGR ||
        dst->format == PIX_FMT_RGB32 ||
        dst->format == PIX_FMT_BGR32 ) 
        n = 4;

    const int src_stride[4] = { src->width,src->uv_width,src->uv_width,src->stride[3] };
    const int dst_stride[4] = { dst->width*n,0,0,0 };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( src->data[1] );
    check_desired_alignment( src->data[2] );
    check_desired_alignment( dst->data[0] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data, src_stride, 0, src->height,(uint8_t * const*) dst->data, dst_stride );
}
void    yuv_convert_and_scale(void *sws , VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( src->data[1] );
    check_desired_alignment( src->data[2] );
    check_desired_alignment( dst->data[0] );
    check_desired_alignment( dst->data[1] );
    check_desired_alignment( dst->data[2] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data, src->stride, 0, src->height,(uint8_t * const*)dst->data, dst->stride );
}
void    yuv_convert_and_scale_grey(void *sws , VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;
    const int src_stride[3] = { src->width,0,0 };
    const int dst_stride[3] = { dst->width,0,0 };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( dst->data[0] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data, src_stride, 0, src->height,(uint8_t * const*) dst->data, dst_stride );
}

void    yuv_convert_and_scale_packed(void *sws , VJFrame *src, VJFrame *dst)
{
    vj_sws *s = (vj_sws*) sws;

    const int src_stride[3] = { src->width,src->uv_width,src->uv_width };
    const int dst_stride[3] = { dst->width * 2,0,0 };
#ifdef STRICT_CHECKING
    check_desired_alignment( src->data[0] );
    check_desired_alignment( src->data[1] );
    check_desired_alignment( src->data[2] );
    check_desired_alignment( dst->data[0] );
#endif
    sws_scale( s->sws,(const uint8_t * const*) src->data, src_stride, 0, src->height,(uint8_t * const*)dst->data, dst_stride );
}

int yuv_sws_get_cpu_flags(void)
{
    int swscale_flags = 0;
    swscale_flags = swscale_flags | global_scaler_;

    return swscale_flags;
}

static struct
{   
    int i;
    const char *name;
} sws_scaler_types[] = 
{
    {   1,  "Fast bilinear (default)" },
    {   2,  "Bilinear" },
    {   3,  "Bicubic"  },
    {   4,  "Nearest neighbour"},
    {   5,  "Experimental"},
    {   6,  "Area"},
    {   7,  "Linear bicubic"},  
    {   8,  "Gaussian"},
    {   9,  "Sinc"},
    {   10, "Lanzcos"},
    {   11, "Natural bicubic spline"},
    {   0,  NULL }
};

const char  *yuv_get_scaler_name(int id)
{
    int i;
    for( i = 0; sws_scaler_types[i].i != 0 ; i ++ )
        if( id == sws_scaler_types[i].i )
            return sws_scaler_types[i].name;
    return NULL;
}


void yuv422to420planar(uint8_t *src[3], uint8_t *dst[3], int len ) {
    
    const int half = len / 2;
    int i;
    for( i = 0; i < half; i ++ ) {
        dst[1][i] = ( src[1][2 * i] + src[1][2 * i + 1]) / 2;
        dst[2][i] = ( src[2][2 * i] + src[2][2 * i + 1]) / 2;
    }

}

#if !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
void    yuv420to422planar( uint8_t *src[3], uint8_t *dst[3], int w, int h )
{
    unsigned int x,y;
    unsigned int k=0;
    const int hei = h >> 1;
    const int wid = w >> 1;
    uint8_t *u = dst[1];
    uint8_t *v = dst[2];
    uint8_t *a = src[1];
    uint8_t *b = src[2];
    for( y = 0 ; y < hei; y ++ ) {
        u = dst[1] + ( (y << 1 ) * wid ); //@ dup
        v = dst[2] + ( (y << 1 ) * wid );
        for( x= 0; x < wid ; x ++ ) {
            u[k] = a[ y * wid + x];
            u[k + wid ] = a[y*wid+x];
            v[k] = b[ y * wid + x];
            v[k + wid ] = b[y * wid + x ];
            k += 2;
        }
    }
}
#endif

#ifdef HAVE_ARM
void yuv420to422planar(uint8_t *src[3], uint8_t *dst[3], int w, int h) {
    const int wid = w >> 1;
    const int hei = h >> 1;

    uint8_t *u_src = src[1];
    uint8_t *v_src = src[2];
    uint8_t *u_dst = dst[1];
    uint8_t *v_dst = dst[2];

    for (int y = 0; y < hei; y++) {
        uint8_t *a = u_src + y * wid;
        uint8_t *b = v_src + y * wid;

        uint8_t *u_even = u_dst + (y << 1) * wid;
        uint8_t *u_odd = u_dst + ((y << 1) + 1) * wid;
        uint8_t *v_even = v_dst + (y << 1) * wid;
        uint8_t *v_odd = v_dst + ((y << 1) + 1) * wid;

        for (int x = 0; x < wid; x += 8) {
            uint8x8_t u_values = vld1_u8(a);
            uint8x8_t v_values = vld1_u8(b);

            vst1_u8(u_even, u_values);
            vst1_u8(v_even, v_values);
            
            vst1_u8(u_odd, u_values);
            vst1_u8(v_odd, v_values);

            a += 8;
            b += 8;
            u_even += 8;
            u_odd += 8;
            v_even += 8;
            v_odd += 8;
        }
    }
}
#endif

#ifdef HAVE_ASM_MMX
static  inline  void copy8( uint8_t *to, uint8_t *to2, uint8_t *from ) {
    __asm__ __volatile__ (
            "movq   (%0),   %%mm0\n" 
            "movq   %%mm0,  (%1)\n"
            "movq   %%mm0,  (%2)\n"
            :: "r" (from), "r" (to) , "r" (to2) : "memory"
        );
}

void    yuv420to422planar( uint8_t *src[3], uint8_t *dst[3], int w, int h )
{
    unsigned int x,y;
    const int hei = (h >> 1);
    const int work = (w >> 1) / 8;
    const int wid = w >> 1;
    uint8_t *u = dst[1];
    uint8_t *v = dst[2];
    uint8_t *a = src[1];
    uint8_t *b = src[2];
    uint8_t *u2 = dst[1];
    uint8_t *v2 = dst[2];
    for( y = 0; y < hei;  y ++ ) {
        u = dst[1] + ( (y << 1 ) * wid );
        u2 = dst[1] + ( ((y << 1) + 1) * wid );
        a = src[1] + ( y * wid );
        for( x = 0; x < work; x ++ ) {
            copy8( u,u2, a );
            u += 8;
            u2 += 8;
            a  += 8;
        }
    }   
    for( y = 0; y < hei;  y ++ ) {
        v = dst[2] + ( (y << 1 ) * wid );
        v2 = dst[2] + ( (( y << 1 ) + 1 ) * wid );
        b = src[2] + ( y * wid );
        for( x = 0; x < work; x ++ ) {
            copy8( v,v2, b );
            v += 8;
            v2 += 8;
            b  += 8;            
        }
    }
       __asm__ __volatile__ ( _EMMS:::"memory");
}
#endif

static void yuy2_scale_pixels_from_yuv_job( void *arg )
{
    vj_task_arg_t *v = (vj_task_arg_t*) arg;
    uint8_t *plane = v->input[0];
    int  len   = v->strides[0];

    unsigned int i;
    for( i = 0; i < len; i += 4 ) {
        plane[i+0] = jpeg_to_CCIR_tableY[ plane[i+0] ];
        plane[i+1] = jpeg_to_CCIR_tableUV[plane[i+1] ];
        plane[i+2] = jpeg_to_CCIR_tableY[ plane[i+2] ];
        plane[i+3] = jpeg_to_CCIR_tableUV[ plane[i+3] ];
    }
}

void    yuy2_scale_pixels_from_yuv( uint8_t *plane, int len )
{
    unsigned int rlen = 2 * len ;
    unsigned int i;
    for( i = 0; i < rlen; i += 4 ) {
        plane[i+0] = jpeg_to_CCIR_tableY[ plane[i+0] ];
        plane[i+1] = jpeg_to_CCIR_tableUV[plane[i+1] ];
        plane[i+2] = jpeg_to_CCIR_tableY[ plane[i+2] ];
        plane[i+3] = jpeg_to_CCIR_tableUV[ plane[i+3] ];
    }
}

static void yuy2_scale_pixels_from_ycbcr_job( void *arg )
{
    vj_task_arg_t *v = (vj_task_arg_t*) arg;
    uint8_t *plane = v->input[0];
    int len = v->strides[0];
    unsigned int i;
    for( i = 0; i < len; i += 4 ) {
        plane[i+0] = CCIR_to_jpeg_tableY[ plane[i+0] ];
        plane[i+1] = CCIR_to_jpeg_tableUV[plane[i+1] ];
        plane[i+2] = CCIR_to_jpeg_tableY[ plane[i+2] ];
        plane[i+3] = CCIR_to_jpeg_tableUV[ plane[i+3] ];
    }
}

void    yuy2_scale_pixels_from_ycbcr( uint8_t *plane, int len )
{
    unsigned int rlen = 2 * len ;
    unsigned int i;
    for( i = 0; i < rlen; i += 4 ) {
        plane[i+0] = CCIR_to_jpeg_tableY[ plane[i+0] ];
        plane[i+1] = CCIR_to_jpeg_tableUV[plane[i+1] ];
        plane[i+2] = CCIR_to_jpeg_tableY[ plane[i+2] ];
        plane[i+3] = CCIR_to_jpeg_tableUV[ plane[i+3] ];
    }
}

static void yuy_scale_pixels_from_yuv_job( void *arg)
{
    vj_task_arg_t *t = (vj_task_arg_t*) arg;
    
    unsigned int i;
    uint8_t *y = t->input[0];
    uint8_t *u = t->input[1];
    uint8_t *v = t->input[2];
    uint8_t *dY = t->output[0];
    uint8_t *dU = t->output[1];
    uint8_t *dV = t->output[2];
    
    for( i = 0; i < t->strides[0] ; i ++ ) {
        dY[i] = jpeg_to_CCIR_tableY[ y[i] ];
    }
    for( i = 0; i < t->strides[1] ; i ++ ) {
        dU[i] = jpeg_to_CCIR_tableUV[ u[i] ];
        dV[i] = jpeg_to_CCIR_tableUV[ v[i] ];
    }
}

void    yuv_scale_pixels_from_yuv( uint8_t *src[3], uint8_t *dst[3], int len, int uv_len ) 
{
    unsigned int i;
    uint8_t *y = src[0];
    uint8_t *u = src[1];
    uint8_t *v = src[2];
    uint8_t *dY = dst[0];
    uint8_t *dU = dst[1];
    uint8_t *dV = dst[2];
    for( i = 0; i < len ; i ++ ) {
        dY[i] = jpeg_to_CCIR_tableY[ y[i] ];
    }
    len = len / 2;
    for( i = 0; i < len ; i ++ ) {
        dU[i] = jpeg_to_CCIR_tableUV[ u[i] ];
        dV[i] = jpeg_to_CCIR_tableUV[ v[i] ];
    }
}
void    yuv_scale_pixels_from_y( uint8_t *plane, int len )
{
    unsigned int i;

    for( i = 0; i < len ; i ++ ) {
        plane[i] = jpeg_to_CCIR_tableY[ plane[i] ];
    }
}
void    yuv_scale_pixels_from_uv( uint8_t *plane, int len )
{
    unsigned int i;

    for( i = 0; i < len ; i ++ ) {
        plane[i] = jpeg_to_CCIR_tableUV[ plane[i] ];
    }
}


void    yuv_scale_pixels_from_ycbcr( uint8_t *plane, float min, float max, int len )
{
    unsigned int i;

    if( max == 235.0f ) {
        for( i = 0; i < len ; i ++ ) {
            plane[i] = CCIR_to_jpeg_tableY[ plane[i] ];
        }
    } else if ( max == 240.0f ) {
        for( i = 0; i < len ; i ++ ) {
            plane[i] = CCIR_to_jpeg_tableUV[ plane[i] ];
        }
    }
}
void    yuv_scale_pixels_from_ycbcr2( uint8_t *plane[3], uint8_t *dst[3], int len )
{
    unsigned int i;
    uint8_t *y = plane[0];
    uint8_t *u = plane[1];
    uint8_t *v = plane[2];
    uint8_t *dy = dst[0];
    uint8_t *du = dst[1];
    uint8_t *dv = dst[2];
    for( i = 0; i < len ; i ++ ) {
        dy[i] = CCIR_to_jpeg_tableY[ y[i] ];
    }

    len = len / 2;
    for( i = 0; i < len ; i ++ ) {
        du[i] = CCIR_to_jpeg_tableUV[ u[i] ];
        dv[i] = CCIR_to_jpeg_tableUV[ v[i] ];
    }
}


#define packv0__( y0,u0,v0,y1 ) (( (int) y0 ) & 0xff ) +\
        ( (((int) u0 ) & 0xff) << 8) +\
        ( ((((int) v0) & 0xff) << 16 )) +\
        ( ((((int) y1) & 0xff) << 24 ) )

#define packv1__( u1,v1,y2,u2 )(( (int) u1 ) & 0xff ) +\
        ( (((int) v1 ) & 0xff) << 8) +\
        ( ((((int) y2) & 0xff) << 16 )) +\
        ( ((((int) u2) & 0xff) << 24 ) )


#define packv2__( v2,y3,u3,v3 )(( (int) v2 ) & 0xff ) +\
        ( (((int) y3 ) & 0xff) << 8) +\
        ( ((((int) u3) & 0xff) << 16 )) +\
        ( ((((int) v3) & 0xff) << 24 ) )

//! YUV 4:2:4 Planar to 4:4:4 Packed: Y, V, U, Y,V, U , .... */
void yuv444_yvu444_1plane(
        uint8_t *data[3],
        const int width,
        const int height,
        uint8_t *dst_buffer)
{
    unsigned int x;
    uint8_t *yp = data[0];
    uint8_t *up = data[2];
    uint8_t *vp = data[1];
    int len = (width * height) >> 2;
    uint8_t *dst = dst_buffer;
#pragma omp simd
    for( x=0; x < len; x ++ )
    {
        dst[0] = packv0__( yp[0],up[0],vp[0],yp[1]);
        dst[1] = packv1__( up[1],vp[1],yp[2],up[2]);
        dst[2] = packv2__( vp[2],yp[3],up[3],vp[3]);

        yp += 4;
        up += 4;
        vp += 4;
        dst += 3;
    }   
    
}

#ifdef HAVE_ARM_NEON
void yuv_interpolate_frames( uint8_t *dst, uint8_t *a, uint8_t *b, const int len, const float frac )
{
    int i;
    const int step = 16;
    const int16_t fixed_frac = (int16_t)(frac * 32767.0f);
    
    int16x8_t v_frac = vdupq_n_s16(fixed_frac);
    
    for( i = 0; i < len - step; i += step ) 
    {
        uint8x16_t va_u8 = vld1q_u8(a + i);
        uint8x16_t vb_u8 = vld1q_u8(b + i);

        int16x8_t va_low  = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(va_u8)));
        int16x8_t va_high = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(va_u8)));
        int16x8_t vb_low  = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(vb_u8)));
        int16x8_t vb_high = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(vb_u8)));

        int16x8_t diff_low = vsubq_s16(vb_low, va_low);
        int16x8_t diff_high = vsubq_s16(vb_high, va_high);
        
        int16x8_t term_low = vshrq_n_s16(vmulq_s16(diff_low, v_frac), 15);
        int16x8_t term_high = vshrq_n_s16(vmulq_s16(diff_high, v_frac), 15);
        
        int16x8_t result_low = vaddq_s16(va_low, term_low);
        int16x8_t result_high = vaddq_s16(va_high, term_high);
        
        uint8x16_t vdst_u8 = vcombine_u8(vmovnq_u16(vreinterpretq_u16_s16(result_low)), 
                                        vmovnq_u16(vreinterpretq_u16_s16(result_high)));

        vst1q_u8(dst + i, vdst_u8);
    }
    for( ; i < len; i++ ) {
        dst[i] = a[i] + ( frac * ( b[i] - a[i] ) );
    }
}

#elif defined(HAVE_ASM_SSE2) || defined(HAVE_ASM_SSE4_1) || defined(HAVE_ASM_AVX) || defined(HAVE_ASM_AVX2)
void yuv_interpolate_frames( uint8_t *dst, uint8_t *a, uint8_t *b, const int len, const float frac )
{
    int i;
    const int step = 16;
    const int16_t fixed_frac = (int16_t)(frac * (float)Q_MAX);
    
    __m128i v_frac = _mm_set1_epi16(fixed_frac);
    __m128i v_zero = _mm_setzero_si128();

    for( i = 0; i < len - step; i += step ) 
    {
        __m128i va_u8 = _mm_loadu_si128((__m128i const*)(a + i));
        __m128i vb_u8 = _mm_loadu_si128((__m128i const*)(b + i));

        __m128i va_low  = _mm_unpacklo_epi8(va_u8, v_zero);
        __m128i va_high = _mm_unpackhi_epi8(va_u8, v_zero);
        __m128i vb_low  = _mm_unpacklo_epi8(vb_u8, v_zero);
        __m128i vb_high = _mm_unpackhi_epi8(vb_u8, v_zero);

        __m128i diff_low  = _mm_sub_epi16(vb_low, va_low);
        __m128i diff_high = _mm_sub_epi16(vb_high, va_high);
        
        __m128i term_mul_low  = _mm_mullo_epi16(diff_low, v_frac);
        __m128i term_mul_high = _mm_mullo_epi16(diff_high, v_frac);
        
        __m128i term_low  = _mm_srai_epi16(term_mul_low, Q_SHIFT);
        __m128i term_high = _mm_srai_epi16(term_mul_high, Q_SHIFT);
        
        __m128i result_low = _mm_add_epi16(va_low, term_low);
        __m128i result_high = _mm_add_epi16(va_high, term_high);
        
        __m128i vdst_u8 = _mm_packus_epi16(result_low, result_high);

        _mm_storeu_si128((__m128i *)(dst + i), vdst_u8);
    }
    for( ; i < len; i++ ) {
        dst[i] = a[i] + ( frac * ( b[i] - a[i] ) );
    }
}

#else
void yuv_interpolate_frames( uint8_t *dst, uint8_t *a, uint8_t *b, const int len, const float frac )
{
    int i;
    for( i = 0; i < len; i ++) {
        dst[i] = a[i] + ( frac * ( b[i] - a[i] ) );
    }
}
#endif
