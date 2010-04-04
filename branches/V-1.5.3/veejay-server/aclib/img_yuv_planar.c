/*
 * img_yuv_planar.c - YUV planar image format conversion routines
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

#include <string.h>

/*************************************************************************/
/*************************************************************************/

/* Standard C implementations */

/*************************************************************************/

/* Identity transformations */

static int yuv420p_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    ac_memcpy(dest[1], src[1], (width/2)*(height/2));
    ac_memcpy(dest[2], src[2], (width/2)*(height/2));
    return 1;
}

static int yuv411p_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    ac_memcpy(dest[1], src[1], (width/4)*height);
    ac_memcpy(dest[2], src[2], (width/4)*height);
    return 1;
}

static int yuv422p_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    ac_memcpy(dest[1], src[1], (width/2)*height);
    ac_memcpy(dest[2], src[2], (width/2)*height);
    return 1;
}

static int yuv444p_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    ac_memcpy(dest[1], src[1], width*height);
    ac_memcpy(dest[2], src[2], width*height);
    return 1;
}

static int y8_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    return 1;
}

/*************************************************************************/

static int yuv420p_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        for (x = 0; x < (width/2 & ~1); x += 2) {
            dest[1][y*(width/4)+x/2] = (src[1][(y/2)*(width/2)+x]
                                      + src[1][(y/2)*(width/2)+x+1] + 1) / 2;
            dest[2][y*(width/4)+x/2] = (src[2][(y/2)*(width/2)+x]
                                      + src[2][(y/2)*(width/2)+x+1] + 1) / 2;
        }
        ac_memcpy(dest[1]+(y+1)*(width/4), dest[1]+y*(width/4), width/4);
        ac_memcpy(dest[2]+(y+1)*(width/4), dest[2]+y*(width/4), width/4);
    }
    return 1;
}

static int yuv420p_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        ac_memcpy(dest[1]+(y  )*(width/2), src[1]+(y/2)*(width/2), width/2);
        ac_memcpy(dest[1]+(y+1)*(width/2), src[1]+(y/2)*(width/2), width/2);
        ac_memcpy(dest[2]+(y  )*(width/2), src[2]+(y/2)*(width/2), width/2);
        ac_memcpy(dest[2]+(y+1)*(width/2), src[2]+(y/2)*(width/2), width/2);
    }
    return 1;
}

static int yuv420p_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y += 2) {
        for (x = 0; x < width; x += 2) {
            dest[1][y*width+x  ] =
            dest[1][y*width+x+1] = src[1][(y/2)*(width/2)+(x/2)];
            dest[2][y*width+x  ] =
            dest[2][y*width+x+1] = src[2][(y/2)*(width/2)+(x/2)];
        }
        ac_memcpy(dest[1]+(y+1)*width, dest[1]+y*width, width);
        ac_memcpy(dest[2]+(y+1)*width, dest[2]+y*width, width);
    }
    return 1;
}

/*************************************************************************/

static int yuv411p_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        for (x = 0; x < ((width/2) & ~1); x += 2) {
            dest[1][(y/2)*(width/2)+x] = (src[1][y*(width/4)+x/2]
                                        + src[1][(y+1)*(width/4)+x/2] + 1) / 2;
            dest[2][(y/2)*(width/2)+x] = (src[2][y*(width/4)+x/2]
                                        + src[2][(y+1)*(width/4)+x/2] + 1) / 2;
            dest[1][(y/2)*(width/2)+x+1] = dest[1][(y/2)*(width/2)+x];
            dest[2][(y/2)*(width/2)+x+1] = dest[2][(y/2)*(width/2)+x];
        }
    }
    return 1;
}

static int yuv411p_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < ((width/2) & ~1); x += 2) {
            dest[1][y*(width/2)+x  ] = src[1][y*(width/4)+x/2];
            dest[1][y*(width/2)+x+1] = src[1][y*(width/4)+x/2];
            dest[2][y*(width/2)+x  ] = src[2][y*(width/4)+x/2];
            dest[2][y*(width/2)+x+1] = src[2][y*(width/4)+x/2];
        }
    }
    return 1;
}

static int yuv411p_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~3); x += 4) {
            dest[1][y*width+x  ] = src[1][y*(width/4)+x/4];
            dest[1][y*width+x+1] = src[1][y*(width/4)+x/4];
            dest[1][y*width+x+2] = src[1][y*(width/4)+x/4];
            dest[1][y*width+x+3] = src[1][y*(width/4)+x/4];
            dest[2][y*width+x  ] = src[2][y*(width/4)+x/4];
            dest[2][y*width+x+1] = src[2][y*(width/4)+x/4];
            dest[2][y*width+x+2] = src[2][y*(width/4)+x/4];
            dest[2][y*width+x+3] = src[2][y*(width/4)+x/4];
        }
    }
    return 1;
}

/*************************************************************************/

static int yuv422p_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        for (x = 0; x < width/2; x++) {
            dest[1][(y/2)*(width/2)+x] = (src[1][y*(width/2)+x]
                                        + src[1][(y+1)*(width/2)+x] + 1) / 2;
            dest[2][(y/2)*(width/2)+x] = (src[2][y*(width/2)+x]
                                        + src[2][(y+1)*(width/2)+x] + 1) / 2;
        }
    }
    return 1;
}

static int yuv422p_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < ((width/2) & ~1); x += 2) {
            dest[1][y*(width/4)+x/2] = (src[1][y*(width/2)+x]
                                      + src[1][y*(width/2)+x+1] + 1) / 2;
            dest[2][y*(width/4)+x/2] = (src[2][y*(width/2)+x]
                                      + src[2][y*(width/2)+x+1] + 1) / 2;
        }
    }
    return 1;
}

static int yuv422p_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[1][y*width+x  ] = src[1][y*(width/2)+x/2];
            dest[1][y*width+x+1] = src[1][y*(width/2)+x/2];
            dest[2][y*width+x  ] = src[2][y*(width/2)+x/2];
            dest[2][y*width+x+1] = src[2][y*(width/2)+x/2];
        }
    }
    return 1;
}

/*************************************************************************/

static int yuv444p_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[1][(y/2)*(width/2)+x/2] = (src[1][y*width+x]
                                          + src[1][y*width+x+1]
                                          + src[1][(y+1)*width+x]
                                          + src[1][(y+1)*width+x+1] + 2) / 4;
            dest[2][(y/2)*(width/2)+x/2] = (src[2][y*width+x]
                                          + src[2][y*width+x+1]
                                          + src[2][(y+1)*width+x]
                                          + src[2][(y+1)*width+x+1] + 2) / 4;
        }
    }
    return 1;
}

static int yuv444p_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~3); x += 4) {
            dest[1][y*(width/4)+x/4] = (src[1][y*width+x]
                                      + src[1][y*width+x+1]
                                      + src[1][y*width+x+2]
                                      + src[1][y*width+x+3] + 2) / 4;
            dest[2][y*(width/4)+x/4] = (src[2][y*width+x]
                                      + src[2][y*width+x+1]
                                      + src[2][y*width+x+2]
                                      + src[2][y*width+x+3] + 2) / 4;
        }
    }
    return 1;
}

static int yuv444p_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    int x, y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < (width & ~1); x += 2) {
            dest[1][y*(width/2)+x/2] = (src[1][y*width+x]
                                      + src[1][y*width+x+1] + 1) / 2;
            dest[2][y*(width/2)+x/2] = (src[2][y*width+x]
                                      + src[2][y*width+x+1] + 1) / 2;
        }
    }
    return 1;
}

/*************************************************************************/

/* We treat Y8 as a planar format */

static int yuvp_y8(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    return 1;
}

static int y8_yuv420p(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    memset(dest[1], 128, (width/2)*(height/2));
    memset(dest[2], 128, (width/2)*(height/2));
    return 1;
}

static int y8_yuv411p(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    memset(dest[1], 128, (width/4)*height);
    memset(dest[2], 128, (width/4)*height);
    return 1;
}

static int y8_yuv422p(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    memset(dest[1], 128, (width/2)*height);
    memset(dest[2], 128, (width/2)*height);
    return 1;
}

static int y8_yuv444p(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    memset(dest[1], 128, width*height);
    memset(dest[2], 128, width*height);
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#if defined(HAVE_ASM_SSE2)

/* SSE2 routines.  See comments in img_x86_common.h for why we don't bother
 * unrolling the loops. */

/* Common macros/data for x86 code */
#include "img_x86_common.h"

/* Average 2 bytes horizontally (e.g. 422P->411P) (unit: 2 source bytes) */
#define AVG_2H(src,dest,count) \
    asm("pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;" /* XMM7: 0x00FF*8 */ \
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 8,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movzbl -2("ESI","ECX",2), %%eax                                \n\
        movzbl -1("ESI","ECX",2), %%edx                                 \n\
        addl %%edx, %%eax                                               \n\
        shrl $1, %%eax                                                  \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2),%%xmm0 #XMM0:FEDCBA9876543210        \n\
        movdqa %%xmm0, %%xmm1           # XMM1: FEDCBA9876543210        \n\
        pand %%xmm7, %%xmm0             # XMM0:  E C A 8 6 4 2 0        \n\
        psrlw $8, %%xmm1                # XMM1:  F D B 9 7 5 3 1        \n\
        pavgw %%xmm1, %%xmm0            # XMM0:  w v u t s r q p (avgs) \n\
        packuswb %%xmm0, %%xmm0         # XMM0: wvutsrqpwvutsrqp        \n\
        movq %%xmm0, -8("EDI","ECX")",                                  \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src), "D" (dest), "c" (count)                            \
        : "eax", "edx")

/* Average 4 bytes horizontally (e.g. 444P->411P) (unit: 4 source bytes) */
#define AVG_4H(src,dest,count) \
    asm("pcmpeqd %%xmm7, %%xmm7; psrld $24, %%xmm7;" /* XMM7: 0x000000FF*4 */ \
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 4,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movzbl -4("ESI","ECX",4), %%eax                                \n\
        movzbl -3("ESI","ECX",4), %%edx                                 \n\
        addl %%edx, %%eax                                               \n\
        movzbl -2("ESI","ECX",4), %%edx                                 \n\
        addl %%edx, %%eax                                               \n\
        movzbl -1("ESI","ECX",4), %%edx                                 \n\
        addl %%edx, %%eax                                               \n\
        shrl $2, %%eax                                                  \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",4),%%xmm0 #XMM0:FEDCBA9876543210        \n\
        movdqa %%xmm0, %%xmm1           # XMM1: FEDCBA9876543210        \n\
        movdqa %%xmm0, %%xmm2           # XMM2: FEDCBA9876543210        \n\
        movdqa %%xmm0, %%xmm3           # XMM3: FEDCBA9876543210        \n\
        pand %%xmm7, %%xmm0             # XMM0:    C   8   4   0        \n\
        psrld $8, %%xmm1                # XMM1:  FED BA9 765 321        \n\
        pand %%xmm7, %%xmm1             # XMM1:    D   9   5   1        \n\
        psrld $16, %%xmm2               # XMM2:   FE  BA  76  32        \n\
        pand %%xmm7, %%xmm2             # XMM2:    E   A   6   2        \n\
        psrld $24, %%xmm3               # XMM3:    F   B   7   3        \n\
        pavgw %%xmm1, %%xmm0            # XMM0:  C+D 8+9 4+5 0+1 (avgs) \n\
        pavgw %%xmm3, %%xmm2            # XMM2:  E+F A+B 6+7 2+3 (avgs) \n\
        pavgw %%xmm2, %%xmm0            # XMM0:    s   r   q   p (avgs) \n\
        packuswb %%xmm0, %%xmm0         # XMM0:  s r q p s r q p        \n\
        packuswb %%xmm0, %%xmm0         # XMM0: srqpsrqpsrqpsrqp        \n\
        movd %%xmm0, -4("EDI","ECX")",                                  \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src), "D" (dest), "c" (count)                            \
        : "eax", "edx")

/* Repeat 2 bytes horizontally (e.g. 422P->444P) (unit: 1 source byte) */
#define REP_2H(src,dest,count) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 8,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movb -1("ESI","ECX"), %%al                                     \n\
        movb %%al, %%ah                                                 \n\
        movw %%ax, -2("EDI","ECX",2)",                                  \
        /* main_loop */                                                 \
        "movq -8("ESI","ECX"), %%xmm0   # XMM0:         76543210        \n\
        punpcklbw %%xmm0, %%xmm0        # XMM0: 7766554433221100        \n\
        movdqu %%xmm0, -16("EDI","ECX",2)",                             \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src), "D" (dest), "c" (count)                            \
        : "eax")

/* Repeat 4 bytes horizontally (e.g. 411P->444P) (unit: 1 source byte) */
#define REP_4H(src,dest,count) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 4,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "movzbl -1("ESI","ECX"), %%eax                                  \n\
        movb %%al, %%ah                                                 \n\
        movl %%eax, %%edx                                               \n\
        shll $16, %%eax                                                 \n\
        orl %%edx, %%eax                                                \n\
        movl %%eax, -4("EDI","ECX",4)",                                 \
        /* main_loop */                                                 \
        "movd -4("ESI","ECX"), %%xmm0   # XMM0:             3210        \n\
        punpcklbw %%xmm0, %%xmm0        # XMM0:         33221100        \n\
        punpcklwd %%xmm0, %%xmm0        # XMM0: 3333222211110000        \n\
        movdqu %%xmm0, -16("EDI","ECX",4)",                             \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src), "D" (dest), "c" (count)                            \
        : "eax", "edx")

/* Average 2 bytes vertically and double horizontally (411P->420P)
 * (unit: 1 source byte) */
#define AVG_411_420(src1,src2,dest,count) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 8,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movzbl -1("ESI","ECX"), %%eax                                  \n\
        movzbl -1("EDX","ECX"), %%ebx                                   \n\
        addl %%ebx, %%eax                                               \n\
        shrl $1, %%eax                                                  \n\
        movb %%al, %%ah                                                 \n\
        movw %%ax, -2("EDI","ECX",2)",                                  \
        /* main_loop */                                                 \
        "movq -8("ESI","ECX"), %%xmm0                                   \n\
        movq -8("EDX","ECX"), %%xmm1                                    \n\
        pavgb %%xmm1, %%xmm0                                            \n\
        punpcklbw %%xmm0, %%xmm0                                        \n\
        movdqu %%xmm0, -16("EDI","ECX",2)",                             \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src1), "d" (src2), "D" (dest), "c" (count)               \
        : "eax")

/* Average 2 bytes vertically (422P->420P) (unit: 1 source byte) */
#define AVG_422_420(src1,src2,dest,count) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 16,                                             \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movzbl -1("ESI","ECX"), %%eax                                  \n\
        movzbl -1("EDX","ECX"), %%ebx                                   \n\
        addl %%ebx, %%eax                                               \n\
        shrl $1, %%eax                                                  \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX"), %%xmm0                                \n\
        movdqu -16("EDX","ECX"), %%xmm1                                 \n\
        pavgb %%xmm1, %%xmm0                                            \n\
        movdqu %%xmm0, -16("EDI","ECX")",                               \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src1), "d" (src2), "D" (dest), "c" (count)               \
        : "eax")

/* Average 4 bytes, 2 horizontally and 2 vertically (444P->420P)
 * (unit: 2 source bytes) */
#define AVG_444_420(src1,src2,dest,count) \
    asm("pcmpeqd %%xmm7, %%xmm7; psrlw $8, %%xmm7;" /* XMM7: 0x00FF*8 */ \
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 8,                                              \
        /* push_regs */ "push "EBX,                                     \
        /* pop_regs  */ "pop "EBX,                                      \
        /* small_loop */                                                \
        "movzbl -2("ESI","ECX",2), %%eax                                \n\
        movzbl -1("ESI","ECX",2), %%ebx                                 \n\
        addl %%ebx, %%eax                                               \n\
        movzbl -2("EDX","ECX",2), %%ebx                                 \n\
        addl %%ebx, %%eax                                               \n\
        movzbl -1("EDX","ECX",2), %%ebx                                 \n\
        addl %%ebx, %%eax                                               \n\
        shrl $2, %%eax                                                  \n\
        movb %%al, -1("EDI","ECX")",                                    \
        /* main_loop */                                                 \
        "movdqu -16("ESI","ECX",2), %%xmm0                              \n\
        movdqu -16("EDX","ECX",2), %%xmm2                               \n\
        movdqa %%xmm0, %%xmm1                                           \n\
        pand %%xmm7, %%xmm0                                             \n\
        psrlw $8, %%xmm1                                                \n\
        pavgw %%xmm1, %%xmm0                                            \n\
        movdqa %%xmm2, %%xmm3                                           \n\
        pand %%xmm7, %%xmm2                                             \n\
        psrlw $8, %%xmm3                                                \n\
        pavgw %%xmm3, %%xmm2                                            \n\
        pavgw %%xmm2, %%xmm0                                            \n\
        packuswb %%xmm0, %%xmm0                                         \n\
        movq %%xmm0, -8("EDI","ECX")",                                  \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src1), "d" (src2), "D" (dest), "c" (count))

/*************************************************************************/

static int yuv420p_yuv411p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        AVG_2H(src[1]+(y/2)*(width/2), dest[1]+y*(width/4), width/4);
        ac_memcpy(dest[1]+(y+1)*(width/4), dest[1]+y*(width/4), width/4);
        AVG_2H(src[2]+(y/2)*(width/2), dest[2]+y*(width/4), width/4);
        ac_memcpy(dest[2]+(y+1)*(width/4), dest[2]+y*(width/4), width/4);
    }
    return 1;
}

static int yuv420p_yuv444p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < height; y += 2) {
        REP_2H(src[1]+(y/2)*(width/2), dest[1]+y*width, width/2);
        ac_memcpy(dest[1]+(y+1)*width, dest[1]+y*width, width);
        REP_2H(src[2]+(y/2)*(width/2), dest[2]+y*width, width/2);
        ac_memcpy(dest[2]+(y+1)*width, dest[2]+y*width, width);
    }
    return 1;
}

/*************************************************************************/

static int yuv411p_yuv420p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        AVG_411_420(src[1]+y*(width/4), src[1]+(y+1)*(width/4),
                    dest[1]+(y/2)*(width/2), width/4);
        AVG_411_420(src[2]+y*(width/4), src[2]+(y+1)*(width/4),
                    dest[2]+(y/2)*(width/2), width/4);
    }
    return 1;
}

static int yuv411p_yuv422p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 3)) {
        /* Fast version, no bytes at end of row to skip */
        REP_2H(src[1], dest[1], (width/4)*height);
        REP_2H(src[2], dest[2], (width/4)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            REP_2H(src[1]+y*(width/4), dest[1]+y*(width/2), width/4);
            REP_2H(src[2]+y*(width/4), dest[2]+y*(width/2), width/4);
        }
    }
    return 1;
}

static int yuv411p_yuv444p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 3)) {
        /* Fast version, no bytes at end of row to skip */
        REP_4H(src[1], dest[1], (width/4)*height);
        REP_4H(src[2], dest[2], (width/4)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            REP_4H(src[1]+y*(width/4), dest[1]+y*width, width/4);
            REP_4H(src[2]+y*(width/4), dest[2]+y*width, width/4);
        }
    }
    return 1;
}

/*************************************************************************/

static int yuv422p_yuv420p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        AVG_422_420(src[1]+y*(width/2), src[1]+(y+1)*(width/2),
                    dest[1]+(y/2)*(width/2), width/2);
        AVG_422_420(src[2]+y*(width/2), src[2]+(y+1)*(width/2),
                    dest[2]+(y/2)*(width/2), width/2);
    }
    return 1;
}

static int yuv422p_yuv411p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 3)) {
        /* Fast version, no bytes at end of row to skip */
        AVG_2H(src[1], dest[1], (width/4)*height);
        AVG_2H(src[2], dest[2], (width/4)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            AVG_2H(src[1]+y*(width/2), dest[1]+y*(width/4), width/4);
            AVG_2H(src[2]+y*(width/2), dest[2]+y*(width/4), width/4);
        }
    }
    return 1;
}

static int yuv422p_yuv444p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 1)) {
        /* Fast version, no bytes at end of row to skip */
        REP_2H(src[1], dest[1], (width/2)*height);
        REP_2H(src[2], dest[2], (width/2)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            REP_2H(src[1]+y*(width/2), dest[1]+y*width, width/2);
            REP_2H(src[2]+y*(width/2), dest[2]+y*width, width/2);
        }
    }
    return 1;
}

/*************************************************************************/

static int yuv444p_yuv420p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    int y;
    ac_memcpy(dest[0], src[0], width*height);
    for (y = 0; y < (height & ~1); y += 2) {
        AVG_444_420(src[1]+y*width, src[1]+(y+1)*width,
                    dest[1]+(y/2)*(width/2), width/2);
        AVG_444_420(src[2]+y*width, src[2]+(y+1)*width,
                    dest[2]+(y/2)*(width/2), width/2);
    }
    return 1;
}

static int yuv444p_yuv411p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 3)) {
        /* Fast version, no bytes at end of row to skip */
        AVG_4H(src[1], dest[1], (width/4)*height);
        AVG_4H(src[2], dest[2], (width/4)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            AVG_4H(src[1]+y*width, dest[1]+y*(width/4), width/4);
            AVG_4H(src[2]+y*width, dest[2]+y*(width/4), width/4);
        }
    }
    return 1;
}

static int yuv444p_yuv422p_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    if (!(width & 1)) {
        /* Fast version, no bytes at end of row to skip */
        AVG_2H(src[1], dest[1], (width/2)*height);
        AVG_2H(src[2], dest[2], (width/2)*height);
    } else {
        /* Slow version, loop through each row */
        int y;
        for (y = 0; y < height; y++) {
            AVG_2H(src[1]+y*width, dest[1]+y*(width/2), width/2);
            AVG_2H(src[2]+y*width, dest[2]+y*(width/2), width/2);
        }
    }
    return 1;
}

/*************************************************************************/

#endif  /* HAVE_ASM_SSE2 */

/*************************************************************************/
/*************************************************************************/

/* Initialization */

int ac_imgconvert_init_yuv_planar(int accel)
{
    if (!register_conversion(IMG_YUV420P, IMG_YUV420P, yuv420p_copy)
     || !register_conversion(IMG_YUV420P, IMG_YUV411P, yuv420p_yuv411p)
     || !register_conversion(IMG_YUV420P, IMG_YUV422P, yuv420p_yuv422p)
     || !register_conversion(IMG_YUV420P, IMG_YUV444P, yuv420p_yuv444p)
     || !register_conversion(IMG_YUV420P, IMG_Y8,      yuvp_y8)

     || !register_conversion(IMG_YUV411P, IMG_YUV420P, yuv411p_yuv420p)
     || !register_conversion(IMG_YUV411P, IMG_YUV411P, yuv411p_copy)
     || !register_conversion(IMG_YUV411P, IMG_YUV422P, yuv411p_yuv422p)
     || !register_conversion(IMG_YUV411P, IMG_YUV444P, yuv411p_yuv444p)
     || !register_conversion(IMG_YUV411P, IMG_Y8,      yuvp_y8)

     || !register_conversion(IMG_YUV422P, IMG_YUV420P, yuv422p_yuv420p)
     || !register_conversion(IMG_YUV422P, IMG_YUV411P, yuv422p_yuv411p)
     || !register_conversion(IMG_YUV422P, IMG_YUV422P, yuv422p_copy)
     || !register_conversion(IMG_YUV422P, IMG_YUV444P, yuv422p_yuv444p)
     || !register_conversion(IMG_YUV422P, IMG_Y8,      yuvp_y8)

     || !register_conversion(IMG_YUV444P, IMG_YUV420P, yuv444p_yuv420p)
     || !register_conversion(IMG_YUV444P, IMG_YUV411P, yuv444p_yuv411p)
     || !register_conversion(IMG_YUV444P, IMG_YUV422P, yuv444p_yuv422p)
     || !register_conversion(IMG_YUV444P, IMG_YUV444P, yuv444p_copy)
     || !register_conversion(IMG_YUV444P, IMG_Y8,      yuvp_y8)

     || !register_conversion(IMG_Y8,      IMG_YUV420P, y8_yuv420p)
     || !register_conversion(IMG_Y8,      IMG_YUV411P, y8_yuv411p)
     || !register_conversion(IMG_Y8,      IMG_YUV422P, y8_yuv422p)
     || !register_conversion(IMG_Y8,      IMG_YUV444P, y8_yuv444p)
     || !register_conversion(IMG_Y8,      IMG_Y8,      y8_copy)
    ) {
        return 0;
    }

#if defined(HAVE_ASM_SSE2)
    if (accel & AC_SSE2) {
        if (!register_conversion(IMG_YUV420P, IMG_YUV411P, yuv420p_yuv411p_sse2)
         || !register_conversion(IMG_YUV420P, IMG_YUV444P, yuv420p_yuv444p_sse2)

         || !register_conversion(IMG_YUV411P, IMG_YUV420P, yuv411p_yuv420p_sse2)
         || !register_conversion(IMG_YUV411P, IMG_YUV422P, yuv411p_yuv422p_sse2)
         || !register_conversion(IMG_YUV411P, IMG_YUV444P, yuv411p_yuv444p_sse2)

         || !register_conversion(IMG_YUV422P, IMG_YUV420P, yuv422p_yuv420p_sse2)
         || !register_conversion(IMG_YUV422P, IMG_YUV411P, yuv422p_yuv411p_sse2)
         || !register_conversion(IMG_YUV422P, IMG_YUV444P, yuv422p_yuv444p_sse2)

         || !register_conversion(IMG_YUV444P, IMG_YUV420P, yuv444p_yuv420p_sse2)
         || !register_conversion(IMG_YUV444P, IMG_YUV411P, yuv444p_yuv411p_sse2)
         || !register_conversion(IMG_YUV444P, IMG_YUV422P, yuv444p_yuv422p_sse2)
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
