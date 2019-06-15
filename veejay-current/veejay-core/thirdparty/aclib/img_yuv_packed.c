/*
 * img_yuv_packed.c - YUV packed image format conversion routines
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

/*************************************************************************/
/*************************************************************************/

/* Standard C implementations */

/*************************************************************************/

/* Identity transformation, works when src==dest */
static int yuv16_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height*2);
    return 1;
}

/* Used for YUY2->UYVY and UYVY->YUY2, works when src==dest */
static int yuv16_swap16(uint8_t **src, uint8_t **dest, int width, int height)
{
    uint16_t *srcp  = (uint16_t *)src[0];
    uint16_t *destp = (uint16_t *)dest[0];
    int i;
    for (i = 0; i < width*height; i++)
        destp[i] = srcp[i]>>8 | srcp[i]<<8;
    return 1;
}

/* Used for YUY2->YVYU and YVYU->YUY2, works when src==dest */
static int yuv16_swapuv(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height/2; i++) {
        uint8_t tmp   = src[0][i*4+1];
        dest[0][i*4  ] = src[0][i*4  ];
        dest[0][i*4+1] = src[0][i*4+3];
        dest[0][i*4+2] = src[0][i*4+2];
        dest[0][i*4+3] = tmp;
    }
    return 1;
}

/*************************************************************************/

static int uyvy_yvyu(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height/2; i++) {
        dest[0][i*4  ] = src[0][i*4+1];
        dest[0][i*4+1] = src[0][i*4+2];
        dest[0][i*4+2] = src[0][i*4+3];
        dest[0][i*4+3] = src[0][i*4  ];
    }
    return 1;
}

static int yvyu_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height/2; i++) {
        dest[0][i*4  ] = src[0][i*4+3];
        dest[0][i*4+1] = src[0][i*4  ];
        dest[0][i*4+2] = src[0][i*4+1];
        dest[0][i*4+3] = src[0][i*4+2];
    }
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#if defined(ARCH_X86) || defined(ARCH_X86_64)

/* Common macros/data for x86 code */
#define DEFINE_MASK_DATA
#include "img_x86_common.h"

/*************************************************************************/

/* Basic assembly routines */

static int yuv16_swap16_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP16_2_X86(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yuv16_swapuv_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_X86(width*height/2);
    return 1;
}

static int uyvy_yvyu_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_X86(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yvyu_uyvy_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_X86(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

/*************************************************************************/

/* MMX routines */

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)  /* i.e. not x86_64 */

static int yuv16_swap16_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP16_2_MMX(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yuv16_swapuv_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_MMX(width*height/2);
    return 1;
}

static int uyvy_yvyu_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_MMX(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yvyu_uyvy_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_MMX(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

#endif  /* HAVE_ASM_MMX && ARCH_X86 */

/*************************************************************************/

/* SSE2 routines */

#if defined(HAVE_ASM_SSE2)

static int yuv16_swap16_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP16_2_SSE2(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yuv16_swapuv_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_SSE2(width*height/2);
    return 1;
}

static int uyvy_yvyu_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_SSE2(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

static int yvyu_uyvy_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_SSE2(width*height/2);
    if (width*height % 1)
        ((uint16_t *)(dest[0]))[width*height-1] =
            src[0][width*height*2-2]<<8 | src[0][width*height*2-1];
    return 1;
}

#endif  /* HAVE_ASM_SSE2 */

/*************************************************************************/

#endif  /* ARCH_X86 || ARCH_X86_64 */

/*************************************************************************/
/*************************************************************************/

/* Initialization */

int ac_imgconvert_init_yuv_packed(int accel)
{
    if (!register_conversion(IMG_YUY2,    IMG_YUY2,    yuv16_copy)
     || !register_conversion(IMG_YUY2,    IMG_UYVY,    yuv16_swap16)
     || !register_conversion(IMG_YUY2,    IMG_YVYU,    yuv16_swapuv)

     || !register_conversion(IMG_UYVY,    IMG_YUY2,    yuv16_swap16)
     || !register_conversion(IMG_UYVY,    IMG_UYVY,    yuv16_copy)
     || !register_conversion(IMG_UYVY,    IMG_YVYU,    uyvy_yvyu)

     || !register_conversion(IMG_YVYU,    IMG_YUY2,    yuv16_swapuv)
     || !register_conversion(IMG_YVYU,    IMG_UYVY,    yvyu_uyvy)
     || !register_conversion(IMG_YVYU,    IMG_YVYU,    yuv16_copy)
    ) {
        return 0;
    }

#if defined(ARCH_X86) || defined(ARCH_X86_64)
    if (accel & (AC_IA32ASM | AC_AMD64ASM)) {
        if (!register_conversion(IMG_YUY2,    IMG_UYVY,    yuv16_swap16_x86)
         || !register_conversion(IMG_YUY2,    IMG_YVYU,    yuv16_swapuv_x86)
         || !register_conversion(IMG_UYVY,    IMG_YUY2,    yuv16_swap16_x86)
         || !register_conversion(IMG_UYVY,    IMG_YVYU,    uyvy_yvyu_x86)
         || !register_conversion(IMG_YVYU,    IMG_YUY2,    yuv16_swapuv_x86)
         || !register_conversion(IMG_YVYU,    IMG_UYVY,    yvyu_uyvy_x86)
        ) {
            return 0;
        }
    }

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)
    if (accel & AC_MMX) {
        if (!register_conversion(IMG_YUY2,    IMG_UYVY,    yuv16_swap16_mmx)
         || !register_conversion(IMG_YUY2,    IMG_YVYU,    yuv16_swapuv_mmx)
         || !register_conversion(IMG_UYVY,    IMG_YUY2,    yuv16_swap16_mmx)
         || !register_conversion(IMG_UYVY,    IMG_YVYU,    uyvy_yvyu_mmx)
         || !register_conversion(IMG_YVYU,    IMG_YUY2,    yuv16_swapuv_mmx)
         || !register_conversion(IMG_YVYU,    IMG_UYVY,    yvyu_uyvy_mmx)
        ) {
            return 0;
        }
    }
#endif

#if defined(HAVE_ASM_SSE2)
    if (accel & AC_SSE2) {
        if (!register_conversion(IMG_YUY2,    IMG_UYVY,    yuv16_swap16_sse2)
         || !register_conversion(IMG_YUY2,    IMG_YVYU,    yuv16_swapuv_sse2)
         || !register_conversion(IMG_UYVY,    IMG_YUY2,    yuv16_swap16_sse2)
         || !register_conversion(IMG_UYVY,    IMG_YVYU,    uyvy_yvyu_sse2)
         || !register_conversion(IMG_YVYU,    IMG_YUY2,    yuv16_swapuv_sse2)
         || !register_conversion(IMG_YVYU,    IMG_UYVY,    yvyu_uyvy_sse2)
        ) {
            return 0;
        }
    }
#endif

#endif  /* ARCH_X86 || ARCH_X86_64 */

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
