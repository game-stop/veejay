/*
 * imgconvert.c - image format conversion routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "ac.h"
#include "imgconvert.h"
#include "img_internal.h"

#include <stdio.h>
#include <stdlib.h>

/*************************************************************************/

static struct {
    ImageFormat srcfmt, destfmt;
    ConversionFunc func;
} *conversions;
static int n_conversions = 0;

/*************************************************************************/
/*************************************************************************/

/* Image conversion routine.  src and dest are arrays of pointers to planes
 * (for packed formats with only one plane, just use `&data'); srcfmt and
 * destfmt specify the source and destination image formats (IMG_*).
 * width and height are in pixels.  Returns 1 on success, 0 on failure. */

int ac_imgconvert(uint8_t **src, ImageFormat srcfmt,
                  uint8_t **dest, ImageFormat destfmt,
                  int width, int height)
{
    int i;

    /* Hack to handle YV12 easily, because conversion routines don't get
     * format tags */
    uint8_t *newsrc[3], *newdest[3];
    if (srcfmt == IMG_YV12) {
        srcfmt = IMG_YUV420P;
        newsrc[0] = src[0];
        newsrc[1] = src[2];
        newsrc[2] = src[1];
        src = newsrc;
    }
    if (destfmt == IMG_YV12) {
        destfmt = IMG_YUV420P;
        newdest[0] = dest[0];
        newdest[1] = dest[2];
        newdest[2] = dest[1];
        dest = newdest;
    }

    for (i = 0; i < n_conversions; i++) {
        if (conversions[i].srcfmt==srcfmt && conversions[i].destfmt==destfmt)
            return (*conversions[i].func)(src, dest, width, height);
    }

    return 0;
}

/*************************************************************************/
/*************************************************************************/

/* Internal use only! */

int ac_imgconvert_init(int accel)
{
    if (!ac_imgconvert_init_yuv_planar(accel)
     || !ac_imgconvert_init_yuv_packed(accel)
     || !ac_imgconvert_init_yuv_mixed(accel)
     || !ac_imgconvert_init_yuv_rgb(accel)
     || !ac_imgconvert_init_rgb_packed(accel)
    ) {
        fprintf(stderr, "ac_imgconvert_init() failed");
        return 0;
    }
    return 1;
}

int register_conversion(ImageFormat srcfmt, ImageFormat destfmt,
                        ConversionFunc function)
{
    int i;

    for (i = 0; i < n_conversions; i++) {
        if (conversions[i].srcfmt==srcfmt && conversions[i].destfmt==destfmt) {
            conversions[i].func = function;
            return 1;
        }
    }

    if (!(conversions = realloc(conversions,
                                (n_conversions+1) * sizeof(*conversions)))) {
        fprintf(stderr, "register_conversion(): out of memory\n");
        return 0;
    }
    conversions[n_conversions].srcfmt  = srcfmt;
    conversions[n_conversions].destfmt = destfmt;
    conversions[n_conversions].func    = function;
    n_conversions++;
    return 1;
}

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
