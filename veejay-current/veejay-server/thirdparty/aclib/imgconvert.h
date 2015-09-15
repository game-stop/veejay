/*
 * imgconvert.h - defines for image format conversion routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef ACLIB_IMGCONVERT_H
#define ACLIB_IMGCONVERT_H

/*************************************************************************/

/* Image format defines */
typedef enum {
    IMG_UNKNOWN = 0,    /* Unknown/unset (dummy value, guaranteed to be 0) */
    /* YUV formats */
    IMG_YUV_BASE = 0x1000,
    IMG_YUV420P,        /* YUV planar, 1 U/V per 2x2 Y pixels */
    IMG_YV12,           /* YUV420P with U and V reversed */
    IMG_YUV411P,        /* YUV planar, 1 U/V per 4x1 Y pixels */
    IMG_YUV422P,        /* YUV planar, 1 U/V per 2x1 Y pixels */
    IMG_YUV444P,        /* YUV planar, 1 U/V per 1x1 Y pixels */
    IMG_YUY2,           /* YUV packed, 1 U/V per 2x1 Y pixels, Y:U:Y:V */
    IMG_UYVY,           /* YUV packed, 1 U/V per 2x1 Y pixels, U:Y:V:Y */
    IMG_YVYU,           /* YUV packed, 1 U/V per 2x1 Y pixels, Y:V:Y:U */
    IMG_Y8,             /* Y-only 8-bit data */
    IMG_YUV_LAST,
    /* RGB formats */
    IMG_RGB_BASE = 0x2000,
    IMG_RGB24,          /* RGB packed, 8 bits per component, R:G:B */
    IMG_BGR24,          /* RGB packed, 8 bits per component, B:G:R */
    IMG_RGBA32,         /* RGB+alpha packed, 8 bits per component, R:G:B:A */
    IMG_ABGR32,         /* RGB+alpha packed, 8 bits per component, A:B:G:R */
    IMG_ARGB32,         /* RGB+alpha packed, 8 bits per component, A:R:G:B */
    IMG_BGRA32,         /* RGB+alpha packed, 8 bits per component, B:G:R:A */
    IMG_GRAY8,          /* Grayscale 8-bit data */
    IMG_RGB_LAST,
} ImageFormat;

/* Alias */
#define IMG_NONE        IMG_UNKNOWN

/* Default YUV and RGB formats */
#define IMG_YUV_DEFAULT         IMG_YUV420P
#define IMG_RGB_DEFAULT         IMG_RGB24

/* Is the given image format a YUV/RGB one? */
#define IS_YUV_FORMAT(fmt)      ((fmt) > IMG_YUV_BASE && (fmt) < IMG_YUV_LAST)
#define IS_RGB_FORMAT(fmt)      ((fmt) > IMG_RGB_BASE && (fmt) < IMG_RGB_LAST)

/* U/V plane size for YUV planar formats (Y plane size is always w*h) */
#define UV_PLANE_SIZE(fmt,w,h) \
    ((fmt)==IMG_YUV420P ? ((w)/2)*((h)/2) : \
     (fmt)==IMG_YV12    ? ((w)/2)*((h)/2) : \
     (fmt)==IMG_YUV411P ? ((w)/4)* (h)    : \
     (fmt)==IMG_YUV422P ? ((w)/2)* (h)    : \
     (fmt)==IMG_YUV444P ?  (w)   * (h)    : 0)

/* Macro to initialize an array of planes from a buffer */
#define YUV_INIT_PLANES(planes,buffer,fmt,w,h) \
    ((planes)[0] = (buffer),                   \
     (planes)[1] = (planes)[0] + (w)*(h),      \
     (planes)[2] = (planes)[1] + UV_PLANE_SIZE((fmt),(w),(h)))

#if 0
/* Structure describing an image.  FIXME: not currently used--this should
 * eventually replace the (planes,format) pairs passed to ac_imgconvert. */
typedef struct {
    ImageFormat format;  /* Format of image data */
    int width, height;   /* Size of image */
    uint8_t *planes[4];  /* Data planes (use planes[0] for packed data) */
    int stride[4];       /* Length of one row in each plane, incl. padding */
} Image;
#endif

/*************************************************************************/

/* Initialization routine.  Returns 1 on success, 0 on failure. */
extern int ac_imgconvert_init(int accel);

/* Conversion routine.  Returns 1 on success, 0 on failure. */
extern int ac_imgconvert(uint8_t **src,         /* Array of source planes */
                         ImageFormat srcfmt,    /* Source image format */
                         uint8_t **dest,        /* Array of dest planes */
                         ImageFormat destfmt,   /* Destination image format */
                         int width,             /* Image width in pixels */
                         int height             /* Image height in pixels */
                        );

/*************************************************************************/

#endif  /* ACLIB_IMGCONVERT_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
