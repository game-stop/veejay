/* veejay - Linux VeeJay
 *       (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <string.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#ifdef USE_GDK_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif
#include <libvje/vje.h>
#include <libvje/effects/common.h>
#include <libel/pixbuf.h>
#include <veejaycore/vims.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/avcommon.h>

typedef struct
{
    char *filename;
    VJFrame   *picA;
    VJFrame   *picB;
    VJFrame   *img;
    uint8_t   *space;
    int   display_w;
    int   display_h;
    int   real_w;
    int   real_h;
    int   fmt;
    void    *scaler;
    uint8_t *pixels;
} vj_pixbuf_t;


typedef struct
{
    char *filename;
    char *type;
    int   out_w;
    int   out_h;
} vj_pixbuf_out_t;

static int  __initialized = 0;


extern int  get_ffmpeg_pixfmt(int id);

static  VJFrame *open_pixbuf( vj_pixbuf_t *pic, const char *filename, int dst_w, int dst_h, int dst_fmt,
            uint8_t *dY, uint8_t *dU, uint8_t *dV, uint8_t *dA )
{
#ifdef USE_GDK_PIXBUF
    GdkPixbuf *image =
        gdk_pixbuf_new_from_file( filename, NULL );

    if(!image)
    {
        return NULL;
    }

    GdkPixbuf *scaled_image = gdk_pixbuf_scale_simple( image, dst_w, dst_h, GDK_INTERP_HYPER );
    if( scaled_image == NULL ) {
        g_object_unref(image);
        return NULL;
    }

    g_object_unref(image);

    image = scaled_image;

    const size_t pixbuf_size = gdk_pixbuf_get_byte_length( image );

    /* convert image to veejay frame in proper dimensions, free image */

    int img_fmt = PIX_FMT_RGB24;

    if( pic->pixels == NULL ) {
        pic->pixels = (uint8_t*) vj_calloc(sizeof(uint8_t) * (pixbuf_size + dst_w)  );
        if(!pic->pixels) {
            g_object_unref(image);
            return NULL;
        }
    }

    veejay_memcpy( pic->pixels, (uint8_t*) gdk_pixbuf_get_pixels( image ), pixbuf_size );
    
    int tmp_dstfmt = dst_fmt;

    if( gdk_pixbuf_get_has_alpha( image )) {
        img_fmt = PIX_FMT_RGBA;
        switch(dst_fmt) {
            case PIX_FMT_YUV420P:
            case PIX_FMT_YUVJ420P:
                tmp_dstfmt = PIX_FMT_YUVA420P;
                break;
            case PIX_FMT_YUVJ422P:
            case PIX_FMT_YUV422P:
                tmp_dstfmt = PIX_FMT_YUVA422P;
            break;
        }
    }       

    VJFrame *dst = yuv_yuv_template( dY, dU, dV, dst_w, dst_h, tmp_dstfmt );

    if( dst->format == PIX_FMT_YUVA422P || dst->format == PIX_FMT_YUVA420P ) {
        dst->data[3] = dA;
    }

    VJFrame *src = yuv_rgb_template(
                   pic->pixels,
                   gdk_pixbuf_get_width(  image ),
                   gdk_pixbuf_get_height( image ),
                   img_fmt
            );

    int stride = gdk_pixbuf_get_rowstride(image);

    if( stride != src->stride[0] )
        src->stride[0] = stride;

    if(pic->scaler == NULL) {
        sws_template tmpl;
        tmpl.flags = 1;
        pic->scaler = yuv_init_swscaler( src,dst, &tmpl, yuv_sws_get_cpu_flags());
        if(pic->scaler == NULL) {
            free(src);
            free(dst);
            free(pic->pixels);
            g_object_unref(image);
            return NULL;
        }
    }

    yuv_convert_any3( pic->scaler, src, src->stride, dst);

    verify_CCIR_auto( dst->format, dst_fmt, dst );

    g_object_unref( image ); 
    
    free(src);

    yuv_free_swscaler( pic->scaler );


    return dst;
#else

    return NULL;
#endif
}

void    vj_picture_cleanup( void *pic )
{
    vj_pixbuf_t *picture = ( vj_pixbuf_t*) pic;
    if(picture)
    {
        if( picture->filename )
            free(picture->filename );
        if(picture->img)
            free(picture->img);
        if(picture->space)
            free(picture->space);
        if( picture )
            free(picture);      
    }

    picture = NULL;
}


VJFrame *vj_picture_get(void *pic)
{
    if(!pic)
        return NULL;
    vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
    return picture->img;
}

int vj_picture_get_height( void *pic )
{
    vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
    if(!picture)
        return 0;
    return picture->real_h;
}   

int vj_picture_get_width( void *pic )
{
    vj_pixbuf_t *picture = (vj_pixbuf_t*) pic;
    if(!picture)
        return 0;
    return picture->real_w;
}   

void    *vj_picture_open( const char *filename, int v_outw, int v_outh, int v_outf )
{
#ifdef USE_GDK_PIXBUF
    vj_pixbuf_t *pic = NULL;
    if(filename == NULL )
    {
        return NULL;
    }

    if(v_outw <= 0 || v_outh <= 0 )
    {
        return NULL;
    }

    pic = (vj_pixbuf_t*)  vj_calloc(sizeof(vj_pixbuf_t));
    if(!pic) 
    {
        return NULL;
    }
    pic->filename = strdup( filename );
    pic->display_w = v_outw;
    pic->display_h = v_outh;
    pic->fmt = v_outf;

    int len = v_outw * v_outh;
    int ulen = len;
    switch( v_outf )
    {       
        case PIX_FMT_YUV420P:
        case PIX_FMT_YUVJ420P:
            ulen = len / 4;
            break;
        case PIX_FMT_YUV422P:
        case PIX_FMT_YUVJ422P:
            ulen = len / 2;
            break;
        case PIX_FMT_YUV444P:
        case PIX_FMT_YUVJ444P:
            ulen = len;
            break;
        case PIX_FMT_GRAY8:
            ulen = 0;
            break;
        default:
            break;
    }

    pic->space = (uint8_t*) vj_malloc( sizeof(uint8_t) * (4 * len));
    if(!pic->space) {
        free(pic->filename);
        free(pic);
        return NULL;
    }

    pic->img = open_pixbuf(
            pic,
            filename,   
            v_outw,
            v_outh,
            v_outf,
            pic->space,
            pic->space + len,
            pic->space + len + ulen,
            pic->space + len + ulen + ulen);

    if(!pic->img) {
        free(pic->space);
        free(pic->filename);
        free(pic);
        return NULL;
    }

    if(pic->pixels) {
        free(pic->pixels);
        pic->pixels = NULL;
    }


    return (void*) pic;
#else
    return NULL;
#endif
}

int vj_picture_probe( const char *filename )
{
    int ret = 0;
#ifdef USE_GDK_PIXBUF
    GdkPixbuf *image =
        gdk_pixbuf_new_from_file( filename, NULL );
    if(image)
    {
        ret = 1;
        g_object_unref( image );
    }
#endif
    return ret;
}

/* image saving */
char    *vj_picture_get_filename( void *pic )
{
    vj_pixbuf_out_t *p = (vj_pixbuf_out_t*) pic;
    if(!p) return NULL;
    return p->filename;
}

void *  vj_picture_prepare_save(const char *filename, char *type, int out_w, int out_h)
{
#ifdef USE_GDK_PIXBUF
    if(!type || !filename )
    {
        veejay_msg(0, "Missing filename or file extension");
        return NULL;
    }

    vj_pixbuf_out_t *pic =  (vj_pixbuf_out_t*) vj_calloc(sizeof(vj_pixbuf_out_t));

    if(!pic)
        return NULL;
      
    if(filename)
        pic->filename = strdup( filename );
    else
        pic->filename = NULL;

    if(strncasecmp(type,"jpg",3 ) == 0)
        pic->type = strdup("jpeg");
    else
        pic->type     = strdup( type );

    pic->out_w = out_w;
    pic->out_h = out_h;

    return (void*) pic;
#else
    return NULL;
#endif
}


static  void    vj_picture_out_cleanup( vj_pixbuf_out_t *pic )
{
    if(pic)
    {
        if(pic->filename)
            free(pic->filename);
        if(pic->type)
            free(pic->type);
        free(pic);
    }
    pic = NULL;
}

static  void    *pic_scaler_ = NULL;
static   int     pic_data_[3] = { 0,0,0};
static   int     pic_changed_ = 0;
static  sws_template    *pic_template_ = NULL;

void    vj_picture_init( void *templ )
{
    if(!__initialized)
    {
        __initialized = 1;
    }

    pic_template_ = (sws_template*) templ;
    pic_template_->flags = yuv_which_scaler();
    pic_changed_ = 1;
    pic_scaler_ = NULL;
}

int vj_picture_save( void *picture, uint8_t **frame, int w, int h , int fmt )
{
    int ret = 0;
#ifdef USE_GDK_PIXBUF
    vj_pixbuf_out_t *pic = (vj_pixbuf_out_t*) picture;
    GdkPixbuf *img_ = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, w, h );

    if(!img_)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to allocate buffer for RGB");
        return 0;
    }
    
    // convert frame to yuv
    VJFrame *src = yuv_yuv_template( frame[0],frame[2],frame[1],w,h,fmt );
    VJFrame *dst = yuv_rgb_template(
        (uint8_t*) gdk_pixbuf_get_pixels( img_ ),
                   gdk_pixbuf_get_width(  img_ ),
                   gdk_pixbuf_get_height( img_ ),
                   PIX_FMT_BGR24
            );

    yuv_convert_any_ac( src, dst );

    if( gdk_pixbuf_savev( img_, pic->filename, pic->type, NULL,NULL,NULL))
    {
        veejay_msg(VEEJAY_MSG_INFO, "Save frame as %s of type %s",
            pic->filename, pic->type );
        ret = 1;
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR,
            "Cant save file as %s (%s) size %d x %d", pic->filename,pic->type, pic->out_w, pic->out_h);
    }

    if( img_ ) 
        g_object_unref( img_ );

    free(src);
    free(dst);

    vj_picture_out_cleanup( pic );
#endif
    return ret;
}

void        vj_picture_free()
{
    if(pic_scaler_) {
        yuv_free_swscaler( pic_scaler_ );
        pic_scaler_ = NULL;
    }
}

#define pic_has_changed(a,b,c) ( (a == pic_data_[0] && b == pic_data_[1] && c == pic_data_[2] ) ? 0: 1)
#define update_pic_data(a,b,c) { pic_data_[0] = a; pic_data_[1] = b; pic_data_[2] = c;}

uint8_t val = 0;
void vj_fast_picture_save_to_mem( VJFrame *frame, int out_w, int out_h, uint8_t *dst )
{
    uint8_t *dest[4];   
    VJFrame src;
    VJFrame *src1 = &src;

    dest[0] = dst;
    dest[1] = dest[0] + (out_w * out_h);
    dest[2] = dest[1] + (out_w * out_h)/4;
    dest[3] = NULL;
    
    veejay_memcpy( src1, frame,sizeof(VJFrame));

    VJFrame *dst1 = yuv_yuv_template( dest[0], dest[1], dest[2], out_w, out_h, (yuv_get_pixel_range() ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P ) );

    pic_changed_ = pic_has_changed( out_w,out_h, dst1->yuv_fmt );

    if(pic_changed_ || pic_scaler_ == NULL )
    {
        if(pic_scaler_)
            yuv_free_swscaler( pic_scaler_ );
        pic_scaler_ = yuv_init_swscaler( src1,dst1, pic_template_, yuv_sws_get_cpu_flags());
        update_pic_data( out_w, out_h, dst1->yuv_fmt );
    }

    yuv_convert_and_scale( pic_scaler_, src1,dst1);

    free(dst1);
}

void    vj_fastbw_picture_save_to_mem( VJFrame *frame, int out_w, int out_h, uint8_t *dst )
{
    uint8_t *planes[4]; 
    VJFrame src;
    VJFrame *src1 = &src;
    
    planes[0] = dst;

    veejay_memcpy( src1, frame,sizeof(VJFrame));

    VJFrame *dst1 = yuv_yuv_template( planes[0], NULL,NULL, out_w, out_h, PIX_FMT_GRAY8 );

    pic_changed_ = pic_has_changed( out_w,out_h, PIX_FMT_GRAY8 );

    if(pic_changed_ )
    {
        if(pic_scaler_)
            yuv_free_swscaler( pic_scaler_ );
        pic_scaler_ = yuv_init_swscaler( src1,dst1, pic_template_, yuv_sws_get_cpu_flags());
        update_pic_data( out_w, out_h, PIX_FMT_GRAY8 );
    }

    yuv_convert_and_scale( pic_scaler_, src1, dst1);

    free(dst1);
}

void    vj_fast_alpha_picture_save_to_mem( VJFrame *frame, int out_w, int out_h, uint8_t *dst )
{
    uint8_t *planes[4]; 
    VJFrame src;
    VJFrame *src1 = &src;
    
    planes[0] = dst;

    veejay_memcpy( src1, frame,sizeof(VJFrame));
    src1->data[0] = src1->data[3];

    VJFrame *dst1 = yuv_yuv_template( planes[0], NULL,NULL, out_w, out_h, PIX_FMT_GRAY8 );

    pic_changed_ = pic_has_changed( out_w,out_h, PIX_FMT_GRAY8 );

    if(pic_changed_ )
    {
        if(pic_scaler_)
            yuv_free_swscaler( pic_scaler_ );
        pic_scaler_ = yuv_init_swscaler( src1,dst1, pic_template_, yuv_sws_get_cpu_flags());
        update_pic_data( out_w, out_h, PIX_FMT_GRAY8 );
    }

    yuv_convert_and_scale( pic_scaler_, src1, dst1);

    free(dst1);
}
