/*
 * img_rgb_packed.c - RGB packed image format conversion routines
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

/* Identity transformations, all work when src==dest */

static int rgb_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height*3);
    return 1;
}

static int rgba_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height*4);
    return 1;
}

static int gray8_copy(uint8_t **src, uint8_t **dest, int width, int height)
{
    ac_memcpy(dest[0], src[0], width*height);
    return 1;
}

/*************************************************************************/

/* Conversions between various 32-bit formats, all usable when src==dest */

/* RGBA<->ABGR and ARGB<->BGRA: reverse byte order */
static int rgba_swapall(uint8_t **src, uint8_t **dest, int width, int height)
{
    uint32_t *srcp  = (uint32_t *)src[0];
    uint32_t *destp = (uint32_t *)dest[0];
    int i;
    for (i = 0; i < width*height; i++) {
        /* This shortcut works regardless of CPU endianness */
        destp[i] =  srcp[i]               >> 24
                 | (srcp[i] & 0x00FF0000) >>  8
                 | (srcp[i] & 0x0000FF00) <<  8
                 |  srcp[i]               << 24;
    }
    return 1;
}

/* RGBA<->BGRA: swap bytes 0 and 2 */
static int rgba_swap02(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        uint8_t tmp    = src[0][i*4+2];
        dest[0][i*4+2] = src[0][i*4  ];
        dest[0][i*4  ] = tmp;
        dest[0][i*4+1] = src[0][i*4+1];
        dest[0][i*4+3] = src[0][i*4+3];
    }
    return 1;
}

/* ARGB<->ABGR: swap bytes 1 and 3 */
static int rgba_swap13(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        uint8_t tmp    = src[0][i*4+3];
        dest[0][i*4+3] = src[0][i*4+1];
        dest[0][i*4+1] = tmp;
        dest[0][i*4  ] = src[0][i*4  ];
        dest[0][i*4+2] = src[0][i*4+2];
    }
    return 1;
}

/* RGBA->ARGB and BGRA->ABGR: alpha moves from byte 3 to byte 0 */
static int rgba_alpha30(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        uint8_t tmp    = src[0][i*4+3];
        dest[0][i*4+3] = src[0][i*4+2];
        dest[0][i*4+2] = src[0][i*4+1];
        dest[0][i*4+1] = src[0][i*4  ];
        dest[0][i*4  ] = tmp;
    }
    return 1;
}

/* ARGB->RGBA and ABGR->BGRA: alpha moves from byte 0 to byte 3 */
static int rgba_alpha03(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        uint8_t tmp    = src[0][i*4  ];
        dest[0][i*4  ] = src[0][i*4+1];
        dest[0][i*4+1] = src[0][i*4+2];
        dest[0][i*4+2] = src[0][i*4+3];
        dest[0][i*4+3] = tmp;
    }
    return 1;
}

/*************************************************************************/

static int rgb24_bgr24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i*3+2];
        dest[0][i*3+1] = src[0][i*3+1];
        dest[0][i*3+2] = src[0][i*3  ];
    }
    return 1;
}

static int rgb24_rgba32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = src[0][i*3  ];
        dest[0][i*4+1] = src[0][i*3+1];
        dest[0][i*4+2] = src[0][i*3+2];
        dest[0][i*4+3] = 0;
    }
    return 1;
}

static int rgb24_abgr32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = 0;
        dest[0][i*4+1] = src[0][i*3+2];
        dest[0][i*4+2] = src[0][i*3+1];
        dest[0][i*4+3] = src[0][i*3  ];
    }
    return 1;
}

static int rgb24_argb32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = 0;
        dest[0][i*4+1] = src[0][i*3  ];
        dest[0][i*4+2] = src[0][i*3+1];
        dest[0][i*4+3] = src[0][i*3+2];
    }
    return 1;
}

static int rgb24_bgra32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = src[0][i*3+2];
        dest[0][i*4+1] = src[0][i*3+1];
        dest[0][i*4+2] = src[0][i*3  ];
        dest[0][i*4+3] = 0;
    }
    return 1;
}

static int rgb24_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*3  ];
        int g = src[0][i*3+1];
        int b = src[0][i*3+2];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

static int bgr24_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*3+2];
        int g = src[0][i*3+1];
        int b = src[0][i*3  ];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

/*************************************************************************/

static int rgba32_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i*4  ];
        dest[0][i*3+1] = src[0][i*4+1];
        dest[0][i*3+2] = src[0][i*4+2];
    }
    return 1;
}

static int bgra32_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i*4+2];
        dest[0][i*3+1] = src[0][i*4+1];
        dest[0][i*3+2] = src[0][i*4  ];
    }
    return 1;
}

static int rgba32_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*4  ];
        int g = src[0][i*4+1];
        int b = src[0][i*4+2];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

static int bgra32_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*4+2];
        int g = src[0][i*4+1];
        int b = src[0][i*4  ];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

/*************************************************************************/

static int argb32_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i*4+1];
        dest[0][i*3+1] = src[0][i*4+2];
        dest[0][i*3+2] = src[0][i*4+3];
    }
    return 1;
}

static int abgr32_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i*4+3];
        dest[0][i*3+1] = src[0][i*4+2];
        dest[0][i*3+2] = src[0][i*4+1];
    }
    return 1;
}

static int argb32_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*4+1];
        int g = src[0][i*4+2];
        int b = src[0][i*4+3];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

static int abgr32_gray8(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        /* Use the Y part of a YUV transformation, scaled to 0..255 */
        int r = src[0][i*4+3];
        int g = src[0][i*4+2];
        int b = src[0][i*4+1];
        dest[0][i] = (19595*r + 38470*g + 7471*b + 32768) >> 16;
    }
    return 1;
}

/*************************************************************************/

static int gray8_rgb24(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*3  ] = src[0][i];
        dest[0][i*3+1] = src[0][i];
        dest[0][i*3+2] = src[0][i];
    }
    return 1;
}

static int gray8_rgba32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = src[0][i];
        dest[0][i*4+1] = src[0][i];
        dest[0][i*4+2] = src[0][i];
        dest[0][i*4+3] = 0;
    }
    return 1;
}

static int gray8_argb32(uint8_t **src, uint8_t **dest, int width, int height)
{
    int i;
    for (i = 0; i < width*height; i++) {
        dest[0][i*4  ] = 0;
        dest[0][i*4+1] = src[0][i];
        dest[0][i*4+2] = src[0][i];
        dest[0][i*4+3] = src[0][i];
    }
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#define DEFINE_MASK_DATA
#include "img_x86_common.h"

/*************************************************************************/

/* Basic assembly routines */

/* RGBA<->ABGR and ARGB<->BGRA: reverse byte order */
static int rgba_swapall_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_REV32_X86(width*height);
    return 1;
}

/* RGBA<->BGRA: swap bytes 0 and 2 */
static int rgba_swap02_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_02_X86(width*height);
    return 1;
}

/* ARGB<->ABGR: swap bytes 1 and 3 */
static int rgba_swap13_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_X86(width*height);
    return 1;
}

/* RGBA->ARGB and BGRA->ABGR: alpha moves from byte 3 to byte 0 */
static int rgba_alpha30_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_X86(width*height);
    return 1;
}

/* ARGB->RGBA and ABGR->BGRA: alpha moves from byte 0 to byte 3 */
static int rgba_alpha03_x86(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_X86(width*height);
    return 1;
}

/*************************************************************************/

/* MMX routines */

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)  /* i.e. not x86_64 */

/* RGBA<->ABGR and ARGB<->BGRA: reverse byte order */
static int rgba_swapall_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_REV32_MMX(width*height);
    return 1;
}

/* RGBA<->BGRA: swap bytes 0 and 2 */
static int rgba_swap02_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_02_MMX(width*height);
    return 1;
}

/* ARGB<->ABGR: swap bytes 1 and 3 */
static int rgba_swap13_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_MMX(width*height);
    return 1;
}

/* RGBA->ARGB and BGRA->ABGR: alpha moves from byte 3 to byte 0 */
static int rgba_alpha30_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_MMX(width*height);
    return 1;
}

/* ARGB->RGBA and ABGR->BGRA: alpha moves from byte 0 to byte 3 */
static int rgba_alpha03_mmx(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_MMX(width*height);
    return 1;
}

#endif  /* HAVE_ASM_MMX && ARCH_X86 */

/*************************************************************************/

/* SSE2 routines */

#if defined(HAVE_ASM_SSE2)

static const struct { uint32_t n[4]; } __attribute__((aligned(16))) rgb_bgr_data = {{
    0xFF0000FF, 0x00FF0000, 0x0000FF00, 0x00000000
}};

#define SHIFT_RBSWAP \
        "movdqa %%xmm6, %%xmm2          # XMM2: low bytes mask          \n\
        pand %%xmm0, %%xmm2             # XMM2: R/B bytes               \n\
        pshuflw $0xB1, %%xmm2, %%xmm2   # XMM2: swap R and B (low quad) \n\
        pand %%xmm7, %%xmm0             # XMM0: G bytes                 \n\
        pshufhw $0xB1, %%xmm2, %%xmm2   # XMM2: swap R and B (high quad)\n\
        por %%xmm2, %%xmm0              # XMM0: data now in BGRA32      \n"

#define SHIFT_AFIRST \
        "pslldq $1, %%xmm0              # XMM0: move A first            \n"

#define SHIFT_ALAST \
        "psrldq $1, %%xmm0              # XMM0: move A last             \n"

#define RGB24TO32(ROFS,GOFS,BOFS,AOFS,SHIFT) \
    asm("pcmpeqd %%xmm5, %%xmm5                                         \n\
        movdqa %%xmm5, %%xmm6                                           \n\
        psrldq $13, %%xmm5              # XMM5: 24-bit mask             \n\
        movdqa %%xmm6, %%xmm7                                           \n\
        psrlw $8, %%xmm6                # XMM6: low bytes mask          \n\
        psllw $8, %%xmm7                # XMM7: high bytes mask         \n"\
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 4,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        movb -3("ESI","EDX"), %%al                                      \n\
        movb %%al, ("#ROFS"-4)("EDI","ECX",4)                           \n\
        movb -2("ESI","EDX"), %%al                                      \n\
        movb %%al, ("#GOFS"-4)("EDI","ECX",4)                           \n\
        movb -1("ESI","EDX"), %%al                                      \n\
        movb %%al, ("#BOFS"-4)("EDI","ECX",4)                           \n\
        movb $0, ("#AOFS"-4)("EDI","ECX",4)",                           \
        /* main_loop */                                                 \
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        # We can't just movdqu, because we might run over the edge      \n\
        movd -12("ESI","EDX"), %%xmm1                                   \n\
        movq -8("ESI","EDX"), %%xmm0                                    \n\
        pshufd $0xD3, %%xmm0, %%xmm0    # shift left by 4 bytes         \n\
        por %%xmm1, %%xmm0              # XMM0: original RGB24 data     \n\
        pshufd $0xF3, %%xmm5, %%xmm2    # XMM2: pixel 1 mask            \n\
        movdqa %%xmm5, %%xmm1           # XMM1: pixel 0 mask            \n\
        pshufd $0xCF, %%xmm5, %%xmm3    # XMM3: pixel 2 mask            \n\
        pand %%xmm0, %%xmm1             # XMM1: pixel 0                 \n\
        pslldq $1, %%xmm0                                               \n\
        pand %%xmm0, %%xmm2             # XMM2: pixel 1                 \n\
        pshufd $0x3F, %%xmm5, %%xmm4    # XMM4: pixel 3 mask            \n\
        por %%xmm2, %%xmm1              # XMM1: pixels 0 and 1          \n\
        pslldq $1, %%xmm0                                               \n\
        pand %%xmm0, %%xmm3             # XMM3: pixel 2                 \n\
        por %%xmm3, %%xmm1              # XMM1: pixels 0, 1, and 2      \n\
        pslldq $1, %%xmm0                                               \n\
        pand %%xmm4, %%xmm0             # XMM0: pixel 3                 \n\
        por %%xmm1, %%xmm0              # XMM0: RGBA32 data             \n\
        "SHIFT"                         # shift bytes to target position\n\
        movdqu %%xmm0, -16("EDI","ECX",4)",                             \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),              \
          "d" (&rgb_bgr_data), "m" (rgb_bgr_data)                       \
        : "eax");

#define RGB32TO24(ROFS,GOFS,BOFS,AOFS,SHIFT) \
    asm("pcmpeqd %%xmm5, %%xmm5                                         \n\
        movdqa %%xmm5, %%xmm6                                           \n\
        psrldq $13, %%xmm5              # 24-bit mask                   \n\
        movdqa %%xmm6, %%xmm7                                           \n\
        psrlw $8, %%xmm6                # low bytes mask                \n\
        psllw $8, %%xmm7                # high bytes mask               \n"\
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize */ 4,                                              \
        /* push_regs */ "",                                             \
        /* pop_regs  */ "",                                             \
        /* small_loop */                                                \
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        movb ("#ROFS"-4)("ESI","ECX",4), %%al                           \n\
        movb %%al, -3("EDI","EDX")                                      \n\
        movb ("#GOFS"-4)("ESI","ECX",4), %%al                           \n\
        movb %%al, -2("EDI","EDX")                                      \n\
        movb ("#BOFS"-4)("ESI","ECX",4), %%al                           \n\
        movb %%al, -1("EDI","EDX")",                                    \
        /* main_loop */                                                 \
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        movdqu -16("ESI","ECX",4), %%xmm0                               \n\
        "SHIFT"                         # shift source data to RGBA     \n\
        pshufd $0xF3, %%xmm5, %%xmm1    # XMM1: pixel 1 mask            \n\
        pshufd $0xCF, %%xmm5, %%xmm2    # XMM2: pixel 2 mask            \n\
        pshufd $0x3F, %%xmm5, %%xmm3    # XMM3: pixel 3 mask            \n\
        pand %%xmm0, %%xmm3             # XMM3: pixel 3                 \n\
        psrldq $1, %%xmm3                                               \n\
        pand %%xmm0, %%xmm2             # XMM2: pixel 2                 \n\
        por %%xmm3, %%xmm2              # XMM2: pixels 2 and 3          \n\
        psrldq $1, %%xmm2                                               \n\
        pand %%xmm0, %%xmm1             # XMM1: pixel 1                 \n\
        pand %%xmm5, %%xmm0             # XMM0: pixel 0                 \n\
        por %%xmm2, %%xmm1              # XMM1: pixels 1, 2, and 3      \n\
        psrldq $1, %%xmm1                                               \n\
        por %%xmm1, %%xmm0              # XMM0: RGB24 data              \n\
        # We can't just movdqu, because we might run over the edge      \n\
        movd %%xmm0, -12("EDI","EDX")   # store low 4 bytes             \n\
        pshufd $0xF9, %%xmm0, %%xmm0    # shift right 4 bytes           \n\
        movq %%xmm0, -8("EDI","EDX")    # store high 8 bytes            \n",\
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),              \
          "d" (&rgb_bgr_data), "m" (rgb_bgr_data)                       \
        : "eax");


/* RGBA<->ABGR and ARGB<->BGRA: reverse byte order */
static int rgba_swapall_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_REV32_SSE2(width*height);
    return 1;
}

/* RGBA<->BGRA: swap bytes 0 and 2 */
static int rgba_swap02_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_02_SSE2(width*height);
    return 1;
}

/* ARGB<->ABGR: swap bytes 1 and 3 */
static int rgba_swap13_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_SWAP32_13_SSE2(width*height);
    return 1;
}

/* RGBA->ARGB and BGRA->ABGR: alpha moves from byte 3 to byte 0 */
static int rgba_alpha30_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROL32_SSE2(width*height);
    return 1;
}

/* ARGB->RGBA and ABGR->BGRA: alpha moves from byte 0 to byte 3 */
static int rgba_alpha03_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_ROR32_SSE2(width*height);
    return 1;
}

/* RGB<->BGR */
static int rgb24_bgr24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("movdqa ("EDX"), %%xmm5         # byte 0 mask                   \n\
        pshufd $0xD2, %%xmm5, %%xmm6    # byte 1 mask                   \n\
        pshufd $0xC9, %%xmm5, %%xmm7    # byte 2 mask                   \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        movb -3("ESI","EDX"), %%al                                      \n\
        movb -2("ESI","EDX"), %%ah                                      \n\
        movb %%ah, -2("EDI","EDX")                                      \n\
        movb -1("ESI","EDX"), %%ah                                      \n\
        movb %%ah, -3("EDI","EDX")                                      \n\
        movb %%al, -1("EDI","EDX")",
        /* main_loop */
        "lea ("ECX","ECX",2),"EDX"                                      \n\
        # We can't just movdqu, because we might run over the edge      \n\
        movd -12("ESI","EDX"), %%xmm1                                   \n\
        movq -8("ESI","EDX"), %%xmm0                                    \n\
        pshufd $0xD3, %%xmm0, %%xmm0    # shift left by 4 bytes         \n\
        por %%xmm1, %%xmm0              # XMM0: original data           \n\
        movdqa %%xmm5, %%xmm2                                           \n\
        movdqa %%xmm6, %%xmm3                                           \n\
        movdqa %%xmm7, %%xmm4                                           \n\
        pand %%xmm0, %%xmm2             # XMM2: byte 0                  \n\
        pslldq $2, %%xmm2               # shift to byte 2 position      \n\
        pand %%xmm0, %%xmm3             # XMM3: byte 1                  \n\
        pand %%xmm0, %%xmm4             # XMM4: byte 2                  \n\
        psrldq $2, %%xmm4               # shift to byte 0 position      \n\
        por %%xmm2, %%xmm3                                              \n\
        por %%xmm4, %%xmm3              # XMM3: reversed data           \n\
        movd %%xmm3, -12("EDI","EDX")   # avoid running over the edge   \n\
        pshufd $0xF9, %%xmm3, %%xmm3    # shift right by 4 bytes        \n\
        movq %%xmm3, -8("EDI","EDX")",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),
          "d" (&rgb_bgr_data), "m" (rgb_bgr_data)
        : "eax");
    return 1;
}

/* RGB->RGBA */
static int rgb24_rgba32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB24TO32(0,1,2,3, "");
    return 1;
}

/* RGB->ABGR */
static int rgb24_abgr32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB24TO32(3,2,1,0, SHIFT_RBSWAP SHIFT_AFIRST);
    return 1;
}

/* RGB->ARGB */
static int rgb24_argb32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB24TO32(1,2,3,0, SHIFT_AFIRST);
    return 1;
}

/* RGB->BGRA */
static int rgb24_bgra32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB24TO32(2,1,0,3, SHIFT_RBSWAP);
    return 1;
}

/* RGBA->RGB */
static int rgba32_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB32TO24(0,1,2,3, "");
    return 1;
}

/* ABGR->RGB */
static int abgr32_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB32TO24(3,2,1,0, SHIFT_ALAST SHIFT_RBSWAP);
    return 1;
}

/* ARGB->RGB */
static int argb32_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB32TO24(1,2,3,0, SHIFT_ALAST);
    return 1;
}

/* BGRA->RGB */
static int bgra32_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    RGB32TO24(2,1,0,3, SHIFT_RBSWAP);
    return 1;
}

/*************************************************************************/

#define R_GRAY  19595
#define G_GRAY  38470
#define B_GRAY   7471
#define INIT_GRAY8 \
        "pxor %%xmm4, %%xmm4            # XMM4: all 0's                 \n\
        movl %3, %%eax                                                  \n\
        movd %%eax, %%xmm5                                              \n\
        pshuflw $0x00, %%xmm5, %%xmm5                                   \n\
        pshufd $0x00, %%xmm5, %%xmm5    # XMM5: R->gray constant        \n\
        movl %4, %%eax                                                  \n\
        movd %%eax, %%xmm6                                              \n\
        pshuflw $0x00, %%xmm6, %%xmm6                                   \n\
        pshufd $0x00, %%xmm6, %%xmm6    # XMM6: G->gray constant        \n\
        movl %5, %%eax                                                  \n\
        movd %%eax, %%xmm7                                              \n\
        pshuflw $0x00, %%xmm7, %%xmm7                                   \n\
        pshufd $0x00, %%xmm7, %%xmm7    # XMM7: B->gray constant        \n\
        pcmpeqd %%xmm3, %%xmm3                                          \n\
        psllw $15, %%xmm3                                               \n\
        psrlw $8, %%xmm3                # XMM3: 0x0080*8 (for rounding) \n"
#define SINGLE_GRAY8(idx,ofsR,ofsG,ofsB) \
        "movzbl "#ofsR"("ESI","idx"), %%eax     # retrieve red byte     \n\
        imull %3, %%eax                 # multiply by red->gray factor  \n\
        movzbl "#ofsG"("ESI","idx"), %%edx      # retrieve green byte   \n\
        imull %4, %%edx                 # multiply by green->gray factor\n\
        addl %%edx, %%eax               # add to total                  \n\
        movzbl "#ofsB"("ESI","idx"), %%edx      # retrieve blue byte    \n\
        imull %5, %%edx                 # multiply by blue->gray factor \n\
        addl %%edx, %%eax               # add to total                  \n\
        addl $0x8000, %%eax             # round                         \n\
        shrl $16, %%eax                 # shift back down               \n\
        movb %%al, -1("EDI","ECX")      # and store                     \n"
#define STORE_GRAY8 \
        "psllw $8, %%xmm0               # XMM0: add 8 bits of precision \n\
        pmulhuw %%xmm5, %%xmm0          # XMM0: r7 r6 r5 r4 r3 r2 r1 r0 \n\
        psllw $8, %%xmm1                # XMM1: add 8 bits of precision \n\
        pmulhuw %%xmm6, %%xmm1          # XMM1: g7 g6 g5 g4 g3 g2 g1 g0 \n\
        paddw %%xmm3, %%xmm0            # XMM0: add rounding constant   \n\
        psllw $8, %%xmm2                # XMM2: add 8 bits of precision \n\
        pmulhuw %%xmm7, %%xmm2          # XMM2: b7 b6 b5 b4 b3 b2 b1 b0 \n\
        paddw %%xmm1, %%xmm0            # XMM0: add green part          \n\
        paddw %%xmm2, %%xmm0            # XMM0: add blue part           \n\
        psrlw $8, %%xmm0                # XMM0: shift back to bytes     \n\
        packuswb %%xmm4, %%xmm0         # XMM0: gray7..gray0 packed     \n\
        movq %%xmm0, -8("EDI","ECX")                                    \n"

#define ASM_RGB24_GRAY(ofsR,ofsG,ofsB,load) \
    asm(INIT_GRAY8                                                      \
        "push "EBX"                                                     \n\
        lea ("ECX","ECX",2),"EBX"                                       \n"\
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ 8,                                             \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ SINGLE_GRAY8(EBX, ofsR,ofsG,ofsB) "subl $3, %%ebx;",\
        /* main_loop  */ load(4) STORE_GRAY8 "subl $24, %%ebx;",        \
        /* emms */ "emms")                                              \
        "pop "EBX                                                       \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),              \
          "i" (R_GRAY), "i" (G_GRAY), "i" (B_GRAY)                      \
        : "eax", "edx")

#define ASM_RGB32_GRAY(ofsR,ofsG,ofsB,load) \
    asm(INIT_GRAY8                                                      \
        SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ 8,                                             \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ SINGLE_GRAY8(ECX",4", ofsR,ofsG,ofsB),         \
        /* main_loop  */ load(4) STORE_GRAY8,                           \
        /* emms */ "emms")                                              \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (width*height),              \
          "i" (R_GRAY), "i" (G_GRAY), "i" (B_GRAY)                      \
        : "eax", "edx")


static int rgb24_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB24_GRAY(-3,-2,-1, SSE2_LOAD_RGB24);
    return 1;
}

static int bgr24_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB24_GRAY(-1,-2,-3, SSE2_LOAD_BGR24);
    return 1;
}

static int rgba32_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB32_GRAY(-4,-3,-2, SSE2_LOAD_RGBA32);
    return 1;
}

static int bgra32_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB32_GRAY(-2,-3,-4, SSE2_LOAD_BGRA32);
    return 1;
}

static int argb32_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB32_GRAY(-3,-2,-1, SSE2_LOAD_ARGB32);
    return 1;
}

static int abgr32_gray8_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    ASM_RGB32_GRAY(-1,-2,-3, SSE2_LOAD_ABGR32);
    return 1;
}

/*************************************************************************/

static int gray8_rgb24_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("# Store all 0's in XMM4                                        \n\
        pxor %%xmm4, %%xmm4                                             \n\
        # Generate mask in XMM7 to select bytes 0,3,6,9 of an XMM register\n\
        pcmpeqd %%xmm7, %%xmm7          # XMM7: all 1's                 \n\
        psrlw $8, %%xmm7                # XMM7: 0x00FF * 8              \n\
        pcmpeqd %%xmm6, %%xmm6          # XMM6: all 1's                 \n\
        psllw $8, %%xmm6                # XMM6: 0xFF00 * 8              \n\
        pslldq $8, %%xmm6                                               \n\
        psrldq $8, %%xmm7                                               \n\
        por %%xmm6, %%xmm7              # XMM7: 0xFF00*4, 0x00FF*4      \n\
        pshufd $0xCC, %%xmm7, %%xmm7    # XMM7: {0xFF00*2, 0x00FF*2} * 2\n\
        pshuflw $0xC0, %%xmm7, %%xmm7   # XMM7.l: FF0000FF00FF00FF      \n\
        psrldq $4, %%xmm7               # XMM7: 0x00000000FF00FF00      \n\
                                        #         00FF00FFFF0000FF      \n\
        pshufd $0xEC, %%xmm7, %%xmm7    # XMM7: 0x00000000FF00FF00      \n\
                                        #         00000000FF0000FF      \n\
        pshuflw $0x24, %%xmm7, %%xmm7   # XMM7.l: 00FF0000FF0000FF      \n\
        pshufhw $0xFC, %%xmm7, %%xmm7   # XMM7.h: 000000000000FF00      \n\
        # Load ECX*3 into EDX ahead of time                             \n\
        lea ("ECX","ECX",2), "EDX"                                      \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movb -1("ESI","ECX"), %%al      # retrieve gray byte            \n\
        movb %%al, -3("EDI","EDX")      # and store 3 times             \n\
        movb %%al, -2("EDI","EDX")                                      \n\
        movb %%al, -1("EDI","EDX")                                      \n\
        subl $3, %%edx                                                  \n",
        /* main_loop */ "\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: G3..G0                  \n\
        pshufd $0xCC, %%xmm0, %%xmm0    # XMM0: {0,0,0,0,G3..G0} * 2    \n\
        pshuflw $0x50, %%xmm0, %%xmm0   # X0.l: G3 G2 G3 G2 G1 G0 G1 G0 \n\
        pshufhw $0x55, %%xmm0, %%xmm0   # X0.h: G3 G2 G3 G2 G3 G2 G3 G2 \n\
        pand %%xmm7, %%xmm0             # XMM0: ------3--2--1--0        \n\
        movdqa %%xmm0, %%xmm1           # XMM1: ------3--2--1--0        \n\
        pslldq $1, %%xmm1               # XMM1: -----3--2--1--0-        \n\
        movdqa %%xmm0, %%xmm2           # XMM2: ------3--2--1--0        \n\
        pslldq $2, %%xmm2               # XMM2: ----3--2--1--0--        \n\
        por %%xmm1, %%xmm0              # XMM0: -----33-22-11-00        \n\
        por %%xmm2, %%xmm0              # XMM0: ----333222111000        \n\
        movd %%xmm0, -12("EDI","EDX")                                   \n\
        pshufd $0xC9, %%xmm0, %%xmm0                                    \n\
        movq %%xmm0, -8("EDI","EDX")                                    \n\
        subl $12, %%edx                                                 \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax", "edx");
    return 1;
}

static int gray8_rgba32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("pxor %%xmm4, %%xmm4            # XMM4: all 0's                 \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movb -1("ESI","ECX"), %%al      # retrieve gray byte            \n\
        movb %%al, -4("EDI","ECX",4)    # and store 3 times             \n\
        movb %%al, -3("EDI","ECX",4)                                    \n\
        movb %%al, -2("EDI","ECX",4)                                    \n\
        movb $0, -1("EDI","ECX",4)      # clear A byte                  \n",
        /* main_loop */ "\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: 00 00 00 00 G3 G2 G1 G0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: 00 00 00 00 G3 G2 G1 G0 \n\
        punpcklbw %%xmm0, %%xmm0        # XMM0: G3 G3 G2 G2 G1 G1 G0 G0 \n\
        punpcklbw %%xmm4, %%xmm1        # XMM1: 00 G3 00 G2 00 G1 00 G0 \n\
        punpcklbw %%xmm1, %%xmm0        # XMM0: 0GGG3 0GGG2 0GGG1 0GGG0 \n\
        movdqu %%xmm0, -16("EDI","ECX",4)                               \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

static int gray8_argb32_sse2(uint8_t **src, uint8_t **dest, int width, int height)
{
    asm("pxor %%xmm4, %%xmm4            # XMM4: all 0's                 \n"
        SIMD_LOOP_WRAPPER(
        /* blocksize */ 4,
        /* push_regs */ "",
        /* pop_regs  */ "",
        /* small_loop */ "\
        movb -1("ESI","ECX"), %%al      # retrieve gray byte            \n\
        movb %%al, -3("EDI","ECX",4)    # and store 3 times             \n\
        movb %%al, -2("EDI","ECX",4)                                    \n\
        movb %%al, -1("EDI","ECX",4)                                    \n\
        movb $0, -4("EDI","ECX",4)      # clear A byte                  \n",
        /* main_loop */ "\
        movd -4("ESI","ECX"), %%xmm0    # XMM0: 00 00 00 00 G3 G2 G1 G0 \n\
        movdqa %%xmm4, %%xmm1           # XMM1: 00 00 00 00 00 00 00 00 \n\
        punpcklbw %%xmm0, %%xmm1        # XMM1: G3 00 G2 00 G1 00 G0 00 \n\
        punpcklbw %%xmm0, %%xmm0        # XMM0: G3 G3 G2 G2 G1 G1 G0 G0 \n\
        punpcklbw %%xmm0, %%xmm1        # XMM0: GGG03 GGG02 GGG01 GGG00 \n\
        movdqu %%xmm1, -16("EDI","ECX",4)                               \n",
        /* emms */ "emms")
        : /* no outputs */
        : "S" (src[0]), "D" (dest[0]), "c" (width*height)
        : "eax");
    return 1;
}

#endif  /* HAVE_ASM_SSE2 */

/*************************************************************************/

#endif  /* ARCH_X86 || ARCH_X86_64 */

/*************************************************************************/
/*************************************************************************/

/* Initialization */

int ac_imgconvert_init_rgb_packed(int accel)
{
    if (!register_conversion(IMG_RGB24,   IMG_RGB24,   rgb_copy)
     || !register_conversion(IMG_RGB24,   IMG_BGR24,   rgb24_bgr24)
     || !register_conversion(IMG_RGB24,   IMG_RGBA32,  rgb24_rgba32)
     || !register_conversion(IMG_RGB24,   IMG_ABGR32,  rgb24_abgr32)
     || !register_conversion(IMG_RGB24,   IMG_ARGB32,  rgb24_argb32)
     || !register_conversion(IMG_RGB24,   IMG_BGRA32,  rgb24_bgra32)
     || !register_conversion(IMG_RGB24,   IMG_GRAY8,   rgb24_gray8)

     || !register_conversion(IMG_BGR24,   IMG_BGR24,   rgb_copy)
     || !register_conversion(IMG_BGR24,   IMG_RGB24,   rgb24_bgr24)
     || !register_conversion(IMG_BGR24,   IMG_RGBA32,  rgb24_bgra32)
     || !register_conversion(IMG_BGR24,   IMG_ABGR32,  rgb24_argb32)
     || !register_conversion(IMG_BGR24,   IMG_ARGB32,  rgb24_abgr32)
     || !register_conversion(IMG_BGR24,   IMG_BGRA32,  rgb24_rgba32)
     || !register_conversion(IMG_BGR24,   IMG_GRAY8,   bgr24_gray8)

     || !register_conversion(IMG_RGBA32,  IMG_RGB24,   rgba32_rgb24)
     || !register_conversion(IMG_RGBA32,  IMG_BGR24,   bgra32_rgb24)
     || !register_conversion(IMG_RGBA32,  IMG_RGBA32,  rgba_copy)
     || !register_conversion(IMG_RGBA32,  IMG_ABGR32,  rgba_swapall)
     || !register_conversion(IMG_RGBA32,  IMG_ARGB32,  rgba_alpha30)
     || !register_conversion(IMG_RGBA32,  IMG_BGRA32,  rgba_swap02)
     || !register_conversion(IMG_RGBA32,  IMG_GRAY8,   rgba32_gray8)

     || !register_conversion(IMG_ABGR32,  IMG_RGB24,   abgr32_rgb24)
     || !register_conversion(IMG_ABGR32,  IMG_BGR24,   argb32_rgb24)
     || !register_conversion(IMG_ABGR32,  IMG_RGBA32,  rgba_swapall)
     || !register_conversion(IMG_ABGR32,  IMG_ABGR32,  rgba_copy)
     || !register_conversion(IMG_ABGR32,  IMG_ARGB32,  rgba_swap13)
     || !register_conversion(IMG_ABGR32,  IMG_BGRA32,  rgba_alpha03)
     || !register_conversion(IMG_ABGR32,  IMG_GRAY8,   abgr32_gray8)

     || !register_conversion(IMG_ARGB32,  IMG_RGB24,   argb32_rgb24)
     || !register_conversion(IMG_ARGB32,  IMG_BGR24,   abgr32_rgb24)
     || !register_conversion(IMG_ARGB32,  IMG_RGBA32,  rgba_alpha03)
     || !register_conversion(IMG_ARGB32,  IMG_ABGR32,  rgba_swap13)
     || !register_conversion(IMG_ARGB32,  IMG_ARGB32,  rgba_copy)
     || !register_conversion(IMG_ARGB32,  IMG_BGRA32,  rgba_swapall)
     || !register_conversion(IMG_ARGB32,  IMG_GRAY8,   argb32_gray8)

     || !register_conversion(IMG_BGRA32,  IMG_RGB24,   bgra32_rgb24)
     || !register_conversion(IMG_BGRA32,  IMG_BGR24,   rgba32_rgb24)
     || !register_conversion(IMG_BGRA32,  IMG_RGBA32,  rgba_swap02)
     || !register_conversion(IMG_BGRA32,  IMG_ABGR32,  rgba_alpha30)
     || !register_conversion(IMG_BGRA32,  IMG_ARGB32,  rgba_swapall)
     || !register_conversion(IMG_BGRA32,  IMG_BGRA32,  rgba_copy)
     || !register_conversion(IMG_BGRA32,  IMG_GRAY8,   bgra32_gray8)

     || !register_conversion(IMG_GRAY8,   IMG_RGB24,   gray8_rgb24)
     || !register_conversion(IMG_GRAY8,   IMG_BGR24,   gray8_rgb24)
     || !register_conversion(IMG_GRAY8,   IMG_RGBA32,  gray8_rgba32)
     || !register_conversion(IMG_GRAY8,   IMG_ABGR32,  gray8_argb32)
     || !register_conversion(IMG_GRAY8,   IMG_ARGB32,  gray8_argb32)
     || !register_conversion(IMG_GRAY8,   IMG_BGRA32,  gray8_rgba32)
     || !register_conversion(IMG_GRAY8,   IMG_GRAY8,   gray8_copy)
    ) {
        return 0;
    }

#if defined(ARCH_X86) || defined(ARCH_X86_64)

    if (accel & (AC_IA32ASM | AC_AMD64ASM)) {
        if (!register_conversion(IMG_RGBA32,  IMG_ABGR32,  rgba_swapall_x86)
         || !register_conversion(IMG_RGBA32,  IMG_ARGB32,  rgba_alpha30_x86)
         || !register_conversion(IMG_RGBA32,  IMG_BGRA32,  rgba_swap02_x86)

         || !register_conversion(IMG_ABGR32,  IMG_RGBA32,  rgba_swapall_x86)
         || !register_conversion(IMG_ABGR32,  IMG_ARGB32,  rgba_swap13_x86)
         || !register_conversion(IMG_ABGR32,  IMG_BGRA32,  rgba_alpha03_x86)

         || !register_conversion(IMG_ARGB32,  IMG_RGBA32,  rgba_alpha03_x86)
         || !register_conversion(IMG_ARGB32,  IMG_ABGR32,  rgba_swap13_x86)
         || !register_conversion(IMG_ARGB32,  IMG_BGRA32,  rgba_swapall_x86)

         || !register_conversion(IMG_BGRA32,  IMG_RGBA32,  rgba_swap02_x86)
         || !register_conversion(IMG_BGRA32,  IMG_ABGR32,  rgba_alpha30_x86)
         || !register_conversion(IMG_BGRA32,  IMG_ARGB32,  rgba_swapall_x86)
        ) {
            return 0;
        }
    }

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)
    if (accel & AC_MMX) {
        if (!register_conversion(IMG_RGBA32,  IMG_ABGR32,  rgba_swapall_mmx)
         || !register_conversion(IMG_RGBA32,  IMG_ARGB32,  rgba_alpha30_mmx)
         || !register_conversion(IMG_RGBA32,  IMG_BGRA32,  rgba_swap02_mmx)

         || !register_conversion(IMG_ABGR32,  IMG_RGBA32,  rgba_swapall_mmx)
         || !register_conversion(IMG_ABGR32,  IMG_ARGB32,  rgba_swap13_mmx)
         || !register_conversion(IMG_ABGR32,  IMG_BGRA32,  rgba_alpha03_mmx)

         || !register_conversion(IMG_ARGB32,  IMG_RGBA32,  rgba_alpha03_mmx)
         || !register_conversion(IMG_ARGB32,  IMG_ABGR32,  rgba_swap13_mmx)
         || !register_conversion(IMG_ARGB32,  IMG_BGRA32,  rgba_swapall_mmx)

         || !register_conversion(IMG_BGRA32,  IMG_RGBA32,  rgba_swap02_mmx)
         || !register_conversion(IMG_BGRA32,  IMG_ABGR32,  rgba_alpha30_mmx)
         || !register_conversion(IMG_BGRA32,  IMG_ARGB32,  rgba_swapall_mmx)
        ) {
            return 0;
        }
    }
#endif

#if defined(HAVE_ASM_SSE2)
    if (accel & AC_SSE2) {
        if (!register_conversion(IMG_RGB24,   IMG_BGR24,   rgb24_bgr24_sse2)
         || !register_conversion(IMG_RGB24,   IMG_RGBA32,  rgb24_rgba32_sse2)
         || !register_conversion(IMG_RGB24,   IMG_ABGR32,  rgb24_abgr32_sse2)
         || !register_conversion(IMG_RGB24,   IMG_ARGB32,  rgb24_argb32_sse2)
         || !register_conversion(IMG_RGB24,   IMG_BGRA32,  rgb24_bgra32_sse2)
         || !register_conversion(IMG_RGB24,   IMG_GRAY8,   rgb24_gray8_sse2)

         || !register_conversion(IMG_BGR24,   IMG_RGB24,   rgb24_bgr24_sse2)
         || !register_conversion(IMG_BGR24,   IMG_RGBA32,  rgb24_bgra32_sse2)
         || !register_conversion(IMG_BGR24,   IMG_ABGR32,  rgb24_argb32_sse2)
         || !register_conversion(IMG_BGR24,   IMG_ARGB32,  rgb24_abgr32_sse2)
         || !register_conversion(IMG_BGR24,   IMG_BGRA32,  rgb24_rgba32_sse2)
         || !register_conversion(IMG_BGR24,   IMG_GRAY8,   bgr24_gray8_sse2)

         || !register_conversion(IMG_RGBA32,  IMG_RGB24,   rgba32_rgb24_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_BGR24,   bgra32_rgb24_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_ABGR32,  rgba_swapall_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_ARGB32,  rgba_alpha30_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_BGRA32,  rgba_swap02_sse2)
         || !register_conversion(IMG_RGBA32,  IMG_GRAY8,   rgba32_gray8_sse2)

         || !register_conversion(IMG_ABGR32,  IMG_RGB24,   abgr32_rgb24_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_BGR24,   argb32_rgb24_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_RGBA32,  rgba_swapall_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_ARGB32,  rgba_swap13_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_BGRA32,  rgba_alpha03_sse2)
         || !register_conversion(IMG_ABGR32,  IMG_GRAY8,   abgr32_gray8_sse2)

         || !register_conversion(IMG_ARGB32,  IMG_RGB24,   argb32_rgb24_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_BGR24,   abgr32_rgb24_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_RGBA32,  rgba_alpha03_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_ABGR32,  rgba_swap13_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_BGRA32,  rgba_swapall_sse2)
         || !register_conversion(IMG_ARGB32,  IMG_GRAY8,   argb32_gray8_sse2)

         || !register_conversion(IMG_BGRA32,  IMG_RGB24,   bgra32_rgb24_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_BGR24,   rgba32_rgb24_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_RGBA32,  rgba_swap02_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_ABGR32,  rgba_alpha30_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_ARGB32,  rgba_swapall_sse2)
         || !register_conversion(IMG_BGRA32,  IMG_GRAY8,   bgra32_gray8_sse2)

         || !register_conversion(IMG_GRAY8,   IMG_RGB24,   gray8_rgb24_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_BGR24,   gray8_rgb24_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_RGBA32,  gray8_rgba32_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_ABGR32,  gray8_argb32_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_ARGB32,  gray8_argb32_sse2)
         || !register_conversion(IMG_GRAY8,   IMG_BGRA32,  gray8_rgba32_sse2)
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
