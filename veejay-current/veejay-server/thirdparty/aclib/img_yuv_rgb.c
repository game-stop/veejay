/*
 * img_yuv_rgb.c - YUV<->RGB image format conversion routines
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "ac.h"
#include "ac_internal.h"
#include "imgconvert.h"
#include "img_internal.h"

#include <string.h>

#define USE_LOOKUP_TABLES  /* for YUV420P->RGB24 */

/*************************************************************************/
/*************************************************************************/

/* Standard C implementations */

const int cY  =  76309;
const int crV = 104597;
const int cgU = -25675;
const int cgV = -53279;
const int cbU = 132201;

/*************************************************************************/

#ifdef USE_LOOKUP_TABLES
# define TABLE_SCALE 16   /* scale factor for Y */
static int Ylutbase[768*TABLE_SCALE];
static int *Ylut = Ylutbase+256*TABLE_SCALE;
static int rVlut[256];
static int gUlut[256];
static int gVlut[256];
static int bUlut[256];
static void yuv_create_tables(void) {
    static int yuv_tables_created = 0;
    if (!yuv_tables_created) {
        int i;
        for (i = -256*TABLE_SCALE; i < 512*TABLE_SCALE; i++) {
            int v = ((cY*(i-16*TABLE_SCALE)/TABLE_SCALE) + 32768) >> 16;
            Ylut[i] = v<0 ? 0 : v>255 ? 255 : v;
        }
        for (i = 0; i < 256; i++) {
            rVlut[i] = ((crV * (i-128)) * TABLE_SCALE + cY/2) / cY;
            gUlut[i] = ((cgU * (i-128)) * TABLE_SCALE + cY/2) / cY;
            gVlut[i] = ((cgV * (i-128)) * TABLE_SCALE + cY/2) / cY;
            bUlut[i] = ((cbU * (i-128)) * TABLE_SCALE + cY/2) / cY;
        }
        yuv_tables_created = 1;
    }
}
# define YUV2RGB(uvofs,rgbsz,rofs,gofs,bofs) do {               \
    int Y = src[0][y*width+x] * TABLE_SCALE;                    \
    int U = src[1][(uvofs)];                                    \
    int V = src[2][(uvofs)];                                    \
    dest[0][(y*width+x)*rgbsz+rofs] = Ylut[Y+rVlut[V]];         \
    dest[0][(y*width+x)*rgbsz+gofs] = Ylut[Y+gUlut[U]+gVlut[V]];\
    dest[0][(y*width+x)*rgbsz+bofs] = Ylut[Y+bUlut[U]];         \
} while (0)
# define YUV2RGB_PACKED(yofs,uofs,vofs,rgbsz,rofs,gofs,bofs) do { \
    int Y = src[0][(y*width+x)*2+yofs] * TABLE_SCALE;           \
    int U = src[0][(y*width+(x&~1))*2+uofs];                    \
    int V = src[0][(y*width+(x&~1))*2+vofs];                    \
    dest[0][(y*width+x)*rgbsz+rofs] = Ylut[Y+rVlut[V]];         \
    dest[0][(y*width+x)*rgbsz+gofs] = Ylut[Y+gUlut[U]+gVlut[V]];\
    dest[0][(y*width+x)*rgbsz+bofs] = Ylut[Y+bUlut[U]];         \
} while (0)
#else  /* !USE_LOOKUP_TABLES */
# define yuv_create_tables() /*nothing*/
# define YUV2RGB(uvofs,rgbsz,rofs,gofs,bofs) do {               \
    int Y = cY * (src[0][y*width+x] - 16);                      \
    int U = src[1][(uvofs)] - 128;                              \
    int V = src[2][(uvofs)] - 128;                              \
    int r = (Y + crV*V + 32768) >> 16;                          \
    int g = (Y + cgU*U + cgV*V + 32768) >> 16;                  \
    int b = (Y + cbU*U + 32768) >> 16;                          \
    dest[0][(y*width+x)*rgbsz+rofs] = r<0 ? 0 : r>255 ? 255 : r;\
    dest[0][(y*width+x)*rgbsz+gofs] = g<0 ? 0 : g>255 ? 255 : g;\
    dest[0][(y*width+x)*rgbsz+bofs] = b<0 ? 0 : b>255 ? 255 : b;\
} while (0)
# define YUV2RGB_PACKED(yofs,uofs,vofs,rgbsz,rofs,gofs,bofs) do { \
    int Y = cY * (src[0][(y*width+x)*2+yofs] - 16);             \
    int U = src[0][(y*width+(x&~1))*2+uofs] - 128;              \
    int V = src[0][(y*width+(x&~1))*2+vofs] - 128;              \
    int r = (Y + crV*V + 32768) >> 16;                          \
    int g = (Y + cgU*U + cgV*V + 32768) >> 16;                  \
    int b = (Y + cbU*U + 32768) >> 16;                          \
    dest[0][(y*width+x)*rgbsz+rofs] = r<0 ? 0 : r>255 ? 255 : r;\
    dest[0][(y*width+x)*rgbsz+gofs] = g<0 ? 0 : g>255 ? 255 : g;\
    dest[0][(y*width+x)*rgbsz+bofs] = b<0 ? 0 : b>255 ? 255 : b;\
} while (0)
#endif

#define YUV2RGB_420P(s,r,g,b)  YUV2RGB((y/2)*(width/2)+(x/2),s,r,g,b)
#define YUV2RGB_411P(s,r,g,b)  YUV2RGB((y  )*(width/4)+(x/4),s,r,g,b)
#define YUV2RGB_422P(s,r,g,b)  YUV2RGB((y  )*(width/2)+(x/2),s,r,g,b)
#define YUV2RGB_444P(s,r,g,b)  YUV2RGB((y  )*(width  )+(x  ),s,r,g,b)
#define YUV2RGB_YUY2(s,r,g,b)  YUV2RGB_PACKED(0,1,3,         s,r,g,b)
#define YUV2RGB_UYVY(s,r,g,b)  YUV2RGB_PACKED(1,0,2,         s,r,g,b)
#define YUV2RGB_YVYU(s,r,g,b)  YUV2RGB_PACKED(0,3,1,         s,r,g,b)

#define DEFINE_YUV2RGB(name,op) \
static int name(uint8_t **src, uint8_t **dest, int width, int height)   \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    yuv_create_tables();                                                \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < width; x++) {                                   \
            op;                                                         \
        }                                                               \
    }                                                                   \
    return 1;                                                           \
}

#define DEFINE_YUV2RGB_SET(rgb,rgbsz,rofs,gofs,bofs) \
    DEFINE_YUV2RGB(yuv420p_##rgb, YUV2RGB_420P(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(yuv411p_##rgb, YUV2RGB_411P(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(yuv422p_##rgb, YUV2RGB_422P(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(yuv444p_##rgb, YUV2RGB_444P(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(yuy2_##rgb,    YUV2RGB_YUY2(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(uyvy_##rgb,    YUV2RGB_UYVY(rgbsz,rofs,gofs,bofs)) \
    DEFINE_YUV2RGB(yvyu_##rgb,    YUV2RGB_YVYU(rgbsz,rofs,gofs,bofs))

DEFINE_YUV2RGB_SET(rgb24,  3,0,1,2)
DEFINE_YUV2RGB_SET(bgr24,  3,2,1,0)
DEFINE_YUV2RGB_SET(rgba32, 4,0,1,2)
DEFINE_YUV2RGB_SET(abgr32, 4,3,2,1)
DEFINE_YUV2RGB_SET(argb32, 4,1,2,3)
DEFINE_YUV2RGB_SET(bgra32, 4,2,1,0)

/* Y8->RGB is defined as part of grayscale stuff below */

/*************************************************************************/

#define RGB2Y() \
    (dest[0][y*width+x] = ((16829*r + 33039*g +  6416*b + 32768) >> 16) + 16)
#define RGB2U(uvofs) \
    (dest[1][(uvofs)]   = ((-9714*r - 19070*g + 28784*b + 32768) >> 16) + 128)
#define RGB2V(uvofs) \
    (dest[2][(uvofs)]   = ((28784*r - 24103*g -  4681*b + 32768) >> 16) + 128)
#define RGB2Y_PACKED(ofs) \
    (dest[0][(y*width+x)*2+(ofs)] = ((16829*r + 33039*g +  6416*b + 32768) >> 16) + 16)
#define RGB2U_PACKED(ofs) \
    (dest[0][(y*width+x)*2+(ofs)] = ((-9714*r - 19070*g + 28784*b + 32768) >> 16) + 128)
#define RGB2V_PACKED(ofs) \
    (dest[0][(y*width+x)*2+(ofs)] = ((28784*r - 24103*g -  4681*b + 32768) >> 16) + 128)

#define RGB2YUV(utest,vtest,uvofs) \
    RGB2Y(); if (utest) RGB2U(uvofs); if (vtest) RGB2V(uvofs)
#define RGB2YUV_PACKED(utest,vtest,yofs,uvofs) \
    RGB2Y_PACKED(yofs); \
    if (utest) RGB2U_PACKED(uvofs); \
    if (vtest) RGB2V_PACKED(uvofs)
/* YUV420P: take Cb/Cr from opposite corners */
#define RGB2YUV_420P  RGB2YUV(!((x|y) & 1), (x&y) & 1, (y/2)*(width/2)+(x/2))
/* YUV411P: take Cb/Cr from points 2 pixels apart */
#define RGB2YUV_411P  RGB2YUV(!(x & 3), !((x^2) & 3), y*(width/4)+(x/4))
/* YUV422P: take Cb/Cr from adjacent pixels */
#define RGB2YUV_422P  RGB2YUV(!(x & 1), x & 1, y*(width/2)+(x/2))
/* YUV444P: every pixel is sampled */
#define RGB2YUV_444P  RGB2YUV(1, 1, y*width+x)
/* YUY2/UYVY/YVYU: take Cb/Cr from the corresponding pixel */
#define RGB2YUV_YUY2  RGB2YUV_PACKED(!(x & 1), x & 1, 0,1)
#define RGB2YUV_UYVY  RGB2YUV_PACKED(!(x & 1), x & 1, 1,0)
#define RGB2YUV_YVYU  RGB2YUV_PACKED(x & 1, !(x & 1), 0,1)

#define DEFINE_RGB2YUV(name,rgbsz,rofs,gofs,bofs,op) \
static int name(uint8_t **src, uint8_t **dest, int width, int height)   \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < width; x++) {                                   \
            int r = src[0][(y*width+x)*rgbsz+rofs];                     \
            int g = src[0][(y*width+x)*rgbsz+gofs];                     \
            int b = src[0][(y*width+x)*rgbsz+bofs];                     \
            op;                                                         \
        }                                                               \
    }                                                                   \
    return 1;                                                           \
}

#define DEFINE_RGB2Y8(name,rgbsz,rofs,gofs,bofs) \
static int name(uint8_t **src, uint8_t **dest, int width, int height)   \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < width; x++) {                                   \
            int r = src[0][(y*width+x)*rgbsz+rofs];                     \
            int g = src[0][(y*width+x)*rgbsz+gofs];                     \
            int b = src[0][(y*width+x)*rgbsz+bofs];                     \
            RGB2Y();                                                    \
        }                                                               \
    }                                                                   \
    return 1;                                                           \
}

#define DEFINE_RGB2YUV_SET(rgb,rgbsz,rofs,gofs,bofs) \
    DEFINE_RGB2YUV(rgb##_yuv420p,  rgbsz,rofs,gofs,bofs, RGB2YUV_420P) \
    DEFINE_RGB2YUV(rgb##_yuv411p,  rgbsz,rofs,gofs,bofs, RGB2YUV_411P) \
    DEFINE_RGB2YUV(rgb##_yuv422p,  rgbsz,rofs,gofs,bofs, RGB2YUV_422P) \
    DEFINE_RGB2YUV(rgb##_yuv444p,  rgbsz,rofs,gofs,bofs, RGB2YUV_444P) \
    DEFINE_RGB2YUV(rgb##_yuy2,     rgbsz,rofs,gofs,bofs, RGB2YUV_YUY2) \
    DEFINE_RGB2YUV(rgb##_uyvy,     rgbsz,rofs,gofs,bofs, RGB2YUV_UYVY) \
    DEFINE_RGB2YUV(rgb##_yvyu,     rgbsz,rofs,gofs,bofs, RGB2YUV_YVYU) \
    DEFINE_RGB2Y8 (rgb##_y8,       rgbsz,rofs,gofs,bofs)

DEFINE_RGB2YUV_SET(rgb24,  3,0,1,2)
DEFINE_RGB2YUV_SET(bgr24,  3,2,1,0)
DEFINE_RGB2YUV_SET(rgba32, 4,0,1,2)
DEFINE_RGB2YUV_SET(abgr32, 4,3,2,1)
DEFINE_RGB2YUV_SET(argb32, 4,1,2,3)
DEFINE_RGB2YUV_SET(bgra32, 4,2,1,0)

/*************************************************************************/

/* All YUV planar formats convert to grayscale the same way */

#ifdef USE_LOOKUP_TABLES
static uint8_t graylut[2][256];
static int graylut_created = 0;
static void gray8_create_tables(void)
{
    if (!graylut_created) {
        int i;
        for (i = 0; i < 256; i++) {
            if (i <= 16)
                graylut[0][i] = 0;
            else if (i >= 235)
                graylut[0][i] = 255;
            else
                graylut[0][i] = (i-16) * 255 / 219;
            graylut[1][i] = 16 + i*219/255;
        }
        graylut_created = 1;
    }
}
# define Y2GRAY(val) (graylut[0][(val)])
# define GRAY2Y(val) (graylut[1][(val)])
#else
# define gray8_create_tables() /*nothing*/
# define Y2GRAY(val) ((val)<16 ? 0 : (val)>=235 ? 255 : ((val)-16)*256/219)
# define GRAY2Y(val) (16 + (val)*219/255)
#endif

static int yuvp_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i] = Y2GRAY(src[0][i]);
    return 1;
}

static int yuy2_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i] = Y2GRAY(src[0][i*2]);
    return 1;
}

static int uyvy_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i] = Y2GRAY(src[0][i*2+1]);
    return 1;
}

/*************************************************************************/

static int gray8_y8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i] = GRAY2Y(src[0][i]);
    return 1;
}

static int gray8_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!ac_imgconvert(src, IMG_GRAY8, dest, IMG_Y8, width, height))
        return 0;
    memset(dest[1], 128, (width/2)*(height/2));
    memset(dest[2], 128, (width/2)*(height/2));
    return 1;
}

static int gray8_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!ac_imgconvert(src, IMG_GRAY8, dest, IMG_Y8, width, height))
        return 0;
    memset(dest[1], 128, (width/4)*height);
    memset(dest[2], 128, (width/4)*height);
    return 1;
}

static int gray8_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!ac_imgconvert(src, IMG_GRAY8, dest, IMG_Y8, width, height))
        return 0;
    memset(dest[1], 128, (width/2)*height);
    memset(dest[2], 128, (width/2)*height);
    return 1;
}

static int gray8_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!ac_imgconvert(src, IMG_GRAY8, dest, IMG_Y8, width, height))
        return 0;
    memset(dest[1], 128, width*height);
    memset(dest[2], 128, width*height);
    return 1;
}

static int gray8_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++) {
        dest[0][i*2  ] = GRAY2Y(src[0][i]);
        dest[0][i*2+1] = 128;
    }
    return 1;
}

static int gray8_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++) {
        dest[0][i*2  ] = 128;
        dest[0][i*2+1] = GRAY2Y(src[0][i]);
    }
    return 1;
}

/*************************************************************************/

/* We only need 3 functions for Y8->RGB (no difference between RGB and BGR) */

static int y8_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i*3] = dest[0][i*3+1] = dest[0][i*3+2] = Y2GRAY(src[0][i]);
    return 1;
}

static int y8_rgba32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i*4] = dest[0][i*4+1] = dest[0][i*4+2] = Y2GRAY(src[0][i]);
    return 1;
}

static int y8_argb32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    gray8_create_tables();
    for (i = 0; i < width*height; i++)
        dest[0][i*4+1] = dest[0][i*4+2] = dest[0][i*4+3] = Y2GRAY(src[0][i]);
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* Accelerated versions of colorspace routines. */

/* Common constant values used in routines: */

#if defined(HAVE_ASM_MMX)

#include "img_x86_common.h"

static const struct { uint16_t n[72]; } __attribute__((aligned(16))) yuv_data = {{
    0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF, /* for odd/even */
    0x0010,0x0010,0x0010,0x0010,0x0010,0x0010,0x0010,0x0010, /* for Y -16    */
    0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080,0x0080, /* for U/V -128 */
    0x2543,0x2543,0x2543,0x2543,0x2543,0x2543,0x2543,0x2543, /* Y constant   */
    0x3313,0x3313,0x3313,0x3313,0x3313,0x3313,0x3313,0x3313, /* rV constant  */
    0xF377,0xF377,0xF377,0xF377,0xF377,0xF377,0xF377,0xF377, /* gU constant  */
    0xE5FC,0xE5FC,0xE5FC,0xE5FC,0xE5FC,0xE5FC,0xE5FC,0xE5FC, /* gV constant  */
    0x408D,0x408D,0x408D,0x408D,0x408D,0x408D,0x408D,0x408D, /* bU constant  */
    0x0008,0x0008,0x0008,0x0008,0x0008,0x0008,0x0008,0x0008, /* for rounding */
}};
/* Note that G->Y exceeds 0x7FFF, so be careful to treat it as unsigned
 * (the rest of the values are signed) */
static const struct { uint16_t n[96]; } __attribute__((aligned(16))) rgb_data = {{
    0x41BD,0x41BD,0x41BD,0x41BD,0x41BD,0x41BD,0x41BD,0x41BD, /* R->Y         */
    0x810F,0x810F,0x810F,0x810F,0x810F,0x810F,0x810F,0x810F, /* G->Y         */
    0x1910,0x1910,0x1910,0x1910,0x1910,0x1910,0x1910,0x1910, /* B->Y         */
    0xDA0E,0xDA0E,0xDA0E,0xDA0E,0xDA0E,0xDA0E,0xDA0E,0xDA0E, /* R->U         */
    0xB582,0xB582,0xB582,0xB582,0xB582,0xB582,0xB582,0xB582, /* G->U         */
    0x7070,0x7070,0x7070,0x7070,0x7070,0x7070,0x7070,0x7070, /* B->U         */
    0x7070,0x7070,0x7070,0x7070,0x7070,0x7070,0x7070,0x7070, /* R->V         */
    0xA1D9,0xA1D9,0xA1D9,0xA1D9,0xA1D9,0xA1D9,0xA1D9,0xA1D9, /* G->V         */
    0xEDB7,0xEDB7,0xEDB7,0xEDB7,0xEDB7,0xEDB7,0xEDB7,0xEDB7, /* B->V         */
    0x0420,0x0420,0x0420,0x0420,0x0420,0x0420,0x0420,0x0420, /* Y +16.5      */
    0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020, /* U/V +128.5   */
    0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF,0x00FF, /* for odd/even */
}};
#define Y_GRAY 0x4A85
#define GRAY_Y 0x36F7
static const struct { uint16_t n[32]; } __attribute__((aligned(16))) gray_data = {{
    Y_GRAY,Y_GRAY,Y_GRAY,Y_GRAY,Y_GRAY,Y_GRAY,Y_GRAY,Y_GRAY, /* 255/219      */
    GRAY_Y,GRAY_Y,GRAY_Y,GRAY_Y,GRAY_Y,GRAY_Y,GRAY_Y,GRAY_Y, /* 219/255      */
    0x0010,0x0010,0x0010,0x0010,0x0010,0x0010,0x0010,0x0010, /* Y +/-16      */
    0x00FF,0xFF00,0x0000,0x00FF,0xFF00,0x0000,0x0000,0x0000, /* for Y->RGB   */
}};

/* Convert 4 RGB32 pixels in EAX/EBX/ECX/EDX to RGB24 in EAX/EBX/ECX */
#define IA32_RGB32_TO_RGB24 \
        "movl %%ebx, %%esi      # ESI: 00 B1 G1 R1                      \n\
        shll $24, %%esi         # ESI: R1 00 00 00                      \n\
        shrl $8, %%ebx          # EBX: 00 00 B1 G1                      \n\
        orl %%esi, %%eax        # EAX: R1 B0 G0 R0                      \n\
        movl %%ecx, %%esi       # ESI: 00 B2 G2 R2                      \n\
        shll $16, %%esi         # ESI: G2 R2 00 00                      \n\
        shrl $16, %%ecx         # ECX: 00 00 00 B2                      \n\
        shll $8, %%edx          # EDX: B3 G3 R3 00                      \n\
        orl %%esi, %%ebx        # EBX: G2 R2 B1 G1                      \n\
        orl %%edx, %%ecx        # ECX: B3 G3 R3 B2                      \n"

/* Convert 4 RGB24 pixels in EAX/EBX/ECX to RGB32 in EAX/EBX/ECX/EDX */
#define IA32_RGB24_TO_RGB32 \
        "movl %%ecx, %%edx      # EDX: B3 G3 R3 B2                      \n\
        shrl $8, %%edx          # EDX: 00 B3 G3 R3                      \n\
        andl $0xFF, %%ecx       # ECX: 00 00 00 B2                      \n\
        movl %%ebx, %%edi       # EDI: G2 R2 B1 G1                      \n\
        andl $0xFFFF0000, %%edi # EDI: G2 R2 00 00                      \n\
        orl %%edi, %%ecx        # ECX: G2 R2 00 B2                      \n\
        rorl $16, %%ecx         # ECX: 00 B2 G2 R2                      \n\
        movl %%eax, %%edi       # EDI: R1 B0 G0 R0                      \n\
        andl $0xFF000000, %%edi # EDI: R1 00 00 00                      \n\
        andl $0x0000FFFF, %%ebx # EBX: 00 00 B1 G1                      \n\
        orl %%edi, %%ebx        # EBX: R1 00 B1 G1                      \n\
        roll $8, %%ebx          # EBX: 00 B1 G1 R1                      \n\
        andl $0x00FFFFFF, %%eax # EAX: 00 B0 G0 R0                      \n"

#endif  /* HAVE_ASM_MMX */

/*************************************************************************/
/*************************************************************************/

/* MMX routines */

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)  /* i.e. not x86_64 */

static inline void mmx_yuv42Xp_to_rgb(uint8_t *srcY, uint8_t *srcU,
                                      uint8_t *srcV);
#define mmx_yuv420p_to_rgb mmx_yuv42Xp_to_rgb
#define mmx_yuv422p_to_rgb mmx_yuv42Xp_to_rgb
static inline void mmx_store_rgb24(uint8_t *dest);
static inline void mmx_store_bgr24(uint8_t *dest);
static inline void mmx_store_rgba32(uint8_t *dest);
static inline void mmx_store_abgr32(uint8_t *dest);
static inline void mmx_store_argb32(uint8_t *dest);
static inline void mmx_store_bgra32(uint8_t *dest);

#define DEFINE_YUV2RGB_MMX(yuv,rgb,uvofs,rgbsz,rofs,gofs,bofs) \
static int yuv##_##rgb##_mmx(uint8_t **src, uint8_t **dest,             \
                             int width, int height)                     \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    yuv_create_tables();                                                \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < (width & ~7); x += 8) {                         \
            mmx_##yuv##_to_rgb(src[0]+y*width+x,                        \
                               src[1]+(uvofs), src[2]+(uvofs));         \
            mmx_store_##rgb(dest[0]+(y*width+x)*rgbsz);                 \
        }                                                               \
        while (x < width) {                                             \
            YUV2RGB(uvofs,rgbsz,rofs,gofs,bofs);                        \
            x++;                                                        \
        }                                                               \
    }                                                                   \
    asm("emms");                                                        \
    return 1;                                                           \
}

#define DEFINE_YUV2RGB_MMX_SET(rgb,rgbsz,rofs,gofs,bofs) \
    DEFINE_YUV2RGB_MMX(yuv420p,rgb,(y/2)*(width/2)+(x/2),rgbsz,rofs,gofs,bofs)\
    DEFINE_YUV2RGB_MMX(yuv422p,rgb,(y  )*(width/2)+(x/2),rgbsz,rofs,gofs,bofs)

DEFINE_YUV2RGB_MMX_SET(rgb24,  3,0,1,2)
DEFINE_YUV2RGB_MMX_SET(bgr24,  3,2,1,0)
DEFINE_YUV2RGB_MMX_SET(rgba32, 4,0,1,2)
DEFINE_YUV2RGB_MMX_SET(abgr32, 4,3,2,1)
DEFINE_YUV2RGB_MMX_SET(argb32, 4,1,2,3)
DEFINE_YUV2RGB_MMX_SET(bgra32, 4,2,1,0)

/************************************/

static inline void mmx_yuv42Xp_to_rgb(uint8_t *srcY, uint8_t *srcU,
                                      uint8_t *srcV)
{
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%mm4, %%mm4       # MM4: 00 00 00 00 00 00 00 00          \n\
        movq ("EAX"), %%mm6     # MM6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0          \n\
        movd ("ECX"), %%mm2     # MM2:             U3 U2 U1 U0          \n\
        movd ("EDX"), %%mm3     # MM3:             V3 V2 V1 V0          \n\
        movq %%mm6, %%mm7       # MM7: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0          \n\
        pand ("ESI"), %%mm6     # MM6:  -Y6-  -Y4-  -Y2-  -Y0-          \n\
        psrlw $8, %%mm7         # MM7:  -Y7-  -Y5-  -Y3-  -Y1-          \n\
        punpcklbw %%mm4, %%mm2  # MM2:  -U3-  -U2-  -U1-  -U0-          \n\
        punpcklbw %%mm4, %%mm3  # MM3:  -V3-  -V2-  -V1-  -V0-          \n\
        psubw 16("ESI"), %%mm6  # MM6: subtract 16                      \n\
        psubw 16("ESI"), %%mm7  # MM7: subtract 16                      \n\
        psubw 32("ESI"), %%mm2  # MM2: subtract 128                     \n\
        psubw 32("ESI"), %%mm3  # MM3: subtract 128                     \n\
        psllw $7, %%mm6         # MM6: convert to fixed point 8.7       \n\
        psllw $7, %%mm7         # MM7: convert to fixed point 8.7       \n\
        psllw $7, %%mm2         # MM2: convert to fixed point 8.7       \n\
        psllw $7, %%mm3         # MM3: convert to fixed point 8.7       \n\
        # Multiply by constants                                         \n\
        pmulhw 48("ESI"), %%mm6 # MM6: -cY6- -cY4- -cY2- -cY0-          \n\
        pmulhw 48("ESI"), %%mm7 # MM6: -cY7- -cY5- -cY3- -cY1-          \n\
        movq 80("ESI"), %%mm4   # MM4: gU constant                      \n\
        movq 96("ESI"), %%mm5   # MM5: gV constant                      \n\
        pmulhw %%mm2, %%mm4     # MM4: -gU3- -gU2- -gU1- -gU0-          \n\
        pmulhw %%mm3, %%mm5     # MM5: -gV3- -gV2- -gV1- -gV0-          \n\
        paddw %%mm5, %%mm4      # MM4:  -g3-  -g2-  -g1-  -g0-          \n\
        pmulhw 64("ESI"), %%mm3 # MM3:  -r3-  -r2-  -r1-  -r0-          \n\
        pmulhw 112("ESI"),%%mm2 # MM2:  -b3-  -b2-  -b1-  -b0-          \n\
        movq %%mm3, %%mm0       # MM0:  -r3-  -r2-  -r1-  -r0-          \n\
        movq %%mm4, %%mm1       # MM1:  -g3-  -g2-  -g1-  -g0-          \n\
        movq %%mm2, %%mm5       # MM5:  -b3-  -b2-  -b1-  -b0-          \n\
        # Add intermediate results and round/shift to get R/G/B values  \n\
        paddw 128("ESI"), %%mm6 # Add rounding value (0.5 @ 8.4 fixed)  \n\
        paddw 128("ESI"), %%mm7                                         \n\
        paddw %%mm6, %%mm0      # MM0:  -R6-  -R4-  -R2-  -R0-          \n\
        psraw $4, %%mm0         # Shift back to 8.0 fixed               \n\
        paddw %%mm6, %%mm1      # MM1:  -G6-  -G4-  -G2-  -G0-          \n\
        psraw $4, %%mm1                                                 \n\
        paddw %%mm6, %%mm2      # MM2:  -B6-  -B4-  -B2-  -B0-          \n\
        psraw $4, %%mm2                                                 \n\
        paddw %%mm7, %%mm3      # MM3:  -R7-  -R5-  -R3-  -R1-          \n\
        psraw $4, %%mm3                                                 \n\
        paddw %%mm7, %%mm4      # MM4:  -G7-  -G5-  -G3-  -G1-          \n\
        psraw $4, %%mm4                                                 \n\
        paddw %%mm7, %%mm5      # MM5:  -B7-  -B5-  -B3-  -B1-          \n\
        psraw $4, %%mm5                                                 \n\
        # Saturate to 0-255 and pack into bytes                         \n\
        packuswb %%mm0, %%mm0   # MM0: R6 R4 R2 R0 R6 R4 R2 R0          \n\
        packuswb %%mm1, %%mm1   # MM1: G6 G4 G2 G0 G6 G4 G2 G0          \n\
        packuswb %%mm2, %%mm2   # MM2: B6 B4 B2 B0 B6 B4 B2 B0          \n\
        packuswb %%mm3, %%mm3   # MM3: R7 R5 R3 R1 R7 R5 R3 R1          \n\
        packuswb %%mm4, %%mm4   # MM4: G7 G5 G3 G1 G7 G5 G3 G1          \n\
        packuswb %%mm5, %%mm5   # MM5: B7 B5 B3 B1 B7 B5 B3 B1          \n\
        punpcklbw %%mm3, %%mm0  # MM0: R7 R6 R5 R4 R3 R2 R1 R0          \n\
        punpcklbw %%mm4, %%mm1  # MM1: G7 G6 G5 G4 G3 G2 G1 G0          \n\
        punpcklbw %%mm5, %%mm2  # MM2: B7 B6 B5 B4 B3 B2 B1 B0          \n"
        : /* no outputs */
        : "a" (srcY), "c" (srcU), "d" (srcV), "S" (&yuv_data), "m" (yuv_data)
    );
}

/************************************/

/* Convert YUV->RGB output to RGBA pixels in MM0..MM3 */
#define MMX_RGB_TO_RGBA "\
        pxor %%mm7, %%mm7       # MM7: 00 00 00 00 00 00 00 00          \n\
        movq %%mm0, %%mm3       # MM3: R7 R6 R5 R4 R3 R2 R1 R0          \n\
        movq %%mm1, %%mm4       # MM4: G7 G6 G5 G4 G3 G2 G1 G0          \n\
        movq %%mm2, %%mm5       # MM5: B7 B6 B5 B4 B3 B2 B1 B0          \n\
        punpcklbw %%mm1, %%mm0  # MM0: G3 R3 G2 R2 G1 R1 G0 R0          \n\
        punpcklbw %%mm7, %%mm2  # MM2: 00 B3 00 B2 00 B1 00 B0          \n\
        movq %%mm0, %%mm1       # MM1: G3 R3 G2 R2 G1 R1 G0 R0          \n\
        punpcklwd %%mm2, %%mm0  # MM0: 00 B1 G1 R1 00 B0 G0 R0          \n\
        punpckhwd %%mm2, %%mm1  # MM1: 00 B3 G3 R3 00 B2 G2 R2          \n\
        punpckhbw %%mm4, %%mm3  # MM3: G7 R7 G6 R6 G5 R5 G4 R4          \n\
        punpckhbw %%mm7, %%mm5  # MM5: 00 B7 00 B6 00 B5 00 B4          \n\
        movq %%mm3, %%mm2       # MM2: G7 R7 G6 R6 G5 R5 G4 R4          \n\
        punpckhwd %%mm5, %%mm3  # MM3: 00 B7 G7 R7 00 B6 G6 R6          \n\
        punpcklwd %%mm5, %%mm2  # MM2: 00 B5 G5 R5 00 B4 G4 R4          \n"

/* Convert YUV->RGB output to BGRA pixels in MM0..MM3 */
#define MMX_RGB_TO_BGRA "\
        pxor %%mm7, %%mm7       # MM7: 00 00 00 00 00 00 00 00          \n\
        movq %%mm0, %%mm5       # MM5: R7 R6 R5 R4 R3 R2 R1 R0          \n\
        movq %%mm1, %%mm4       # MM4: G7 G6 G5 G4 G3 G2 G1 G0          \n\
        movq %%mm2, %%mm3       # MM3: B7 B6 B5 B4 B3 B2 B1 B0          \n\
        punpcklbw %%mm1, %%mm2  # MM2: G3 B3 G2 B2 G1 B1 G0 B0          \n\
        punpcklbw %%mm7, %%mm0  # MM0: 00 R3 00 R2 00 R1 00 R0          \n\
        movq %%mm2, %%mm1       # MM1: G3 B3 G2 B2 G1 B1 G0 B0          \n\
        punpcklwd %%mm0, %%mm2  # MM2: 00 R1 G1 B1 00 R0 G0 B0          \n\
        punpckhwd %%mm0, %%mm1  # MM1: 00 R3 G3 B3 00 R2 G2 B2          \n\
        movq %%mm2, %%mm0       # MM0: 00 R1 G1 B1 00 R0 G0 B0          \n\
        punpckhbw %%mm4, %%mm3  # MM3: G7 B7 G6 B6 G5 B5 G4 B4          \n\
        punpckhbw %%mm7, %%mm5  # MM5: 00 R7 00 R6 00 R5 00 R4          \n\
        movq %%mm3, %%mm2       # MM2: G7 B7 G6 B6 G5 B5 G4 B4          \n\
        punpckhwd %%mm5, %%mm3  # MM3: 00 R7 G7 B7 00 R6 G6 B6          \n\
        punpcklwd %%mm5, %%mm2  # MM2: 00 R5 G5 B5 00 R4 G4 B4          \n"


static inline void mmx_store_rgb24(uint8_t *dest)
{
    /* It looks like it's fastest to go to RGB32 first, then shift the
     * result to merge the 24-bit pixels together. */
    asm(MMX_RGB_TO_RGBA                                                  "\
        movq %%mm0, %%mm4       # MM4: 00 B1 G1 R1 00 B0 G0 R0          \n\
        movq %%mm1, %%mm5       # MM5: 00 B3 G3 R3 00 B2 G2 R2          \n\
        movq %%mm2, %%mm6       # MM6: 00 B5 G5 R5 00 B4 G4 R4          \n\
        movq %%mm3, %%mm7       # MM7: 00 B7 G7 R7 00 B6 G6 R6          \n\
        psrlq $32, %%mm4        # MM4: 00 00 00 00 00 B1 G1 R1          \n\
        psrlq $32, %%mm5        # MM5: 00 00 00 00 00 B3 G3 R3          \n\
        psrlq $32, %%mm6        # MM6: 00 00 00 00 00 B5 G5 R5          \n\
        psrlq $32, %%mm7        # MM7: 00 00 00 00 00 B7 G7 R7          \n\
        push "EBX"                                                      \n\
        movd %%mm0, %%eax       # EAX: 00 B0 G0 R0                      \n\
        movd %%mm4, %%ebx       # EBX: 00 B1 G1 R1                      \n\
        movd %%mm1, %%ecx       # ECX: 00 B2 G2 R2                      \n\
        movd %%mm5, %%edx       # EDX: 00 B3 G3 R3                      \n\
        "IA32_RGB32_TO_RGB24"                                           \n\
        movl %%eax, ("EDI")                                             \n\
        movl %%ebx, 4("EDI")                                            \n\
        movl %%ecx, 8("EDI")                                            \n\
        movd %%mm2, %%eax       # EAX: 00 B4 G4 R4                      \n\
        movd %%mm6, %%ebx       # EBX: 00 B5 G5 R5                      \n\
        movd %%mm3, %%ecx       # ECX: 00 B6 G6 R6                      \n\
        movd %%mm7, %%edx       # EDX: 00 B7 G7 R7                      \n\
        "IA32_RGB32_TO_RGB24"                                           \n\
        movl %%eax, 12("EDI")                                           \n\
        movl %%ebx, 16("EDI")                                           \n\
        movl %%ecx, 20("EDI")                                           \n\
        pop "EBX"                                                       \n"
        : /* no outputs */
        : "D" (dest)
        : "eax", "ecx", "edx", "esi"
    );
}

static inline void mmx_store_bgr24(uint8_t *dest)
{
    asm(MMX_RGB_TO_BGRA                                                  "\
        movq %%mm0, %%mm4       # MM4: 00 B1 G1 R1 00 B0 G0 R0          \n\
        movq %%mm1, %%mm5       # MM5: 00 B3 G3 R3 00 B2 G2 R2          \n\
        movq %%mm2, %%mm6       # MM6: 00 B5 G5 R5 00 B4 G4 R4          \n\
        movq %%mm3, %%mm7       # MM7: 00 B7 G7 R7 00 B6 G6 R6          \n\
        psrlq $32, %%mm4        # MM4: 00 00 00 00 00 B1 G1 R1          \n\
        psrlq $32, %%mm5        # MM5: 00 00 00 00 00 B3 G3 R3          \n\
        psrlq $32, %%mm6        # MM6: 00 00 00 00 00 B5 G5 R5          \n\
        psrlq $32, %%mm7        # MM7: 00 00 00 00 00 B7 G7 R7          \n\
        push "EBX"                                                      \n\
        movd %%mm0, %%eax       # EAX: 00 B0 G0 R0                      \n\
        movd %%mm4, %%ebx       # EBX: 00 B1 G1 R1                      \n\
        movd %%mm1, %%ecx       # ECX: 00 B2 G2 R2                      \n\
        movd %%mm5, %%edx       # EDX: 00 B3 G3 R3                      \n\
        "IA32_RGB32_TO_RGB24"                                           \n\
        movl %%eax, ("EDI")                                             \n\
        movl %%ebx, 4("EDI")                                            \n\
        movl %%ecx, 8("EDI")                                            \n\
        movd %%mm2, %%eax       # EAX: 00 B4 G4 R4                      \n\
        movd %%mm6, %%ebx       # EBX: 00 B5 G5 R5                      \n\
        movd %%mm3, %%ecx       # ECX: 00 B6 G6 R6                      \n\
        movd %%mm7, %%edx       # EDX: 00 B7 G7 R7                      \n\
        "IA32_RGB32_TO_RGB24"                                           \n\
        movl %%eax, 12("EDI")                                           \n\
        movl %%ebx, 16("EDI")                                           \n\
        movl %%ecx, 20("EDI")                                           \n\
        pop "EBX"                                                       \n"
        : /* no outputs */
        : "D" (dest)
        : "eax", "ecx", "edx", "esi"
    );
}

static inline void mmx_store_rgba32(uint8_t *dest)
{
    asm(MMX_RGB_TO_RGBA                                                  "\
        movq %%mm0,   ("EDI")                                           \n\
        movq %%mm1,  8("EDI")                                           \n\
        movq %%mm2, 16("EDI")                                           \n\
        movq %%mm3, 24("EDI")                                           \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void mmx_store_abgr32(uint8_t *dest)
{
    asm(MMX_RGB_TO_BGRA                                                  "\
        psllq $8, %%mm0                                                 \n\
        psllq $8, %%mm1                                                 \n\
        psllq $8, %%mm2                                                 \n\
        psllq $8, %%mm3                                                 \n\
        movq %%mm0,   ("EDI")                                           \n\
        movq %%mm1,  8("EDI")                                           \n\
        movq %%mm2, 16("EDI")                                           \n\
        movq %%mm3, 24("EDI")                                           \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void mmx_store_argb32(uint8_t *dest)
{
    asm(MMX_RGB_TO_RGBA                                                  "\
        psllq $8, %%mm0                                                 \n\
        psllq $8, %%mm1                                                 \n\
        psllq $8, %%mm2                                                 \n\
        psllq $8, %%mm3                                                 \n\
        movq %%mm0,   ("EDI")                                           \n\
        movq %%mm1,  8("EDI")                                           \n\
        movq %%mm2, 16("EDI")                                           \n\
        movq %%mm3, 24("EDI")                                           \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void mmx_store_bgra32(uint8_t *dest)
{
    asm(MMX_RGB_TO_BGRA                                                  "\
        movq %%mm0,   ("EDI")                                           \n\
        movq %%mm1,  8("EDI")                                           \n\
        movq %%mm2, 16("EDI")                                           \n\
        movq %%mm3, 24("EDI")                                           \n"
        : /* no outputs */
        : "D" (dest)
    );
}

#endif  /* HAVE_ASM_MMX && ARCH_X86 */

/*************************************************************************/
/*************************************************************************/

/* SSE2 routines */

#if defined(HAVE_ASM_SSE2)

/*************************************************************************/

static inline void sse2_load_yuv420p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_yuv411p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_yuv422p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_yuv444p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_yuy2(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_uyvy(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width);
static inline void sse2_load_yvyu(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width);
static inline void sse2_yuv_to_rgb(void);
static inline void sse2_yuv444_to_rgb(void);
static inline void sse2_store_rgb24(uint8_t *dest);
static inline void sse2_store_bgr24(uint8_t *dest);
static inline void sse2_store_rgba32(uint8_t *dest);
static inline void sse2_store_abgr32(uint8_t *dest);
static inline void sse2_store_argb32(uint8_t *dest);
static inline void sse2_store_bgra32(uint8_t *dest);

#define DEFINE_YUV2RGB_SSE2(yuv,y2r,rgb,rgbsz,slowop) \
static int yuv##_##rgb##_sse2(uint8_t **src, uint8_t **dest,            \
                              int width, int height)                    \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    yuv_create_tables();                                                \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < (width & ~15); x += 16) {                       \
            sse2_load_##yuv(src[0], src[1], src[2], x, y, width);       \
            sse2_##y2r();                                               \
            sse2_store_##rgb(dest[0] + (y*width+x)*rgbsz);              \
        }                                                               \
        while (x < width) {                                             \
            slowop;                                                     \
            x++;                                                        \
        }                                                               \
    }                                                                   \
    asm("emms");                                                        \
    return 1;                                                           \
}

#define DEFINE_YUV2RGB_SSE2_SET(rgb,sz,r,g,b) \
    DEFINE_YUV2RGB_SSE2(yuv420p, yuv_to_rgb,   rgb,sz, YUV2RGB_420P(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(yuv411p, yuv_to_rgb,   rgb,sz, YUV2RGB_411P(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(yuv422p, yuv_to_rgb,   rgb,sz, YUV2RGB_422P(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(yuv444p, yuv444_to_rgb,rgb,sz, YUV2RGB_444P(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(yuy2,    yuv_to_rgb,   rgb,sz, YUV2RGB_YUY2(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(uyvy,    yuv_to_rgb,   rgb,sz, YUV2RGB_UYVY(sz,r,g,b))\
    DEFINE_YUV2RGB_SSE2(yvyu,    yuv_to_rgb,   rgb,sz, YUV2RGB_YVYU(sz,r,g,b))

DEFINE_YUV2RGB_SSE2_SET(rgb24,  3,0,1,2)
DEFINE_YUV2RGB_SSE2_SET(bgr24,  3,2,1,0)
DEFINE_YUV2RGB_SSE2_SET(rgba32, 4,0,1,2)
DEFINE_YUV2RGB_SSE2_SET(abgr32, 4,3,2,1)
DEFINE_YUV2RGB_SSE2_SET(argb32, 4,1,2,3)
DEFINE_YUV2RGB_SSE2_SET(bgra32, 4,2,1,0)

/************************************/

static inline void sse2_load_yuv420p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width)
{
    srcY += y*width+x;
    srcU += (y/2)*(width/2)+(x/2);
    srcV += (y/2)*(width/2)+(x/2);
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: YF...................Y0         \n\
        movq ("ECX"), %%xmm2    # XMM2:             U7.......U0         \n\
        movq ("EDX"), %%xmm3    # XMM3:             V7.......V0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n\
        punpcklbw %%xmm4,%%xmm2 # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        punpcklbw %%xmm4,%%xmm3 # XMM3: V7 V6 V5 V4 V3 V2 V1 V0         \n"
        : /* no outputs */
        : "a" (srcY), "c" (srcU), "d" (srcV), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_yuv411p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width)
{
    srcY += y*width+x;
    srcU += y*(width/4)+(x/4);
    srcV += y*(width/4)+(x/4);
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: YF...................Y0         \n\
        movd ("ECX"), %%xmm2    # XMM2:                   U3.U0         \n\
        punpcklbw %%xmm2,%%xmm2 # XMM2:             U3 U3.U0 U0         \n\
        movd ("EDX"), %%xmm3    # XMM3:                   V3.V0         \n\
        punpcklbw %%xmm3,%%xmm3 # XMM2:             V3 V3.V0 V0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n\
        punpcklbw %%xmm4,%%xmm2 # XMM2: U3 U3 U2 U2 U1 U1 U0 U0         \n\
        punpcklbw %%xmm4,%%xmm3 # XMM3: V3 V3 V2 V2 V1 V1 V0 V0         \n"
        : /* no outputs */
        : "a" (srcY), "c" (srcU), "d" (srcV), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_yuv422p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width)
{
    srcY += y*width+x;
    srcU += y*(width/2)+(x/2);
    srcV += y*(width/2)+(x/2);
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: YF...................Y0         \n\
        movq ("ECX"), %%xmm2    # XMM2:             U7.......U0         \n\
        movq ("EDX"), %%xmm3    # XMM3:             V7.......V0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n\
        punpcklbw %%xmm4,%%xmm2 # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        punpcklbw %%xmm4,%%xmm3 # XMM3: V7 V6 V5 V4 V3 V2 V1 V0         \n"
        : /* no outputs */
        : "a" (srcY), "c" (srcU), "d" (srcV), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_yuv444p(uint8_t *srcY, uint8_t *srcU,
                                     uint8_t *srcV, int x, int y, int width)
{
    srcY += y*width+x;
    srcU += y*width+x;
    srcV += y*width+x;
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: YF...................Y0         \n\
        movdqu ("ECX"), %%xmm2  # XMM2: UF...................U0         \n\
        movdqu ("EDX"), %%xmm0  # XMM0: VF...................V0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        punpcklbw %%xmm4,%%xmm6 # XMM6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0         \n\
        punpckhbw %%xmm4,%%xmm7 # XMM7: YF YE YD YC YB YA Y9 Y8         \n\
        movdqa %%xmm2, %%xmm5   # XMM5: UF...................U0         \n\
        punpcklbw %%xmm4,%%xmm2 # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        punpckhbw %%xmm4,%%xmm5 # XMM5: UF UE UD UC UB UA U9 U8         \n\
        movdqa %%xmm0, %%xmm3   # XMM3: VF...................V0         \n\
        punpcklbw %%xmm4,%%xmm0 # XMM0: V7 V6 V5 V4 V3 V2 V1 V0         \n\
        punpckhbw %%xmm4,%%xmm3 # XMM3: VF VE VD VC VB VA V9 V8         \n"
        : /* no outputs */
        : "a" (srcY), "c" (srcU), "d" (srcV), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_yuy2(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width)
{
    srcY += (y*width+x)*2;
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: V3 Y7.............U0 Y0         \n\
        movdqu 16("EAX"),%%xmm7 # XMM7: V7 YF.............U4 Y8         \n\
        movdqa %%xmm6, %%xmm2   # XMM2: V3 Y7.............U0 Y0         \n\
        psrlw $8, %%xmm2        # XMM2: V3 U3 V2 U2 V1 U1 V0 U0         \n\
        pand ("ESI"), %%xmm6    # XMM6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0         \n\
        movdqa %%xmm7, %%xmm3   # XMM3: V7 YF.............U4 Y8         \n\
        psrlw $8, %%xmm3        # XMM3: V7 U7 V6 U6 V5 U5 V4 U4         \n\
        pand ("ESI"), %%xmm7    # XMM6: YF YE YD YC YB YA Y9 Y8         \n\
        packuswb %%xmm3, %%xmm2 # XMM2: V7 U7.............V0 U0         \n\
        movdqa %%xmm2, %%xmm3   # XMM3: V7 U7.............V0 U0         \n\
        pand ("ESI"), %%xmm2    # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        psrlw $8, %%xmm3        # XMM3: V7 V6 V5 V4 V3 V2 V1 V0         \n\
        packuswb %%xmm7, %%xmm6 # XMM6: YF...................Y0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n"
        : /* no outputs */
        : "a" (srcY), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_uyvy(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width)
{
    srcY += (y*width+x)*2;
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: Y7 V3.............Y0 00         \n\
        movdqu 16("EAX"),%%xmm7 # XMM7: YF V7.............Y8 U4         \n\
        movdqa %%xmm6, %%xmm2   # XMM2: Y7 V3.............Y0 U0         \n\
        pand ("ESI"), %%xmm2    # XMM2: V3 U3 V2 U2 V1 U1 V0 U0         \n\
        psrlw $8, %%xmm6        # XMM6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0         \n\
        movdqa %%xmm7, %%xmm3   # XMM3: YF V7.............Y8 U4         \n\
        pand ("ESI"), %%xmm3    # XMM3: V7 U7 V6 U6 V5 U5 V4 U4         \n\
        psrlw $8, %%xmm7        # XMM6: YF YE YD YC YB YA Y9 Y8         \n\
        packuswb %%xmm3, %%xmm2 # XMM2: V7 U7.............V0 U0         \n\
        movdqa %%xmm2, %%xmm3   # XMM3: V7 U7.............V0 U0         \n\
        pand ("ESI"), %%xmm2    # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        psrlw $8, %%xmm3        # XMM3: V7 V6 V5 V4 V3 V2 V1 V0         \n\
        packuswb %%xmm7, %%xmm6 # XMM6: YF...................Y0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n"
        : /* no outputs */
        : "a" (srcY), "S" (&yuv_data), "m" (yuv_data)
    );
}

static inline void sse2_load_yvyu(uint8_t *srcY, uint8_t *srcU,
                                  uint8_t *srcV, int x, int y, int width)
{
    srcY += (y*width+x)*2;
    asm("\
        # Load data, bias and expand to 16 bits                         \n\
        pxor %%xmm4, %%xmm4     # XMM4: 00 00 00 00 00 00 00 00         \n\
        movdqu ("EAX"), %%xmm6  # XMM6: U3 Y7.............V0 Y0         \n\
        movdqu 16("EAX"),%%xmm7 # XMM7: U7 YF.............V4 Y8         \n\
        movdqa %%xmm6, %%xmm2   # XMM2: U3 Y7.............V0 Y0         \n\
        psrlw $8, %%xmm2        # XMM2: U3 V3 U2 V2 U1 V1 U0 V0         \n\
        pand ("ESI"), %%xmm6    # XMM6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0         \n\
        movdqa %%xmm7, %%xmm3   # XMM3: U7 YF.............V4 Y8         \n\
        psrlw $8, %%xmm3        # XMM3: U7 V7 U6 V6 U5 V5 U4 V4         \n\
        pand ("ESI"), %%xmm7    # XMM6: YF YE YD YC YB YA Y9 Y8         \n\
        packuswb %%xmm3, %%xmm2 # XMM2: U7 V7.............U0 V0         \n\
        movdqa %%xmm2, %%xmm3   # XMM3: U7 V7.............U0 V0         \n\
        psrlw $8, %%xmm2        # XMM2: U7 U6 U5 U4 U3 U2 U1 U0         \n\
        pand ("ESI"), %%xmm3    # XMM3: V7 V6 V5 V4 V3 V2 V1 V0         \n\
        packuswb %%xmm7, %%xmm6 # XMM6: YF...................Y0         \n\
        movdqa %%xmm6, %%xmm7   # XMM7: YF...................Y0         \n\
        pand ("ESI"), %%xmm6    # XMM6: YE YC YA Y8 Y6 Y4 Y2 Y0         \n\
        psrlw $8, %%xmm7        # XMM7: YF YD YB Y9 Y7 Y5 Y3 Y1         \n"
        : /* no outputs */
        : "a" (srcY), "S" (&yuv_data), "m" (yuv_data)
    );
}

/************************************/

/* Standard YUV->RGB (Yodd=XMM7 Yeven=XMM6 U=XMM2 V=XMM3) */
static inline void sse2_yuv_to_rgb(void)
{
    asm("\
        psubw 16("ESI"), %%xmm6 # XMM6: subtract 16                     \n\
        psllw $7, %%xmm6        # XMM6: convert to fixed point 8.7      \n\
        psubw 16("ESI"), %%xmm7 # XMM7: subtract 16                     \n\
        psllw $7, %%xmm7        # XMM7: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm2 # XMM2: subtract 128                    \n\
        psllw $7, %%xmm2        # XMM2: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm3 # XMM3: subtract 128                    \n\
        psllw $7, %%xmm3        # XMM3: convert to fixed point 8.7      \n\
        # Multiply by constants                                         \n\
        pmulhw 48("ESI"),%%xmm6 # XMM6: cYE.................cY0         \n\
        pmulhw 48("ESI"),%%xmm7 # XMM7: cYF.................cY1         \n\
        movdqa 80("ESI"),%%xmm4 # XMM4: gU constant                     \n\
        pmulhw %%xmm2, %%xmm4   # XMM4: gU7.................gU0         \n\
        movdqa 96("ESI"),%%xmm5 # XMM5: gV constant                     \n\
        pmulhw %%xmm3, %%xmm5   # XMM5: gV7.................gV0         \n\
        paddw %%xmm5, %%xmm4    # XMM4: g7 g6 g5 g4 g3 g2 g1 g0         \n\
        pmulhw 64("ESI"),%%xmm3 # XMM3: r7 r6 r5 r4 r3 r2 r1 r0         \n\
        pmulhw 112("ESI"),%%xmm2 #XMM2: b7 b6 b5 b4 b3 b2 b1 b0         \n\
        movdqa %%xmm3, %%xmm0   # XMM0: r7 r6 r5 r4 r3 r2 r1 r0         \n\
        movdqa %%xmm4, %%xmm1   # XMM1: g7 g6 g5 g4 g3 g2 g1 g0         \n\
        movdqa %%xmm2, %%xmm5   # XMM5: b7 b6 b5 b4 b3 b2 b1 b0         \n\
        # Add intermediate results and round/shift to get R/G/B values  \n\
        paddw 128("ESI"),%%xmm6 # Add rounding value (0.5 @ 8.4 fixed)  \n\
        paddw 128("ESI"),%%xmm7                                         \n\
        paddw %%xmm6, %%xmm0    # XMM0: RE RC RA R8 R6 R4 R2 R0         \n\
        psraw $4, %%xmm0        # Shift back to 8.0 fixed               \n\
        paddw %%xmm6, %%xmm1    # XMM1: GE GC GA G8 G6 G4 G2 G0         \n\
        psraw $4, %%xmm1                                                \n\
        paddw %%xmm6, %%xmm2    # XMM2: BE BC BA B8 B6 B4 B2 B0         \n\
        psraw $4, %%xmm2                                                \n\
        paddw %%xmm7, %%xmm3    # XMM3: RF RD RB R9 R7 R5 R3 R1         \n\
        psraw $4, %%xmm3                                                \n\
        paddw %%xmm7, %%xmm4    # XMM4: GF GD GB G9 G7 G5 G3 G1         \n\
        psraw $4, %%xmm4                                                \n\
        paddw %%xmm7, %%xmm5    # XMM5: BF BD BB B9 B7 B5 B3 B1         \n\
        psraw $4, %%xmm5                                                \n\
        # Saturate to 0-255 and pack into bytes                         \n\
        packuswb %%xmm0, %%xmm0 # XMM0: RE.......R0 RE.......R0         \n\
        packuswb %%xmm1, %%xmm1 # XMM1: GE.......G0 GE.......G0         \n\
        packuswb %%xmm2, %%xmm2 # XMM2: BE.......B0 BE.......B0         \n\
        packuswb %%xmm3, %%xmm3 # XMM3: RF.......R1 RF.......R1         \n\
        packuswb %%xmm4, %%xmm4 # XMM4: GF.......G1 GF.......G1         \n\
        packuswb %%xmm5, %%xmm5 # XMM5: BF.......B1 BF.......B1         \n\
        punpcklbw %%xmm3,%%xmm0 # XMM0: RF...................R0         \n\
        punpcklbw %%xmm4,%%xmm1 # XMM1: GF...................G0         \n\
        punpcklbw %%xmm5,%%xmm2 # XMM2: BF...................B0         \n"
        : /* no outputs */
        : "S" (&yuv_data), "m" (yuv_data)
    );
}

/* YUV444 YUV->RGB (Y=XMM7:XMM6 U=XMM5:XMM2 V=XMM3:XMM0) */
static inline void sse2_yuv444_to_rgb(void)
{
    asm("\
        psubw 16("ESI"), %%xmm6 # XMM6: subtract 16                     \n\
        psllw $7, %%xmm6        # XMM6: convert to fixed point 8.7      \n\
        psubw 16("ESI"), %%xmm7 # XMM7: subtract 16                     \n\
        psllw $7, %%xmm7        # XMM7: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm2 # XMM2: subtract 128                    \n\
        psllw $7, %%xmm2        # XMM2: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm5 # XMM5: subtract 128                    \n\
        psllw $7, %%xmm5        # XMM5: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm0 # XMM0: subtract 128                    \n\
        psllw $7, %%xmm0        # XMM0: convert to fixed point 8.7      \n\
        psubw 32("ESI"), %%xmm3 # XMM3: subtract 128                    \n\
        psllw $7, %%xmm3        # XMM3: convert to fixed point 8.7      \n\
        # Multiply by constants                                         \n\
        pmulhw 48("ESI"),%%xmm6 # XMM6: cY7.................cY0         \n\
        movdqa 80("ESI"),%%xmm1 # XMM1: gU constant                     \n\
        pmulhw %%xmm2, %%xmm1   # XMM1: gU7.................gU0         \n\
        movdqa 96("ESI"),%%xmm4 # XMM4: gV constant                     \n\
        pmulhw %%xmm0, %%xmm4   # XMM4: gV7.................gV0         \n\
        paddw %%xmm4, %%xmm1    # XMM1: g7 g6 g5 g4 g3 g2 g1 g0         \n\
        pmulhw 64("ESI"),%%xmm0 # XMM0: r7 r6 r5 r4 r3 r2 r1 r0         \n\
        pmulhw 112("ESI"),%%xmm2 #XMM2: b7 b6 b5 b4 b3 b2 b1 b0         \n\
        # Add intermediate results and round/shift to get R/G/B values  \n\
        paddw 128("ESI"),%%xmm6 # Add rounding value (0.5 @ 8.4 fixed)  \n\
        paddw %%xmm6, %%xmm0    # XMM0: R7 R6 R5 R4 R3 R2 R1 R0         \n\
        psraw $4, %%xmm0        # Shift back to 8.0 fixed               \n\
        paddw %%xmm6, %%xmm1    # XMM1: G7 G6 G5 G4 G3 G2 G1 G0         \n\
        psraw $4, %%xmm1                                                \n\
        paddw %%xmm6, %%xmm2    # XMM2: B7 B6 B5 B4 B3 B2 B1 B0         \n\
        psraw $4, %%xmm2                                                \n\
        # Do it all over again for pixels 8-15                          \n\
        pmulhw 48("ESI"),%%xmm7 # XMM7: cYF.................cY8         \n\
        movdqa 80("ESI"),%%xmm6 # XMM6: gU constant                     \n\
        pmulhw %%xmm5, %%xmm6   # XMM6: gUF.................gU8         \n\
        movdqa 96("ESI"),%%xmm4 # XMM4: gV constant                     \n\
        pmulhw %%xmm3, %%xmm4   # XMM4: gVF.................gV8         \n\
        paddw %%xmm6, %%xmm4    # XMM4: gF gE gD gC gB gA g9 g8         \n\
        pmulhw 64("ESI"),%%xmm3 # XMM3: rF rE rD rC rB rA r9 r8         \n\
        pmulhw 112("ESI"),%%xmm5 #XMM5: bF bE bD bC bB bA b9 b8         \n\
        paddw 128("ESI"),%%xmm7 # Add rounding value (0.5 @ 8.4 fixed)  \n\
        paddw %%xmm7, %%xmm3    # XMM3: RF RE RD RC RB RA R9 R8         \n\
        psraw $4, %%xmm3                                                \n\
        paddw %%xmm7, %%xmm4    # XMM4: GF GE GD GC GB GA G9 G8         \n\
        psraw $4, %%xmm4                                                \n\
        paddw %%xmm7, %%xmm5    # XMM5: BF BE BD BC BB BA B9 B8         \n\
        psraw $4, %%xmm5                                                \n\
        # Saturate to 0-255 and pack into bytes                         \n\
        packuswb %%xmm3, %%xmm0 # XMM0: RF...................R0         \n\
        packuswb %%xmm4, %%xmm1 # XMM1: GF...................G0         \n\
        packuswb %%xmm5, %%xmm2 # XMM2: BF...................B0         \n"
        : /* no outputs */
        : "S" (&yuv_data), "m" (yuv_data)
    );
}

/************************************/

/* Convert YUV->RGB output to RGBA pixels in XMM0..XMM3 */
#define SSE2_RGB_TO_RGBA "\
        pxor %%xmm7, %%xmm7     # XMM7: 00 00 00 00 00 00 00 00         \n\
        movdqa %%xmm0, %%xmm3   # XMM3: RF...................R0         \n\
        movdqa %%xmm1, %%xmm4   # XMM4: GF...................G0         \n\
        movdqa %%xmm2, %%xmm5   # XMM5: BF...................B0         \n\
        punpcklbw %%xmm1,%%xmm0 # XMM0: G7 R7.............G0 R0         \n\
        punpcklbw %%xmm7,%%xmm2 # XMM2: 00 B7.............00 B0         \n\
        movdqa %%xmm0, %%xmm1   # XMM1: G7 R7.............G0 R0         \n\
        punpcklwd %%xmm2,%%xmm0 # XMM0: 0BGR3 0BGR2 0BGR1 0BGR0         \n\
        punpckhwd %%xmm2,%%xmm1 # XMM1: 0BGR7 0BGR6 0BGR5 0BGR4         \n\
        punpckhbw %%xmm4,%%xmm3 # XMM3: GF RF.............G8 R8         \n\
        punpckhbw %%xmm7,%%xmm5 # XMM5: 00 BF.............00 B8         \n\
        movdqa %%xmm3, %%xmm2   # XMM2: GF RF.............G8 R8         \n\
        punpckhwd %%xmm5,%%xmm3 # XMM3: 0BGRF 0BGRE 0BGRD 0BGRC         \n\
        punpcklwd %%xmm5,%%xmm2 # XMM2: 0BGRB 0BGRA 0BGR9 0BGR8         \n"

/* Convert YUV->RGB output to BGRA pixels in XMM0..XMM3 */
#define SSE2_RGB_TO_BGRA "\
        pxor %%xmm7, %%xmm7     # XMM7: 00 00 00 00 00 00 00 00         \n\
        movdqa %%xmm0, %%xmm5   # XMM5: RF...................R0         \n\
        movdqa %%xmm1, %%xmm4   # XMM4: GF...................G0         \n\
        movdqa %%xmm2, %%xmm3   # XMM3: BF...................B0         \n\
        punpcklbw %%xmm1,%%xmm2 # XMM0: G7 B7.............G0 B0         \n\
        punpcklbw %%xmm7,%%xmm0 # XMM2: 00 R7.............00 R0         \n\
        movdqa %%xmm2, %%xmm1   # XMM1: G7 B7.............G0 B0         \n\
        punpcklwd %%xmm0,%%xmm2 # XMM2: 0RGB3 0RGB2 0RGB1 0RGB0         \n\
        punpckhwd %%xmm0,%%xmm1 # XMM1: 0RGB7 0RGB6 0RGB5 0RGB4         \n\
        movdqa %%xmm2, %%xmm0   # XMM0: 0RGB3 0RGB2 0RGB1 0RGB0         \n\
        punpckhbw %%xmm4,%%xmm3 # XMM3: GF BF.............G8 B8         \n\
        punpckhbw %%xmm7,%%xmm5 # XMM5: 00 RF.............00 R8         \n\
        movdqa %%xmm3, %%xmm2   # XMM2: GF BF.............G8 B8         \n\
        punpckhwd %%xmm5,%%xmm3 # XMM3: 0RGBF 0RGBE 0RGBD 0RGBC         \n\
        punpcklwd %%xmm5,%%xmm2 # XMM2: 0RGBB 0RGBA 0RGB9 0RGB8         \n"

/* Convert and 4 RGBA32 (BGRA32) pixels in XMMn to RGB24 (BGR24) and store
 * at EDI+(12*n) */
#define SSE2_RGB32_TO_RGB24(n) "\
        movd %%xmm"#n", %%eax   # EAX: 00 B0 G0 R0                      \n\
        psrldq $4, %%xmm"#n"    # XMMn: 00000 0BGR3 0BGR2 0BGR1         \n\
        movd %%xmm"#n", %%ebx   # EBX: 00 B1 G1 R1                      \n\
        psrldq $4, %%xmm"#n"    # XMMn: 00000 00000 0BGR3 0BGR2         \n\
        movd %%xmm"#n", %%ecx   # ECX: 00 B2 G2 R2                      \n\
        psrldq $4, %%xmm"#n"    # XMMn: 00000 00000 00000 0BGR3         \n\
        movd %%xmm"#n", %%edx   # EDX: 00 B3 G3 R3                      \n\
        "IA32_RGB32_TO_RGB24"                                           \n\
        movl %%eax, 12*"#n"+0("EDI")                                    \n\
        movl %%ebx, 12*"#n"+4("EDI")                                    \n\
        movl %%ecx, 12*"#n"+8("EDI")                                    \n"


static inline void sse2_store_rgb24(uint8_t *dest)
{
    /* It looks like it's fastest to go to RGB32 first, then shift the
     * result to merge the 24-bit pixels together. */
    asm(SSE2_RGB_TO_RGBA                                                 "\
        push "EBX"                                                      \n\
        "SSE2_RGB32_TO_RGB24(0)"                                        \n\
        "SSE2_RGB32_TO_RGB24(1)"                                        \n\
        "SSE2_RGB32_TO_RGB24(2)"                                        \n\
        "SSE2_RGB32_TO_RGB24(3)"                                        \n\
        pop "EBX"                                                       \n"
        : /* no outputs */
        : "D" (dest)
        : "eax", "ecx", "edx", "esi"
    );
}

static inline void sse2_store_bgr24(uint8_t *dest)
{
    asm(SSE2_RGB_TO_BGRA                                                 "\
        push "EBX"                                                      \n\
        "SSE2_RGB32_TO_RGB24(0)"                                        \n\
        "SSE2_RGB32_TO_RGB24(1)"                                        \n\
        "SSE2_RGB32_TO_RGB24(2)"                                        \n\
        "SSE2_RGB32_TO_RGB24(3)"                                        \n\
        pop "EBX"                                                       \n"
        : /* no outputs */
        : "D" (dest)
        : "eax", "ecx", "edx", "esi"
    );
}

/* It would be nice to be able to use movntdq here for a 50% speedup,
 * but we're not guaranteed alignment... (think 766x512 for example) */
static inline void sse2_store_rgba32(uint8_t *dest)
{
    asm(SSE2_RGB_TO_RGBA                                                 "\
        movdqu %%xmm0,   ("EDI")                                        \n\
        movdqu %%xmm1, 16("EDI")                                        \n\
        movdqu %%xmm2, 32("EDI")                                        \n\
        movdqu %%xmm3, 48("EDI")                                        \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void sse2_store_abgr32(uint8_t *dest)
{
    asm(SSE2_RGB_TO_BGRA                                                 "\
        pslldq $1, %%xmm0                                               \n\
        pslldq $1, %%xmm1                                               \n\
        pslldq $1, %%xmm2                                               \n\
        pslldq $1, %%xmm3                                               \n\
        movdqu %%xmm0,   ("EDI")                                        \n\
        movdqu %%xmm1, 16("EDI")                                        \n\
        movdqu %%xmm2, 32("EDI")                                        \n\
        movdqu %%xmm3, 48("EDI")                                        \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void sse2_store_argb32(uint8_t *dest)
{
    asm(SSE2_RGB_TO_RGBA                                                 "\
        pslldq $1, %%xmm0                                               \n\
        pslldq $1, %%xmm1                                               \n\
        pslldq $1, %%xmm2                                               \n\
        pslldq $1, %%xmm3                                               \n\
        movdqu %%xmm0,   ("EDI")                                        \n\
        movdqu %%xmm1, 16("EDI")                                        \n\
        movdqu %%xmm2, 32("EDI")                                        \n\
        movdqu %%xmm3, 48("EDI")                                        \n"
        : /* no outputs */
        : "D" (dest)
    );
}

static inline void sse2_store_bgra32(uint8_t *dest)
{
    asm(SSE2_RGB_TO_BGRA                                                 "\
        movdqu %%xmm0,   ("EDI")                                        \n\
        movdqu %%xmm1, 16("EDI")                                        \n\
        movdqu %%xmm2, 32("EDI")                                        \n\
        movdqu %%xmm3, 48("EDI")                                        \n"
        : /* no outputs */
        : "D" (dest)
    );
}

/*************************************************************************/

static inline void sse2_load_rgb24(uint8_t *src);
static inline void sse2_load_bgr24(uint8_t *src);
static inline void sse2_load_rgba32(uint8_t *src);
static inline void sse2_load_abgr32(uint8_t *src);
static inline void sse2_load_argb32(uint8_t *src);
static inline void sse2_load_bgra32(uint8_t *src);
static inline void sse2_rgb_to_yuv420p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_yuv411p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_yuv422p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_yuv444p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_yuy2(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_uyvy(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_yvyu(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);
static inline void sse2_rgb_to_y8(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width);

#define DEFINE_RGB2YUV_SSE2(rgb,yuv,rgbsz,rofs,gofs,bofs,slowop) \
static int rgb##_##yuv##_sse2(uint8_t **src, uint8_t **dest,            \
                              int width, int height)                    \
{                                                                       \
    int x, y;                                                           \
                                                                        \
    for (y = 0; y < height; y++) {                                      \
        for (x = 0; x < (width & ~7); x += 8) {                         \
            sse2_load_##rgb(src[0]+(y*width+x)*rgbsz);                  \
            sse2_rgb_to_##yuv(dest[0], dest[1], dest[2], x, y, width);  \
        }                                                               \
        while (x < width) {                                             \
            int r = src[0][(y*width+x)*rgbsz+rofs];                     \
            int g = src[0][(y*width+x)*rgbsz+gofs];                     \
            int b = src[0][(y*width+x)*rgbsz+bofs];                     \
            slowop;                                                     \
            x++;                                                        \
        }                                                               \
    }                                                                   \
    asm("emms");                                                        \
    return 1;                                                           \
}

#define DEFINE_RGB2YUV_SSE2_SET(rgb,sz,r,g,b) \
    DEFINE_RGB2YUV_SSE2(rgb,yuv420p, sz,r,g,b, RGB2YUV_420P) \
    DEFINE_RGB2YUV_SSE2(rgb,yuv411p, sz,r,g,b, RGB2YUV_411P) \
    DEFINE_RGB2YUV_SSE2(rgb,yuv422p, sz,r,g,b, RGB2YUV_422P) \
    DEFINE_RGB2YUV_SSE2(rgb,yuv444p, sz,r,g,b, RGB2YUV_444P) \
    DEFINE_RGB2YUV_SSE2(rgb,yuy2,    sz,r,g,b, RGB2YUV_YUY2) \
    DEFINE_RGB2YUV_SSE2(rgb,uyvy,    sz,r,g,b, RGB2YUV_UYVY) \
    DEFINE_RGB2YUV_SSE2(rgb,yvyu,    sz,r,g,b, RGB2YUV_YVYU) \
    DEFINE_RGB2YUV_SSE2(rgb,y8,      sz,r,g,b, RGB2Y())

DEFINE_RGB2YUV_SSE2_SET(rgb24,  3,0,1,2)
DEFINE_RGB2YUV_SSE2_SET(bgr24,  3,2,1,0)
DEFINE_RGB2YUV_SSE2_SET(rgba32, 4,0,1,2)
DEFINE_RGB2YUV_SSE2_SET(abgr32, 4,3,2,1)
DEFINE_RGB2YUV_SSE2_SET(argb32, 4,1,2,3)
DEFINE_RGB2YUV_SSE2_SET(bgra32, 4,2,1,0)

/************************************/

/* Split 8 RGBA pixels in XMMr/XMMb into R/G/B in XMM0/XMM1/XMM2.
 * r and b are 0 and 2 for RGB, 2 and 0 for BGR */
#define SSE2_SPLIT_RGB32(r,b) "\
        movdqa 176(%%edi), %%xmm7       # XMM7: 00FF*8                  \n\
        movdqa %%xmm"#r", %%xmm1        # XMM1: XBGR3 XBGR2 XBGR1 XBGR0 \n\
        movdqa %%xmm"#b", %%xmm3        # XMM3: XBGR7 XBGR6 XBGR5 XBGR4 \n\
        pand %%xmm7, %%xmm"#r"          # XMMr: B3 R3 B2 R2 B1 R1 B0 R0 \n\
        psrld $8, %%xmm1                # XMM1: -XBG3 -XBG2 -XBG1 -XBG0 \n\
        pand %%xmm7, %%xmm"#b"          # XMMb: B7 R7 B6 R6 B5 R5 B4 R4 \n\
        psrld $8, %%xmm3                # XMM3: -XBG7 -XBG6 -XBG5 -XBG4 \n\
        pand %%xmm7, %%xmm1             # XMM1: XX G3 XX G2 XX G1 XX G0 \n\
        packuswb %%xmm"#b", %%xmm"#r"   # XMMr: B7 R7 ........... B0 R0 \n\
        pand %%xmm7, %%xmm3             # XMM3: XX G7 XX G6 XX G5 XX G4 \n\
        movdqa %%xmm"#r", %%xmm"#b"     # XMMb: B7 R7 ........... B0 R0 \n\
        packuswb %%xmm3, %%xmm1         # XMM1: XX G7 ........... XX G0 \n\
        pand %%xmm7, %%xmm"#r"          # XMMr: R7 R6 R5 R4 R3 R2 R1 R0 \n\
        psrlw $8, %%xmm"#b"             # XMMb: B7 B6 B5 B4 B3 B2 B1 B0 \n\
        pand %%xmm7, %%xmm1             # XMM1: G7 G6 G5 G4 G3 G2 G1 G0 \n"

static inline void sse2_load_rgb24(uint8_t *src)
{
    asm("\
        push "EBX"                                                      \n\
        # Make stack space for loading XMM registers                    \n\
        sub $24, "ESP"                                                  \n\
        # Copy source pixels to appropriate positions in stack (this    \n\
        # seems to be the fastest way to get them where we want them)   \n\
        movl $8, %%ebx                                                  \n\
        movl $24, %%edx                                                 \n\
        0:                                                              \n\
        movb -3("ESI","EDX"), %%al                                      \n\
        movb %%al, 0-1("ESP","EBX")                                     \n\
        movb -2("ESI","EDX"), %%al                                      \n\
        movb %%al, 8-1("ESP","EBX")                                     \n\
        movb -1("ESI","EDX"), %%al                                      \n\
        movb %%al, 16-1("ESP","EBX")                                    \n\
        subl $3, %%edx                                                  \n\
        subl $1, %%ebx                                                  \n\
        jnz 0b                                                          \n\
        # Load XMM0-XMM2 with R/G/B values and expand to 16-bit         \n\
        pxor %%xmm7, %%xmm7                                             \n\
        movq ("ESP"), %%xmm0                                            \n\
        punpcklbw %%xmm7, %%xmm0                                        \n\
        movq 8("ESP"), %%xmm1                                           \n\
        punpcklbw %%xmm7, %%xmm1                                        \n\
        movq 16("ESP"), %%xmm2                                          \n\
        punpcklbw %%xmm7, %%xmm2                                        \n\
        add $24, "ESP"                                                  \n\
        pop "EBX"                                                       \n"
        : /* no outputs */
        : "S" (src)
        : "eax", "ecx", "edx", "edi"
    );
}

static inline void sse2_load_bgr24(uint8_t *src)
{
    /* Load as RGB and swap registers */
    sse2_load_rgb24(src);
    asm("\
        movdqa %%xmm0, %%xmm3                                           \n\
        movdqa %%xmm2, %%xmm0                                           \n\
        movdqa %%xmm3, %%xmm2                                           \n"
        : /* no outputs */
        : /* no inputs */
    );
}

static inline void sse2_load_rgba32(uint8_t *src)
{
    asm("\
        movdqu (%%esi), %%xmm0          # XMM0: XBGR3 XBGR2 XBGR1 XBGR0 \n\
        movdqu 16(%%esi), %%xmm2        # XMM2: XBGR7 XBGR6 XBGR5 XBGR4 \n\
        "SSE2_SPLIT_RGB32(0,2)"                                         \n"
        : /* no outputs */
        : "S" (src), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_load_abgr32(uint8_t *src)
{
    asm("\
        movdqu (%%esi), %%xmm2          # XMM2: RGBX3 RGBX2 RGBX1 RGBX0 \n\
        movdqu 16(%%esi), %%xmm0        # XMM0: RGBX7 RGBX6 RGBX5 RGBX4 \n\
        psrld $8, %%xmm2                # XMM2: -RGB3 -RGB2 -RGB1 -RGB0 \n\
        psrld $8, %%xmm0                # XMM0: -RGB7 -RGB6 -RGB5 -RGB4 \n\
        "SSE2_SPLIT_RGB32(2,0)"                                         \n"
        : /* no outputs */
        : "S" (src), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_load_argb32(uint8_t *src)
{
    asm("\
        movdqu (%%esi), %%xmm0          # XMM0: BGRX3 BGRX2 BGRX1 BGRX0 \n\
        movdqu 16(%%esi), %%xmm2        # XMM2: BGRX7 BGRX6 BGRX5 BGRX4 \n\
        psrld $8, %%xmm0                # XMM0: -BGR3 -BGR2 -BGR1 -BGR0 \n\
        psrld $8, %%xmm2                # XMM2: -BGR7 -BGR6 -BGR5 -BGR4 \n\
        "SSE2_SPLIT_RGB32(0,2)"                                         \n"
        : /* no outputs */
        : "S" (src), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_load_bgra32(uint8_t *src)
{
    asm("\
        movdqu (%%esi), %%xmm2          # XMM2: XRGB3 XRGB2 XRGB1 XRGB0 \n\
        movdqu 16(%%esi), %%xmm0        # XMM0: XRGB7 XRGB6 XRGB5 XRGB4 \n\
        "SSE2_SPLIT_RGB32(2,0)"                                         \n"
        : /* no outputs */
        : "S" (src), "D" (&rgb_data), "m" (rgb_data)
    );
}

/************************************/

#define SSE2_RGB2Y "\
        # Make RGB data into 8.6 fixed-point, then create 8.6           \n\
        # fixed-point Y data in XMM3                                    \n\
        psllw $6, %%xmm0                                                \n\
        movdqa %%xmm0, %%xmm3                                           \n\
        pmulhuw ("EDI"), %%xmm3                                         \n\
        psllw $6, %%xmm1                                                \n\
        movdqa %%xmm1, %%xmm6                                           \n\
        pmulhuw 16("EDI"), %%xmm6                                       \n\
        psllw $6, %%xmm2                                                \n\
        movdqa %%xmm2, %%xmm7                                           \n\
        pmulhuw 32("EDI"), %%xmm7                                       \n\
        paddw %%xmm6, %%xmm3    # No possibility of overflow            \n\
        paddw %%xmm7, %%xmm3                                            \n\
        paddw 144("EDI"), %%xmm3                                        \n"
#define SSE2_RGB2U "\
        # Create 8.6 fixed-point U data in XMM4                         \n\
        movdqa %%xmm0, %%xmm4                                           \n\
        pmulhw 48("EDI"), %%xmm4                                        \n\
        movdqa %%xmm1, %%xmm6                                           \n\
        pmulhw 64("EDI"), %%xmm6                                        \n\
        movdqa %%xmm2, %%xmm7                                           \n\
        pmulhw 80("EDI"), %%xmm7                                        \n\
        paddw %%xmm6, %%xmm4                                            \n\
        paddw %%xmm7, %%xmm4                                            \n\
        paddw 160("EDI"), %%xmm4                                        \n"
#define SSE2_RGB2U0 "\
        # Create 8.6 fixed-point U data in XMM0                         \n\
        pmulhw 48("EDI"), %%xmm0                                        \n\
        pmulhw 64("EDI"), %%xmm1                                        \n\
        pmulhw 80("EDI"), %%xmm2                                        \n\
        paddw %%xmm1, %%xmm0                                            \n\
        paddw %%xmm2, %%xmm0                                            \n\
        paddw 160("EDI"), %%xmm0                                        \n"
#define SSE2_RGB2V "\
        # Create 8.6 fixed-point V data in XMM0                         \n\
        pmulhw 96("EDI"), %%xmm0                                        \n\
        pmulhw 112("EDI"), %%xmm1                                       \n\
        pmulhw 128("EDI"), %%xmm2                                       \n\
        paddw %%xmm1, %%xmm0                                            \n\
        paddw %%xmm2, %%xmm0                                            \n\
        paddw 160("EDI"), %%xmm0                                        \n"
#define SSE2_PACKYU "\
        # Shift back down to 8-bit values                               \n\
        psraw $6, %%xmm3                                                \n\
        psraw $6, %%xmm0                                                \n\
        # Pack into bytes                                               \n\
        pxor %%xmm7, %%xmm7                                             \n\
        packuswb %%xmm7, %%xmm3                                         \n\
        packuswb %%xmm7, %%xmm0                                         \n"
#define SSE2_PACKYUV "\
        # Shift back down to 8-bit values                               \n\
        psraw $6, %%xmm3                                                \n\
        psraw $6, %%xmm4                                                \n\
        psraw $6, %%xmm0                                                \n\
        # Pack into bytes                                               \n\
        pxor %%xmm7, %%xmm7                                             \n\
        packuswb %%xmm7, %%xmm3                                         \n\
        packuswb %%xmm7, %%xmm4                                         \n\
        packuswb %%xmm7, %%xmm0                                         \n"
#define SSE2_STRIPU(N) "\
        # Remove every odd U value                                      \n\
        pand 176("EDI"), %%xmm"#N"                                      \n\
        packuswb %%xmm7, %%xmm"#N"                                      \n"
#define SSE2_STRIPV "\
        # Remove every even V value                                     \n\
        psrlw $8, %%xmm0                                                \n\
        packuswb %%xmm7, %%xmm0                                         \n"

static inline void sse2_rgb_to_yuv420p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    if (y%2 == 0) {
        asm("\
            "SSE2_RGB2Y"                                                \n\
            "SSE2_RGB2U0"                                               \n\
            "SSE2_PACKYU"                                               \n\
            "SSE2_STRIPU(0)"                                            \n\
            # Store into destination pointers                           \n\
            movq %%xmm3, ("EAX")                                        \n\
            movd %%xmm0, ("ECX")                                        \n"
            : /* no outputs */
            : "a" (destY+y*width+x), "c" (destU+(y/2)*(width/2)+(x/2)),
              "D" (&rgb_data), "m" (rgb_data)
        );
    } else {
        asm("\
            "SSE2_RGB2Y"                                                \n\
            "SSE2_RGB2V"                                                \n\
            "SSE2_PACKYU"                                               \n\
            "SSE2_STRIPV"                                               \n\
            # Store into destination pointers                           \n\
            movq %%xmm3, ("EAX")                                        \n\
            movd %%xmm0, ("EDX")                                        \n"
            : /* no outputs */
            : "a" (destY+y*width+x), "d" (destV+(y/2)*(width/2)+(x/2)),
              "D" (&rgb_data), "m" (rgb_data)
        );
    }
}

static inline void sse2_rgb_to_yuv411p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        "SSE2_STRIPU(4)"                                                \n\
        "SSE2_STRIPU(4)"                                                \n\
        "SSE2_STRIPU(0)"                                                \n\
        "SSE2_STRIPV"                                                   \n\
        # Store into destination pointers                               \n\
        movq %%xmm3, ("EAX")                                            \n\
        push "EAX"  # needed because GCC might rely on it later         \n\
        movd %%xmm4, %%eax                                              \n\
        movw %%ax, ("ECX")                                              \n\
        movd %%xmm0, %%eax                                              \n\
        movw %%ax, ("EDX")                                              \n\
        pop "EAX"                                                       \n"
        : /* no outputs */
        : "a" (destY+y*width+x), "c" (destU+y*(width/4)+(x/4)),
          "d" (destV+y*(width/4)+(x/4)), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_yuv422p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        "SSE2_STRIPU(4)"                                                \n\
        "SSE2_STRIPV"                                                   \n\
        # Store into destination pointers                               \n\
        movq %%xmm3, ("EAX")                                            \n\
        movd %%xmm4, ("ECX")                                            \n\
        movd %%xmm0, ("EDX")                                            \n"
        : /* no outputs */
        : "a" (destY+y*width+x), "c" (destU+y*(width/2)+(x/2)),
          "d" (destV+y*(width/2)+(x/2)), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_yuv444p(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        # Store into destination pointers                               \n\
        movq %%xmm3, ("EAX")                                            \n\
        movq %%xmm4, ("ECX")                                            \n\
        movq %%xmm0, ("EDX")                                            \n"
        : /* no outputs */
        : "a" (destY+y*width+x), "c" (destU+y*width+x), "d" (destV+y*width+x),
          "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_yuy2(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        "SSE2_STRIPU(4)"                                                \n\
        "SSE2_STRIPV"                                                   \n\
        # Interleave Y/U/V                                              \n\
        punpcklbw %%xmm0, %%xmm4                                        \n\
        punpcklbw %%xmm4, %%xmm3                                        \n\
        # Store into destination pointer                                \n\
        movdqu %%xmm3, ("EAX")                                          \n"
        : /* no outputs */
        : "a" (destY+(y*width+x)*2), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_uyvy(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        "SSE2_STRIPU(4)"                                                \n\
        "SSE2_STRIPV"                                                   \n\
        # Interleave Y/U/V                                              \n\
        punpcklbw %%xmm0, %%xmm4                                        \n\
        punpcklbw %%xmm3, %%xmm4                                        \n\
        # Store into destination pointer                                \n\
        movdqu %%xmm4, ("EAX")                                          \n"
        : /* no outputs */
        : "a" (destY+(y*width+x)*2), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_yvyu(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        "SSE2_RGB2Y"                                                    \n\
        "SSE2_RGB2U"                                                    \n\
        "SSE2_RGB2V"                                                    \n\
        "SSE2_PACKYUV"                                                  \n\
        # Remove every odd V value                                      \n\
        pand 176("EDI"), %%xmm0                                         \n\
        packuswb %%xmm7, %%xmm0                                         \n\
        # Remove every even U value                                     \n\
        psrlw $8, %%xmm4                                                \n\
        packuswb %%xmm7, %%xmm4                                         \n\
        # Interleave Y/U/V                                              \n\
        punpcklbw %%xmm4, %%xmm0                                        \n\
        punpcklbw %%xmm0, %%xmm3                                        \n\
        # Store into destination pointer                                \n\
        movdqu %%xmm3, ("EAX")                                          \n"
        : /* no outputs */
        : "a" (destY+(y*width+x)*2), "D" (&rgb_data), "m" (rgb_data)
    );
}

static inline void sse2_rgb_to_y8(
    uint8_t *destY, uint8_t *destU, uint8_t *destV, int x, int y, int width)
{
    asm("\
        psllw $6, %%xmm0                                                \n\
        pmulhuw ("EDI"), %%xmm0                                         \n\
        psllw $6, %%xmm1                                                \n\
        pmulhuw 16("EDI"), %%xmm1                                       \n\
        psllw $6, %%xmm2                                                \n\
        pmulhuw 32("EDI"), %%xmm2                                       \n\
        paddw %%xmm1, %%xmm0            # No possibility of overflow    \n\
        paddw %%xmm2, %%xmm0                                            \n\
        paddw 144("EDI"), %%xmm0                                        \n\
        psraw $6, %%xmm0                                                \n\
        packuswb %%xmm0, %%xmm0                                         \n\
        movq %%xmm0, ("EAX")                                            \n"
        : /* no outputs */
        : "a" (destY+y*width+x), "D" (&rgb_data), "m" (rgb_data)
    );
}

/*************************************************************************/

static int yuvp_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 16,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%edx                 # (trash EDX, we don't need it  \n\
        cmovnz %%edx, %%eax             #  anymore)                     \n\
        movl $0, %%edx                                                  \n\
        cmovs %%edx, %%eax                                              \n\
        movb %%al, -1("EDI","ECX")      # and store                     \n",
        /* main_loop */ "\
        movdqu -16("ESI","ECX"), %%xmm0 # XMM0: Y15..Y0                 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: Y15..Y0                 \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: Y7..Y0                  \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        psllw $2, %%xmm0                # XMM0: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        punpckhbw %%xmm4, %%xmm1        # XMM1: Y15..Y8 << 8            \n\
        psubw %%xmm6, %%xmm1            # XMM1: unbias by 16            \n\
        psllw $2, %%xmm1                # XMM1: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm1           # XMM1: multiply by 255/219>>2  \n\
        packuswb %%xmm1, %%xmm0         # XMM0: G15..G0, saturated      \n\
        movdqu %%xmm0, -16("EDI","ECX")                                 \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

static int yuy2_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pcmpeqd %%xmm5, %%xmm5                                          \n\
        psrlw $8, %%xmm5                # constant: 0x00FF              \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 8,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -2("ESI","ECX",2), %%eax # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%edx                 # (trash EDX, we don't need it  \n\
        cmovnz %%edx, %%eax             #  anymore)                     \n\
        movl $0, %%edx                                                  \n\
        cmovs %%edx, %%eax                                              \n\
        movb %%al, -1("EDI","ECX")      # and store                     \n",
        /* main_loop */ "\
        movdqu -16("ESI","ECX",2),%%xmm0 #XMM0: V3 Y7..U0 Y0            \n\
        pand %%xmm5, %%xmm0             # XMM0: Y7..Y0                  \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        psllw $2, %%xmm0                # XMM0: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        packuswb %%xmm0, %%xmm0         # XMM0: G7..G0, saturated       \n\
        movq %%xmm0, -8("EDI","ECX")                                    \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

static int uyvy_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6                                        \n\
        psllw $2, %%xmm6                # constant: 16<<2               \n\
        pcmpeqd %%xmm5, %%xmm5                                          \n\
        psllw $8, %%xmm5                # constant: 0xFF00              \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 8,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX",2), %%eax # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%edx                 # (trash EDX, we don't need it  \n\
        cmovnz %%edx, %%eax             #  anymore)                     \n\
        movl $0, %%edx                                                  \n\
        cmovs %%edx, %%eax                                              \n\
        movb %%al, -1("EDI","ECX")      # and store                     \n",
        /* main_loop */ "\
        movdqu -16("ESI","ECX",2),%%xmm0 #XMM0: Y7 V3..Y0 U0            \n\
        pand %%xmm5, %%xmm0             # XMM0: Y7..Y0 << 8             \n\
        psrlw $6, %%xmm0                # XMM0: fixed point 8.2         \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        packuswb %%xmm0, %%xmm0         # XMM0: G7..G0, saturated       \n\
        movq %%xmm0, -8("EDI","ECX")                                    \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

/*************************************************************************/

static int gray8_y8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa 16("EDX"), %%xmm7       # constant: 219/255             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 16,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve gray byte            \n\
        imull %3, %%eax                 # multiply by 219/255           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        addl $16, %%eax                 # add 16                        \n\
        movb %%al, -1("EDI","ECX")      # and store                     \n",
        /* main_loop */ "\
        movdqu -16("ESI","ECX"), %%xmm2 # XMM2: G15..G0                 \n\
        movdqa %%xmm4, %%xmm0                                           \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: G7..G0 << 8             \n\
        pmulhuw %%xmm7, %%xmm0          # XMM0: multiply by 219/255>>2  \n\
        movdqa %%xmm4, %%xmm1                                           \n\
        punpckhbw %%xmm2, %%xmm1        # XMM1: G15..G8 << 8            \n\
        pmulhuw %%xmm7, %%xmm1          # XMM1: multiply by 219/255>>2  \n\
        psrlw $6, %%xmm0                # XMM0: shift down to 8 bits    \n\
        paddw %%xmm6, %%xmm0            # XMM0: bias by 16              \n\
        psrlw $6, %%xmm1                # XMM1: shift down to 8 bits    \n\
        paddw %%xmm6, %%xmm1            # XMM1: bias by 16              \n\
        packuswb %%xmm1, %%xmm0         # XMM0: Y15..Y0                 \n\
        movdqu %%xmm0, -16("EDI","ECX")                                 \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (GRAY_Y), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

static int gray8_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa 16("EDX"), %%xmm7       # constant: 219/255             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pcmpeqd %%xmm5, %%xmm5                                          \n\
        psllw $15, %%xmm5               # constant: 0x8000              \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 8,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve gray byte            \n\
        imull %3, %%eax                 # multiply by 219/255           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        addl $16, %%eax                 # add 16                        \n\
        movb %%al, -2("EDI","ECX",2)    # and store                     \n\
        movb $128, -1("EDI","ECX",2)    # store 128 in U/V byte         \n",
        /* main_loop */ "\
        movq -8("ESI","ECX"), %%xmm2    # XMM2: G5..G0                  \n\
        movdqa %%xmm4, %%xmm0                                           \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: G7..G0 << 8             \n\
        pmulhuw %%xmm7, %%xmm0          # XMM0: multiply by 219/255>>2  \n\
        psrlw $6, %%xmm0                # XMM0: shift down to 8 bits    \n\
        paddw %%xmm6, %%xmm0            # XMM0: bias by 16              \n\
        por %%xmm5, %%xmm0              # XMM0: OR in U/V bytes         \n\
        movdqu %%xmm0, -16("EDI","ECX",2)                               \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (GRAY_Y), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

static int gray8_uyvy_sse2(uint8_t **src, uint8_t **dest, int width, int height) {
    asm("movdqa 16("EDX"), %%xmm7       # constant: 219/255             \n\
        movdqa 32("EDX"), %%xmm6                                        \n\
        psllw $8, %%xmm6                # constant: 16 << 8             \n\
        pcmpeqd %%xmm5, %%xmm5                                          \n\
        psllw $15, %%xmm5                                               \n\
        psrlw $8, %%xmm5                # constant: 0x0080              \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n\
        pcmpeqd %%xmm3, %%xmm3                                          \n\
        psllw $8, %%xmm3                # constant: 0xFF00              \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 8,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve gray byte            \n\
        imull %3, %%eax                 # multiply by 219/255           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        addl $16, %%eax                 # add 16                        \n\
        movb %%al, -1("EDI","ECX",2)    # and store                     \n\
        movb $128, -2("EDI","ECX",2)    # store 128 in U/V byte         \n",
        /* main_loop */ "\
        movq -8("ESI","ECX"), %%xmm2    # XMM2: G5..G0                  \n\
        movdqa %%xmm4, %%xmm0                                           \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: G7..G0 << 8             \n\
        pmulhuw %%xmm7, %%xmm0          # XMM0: multiply by 219/255>>2  \n\
        psllw $2, %%xmm0                # XMM0: shift results to hi byte\n\
        pand %%xmm3, %%xmm0             # XMM0: clear low byte          \n\
        paddw %%xmm6, %%xmm0            # XMM0: bias by 16              \n\
        por %%xmm5, %%xmm0              # XMM0: OR in U/V bytes         \n\
        movdqu %%xmm0, -16("EDI","ECX",2)                               \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (GRAY_Y), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

/*************************************************************************/

static int y8_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        movdqa 48("EDX"), %%xmm5        # constant: bytes 0/3/6/9 mask  \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "push "EBX,
        /* pop_regs  */ "pop "EBX,
        /* small_loop */ "\
        lea ("ECX","ECX",2), "EDX"      # 3*count for RGB offset        \n\
        movzbl -1("ESI","ECX"), %%eax   # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%ebx                                                 \n\
        cmovnz %%ebx, %%eax                                             \n\
        movl $0, %%ebx                                                  \n\
        cmovs %%ebx, %%eax                                              \n\
        movb %%al, -3("EDI","EDX")      # and store                     \n\
        movb %%al, -2("EDI","EDX")                                      \n\
        movb %%al, -1("EDI","EDX")                                      \n",
        /* main_loop */ "\
        lea ("ECX","ECX",2), "EDX"                                      \n\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: Y3..Y0                  \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: Y3..Y0 in 16 bits       \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        psllw $2, %%xmm0                # XMM0: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        packuswb %%xmm0, %%xmm0         # XMM0: G3..G0, saturated       \n\
        pshuflw $0x50, %%xmm0, %%xmm0   # X0.l: G3 G2 G3 G2 G1 G0 G1 G0 \n\
        pshufhw $0x55, %%xmm0, %%xmm0   # X0.h: G3 G2 G3 G2 G3 G2 G3 G2 \n\
        pand %%xmm5, %%xmm0             # XMM0: ------3--2--1--0        \n\
        movdqa %%xmm0, %%xmm1           # XMM1: ------3--2--1--0        \n\
        pslldq $1, %%xmm1               # XMM1: -----3--2--1--0-        \n\
        movdqa %%xmm0, %%xmm2           # XMM2: ------3--2--1--0        \n\
        pslldq $2, %%xmm2               # XMM2: ----3--2--1--0--        \n\
        por %%xmm1, %%xmm0              # XMM0: -----33-22-11-00        \n\
        por %%xmm2, %%xmm0              # XMM0: ----333222111000        \n\
        movd %%xmm0, -12("EDI","EDX")                                   \n\
        pshufd $0xC9, %%xmm0, %%xmm0                                    \n\
        movq %%xmm0, -8("EDI","EDX")                                    \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

/* 4BPP is slightly easier... */
static int y8_rgba32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%edx                                                 \n\
        cmovnz %%edx, %%eax                                             \n\
        movl $0, %%edx                                                  \n\
        cmovs %%edx, %%eax                                              \n\
        movb %%al, -4("EDI","ECX",4)      # and store                   \n\
        movb %%al, -3("EDI","ECX",4)                                    \n\
        movb %%al, -2("EDI","ECX",4)                                    \n",
        /* main_loop */ "\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: Y3..Y0                  \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: Y3..Y0 in 16 bits       \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        psllw $2, %%xmm0                # XMM0: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        packuswb %%xmm0, %%xmm0         # XMM0: G3..G0, saturated       \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: G3..G0 in 16 bits       \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: ---3---2---1---0        \n\
        movdqa %%xmm0, %%xmm1           # XMM1: ---3---2---1---0        \n\
        pslldq $1, %%xmm1               # XMM1: --3---2---1---0-        \n\
        movdqa %%xmm0, %%xmm2           # XMM2: ---3---2---1---0        \n\
        pslldq $2, %%xmm2               # XMM2: -3---2---1---0--        \n\
        por %%xmm1, %%xmm0              # XMM0: --33--22--11--00        \n\
        por %%xmm2, %%xmm0              # XMM0: -333-222-111-000        \n\
        movntdq %%xmm0, -16("EDI","ECX",4)                              \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

static int y8_argb32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm7         # constant: 255/219             \n\
        movdqa 32("EDX"), %%xmm6        # constant: 16                  \n\
        pxor %%xmm4, %%xmm4             # constant: 0                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movzbl -1("ESI","ECX"), %%eax   # retrieve Y byte               \n\
        subl $16, %%eax                 # subtract 16                   \n\
        imull %3, %%eax                 # multiply by 255/219           \n\
        shrl $14, %%eax                 # shift down to 8 bits          \n\
        testb %%ah, %%ah                # saturate to 0..255            \n\
        movl $-1, %%edx                                                 \n\
        cmovnz %%edx, %%eax                                             \n\
        movl $0, %%edx                                                  \n\
        cmovs %%edx, %%eax                                              \n\
        movb %%al, -3("EDI","ECX",4)      # and store                   \n\
        movb %%al, -2("EDI","ECX",4)                                    \n\
        movb %%al, -1("EDI","ECX",4)                                    \n",
        /* main_loop */ "\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: Y3..Y0                  \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: Y3..Y0 in 16 bits       \n\
        psubw %%xmm6, %%xmm0            # XMM0: unbias by 16            \n\
        psllw $2, %%xmm0                # XMM0: fixed point 8.2         \n\
        pmulhw %%xmm7, %%xmm0           # XMM0: multiply by 255/219>>2  \n\
        packuswb %%xmm0, %%xmm0         # XMM0: G3..G0, saturated       \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: G3..G0 in 16 bits       \n\
        movdqa %%xmm4, %%xmm3           # XMM3: 0                       \n\
        punpcklbw %%xmm0, %%xmm3        # XMM3: --3---2---1---0-        \n\
        movdqa %%xmm3, %%xmm1           # XMM1: --3---2---1---0-        \n\
        pslldq $1, %%xmm1               # XMM1: -3---2---1---0--        \n\
        movdqa %%xmm3, %%xmm2           # XMM2: --3---2---1---0-        \n\
        pslldq $2, %%xmm2               # XMM2: 3---2---1---0---        \n\
        por %%xmm1, %%xmm3              # XMM3: -33--22--11--00-        \n\
        por %%xmm2, %%xmm3              # XMM3: 333-222-111-000-        \n\
        movntdq %%xmm3, -16("EDI","ECX",4)                              \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "i" (Y_GRAY), "d" (&gray_data), "m" (gray_data)
        : "eax");
    return 1;
}

/*************************************************************************/

#endif  /* HAVE_ASM_SSE2 */

/*************************************************************************/
/*************************************************************************/

/* Initialization */

int ac_imgconvert_init_yuv_rgb(int accel)
{
    /******** Standard C implementations ********/

    //---- YUV->RGB ----//

    if (!register_conversion(IMG_YUV420P, IMG_RGB24,   yuv420p_rgb24)
     || !register_conversion(IMG_YUV411P, IMG_RGB24,   yuv411p_rgb24)
     || !register_conversion(IMG_YUV422P, IMG_RGB24,   yuv422p_rgb24)
     || !register_conversion(IMG_YUV444P, IMG_RGB24,   yuv444p_rgb24)
     || !register_conversion(IMG_YUY2,    IMG_RGB24,   yuy2_rgb24)
     || !register_conversion(IMG_UYVY,    IMG_RGB24,   uyvy_rgb24)
     || !register_conversion(IMG_YVYU,    IMG_RGB24,   yvyu_rgb24)
     || !register_conversion(IMG_Y8,      IMG_RGB24,   y8_rgb24)

     || !register_conversion(IMG_YUV420P, IMG_BGR24,   yuv420p_bgr24)
     || !register_conversion(IMG_YUV411P, IMG_BGR24,   yuv411p_bgr24)
     || !register_conversion(IMG_YUV422P, IMG_BGR24,   yuv422p_bgr24)
     || !register_conversion(IMG_YUV444P, IMG_BGR24,   yuv444p_bgr24)
     || !register_conversion(IMG_YUY2,    IMG_BGR24,   yuy2_bgr24)
     || !register_conversion(IMG_UYVY,    IMG_BGR24,   uyvy_bgr24)
     || !register_conversion(IMG_YVYU,    IMG_BGR24,   yvyu_bgr24)
     || !register_conversion(IMG_Y8,      IMG_BGR24,   y8_rgb24)

     || !register_conversion(IMG_YUV420P, IMG_RGBA32,  yuv420p_rgba32)
     || !register_conversion(IMG_YUV411P, IMG_RGBA32,  yuv411p_rgba32)
     || !register_conversion(IMG_YUV422P, IMG_RGBA32,  yuv422p_rgba32)
     || !register_conversion(IMG_YUV444P, IMG_RGBA32,  yuv444p_rgba32)
     || !register_conversion(IMG_YUY2,    IMG_RGBA32,  yuy2_rgba32)
     || !register_conversion(IMG_UYVY,    IMG_RGBA32,  uyvy_rgba32)
     || !register_conversion(IMG_YVYU,    IMG_RGBA32,  yvyu_rgba32)
     || !register_conversion(IMG_Y8,      IMG_RGBA32,  y8_rgba32)

     || !register_conversion(IMG_YUV420P, IMG_ABGR32,  yuv420p_abgr32)
     || !register_conversion(IMG_YUV411P, IMG_ABGR32,  yuv411p_abgr32)
     || !register_conversion(IMG_YUV422P, IMG_ABGR32,  yuv422p_abgr32)
     || !register_conversion(IMG_YUV444P, IMG_ABGR32,  yuv444p_abgr32)
     || !register_conversion(IMG_YUY2,    IMG_ABGR32,  yuy2_abgr32)
     || !register_conversion(IMG_UYVY,    IMG_ABGR32,  uyvy_abgr32)
     || !register_conversion(IMG_YVYU,    IMG_ABGR32,  yvyu_abgr32)
     || !register_conversion(IMG_Y8,      IMG_ABGR32,  y8_argb32)

     || !register_conversion(IMG_YUV420P, IMG_ARGB32,  yuv420p_argb32)
     || !register_conversion(IMG_YUV411P, IMG_ARGB32,  yuv411p_argb32)
     || !register_conversion(IMG_YUV422P, IMG_ARGB32,  yuv422p_argb32)
     || !register_conversion(IMG_YUV444P, IMG_ARGB32,  yuv444p_argb32)
     || !register_conversion(IMG_YUY2,    IMG_ARGB32,  yuy2_argb32)
     || !register_conversion(IMG_UYVY,    IMG_ARGB32,  uyvy_argb32)
     || !register_conversion(IMG_YVYU,    IMG_ARGB32,  yvyu_argb32)
     || !register_conversion(IMG_Y8,      IMG_ARGB32,  y8_argb32)

     || !register_conversion(IMG_YUV420P, IMG_BGRA32,  yuv420p_bgra32)
     || !register_conversion(IMG_YUV411P, IMG_BGRA32,  yuv411p_bgra32)
     || !register_conversion(IMG_YUV422P, IMG_BGRA32,  yuv422p_bgra32)
     || !register_conversion(IMG_YUV444P, IMG_BGRA32,  yuv444p_bgra32)
     || !register_conversion(IMG_YUY2,    IMG_BGRA32,  yuy2_bgra32)
     || !register_conversion(IMG_UYVY,    IMG_BGRA32,  uyvy_bgra32)
     || !register_conversion(IMG_YVYU,    IMG_BGRA32,  yvyu_bgra32)
     || !register_conversion(IMG_Y8,      IMG_BGRA32,  y8_rgba32)

    //---- RGB->YUV ----//

     || !register_conversion(IMG_RGB24,   IMG_YUV420P, rgb24_yuv420p)
     || !register_conversion(IMG_RGB24,   IMG_YUV411P, rgb24_yuv411p)
     || !register_conversion(IMG_RGB24,   IMG_YUV422P, rgb24_yuv422p)
     || !register_conversion(IMG_RGB24,   IMG_YUV444P, rgb24_yuv444p)
     || !register_conversion(IMG_RGB24,   IMG_YUY2,    rgb24_yuy2)
     || !register_conversion(IMG_RGB24,   IMG_UYVY,    rgb24_uyvy)
     || !register_conversion(IMG_RGB24,   IMG_YVYU,    rgb24_yvyu)
     || !register_conversion(IMG_RGB24,   IMG_Y8,      rgb24_y8)

     || !register_conversion(IMG_BGR24,   IMG_YUV420P, bgr24_yuv420p)
     || !register_conversion(IMG_BGR24,   IMG_YUV411P, bgr24_yuv411p)
     || !register_conversion(IMG_BGR24,   IMG_YUV422P, bgr24_yuv422p)
     || !register_conversion(IMG_BGR24,   IMG_YUV444P, bgr24_yuv444p)
     || !register_conversion(IMG_BGR24,   IMG_YUY2,    bgr24_yuy2)
     || !register_conversion(IMG_BGR24,   IMG_UYVY,    bgr24_uyvy)
     || !register_conversion(IMG_BGR24,   IMG_YVYU,    bgr24_yvyu)
     || !register_conversion(IMG_BGR24,   IMG_Y8,      bgr24_y8)

     || !register_conversion(IMG_RGBA32,  IMG_YUV420P, rgba32_yuv420p)
     || !register_conversion(IMG_RGBA32,  IMG_YUV411P, rgba32_yuv411p)
     || !register_conversion(IMG_RGBA32,  IMG_YUV422P, rgba32_yuv422p)
     || !register_conversion(IMG_RGBA32,  IMG_YUV444P, rgba32_yuv444p)
     || !register_conversion(IMG_RGBA32,  IMG_YUY2,    rgba32_yuy2)
     || !register_conversion(IMG_RGBA32,  IMG_UYVY,    rgba32_uyvy)
     || !register_conversion(IMG_RGBA32,  IMG_YVYU,    rgba32_yvyu)
     || !register_conversion(IMG_RGBA32,  IMG_Y8,      rgba32_y8)

     || !register_conversion(IMG_ABGR32,  IMG_YUV420P, abgr32_yuv420p)
     || !register_conversion(IMG_ABGR32,  IMG_YUV411P, abgr32_yuv411p)
     || !register_conversion(IMG_ABGR32,  IMG_YUV422P, abgr32_yuv422p)
     || !register_conversion(IMG_ABGR32,  IMG_YUV444P, abgr32_yuv444p)
     || !register_conversion(IMG_ABGR32,  IMG_YUY2,    abgr32_yuy2)
     || !register_conversion(IMG_ABGR32,  IMG_UYVY,    abgr32_uyvy)
     || !register_conversion(IMG_ABGR32,  IMG_YVYU,    abgr32_yvyu)
     || !register_conversion(IMG_ABGR32,  IMG_Y8,      abgr32_y8)

     || !register_conversion(IMG_ARGB32,  IMG_YUV420P, argb32_yuv420p)
     || !register_conversion(IMG_ARGB32,  IMG_YUV411P, argb32_yuv411p)
     || !register_conversion(IMG_ARGB32,  IMG_YUV422P, argb32_yuv422p)
     || !register_conversion(IMG_ARGB32,  IMG_YUV444P, argb32_yuv444p)
     || !register_conversion(IMG_ARGB32,  IMG_YUY2,    argb32_yuy2)
     || !register_conversion(IMG_ARGB32,  IMG_UYVY,    argb32_uyvy)
     || !register_conversion(IMG_ARGB32,  IMG_YVYU,    argb32_yvyu)
     || !register_conversion(IMG_ARGB32,  IMG_Y8,      argb32_y8)

     || !register_conversion(IMG_BGRA32,  IMG_YUV420P, bgra32_yuv420p)
     || !register_conversion(IMG_BGRA32,  IMG_YUV411P, bgra32_yuv411p)
     || !register_conversion(IMG_BGRA32,  IMG_YUV422P, bgra32_yuv422p)
     || !register_conversion(IMG_BGRA32,  IMG_YUV444P, bgra32_yuv444p)
     || !register_conversion(IMG_BGRA32,  IMG_YUY2,    bgra32_yuy2)
     || !register_conversion(IMG_BGRA32,  IMG_UYVY,    bgra32_uyvy)
     || !register_conversion(IMG_BGRA32,  IMG_YVYU,    bgra32_yvyu)
     || !register_conversion(IMG_BGRA32,  IMG_Y8,      bgra32_y8)

    //---- Grayscale ----//

     || !register_conversion(IMG_YUV420P, IMG_GRAY8,   yuvp_gray8)
     || !register_conversion(IMG_YUV411P, IMG_GRAY8,   yuvp_gray8)
     || !register_conversion(IMG_YUV422P, IMG_GRAY8,   yuvp_gray8)
     || !register_conversion(IMG_YUV444P, IMG_GRAY8,   yuvp_gray8)
     || !register_conversion(IMG_YUY2,    IMG_GRAY8,   yuy2_gray8)
     || !register_conversion(IMG_UYVY,    IMG_GRAY8,   uyvy_gray8)
     || !register_conversion(IMG_YVYU,    IMG_GRAY8,   yuy2_gray8)
     || !register_conversion(IMG_Y8,      IMG_GRAY8,   yuvp_gray8)

     || !register_conversion(IMG_GRAY8,   IMG_YUV420P, gray8_yuv420p)
     || !register_conversion(IMG_GRAY8,   IMG_YUV411P, gray8_yuv411p)
     || !register_conversion(IMG_GRAY8,   IMG_YUV422P, gray8_yuv422p)
     || !register_conversion(IMG_GRAY8,   IMG_YUV444P, gray8_yuv444p)
     || !register_conversion(IMG_GRAY8,   IMG_YUY2,    gray8_yuy2)
     || !register_conversion(IMG_GRAY8,   IMG_UYVY,    gray8_uyvy)
     || !register_conversion(IMG_GRAY8,   IMG_YVYU,    gray8_yuy2)
     || !register_conversion(IMG_GRAY8,   IMG_Y8,      gray8_y8)
    ) {
        return 0;
    }

    /******** MMX implementations ********/

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)
    if (accel & AC_MMX) {

        //---- YUV->RGB ----//

        if (!register_conversion(IMG_YUV420P, IMG_RGB24,   yuv420p_rgb24_mmx)
         || !register_conversion(IMG_YUV422P, IMG_RGB24,   yuv422p_rgb24_mmx)
         || !register_conversion(IMG_YUV420P, IMG_BGR24,   yuv420p_bgr24_mmx)
         || !register_conversion(IMG_YUV422P, IMG_BGR24,   yuv422p_bgr24_mmx)
         || !register_conversion(IMG_YUV420P, IMG_RGBA32,  yuv420p_rgba32_mmx)
         || !register_conversion(IMG_YUV422P, IMG_RGBA32,  yuv422p_rgba32_mmx)
         || !register_conversion(IMG_YUV420P, IMG_ABGR32,  yuv420p_abgr32_mmx)
         || !register_conversion(IMG_YUV422P, IMG_ABGR32,  yuv422p_abgr32_mmx)
         || !register_conversion(IMG_YUV420P, IMG_ARGB32,  yuv420p_argb32_mmx)
         || !register_conversion(IMG_YUV422P, IMG_ARGB32,  yuv422p_argb32_mmx)
         || !register_conversion(IMG_YUV420P, IMG_BGRA32,  yuv420p_bgra32_mmx)
         || !register_conversion(IMG_YUV422P, IMG_BGRA32,  yuv422p_bgra32_mmx)
        ) {
            return 0;
        }
    }
#endif

    /******** SSE2 implementations ********/

#if defined(HAVE_ASM_SSE2)
    if (HAS_ACCEL(accel, AC_SSE2)) {

        //---- YUV->RGB ----//
        if (!register_conversion(IMG_YUV420P, IMG_RGB24,   yuv420p_rgb24_sse2)
         || !register_conversion(IMG_YUV411P, IMG_RGB24,   yuv411p_rgb24_sse2)
         || !register_conversion(IMG_YUV422P, IMG_RGB24,   yuv422p_rgb24_sse2)
         || !register_conversion(IMG_YUV444P, IMG_RGB24,   yuv444p_rgb24_sse2)
         || !register_conversion(IMG_YUY2,    IMG_RGB24,   yuy2_rgb24_sse2)
         || !register_conversion(IMG_UYVY,    IMG_RGB24,   uyvy_rgb24_sse2)
         || !register_conversion(IMG_YVYU,    IMG_RGB24,   yvyu_rgb24_sse2)
         || !register_conversion(IMG_Y8,      IMG_RGB24,   y8_rgb24_sse2)

         || !register_conversion(IMG_YUV420P, IMG_BGR24,   yuv420p_bgr24_sse2)
         || !register_conversion(IMG_YUV411P, IMG_BGR24,   yuv411p_bgr24_sse2)
         || !register_conversion(IMG_YUV422P, IMG_BGR24,   yuv422p_bgr24_sse2)
         || !register_conversion(IMG_YUV444P, IMG_BGR24,   yuv444p_bgr24_sse2)
         || !register_conversion(IMG_YUY2,    IMG_BGR24,   yuy2_bgr24_sse2)
         || !register_conversion(IMG_UYVY,    IMG_BGR24,   uyvy_bgr24_sse2)
         || !register_conversion(IMG_YVYU,    IMG_BGR24,   yvyu_bgr24_sse2)
         || !register_conversion(IMG_Y8,      IMG_BGR24,   y8_rgb24_sse2)

         || !register_conversion(IMG_YUV420P, IMG_RGBA32,  yuv420p_rgba32_sse2)
         || !register_conversion(IMG_YUV411P, IMG_RGBA32,  yuv411p_rgba32_sse2)
         || !register_conversion(IMG_YUV422P, IMG_RGBA32,  yuv422p_rgba32_sse2)
         || !register_conversion(IMG_YUV444P, IMG_RGBA32,  yuv444p_rgba32_sse2)
         || !register_conversion(IMG_YUY2,    IMG_RGBA32,  yuy2_rgba32_sse2)
         || !register_conversion(IMG_UYVY,    IMG_RGBA32,  uyvy_rgba32_sse2)
         || !register_conversion(IMG_YVYU,    IMG_RGBA32,  yvyu_rgba32_sse2)
         || !register_conversion(IMG_Y8,      IMG_RGBA32,  y8_rgba32_sse2)

         || !register_conversion(IMG_YUV420P, IMG_ABGR32,  yuv420p_abgr32_sse2)
         || !register_conversion(IMG_YUV411P, IMG_ABGR32,  yuv411p_abgr32_sse2)
         || !register_conversion(IMG_YUV422P, IMG_ABGR32,  yuv422p_abgr32_sse2)
         || !register_conversion(IMG_YUV444P, IMG_ABGR32,  yuv444p_abgr32_sse2)
         || !register_conversion(IMG_YUY2,    IMG_ABGR32,  yuy2_abgr32_sse2)
         || !register_conversion(IMG_UYVY,    IMG_ABGR32,  uyvy_abgr32_sse2)
         || !register_conversion(IMG_YVYU,    IMG_ABGR32,  yvyu_abgr32_sse2)
         || !register_conversion(IMG_Y8,      IMG_ABGR32,  y8_argb32_sse2)

         || !register_conversion(IMG_YUV420P, IMG_ARGB32,  yuv420p_argb32_sse2)
         || !register_conversion(IMG_YUV411P, IMG_ARGB32,  yuv411p_argb32_sse2)
         || !register_conversion(IMG_YUV422P, IMG_ARGB32,  yuv422p_argb32_sse2)
         || !register_conversion(IMG_YUV444P, IMG_ARGB32,  yuv444p_argb32_sse2)
         || !register_conversion(IMG_YUY2,    IMG_ARGB32,  yuy2_argb32_sse2)
         || !register_conversion(IMG_UYVY,    IMG_ARGB32,  uyvy_argb32_sse2)
         || !register_conversion(IMG_YVYU,    IMG_ARGB32,  yvyu_argb32_sse2)
         || !register_conversion(IMG_Y8,      IMG_ARGB32,  y8_argb32_sse2)

         || !register_conversion(IMG_YUV420P, IMG_BGRA32,  yuv420p_bgra32_sse2)
         || !register_conversion(IMG_YUV411P, IMG_BGRA32,  yuv411p_bgra32_sse2)
         || !register_conversion(IMG_YUV422P, IMG_BGRA32,  yuv422p_bgra32_sse2)
         || !register_conversion(IMG_YUV444P, IMG_BGRA32,  yuv444p_bgra32_sse2)
         || !register_conversion(IMG_YUY2,    IMG_BGRA32,  yuy2_bgra32_sse2)
         || !register_conversion(IMG_UYVY,    IMG_BGRA32,  uyvy_bgra32_sse2)
         || !register_conversion(IMG_YVYU,    IMG_BGRA32,  yvyu_bgra32_sse2)
         || !register_conversion(IMG_Y8,      IMG_BGRA32,  y8_rgba32_sse2)

        //---- RGB->YUV ----//

         || !register_conversion(IMG_RGB24,   IMG_YUV420P, rgb24_yuv420p_sse2)
         || !register_conversion(IMG_RGB24,   IMG_YUV411P, rgb24_yuv411p_sse2)
         || !register_conversion(IMG_RGB24,   IMG_YUV422P, rgb24_yuv422p_sse2)
         || !register_conversion(IMG_RGB24,   IMG_YUV444P, rgb24_yuv444p_sse2)
         || !register_conversion(IMG_RGB24,   IMG_YUY2,    rgb24_yuy2_sse2)
         || !register_conversion(IMG_RGB24,   IMG_UYVY,    rgb24_uyvy_sse2)
         || !register_conversion(IMG_RGB24,   IMG_YVYU,    rgb24_yvyu_sse2)
         || !register_conversion(IMG_RGB24,   IMG_Y8,      rgb24_y8_sse2)

         || !register_conversion(IMG_BGR24,   IMG_YUV420P, bgr24_yuv420p_sse2)
         || !register_conversion(IMG_BGR24,   IMG_YUV411P, bgr24_yuv411p_sse2)
         || !register_conversion(IMG_BGR24,   IMG_YUV422P, bgr24_yuv422p_sse2)
         || !register_conversion(IMG_BGR24,   IMG_YUV444P, bgr24_yuv444p_sse2)
         || !register_conversion(IMG_BGR24,   IMG_YUY2,    bgr24_yuy2_sse2)
         || !register_conversion(IMG_BGR24,   IMG_UYVY,    bgr24_uyvy_sse2)
         || !register_conversion(IMG_BGR24,   IMG_YVYU,    bgr24_yvyu_sse2)
         || !register_conversion(IMG_BGR24,   IMG_Y8,      bgr24_y8_sse2)

         || !register_conversion(IMG_RGBA32,  IMG_YUV420P, rgba32_yuv420p_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_YUV411P, rgba32_yuv411p_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_YUV422P, rgba32_yuv422p_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_YUV444P, rgba32_yuv444p_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_YUY2,    rgba32_yuy2_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_UYVY,    rgba32_uyvy_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_YVYU,    rgba32_yvyu_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_Y8,      rgba32_y8_sse2)

         || !register_conversion(IMG_ABGR32,  IMG_YUV420P, abgr32_yuv420p_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_YUV411P, abgr32_yuv411p_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_YUV422P, abgr32_yuv422p_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_YUV444P, abgr32_yuv444p_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_YUY2,    abgr32_yuy2_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_UYVY,    abgr32_uyvy_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_YVYU,    abgr32_yvyu_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_Y8,      abgr32_y8_sse2)

         || !register_conversion(IMG_ARGB32,  IMG_YUV420P, argb32_yuv420p_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_YUV411P, argb32_yuv411p_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_YUV422P, argb32_yuv422p_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_YUV444P, argb32_yuv444p_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_YUY2,    argb32_yuy2_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_UYVY,    argb32_uyvy_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_YVYU,    argb32_yvyu_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_Y8,      argb32_y8_sse2)

         || !register_conversion(IMG_BGRA32,  IMG_YUV420P, bgra32_yuv420p_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_YUV411P, bgra32_yuv411p_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_YUV422P, bgra32_yuv422p_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_YUV444P, bgra32_yuv444p_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_YUY2,    bgra32_yuy2_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_UYVY,    bgra32_uyvy_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_YVYU,    bgra32_yvyu_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_Y8,      bgra32_y8_sse2)

        //---- Grayscale ----//

         || !register_conversion(IMG_GRAY8,   IMG_YUY2,    gray8_yuy2_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_UYVY,    gray8_uyvy_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_YVYU,    gray8_yuy2_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_Y8,      gray8_y8_sse2)
        ) {
            return 0;
        }
    }

    /* YUV->GRAY8 routines use CMOVcc */
    if (HAS_ACCEL(accel, AC_CMOVE|AC_SSE2)) {
        if (!register_conversion(IMG_YUV420P, IMG_GRAY8,   yuvp_gray8_sse2)
         || !register_conversion(IMG_YUV411P, IMG_GRAY8,   yuvp_gray8_sse2)
         || !register_conversion(IMG_YUV422P, IMG_GRAY8,   yuvp_gray8_sse2)
         || !register_conversion(IMG_YUV444P, IMG_GRAY8,   yuvp_gray8_sse2)
         || !register_conversion(IMG_YUY2,    IMG_GRAY8,   yuy2_gray8_sse2)
         || !register_conversion(IMG_UYVY,    IMG_GRAY8,   uyvy_gray8_sse2)
         || !register_conversion(IMG_YVYU,    IMG_GRAY8,   yuy2_gray8_sse2)
         || !register_conversion(IMG_Y8,      IMG_GRAY8,   yuvp_gray8_sse2)
        ) {
            return 0;
        }
    }
#endif

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
