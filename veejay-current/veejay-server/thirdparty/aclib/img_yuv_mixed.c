/*
 * img_yuv_packed.c - YUV planar<->packed image format conversion routines
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

/* Wrappers for UYVY and YVYU */
/* Note: we rely on YUY2<->{UYVY,YVYU} working for src==dest */
/* FIXME: when converting from UYVY/YVYU, src is destroyed! */

static int uyvy_yvyu_wrapper(uint8_t **src, ImageFormat srcfmt,
                             uint8_t **dest, ImageFormat destfmt,
                             int width, int height)
{
    if (srcfmt == IMG_UYVY || srcfmt == IMG_YVYU)
        return ac_imgconvert(src, srcfmt, src, IMG_YUY2, width, height)
            && ac_imgconvert(src, IMG_YUY2, dest, destfmt, width, height);
    else
        return ac_imgconvert(src, srcfmt, dest, IMG_YUY2, width, height)
            && ac_imgconvert(dest, IMG_YUY2, dest, destfmt, width, height);
}

static int yuv420p_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV420P, dest, IMG_UYVY, width, height); }

static int yuv420p_yvyu(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV420P, dest, IMG_YVYU, width, height); }

static int yuv411p_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV411P, dest, IMG_UYVY, width, height); }

static int yuv411p_yvyu(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV411P, dest, IMG_YVYU, width, height); }

static int yuv422p_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV422P, dest, IMG_UYVY, width, height); }

static int yuv422p_yvyu(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV422P, dest, IMG_YVYU, width, height); }

static int yuv444p_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV444P, dest, IMG_UYVY, width, height); }

static int yuv444p_yvyu(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YUV444P, dest, IMG_YVYU, width, height); }

static int uyvy_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_UYVY, dest, IMG_YUV420P, width, height); }

static int yvyu_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YVYU, dest, IMG_YUV420P, width, height); }

static int uyvy_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_UYVY, dest, IMG_YUV411P, width, height); }

static int yvyu_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YVYU, dest, IMG_YUV411P, width, height); }

static int uyvy_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_UYVY, dest, IMG_YUV422P, width, height); }

static int yvyu_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YVYU, dest, IMG_YUV422P, width, height); }

static int uyvy_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_UYVY, dest, IMG_YUV444P, width, height); }

static int yvyu_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{ return uyvy_yvyu_wrapper(src, IMG_YVYU, dest, IMG_YUV444P, width, height); }

/*************************************************************************/

static int yuv420p_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;

    for (y = 0; y < (height & ~1); y++) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[0][(y*width+x)*2  ] = src[0][y*width+x];
            dest[0][(y*width+x)*2+1] = src[1][(y/2)*(width/2)+x/2];
            dest[0][(y*width+x)*2+2] = src[0][y*width+x+1];
            dest[0][(y*width+x)*2+3] = src[2][(y/2)*(width/2)+x/2];
        }
    }
    return 1;
}

static int yuv411p_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;

    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[0][(y*width+x)*2  ] = src[0][y*width+x];
            dest[0][(y*width+x)*2+1] = src[1][y*(width/4)+x/4];
            dest[0][(y*width+x)*2+2] = src[0][y*width+x+1];
            dest[0][(y*width+x)*2+3] = src[2][y*(width/4)+x/4];
        }
    }
    return 1;
}

static int yuv422p_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < (width/2)*height; i++) {
        dest[0][i*4  ] = src[0][i*2];
        dest[0][i*4+1] = src[1][i];
        dest[0][i*4+2] = src[0][i*2+1];
        dest[0][i*4+3] = src[2][i];
    }
    return 1;
}

static int yuv444p_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < (width/2)*height; i++) {
        dest[0][i*4  ] = src[0][i*2];
        dest[0][i*4+1] = (src[1][i*2] + src[1][i*2+1]) / 2;
        dest[0][i*4+2] = src[0][i*2+1];
        dest[0][i*4+3] = (src[2][i*2] + src[2][i*2+1]) / 2;
    }
    return 1;
}

/*************************************************************************/

static int yuy2_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;

    for (y = 0; y < (height & ~1); y++) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[0][y*width+x  ] = src[0][(y*width+x)*2  ];
            dest[0][y*width+x+1] = src[0][(y*width+x)*2+2];
            if (y%2 == 0) {
                dest[1][(y/2)*(width/2)+x/2] = src[0][(y*width+x)*2+1];
                dest[2][(y/2)*(width/2)+x/2] = src[0][(y*width+x)*2+3];
            } else {
                dest[1][(y/2)*(width/2)+x/2] =
                    (dest[1][(y/2)*(width/2)+x/2] + src[0][(y*width+x)*2+1] + 1) / 2;
                dest[2][(y/2)*(width/2)+x/2] =
                    (dest[2][(y/2)*(width/2)+x/2] + src[0][(y*width+x)*2+3] + 1) / 2;
            }
        }
    }
    return 1;
}

static int yuy2_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~3); x += 4) {
            dest[0][y*width+x]       = src[0][(y*width+x)*2  ];
            dest[0][y*width+x+1]     = src[0][(y*width+x)*2+2];
            dest[0][y*width+x+2]     = src[0][(y*width+x)*2+4];
            dest[0][y*width+x+3]     = src[0][(y*width+x)*2+6];
            dest[1][y*(width/4)+x/4] = (src[0][(y*width+x)*2+1]
                                      + src[0][(y*width+x)*2+5] + 1) / 2;
            dest[2][y*(width/4)+x/4] = (src[0][(y*width+x)*2+3]
                                      + src[0][(y*width+x)*2+7] + 1) / 2;
        }
    }
    return 1;
}

static int yuy2_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < (width/2)*height; i++) {
        dest[0][i*2]   = src[0][i*4  ];
        dest[1][i]     = src[0][i*4+1];
        dest[0][i*2+1] = src[0][i*4+2];
        dest[2][i]     = src[0][i*4+3];
    }
    return 1;
}

static int yuy2_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < (width & ~1)*height; i += 2) {
        dest[0][i]   = src[0][i*2  ];
        dest[1][i]   = src[0][i*2+1];
        dest[1][i+1] = src[0][i*2+1];
        dest[0][i+1] = src[0][i*2+2];
        dest[2][i]   = src[0][i*2+3];
        dest[2][i+1] = src[0][i*2+3];
    }
    return 1;
}

/*************************************************************************/

static int y8_yuy2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*2  ] = src[0][i];
        dest[0][i*2+1] = 128;
    }
    return 1;
}

static int y8_uyvy(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*2  ] = 128;
        dest[0][i*2+1] = src[0][i];
    }
    return 1;
}

static int yuy2_y8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++)
        dest[0][i] = src[0][i*2];
    return 1;
}

static int uyvy_y8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++)
        dest[0][i] = src[0][i*2+1];
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#if defined(HAVE_ASM_SSE2)

/* SSE2 routines.  See comments in img_x86_common.h for why we don't bother
 * unrolling the loops. */

/* Common macros/data for x86 code */
#include "img_x86_common.h"

/* YUV420P (1 row) or YUV422P -> YUY2 (unit: 2 pixels) */
#define YUV42XP_YUY2 \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 8,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movb -1("EDX","ECX"), %%bh                                     \n\
        movb -1("ESI","ECX",2), %%bl                                    \n\
        shll $16, %%ebx                                                 \n\
        movb -1("EAX","ECX"), %%bh                                      \n\
        movb -2("ESI","ECX",2), %%bl                                    \n\
        movl %%ebx, -4("EDI","ECX",4)",                                 \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2),%%xmm0 #XM0: YF YE YD ..... Y2 Y1 Y0 \n\
        movq -8("EAX","ECX"), %%xmm2    # XMM2: U7 U6 U5 U4 U3 U2 U1 U0 \n\
        movq -8("EDX","ECX"), %%xmm3    # XMM3: V7 V6 V5 V4 V3 V2 V1 V0 \n\
        punpcklbw %%xmm3, %%xmm2        # XMM2: V7 U7 V6 ..... U1 V0 U0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: YF YE YD ..... Y2 Y1 Y0 \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        punpckhbw %%xmm2, %%xmm1        # XMM1: V7 YF U7 ..... Y9 U4 Y8 \n\
        movdqu %%xmm0, -32("EDI","ECX",4)                               \n\
        movdqu %%xmm1, -16("EDI","ECX",4)",                             \
        /* emms */ "emms")

/* YUV411P -> YUY2 (unit: 4 pixels) */
#define YUV411P_YUY2 \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 4,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movb -1("EDX","ECX"), %%bh                                     \n\
        movb -1("ESI","ECX",4), %%bl                                    \n\
        shll $16, %%ebx                                                 \n\
        movb -1("EAX","ECX"), %%bh                                      \n\
        movb -2("ESI","ECX",4), %%bl                                    \n\
        movl %%ebx, -4("EDI","ECX",8)                                   \n\
        movb -1("EDX","ECX"), %%bh                                      \n\
        movb -3("ESI","ECX",4), %%bl                                    \n\
        shll $16, %%ebx                                                 \n\
        movb -1("EAX","ECX"), %%bh                                      \n\
        movb -4("ESI","ECX",4), %%bl                                    \n\
        movl %%ebx, -8("EDI","ECX",8)",                                 \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XM0: YF YE YD ..... Y2 Y1 Y0 \n\
        movd -4("EAX","ECX"), %%xmm2    # XMM2:             U3 U2 U1 U0 \n\
        punpcklbw %%xmm2, %%xmm2        # XMM2: U3 U3 U2 U2 U1 U1 U0 U0 \n\
        movd -4("EDX","ECX"), %%xmm3    # XMM3:             V3 V2 V1 V0 \n\
        punpcklbw %%xmm3, %%xmm3        # XMM3: V3 V3 V2 V2 V1 V1 V0 V0 \n\
        punpcklbw %%xmm3, %%xmm2        # XMM2: V3 U3 V3 ..... U0 V0 U0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: YF YE YD ..... Y2 Y1 Y0 \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: V1 Y7 U1 ..... Y1 U0 Y0 \n\
        punpckhbw %%xmm2, %%xmm1        # XMM1: V3 YF U3 ..... Y9 U2 Y8 \n\
        movdqu %%xmm0, -32("EDI","ECX",8)                               \n\
        movdqu %%xmm1, -16("EDI","ECX",8)",                             \
        /* emms */ "emms")

/* YUV444P -> YUY2 (unit: 2 pixels) */
#define YUV444P_YUY2 \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 8,                                              \
        /* push_regs */ "push "EBX"; push "EBP,                         \
        /* pop_regs  */ "pop "EBP"; pop "EBX,                           \
        /* small_loop */                                                \
        "movzbl -1("EDX","ECX",2), %%ebx                                \n\
        movzbl -2("EDX","ECX",2), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -1("EDI","ECX",4)                                    \n\
        movb -1("ESI","ECX",2), %%bl                                    \n\
        movb %%bl, -2("EDI","ECX",4)                                    \n\
        movzbl -1("EAX","ECX",2), %%ebx                                 \n\
        movzbl -2("EAX","ECX",2), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -3("EDI","ECX",4)                                    \n\
        movb -2("ESI","ECX",2), %%bl                                    \n\
        movb %%bl, -4("EDI","ECX",4)",                                  \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2),%%xmm0 #XM0: YF YE YD ..... Y2 Y1 Y0 \n\
        movdqu -16("EAX","ECX",2), %%xmm2 #XM2: UF UE UD ..... U2 U1 U0 \n\
        movdqu -16("EDX","ECX",2), %%xmm3 #XM3: VF VE VD ..... V2 V1 V0 \n\
        movdqa %%xmm2, %%xmm4           # XMM4: UF UE UD ..... U2 U1 U0 \n\
        pand %%xmm7, %%xmm2             # XMM2: -- UE -- ..... U2 -- U0 \n\
        psrlw $8, %%xmm4                # XMM4: -- UF -- ..... U3 -- U1 \n\
        pavgw %%xmm4, %%xmm2            # XMM2: -- u7 -- ..... u1 -- u0 \n\
        movdqa %%xmm3, %%xmm5           # XMM4: UF UE UD ..... U2 U1 U0 \n\
        pand %%xmm7, %%xmm3             # XMM3: -- VE -- ..... V2 -- V0 \n\
        psrlw $8, %%xmm5                # XMM5: -- VF -- ..... V3 -- V1 \n\
        pavgw %%xmm5, %%xmm3            # XMM3: -- v7 -- ..... v1 -- v0 \n\
        psllw $8, %%xmm3                # XMM3: v7 -- v6 ..... -- v0 -- \n\
        por %%xmm3, %%xmm2              # XMM2: v7 u7 v6 ..... u1 v0 u0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: YF YE YD ..... Y2 Y1 Y0 \n\
        punpcklbw %%xmm2, %%xmm0        # XMM0: v3 Y7 u3 ..... Y1 u0 Y0 \n\
        punpckhbw %%xmm2, %%xmm1        # XMM1: v7 YF u7 ..... Y9 u4 Y8 \n\
        movdqu %%xmm0, -32("EDI","ECX",4)                               \n\
        movdqu %%xmm1, -16("EDI","ECX",4)",                             \
        /* emms */ "emms")

/* YUY2 -> YUV420P (U row) (unit: 2 pixels) */
#define YUY2_YUV420P_U \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 4,                                              \
        /* push_regs */ "push "EBX"; push "EBP,                         \
        /* pop_regs  */ "pop "EBP"; pop "EBX,                           \
        /* small_loop */                                                \
        "movb -4("ESI","ECX",4), %%bl                                   \n\
        movb %%bl, -2("EDI","ECX",2)                                    \n\
        movb -2("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EDI","ECX",2)                                    \n\
        movzbl -3("ESI","ECX",4), %%ebx                                 \n\
        movzbl -3("EAX","ECX",4), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -1("EDX","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqu -16("EAX","ECX",4),%%xmm2 #XMM2: Vd Yh Ud ..... Yb Ua Ya \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        psrlw $8, %%xmm1                # XMM1: -- V3 -- ..... V0 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- Vd -- ..... Va -- Ua \n\
        pavgw %%xmm2, %%xmm1            # XMM1: -- v3 -- ..... v0 -- u0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1: v3 u3 v2 u2 v1 u1 v0 u0 \n\
        pand %%xmm7, %%xmm1             # XMM1: -- u3 -- u2 -- u1 -- u0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1:             u3 u2 u1 u0 \n\
        movq %%xmm0, -8("EDI","ECX",2)                                  \n\
        movd %%xmm1, -4("EDX","ECX")",                                  \
        /* emms */ "emms")

/* YUY2 -> YUV420P (V row) (unit: 2 pixels) */
#define YUY2_YUV420P_V \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 4,                                              \
        /* push_regs */ "push "EBX"; push "EBP,                         \
        /* pop_regs  */ "pop "EBP"; pop "EBX,                           \
        /* small_loop */                                                \
        "movb -4("ESI","ECX",4), %%bl                                   \n\
        movb %%bl, -2("EDI","ECX",2)                                    \n\
        movb -2("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EDI","ECX",2)                                    \n\
        movzbl -1("ESI","ECX",4), %%ebx                                 \n\
        movzbl -1("EAX","ECX",4), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -1("EDX","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqu -16("EAX","ECX",4),%%xmm2 #XMM2: Vd Yh Ud ..... Yb Ua Ya \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        psrlw $8, %%xmm1                # XMM1: -- V3 -- ..... V0 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- Vd -- ..... Va -- Ua \n\
        pavgw %%xmm1, %%xmm2            # XMM2: -- v3 -- ..... v0 -- u0 \n\
        packuswb %%xmm2, %%xmm2         # XMM2: v3 u3 v2 u2 v1 u1 v0 u0 \n\
        psrlw $8, %%xmm2                # XMM2: -- v3 -- v2 -- v1 -- v0 \n\
        packuswb %%xmm2, %%xmm2         # XMM2:             v3 v2 v1 v0 \n\
        movq %%xmm0, -8("EDI","ECX",2)                                  \n\
        movd %%xmm2, -4("EDX","ECX")",                                  \
        /* emms */ "emms")

/* YUY2 -> YUV411P (unit: 4 pixels) */
#define YUY2_YUV411P \
    /* Load 0x000..000FFFFFFFF into XMM6, 0x00FF*8 into XMM7 for masking */ \
    "pcmpeqd %%xmm6, %%xmm6; psrldq $12, %%xmm6;"                       \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 2,                                              \
        /* push_regs */ "push "EBX"; push "EBP,                         \
        /* pop_regs  */ "pop "EBP"; pop "EBX,                           \
        /* small_loop */                                                \
        "movb -8("ESI","ECX",8), %%bl                                   \n\
        movb %%bl, -4("EDI","ECX",4)                                    \n\
        movb -6("ESI","ECX",8), %%bl                                    \n\
        movb %%bl, -3("EDI","ECX",4)                                    \n\
        movb -4("ESI","ECX",8), %%bl                                    \n\
        movb %%bl, -2("EDI","ECX",4)                                    \n\
        movb -2("ESI","ECX",8), %%bl                                    \n\
        movb %%bl, -1("EDI","ECX",4)                                    \n\
        movzbl -7("ESI","ECX",8), %%ebx                                 \n\
        movzbl -3("ESI","ECX",8), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -1("EAX","ECX")                                      \n\
        movzbl -5("ESI","ECX",8), %%ebx                                 \n\
        movzbl -1("ESI","ECX",8), %%ebp                                 \n\
        addl %%ebp, %%ebx                                               \n\
        shrl $1, %%ebx                                                  \n\
        movb %%bl, -1("EDX","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",8),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        psrlw $8, %%xmm1                # XMM1: -- V3 -- ..... V0 -- U0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        movdqa %%xmm1, %%xmm2           # XMM2: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        pand %%xmm7, %%xmm1             # XMM1: -- U3 -- U2 -- U1 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- V3 -- V2 -- V1 -- V0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1:             U3 U2 U1 U0 \n\
        packuswb %%xmm2, %%xmm2         # XMM2:             V3 V2 V1 V0 \n\
        pand %%xmm6, %%xmm1             # XMM1: -- -- -- -- U3 U2 U1 U0 \n\
        psllq $32, %%xmm2               # XMM2: V3 V2 V1 V0 -- -- -- -- \n\
        por %%xmm1, %%xmm2              # XMM2: V3 V2 V1 V0 U3 U2 U1 U0 \n\
        movdqa %%xmm2, %%xmm1           # XMM1: V3 V2 V1 V0 U3 U2 U1 U0 \n\
        pand %%xmm7, %%xmm1             # XMM1: -- V2 -- V0 -- U2 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- V3 -- V1 -- U3 -- U1 \n\
        pavgw %%xmm2, %%xmm1            # XMM1: -- v1 -- v0 -- u1 -- u0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1:             v1 v0 u1 u0 \n\
        movq %%xmm0, -8("EDI","ECX",4)                                  \n\
        movd %%xmm1, %%ebx                                              \n\
        movw %%bx, -2("EAX","ECX")                                      \n\
        shrl $16, %%ebx;                                                \n\
        movw %%bx, -2("EDX","ECX")",                                    \
        /* emms */ "emms")

/* YUY2 -> YUV422P (unit: 2 pixels) */
#define YUY2_YUV422P \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 4,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movb -4("ESI","ECX",4), %%bl                                   \n\
        movb %%bl, -2("EDI","ECX",2)                                    \n\
        movb -2("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EDI","ECX",2)                                    \n\
        movb -3("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EAX","ECX")                                      \n\
        movb -1("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EDX","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        psrlw $8, %%xmm1                # XMM1: -- V3 -- ..... V0 -- U0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        movdqa %%xmm1, %%xmm2           # XMM2: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        pand %%xmm7, %%xmm1             # XMM1: -- U3 -- U2 -- U1 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- V3 -- V2 -- V1 -- V0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1:             U3 U2 U1 U0 \n\
        packuswb %%xmm2, %%xmm2         # XMM2:             V3 V2 V1 V0 \n\
        movq %%xmm0, -8("EDI","ECX",2)                                  \n\
        movd %%xmm1, -4("EAX","ECX")                                    \n\
        movd %%xmm2, -4("EDX","ECX")",                                  \
        /* emms */ "emms")

/* YUY2 -> YUV444P (unit: 2 pixels) */
#define YUY2_YUV444P \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 4,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movb -4("ESI","ECX",4), %%bl                                   \n\
        movb %%bl, -2("EDI","ECX",2)                                    \n\
        movb -2("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -1("EDI","ECX",2)                                    \n\
        movb -3("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -2("EAX","ECX",2)                                    \n\
        movb %%bl, -1("EAX","ECX",2)                                    \n\
        movb -1("ESI","ECX",4), %%bl                                    \n\
        movb %%bl, -2("EDX","ECX",2)                                    \n\
        movb %%bl, -1("EDX","ECX",2)",                                  \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        psrlw $8, %%xmm1                # XMM1: -- V3 -- ..... V0 -- U0 \n\
        packuswb %%xmm1, %%xmm1         # XMM1: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        movdqa %%xmm1, %%xmm2           # XMM2: V3 U3 V2 U2 V1 U1 V0 U0 \n\
        pand %%xmm7, %%xmm1             # XMM1: -- U3 -- U2 -- U1 -- U0 \n\
        psrlw $8, %%xmm2                # XMM2: -- V3 -- V2 -- V1 -- V0 \n\
        movdqa %%xmm1, %%xmm3           # XMM3: -- U3 -- U2 -- U1 -- U0 \n\
        psllw $8, %%xmm3                # XMM3: U3 -- U2 -- U1 -- U0 -- \n\
        por %%xmm3, %%xmm1              # XMM1: U3 U3 U2 U2 U1 U1 U0 U0 \n\
        movdqa %%xmm2, %%xmm3           # XMM3: -- V3 -- V2 -- V1 -- V0 \n\
        psllw $8, %%xmm3                # XMM3: V3 -- V2 -- V1 -- V0 -- \n\
        por %%xmm3, %%xmm2              # XMM1: V3 V3 V2 V2 V1 V1 V0 V0 \n\
        movq %%xmm0, -8("EDI","ECX",2)                                  \n\
        movq %%xmm1, -8("EAX","ECX",2)                                  \n\
        movq %%xmm2, -8("EDX","ECX",2)",                                \
        /* emms */ "emms")


/* Y8 -> YUY2/YVYU (unit: 1 pixel) */
#define Y8_YUY2 \
    /* Load 0x80*16 into XMM7 for interlacing U/V */                    \
    "pcmpeqd %%xmm7, %%xmm7; psllw $7, %%xmm7; packsswb %%xmm7, %%xmm7;"\
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 16,                                             \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movb -1("ESI","ECX"), %%al                                     \n\
        movb %%al, -2("EDI","ECX",2)                                    \n\
        movb $0x80, -1("EDI","ECX",2)",                                 \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX"),%%xmm0 # XMM0: YF YE YD ..... Y2 Y1 Y0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: YF YE YD ..... Y2 Y1 Y0 \n\
        punpcklbw %%xmm7, %%xmm0        # XMM0: 80 Y7 80 ..... Y1 80 Y0 \n\
        movdqu %%xmm0, -32("EDI","ECX",2)                               \n\
        punpckhbw %%xmm7, %%xmm1        # XMM1: 80 YF 80 ..... Y9 80 Y8 \n\
        movdqu %%xmm1, -16("EDI","ECX",2)",                             \
        /* emms */ "emms")

/* Y8 -> UYVY (unit: 1 pixel) */
#define Y8_UYVY \
    /* Load 0x80*16 into XMM7 for interlacing U/V */                    \
    "pcmpeqd %%xmm7, %%xmm7; psllw $7, %%xmm7; packsswb %%xmm7, %%xmm7;"\
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 16,                                             \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movb -1("ESI","ECX"), %%al                                     \n\
        movb %%al, -1("EDI","ECX",2)                                    \n\
        movb $0x80, -2("EDI","ECX",2)",                                 \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX"),%%xmm0 # XMM0: YF YE YD ..... Y2 Y1 Y0 \n\
        movdqa %%xmm7, %%xmm1           # XMM1: 80 80 80 ..... 80 80 80 \n\
        punpcklbw %%xmm0, %%xmm1        # XMM1: Y7 80 Y6 ..... 80 Y0 80 \n\
        movdqu %%xmm1, -32("EDI","ECX",2)                               \n\
        movdqa %%xmm7, %%xmm2           # XMM2: 80 80 80 ..... 80 80 80 \n\
        punpckhbw %%xmm0, %%xmm2        # XMM0: YF 80 YE ..... 80 Y8 80 \n\
        movdqu %%xmm2, -16("EDI","ECX",2)",                             \
        /* emms */ "emms")

/* YUY2/YVYU -> Y8 (unit: 1 pixel) */
#define YUY2_Y8 \
    /* Load 0x00FF*8 into XMM7 for masking */                           \
    "pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;"                         \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 8,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movb -2("ESI","ECX",2), %%al                                   \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2),%%xmm0 #XM0: V3 Y7 U3 ..... Y1 U0 Y0 \n\
        pand %%xmm7, %%xmm0             # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        movq %%xmm0, -8("EDI","ECX")",                                  \
        /* emms */ "emms")

/* UYVY -> Y8 (unit: 1 pixel) */
#define UYVY_Y8 \
    SIMD_LOOP_WRAPPER(                                                  \
        /* blocksize */ 8,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movb -1("ESI","ECX",2), %%al                                   \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2),%%xmm0 #XM0: Y7 V3 Y6 ..... V0 Y0 U0 \n\
        psrlw $8, %%xmm0                # XMM0: -- Y7 -- ..... Y1 -- Y0 \n\
        packuswb %%xmm0, %%xmm0         # XMM0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 \n\
        movq %%xmm0, -8("EDI","ECX")",                                  \
        /* emms */ "emms")

/*************************************************************************/

static int yuv420p_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    for (y = 0; y < (height & ~1); y++) {
        asm(YUV42XP_YUY2
            : /* no outputs */
            : "S" (src[0]+y*width), "a" (src[1]+(y/2)*(width/2)),
              "d" (src[2]+(y/2)*(width/2)), "D" (dest[0]+y*width*2),
              "c" (width/2));
    }
    return 1;
}

static int yuv411p_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 3)) {
        asm(YUV411P_YUY2
            : /* no outputs */
            : "S" (src[0]), "a" (src[1]), "d" (src[2]), "D" (dest[0]),
              "c" ((width/4)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUV411P_YUY2
                : /* no outputs */
                : "S" (src[0]+y*width), "a" (src[1]+y*(width/4)),
                  "d" (src[2]+y*(width/4)), "D" (dest[0]+y*width*2),
                  "c" (width/4));
        }
    }
    return 1;
}

static int yuv422p_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 1)) {
        asm(YUV42XP_YUY2
            : /* no outputs */
            : "S" (src[0]), "a" (src[1]), "d" (src[2]), "D" (dest[0]),
              "c" ((width/2)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUV42XP_YUY2
                : /* no outputs */
                : "S" (src[0]+y*width), "a" (src[1]+y*(width/2)),
                  "d" (src[2]+y*(width/2)), "D" (dest[0]+y*width*2),
                  "c" (width/2));
        }
    }
    return 1;
}

static int yuv444p_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 1)) {
        asm(YUV444P_YUY2
            : /* no outputs */
            : "S" (src[0]), "a" (src[1]), "d" (src[2]), "D" (dest[0]),
              "c" ((width/2)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUV444P_YUY2
                : /* no outputs */
                : "S" (src[0]+y*width), "a" (src[1]+y*(width/2)),
                  "d" (src[2]+y*(width/2)), "D" (dest[0]+y*width*2),
                  "c" (width/2));
        }
    }
    return 1;
}

/*************************************************************************/

static int yuy2_yuv420p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;

    for (y = 0; y < (height & ~1); y += 2) {
        asm(YUY2_YUV420P_U
            : /* no outputs */
            : "S" (src[0]+y*width*2), "a" (src[0]+(y+1)*width*2),
              "D" (dest[0]+y*width), "d" (dest[1]+(y/2)*(width/2)),
              "c" (width/2));
        asm(YUY2_YUV420P_V
            : /* no outputs */
            : "S" (src[0]+(y+1)*width*2), "a" (src[0]+y*width*2),
              "D" (dest[0]+(y+1)*width), "d" (dest[2]+(y/2)*(width/2)),
              "c" (width/2));
    }
    return 1;
}

static int yuy2_yuv411p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 3)) {
        asm(YUY2_YUV411P
            : /* no outputs */
            : "S" (src[0]), "D" (dest[0]), "a" (dest[1]), "d" (dest[2]),
              "c" ((width/4)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUY2_YUV411P
                : /* no outputs */
                : "S" (src[0]+y*width*2), "D" (dest[0]+y*width),
                  "a" (dest[1]+y*(width/4)), "d" (dest[2]+y*(width/4)),
                  "c" (width/4));
        }
    }
    return 1;
}

static int yuy2_yuv422p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 1)) {
        asm(YUY2_YUV422P
            : /* no outputs */
            : "S" (src[0]), "D" (dest[0]), "a" (dest[1]), "d" (dest[2]),
              "c" ((width/2)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUY2_YUV422P
                : /* no outputs */
                : "S" (src[0]+y*width*2), "D" (dest[0]+y*width),
                  "a" (dest[1]+y*(width/2)), "d" (dest[2]+y*(width/2)),
                  "c" (width/2));
        }
    }
    return 1;
}

static int yuy2_yuv444p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    if (!(width & 1)) {
        asm(YUY2_YUV444P
            : /* no outputs */
            : "S" (src[0]), "D" (dest[0]), "a" (dest[1]), "d" (dest[2]),
              "c" ((width/2)*height));
    } else {
        int y;
        for (y = 0; y < height; y++) {
            asm(YUY2_YUV444P
                : /* no outputs */
                : "S" (src[0]+y*width*2), "D" (dest[0]+y*width),
                  "a" (dest[1]+y*width), "d" (dest[2]+y*width),
                  "c" (width/2));
        }
    }
    return 1;
}

/*************************************************************************/

static int y8_yuy2_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm(Y8_YUY2
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

static int y8_uyvy_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm(Y8_UYVY
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

static int yuy2_y8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm(YUY2_Y8
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

static int uyvy_y8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm(UYVY_Y8
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

/*************************************************************************/

#endif  /* HAVE_ASM_SSE2 */

/*************************************************************************/
/*************************************************************************/

/* Initialization */

int ac_imgconvert_init_yuv_mixed(int accel)
{
    if (!register_conversion(IMG_YUV420P, IMG_YUY2,    yuv420p_yuy2)
     || !register_conversion(IMG_YUV411P, IMG_YUY2,    yuv411p_yuy2)
     || !register_conversion(IMG_YUV422P, IMG_YUY2,    yuv422p_yuy2)
     || !register_conversion(IMG_YUV444P, IMG_YUY2,    yuv444p_yuy2)
     || !register_conversion(IMG_Y8,      IMG_YUY2,    y8_yuy2)
     || !register_conversion(IMG_YUV420P, IMG_UYVY,    yuv420p_uyvy)
     || !register_conversion(IMG_YUV411P, IMG_UYVY,    yuv411p_uyvy)
     || !register_conversion(IMG_YUV422P, IMG_UYVY,    yuv422p_uyvy)
     || !register_conversion(IMG_YUV444P, IMG_UYVY,    yuv444p_uyvy)
     || !register_conversion(IMG_Y8,      IMG_UYVY,    y8_uyvy)
     || !register_conversion(IMG_YUV420P, IMG_YVYU,    yuv420p_yvyu)
     || !register_conversion(IMG_YUV411P, IMG_YVYU,    yuv411p_yvyu)
     || !register_conversion(IMG_YUV422P, IMG_YVYU,    yuv422p_yvyu)
     || !register_conversion(IMG_YUV444P, IMG_YVYU,    yuv444p_yvyu)
     || !register_conversion(IMG_Y8,      IMG_YVYU,    y8_yuy2)

     || !register_conversion(IMG_YUY2,    IMG_YUV420P, yuy2_yuv420p)
     || !register_conversion(IMG_YUY2,    IMG_YUV411P, yuy2_yuv411p)
     || !register_conversion(IMG_YUY2,    IMG_YUV422P, yuy2_yuv422p)
     || !register_conversion(IMG_YUY2,    IMG_YUV444P, yuy2_yuv444p)
     || !register_conversion(IMG_YUY2,    IMG_Y8,      yuy2_y8)
     || !register_conversion(IMG_UYVY,    IMG_YUV420P, uyvy_yuv420p)
     || !register_conversion(IMG_UYVY,    IMG_YUV411P, uyvy_yuv411p)
     || !register_conversion(IMG_UYVY,    IMG_YUV422P, uyvy_yuv422p)
     || !register_conversion(IMG_UYVY,    IMG_YUV444P, uyvy_yuv444p)
     || !register_conversion(IMG_UYVY,    IMG_Y8,      uyvy_y8)
     || !register_conversion(IMG_YVYU,    IMG_YUV420P, yvyu_yuv420p)
     || !register_conversion(IMG_YVYU,    IMG_YUV411P, yvyu_yuv411p)
     || !register_conversion(IMG_YVYU,    IMG_YUV422P, yvyu_yuv422p)
     || !register_conversion(IMG_YVYU,    IMG_YUV444P, yvyu_yuv444p)
     || !register_conversion(IMG_YVYU,    IMG_Y8,      yuy2_y8)
    ) {
        return 0;
    }

#if defined(HAVE_ASM_SSE2)
    if (accel & AC_SSE2) {
        if (!register_conversion(IMG_YUV420P, IMG_YUY2,    yuv420p_yuy2_sse2)
         || !register_conversion(IMG_YUV411P, IMG_YUY2,    yuv411p_yuy2_sse2)
         || !register_conversion(IMG_YUV422P, IMG_YUY2,    yuv422p_yuy2_sse2)
         || !register_conversion(IMG_YUV444P, IMG_YUY2,    yuv444p_yuy2_sse2)
         || !register_conversion(IMG_Y8,      IMG_YUY2,    y8_yuy2_sse2)
         || !register_conversion(IMG_Y8,      IMG_UYVY,    y8_uyvy_sse2)
         || !register_conversion(IMG_Y8,      IMG_YVYU,    y8_yuy2_sse2)

         || !register_conversion(IMG_YUY2,    IMG_YUV420P, yuy2_yuv420p_sse2)
         || !register_conversion(IMG_YUY2,    IMG_YUV411P, yuy2_yuv411p_sse2)
         || !register_conversion(IMG_YUY2,    IMG_YUV422P, yuy2_yuv422p_sse2)
         || !register_conversion(IMG_YUY2,    IMG_YUV444P, yuy2_yuv444p_sse2)
         || !register_conversion(IMG_YUY2,    IMG_Y8,      yuy2_y8_sse2)
         || !register_conversion(IMG_UYVY,    IMG_Y8,      uyvy_y8_sse2)
         || !register_conversion(IMG_YVYU,    IMG_Y8,      yuy2_y8_sse2)
        ) {
            return 0;
        }
    }
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
