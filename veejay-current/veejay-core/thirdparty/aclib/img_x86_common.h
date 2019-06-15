/*
 * img_x86_common.h - common x86/x86-64 assembly macros
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef ACLIB_IMG_X86_COMMON_H
#define ACLIB_IMG_X86_COMMON_H

/*************************************************************************/

/* Register names for pointers */
#ifdef ARCH_X86_64
# define EAX "%%rax"
# define EBX "%%rbx"
# define ECX "%%rcx"
# define EDX "%%rdx"
# define ESP "%%rsp"
# define EBP "%%rbp"
# define ESI "%%rsi"
# define EDI "%%rdi"
#else
# define EAX "%%eax"
# define EBX "%%ebx"
# define ECX "%%ecx"
# define EDX "%%edx"
# define ESP "%%esp"
# define EBP "%%ebp"
# define ESI "%%esi"
# define EDI "%%edi"
#endif

/* Data for isolating particular bytes.  Used by the SWAP32 macros; if you
 * use them, make sure to define DEFINE_MASK_DATA before including this
 * file! */
#ifdef DEFINE_MASK_DATA
static const struct { uint32_t n[64]; } __attribute__((aligned(16))) mask_data = {{
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF,
    0x0000FF00, 0x0000FF00, 0x0000FF00, 0x0000FF00,
    0x0000FFFF, 0x0000FFFF, 0x0000FFFF, 0x0000FFFF,
    0x00FF0000, 0x00FF0000, 0x00FF0000, 0x00FF0000,
    0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF,
    0x00FFFF00, 0x00FFFF00, 0x00FFFF00, 0x00FFFF00,
    0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF,
    0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF,
    0xFF00FF00, 0xFF00FF00, 0xFF00FF00, 0xFF00FF00,
    0xFF00FFFF, 0xFF00FFFF, 0xFF00FFFF, 0xFF00FFFF,
    0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000,
    0xFFFF00FF, 0xFFFF00FF, 0xFFFF00FF, 0xFFFF00FF,
    0xFFFFFF00, 0xFFFFFF00, 0xFFFFFF00, 0xFFFFFF00,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
}};
#endif

/*************************************************************************/

/* Basic assembly macros, used for odd-count loops */

/* Swap bytes in pairs of 16-bit values */
#define X86_SWAP16_2 \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        movl %%eax, %%edx                                               \n\
        shll $8, %%eax                                                  \n\
        andl $0xFF00FF00, %%eax                                         \n\
        shrl $8, %%edx                                                  \n\
        andl $0x00FF00FF, %%edx                                         \n\
        orl %%edx, %%eax                                                \n\
        movl %%eax, -4("EDI","ECX",4)"

/* Swap words in a 32-bit value */
#define X86_SWAP32 \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        roll $16, %%eax                                                 \n\
        movl %%eax, -4("EDI","ECX",4)"

/* Swap bytes 0 and 2 of a 32-bit value */
#define X86_SWAP32_02 \
        "movw -4("ESI","ECX",4), %%ax                                   \n\
        movw -2("ESI","ECX",4), %%dx                                    \n\
        xchg %%dl, %%al                                                 \n\
        movw %%ax, -4("EDI","ECX",4)                                    \n\
        movw %%dx, -2("EDI","ECX",4)"

/* Swap bytes 1 and 3 of a 32-bit value */
#define X86_SWAP32_13 \
        "movw -4("ESI","ECX",4), %%ax                                   \n\
        movw -2("ESI","ECX",4), %%dx                                    \n\
        xchg %%dh, %%ah                                                 \n\
        movw %%ax, -4("EDI","ECX",4)                                    \n\
        movw %%dx, -2("EDI","ECX",4)"

/* Reverse the order of bytes in a 32-bit value */
#define X86_REV32 \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        xchg %%ah, %%al                                                 \n\
        roll $16, %%eax                                                 \n\
        xchg %%ah, %%al                                                 \n\
        movl %%eax, -4("EDI","ECX",4)"

/* The same, using the BSWAP instruction */
#define X86_REV32_BSWAP \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        bswap %%eax                                                     \n\
        movl %%eax, -4("EDI","ECX",4)"

/* Rotate a 32-bit value left 8 bits */
#define X86_ROL32 \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        roll $8, %%eax                                                  \n\
        movl %%eax, -4("EDI","ECX",4)"

/* Rotate a 32-bit value right 8 bits */
#define X86_ROR32 \
        "movl -4("ESI","ECX",4), %%eax                                  \n\
        rorl $8, %%eax                                                  \n\
        movl %%eax, -4("EDI","ECX",4)"

/*************************************************************************/

/* Basic assembly routines.  Sizes are all given in 32-bit units. */

#define ASM_SWAP16_2_X86(size) \
    asm("0: "X86_SWAP16_2"                                              \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax", "edx")

#define ASM_SWAP32_X86(size) \
    asm("0: "X86_SWAP32"                                                \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax", "edx")

#define ASM_SWAP32_02_X86(size) \
    asm("0: "X86_SWAP32_02"                                             \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax", "edx")

#define ASM_SWAP32_13_X86(size) \
    asm("0: "X86_SWAP32_13"                                             \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax", "edx")

#define ASM_REV32_X86(size) \
    asm("0: "X86_REV32"                                                 \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

#define ASM_ROL32_X86(size) \
    asm("0: "X86_ROL32"                                                 \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

#define ASM_ROR32_X86(size) \
    asm("0: "X86_ROR32"                                                 \n\
        subl $1, %%ecx                                                  \n\
        jnz 0b"                                                         \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

/*************************************************************************/
/*************************************************************************/

/* Wrapper for SIMD loops.  This generates the body of an asm() construct
 * (the string only, not the input/output/clobber lists) given the data
 * block size (number of data units processed per SIMD loop iteration),
 * instructions to save and restore unclobberable registers (such as EBX),
 * and the bodies of the odd-count and main loops.  The data count is
 * assumed to be preloaded in ECX.  Parameters are:
 *     blocksize: number of units of data processed per SIMD loop (must be
 *                a power of 2); can be a constant or a numerical
 *                expression containing only constants
 *     push_regs: string constant containing instructions to push registers
 *                that must be saved over the small loop
 *      pop_regs: string constant containing instructions to pop registers
 *                saved by `push_regs' (restored before the main loop)
 *    small_loop: loop for handling data elements one at a time (when the
 *                count is not a multiple of `blocksize'
 *     main_loop: main SIMD loop for processing data
 *          emms: EMMS/SFENCE instructions to end main loop with, as needed
 */

#define SIMD_LOOP_WRAPPER(blocksize,push_regs,pop_regs,small_loop,main_loop,emms) \
        /* Always save ECX--GCC may rely on it being unmodified */      \
        "push "ECX"; "                                                  \
        /* Check whether the count is a multiple of the blocksize (this \
         * can cause branch mispredicts but seems to be faster overall) */ \
        "testl $(("#blocksize")-1), %%ecx; "                            \
        "jz 1f; "                                                       \
        /* It's not--run the small loop to align the count */           \
        push_regs"; "                                                   \
        "0: "                                                           \
        small_loop"; "                                                  \
        "subl $1, %%ecx; "                                              \
        "testl $(("#blocksize")-1), %%ecx; "                            \
        "jnz 0b; "                                                      \
        pop_regs"; "                                                    \
        /* Make sure there's some data left */                          \
        "testl %%ecx, %%ecx; "                                          \
        "jz 2f; "                                                       \
        /* Now run the main SIMD loop */                                \
        "1: "                                                           \
        main_loop"; "                                                   \
        "subl $("#blocksize"), %%ecx; "                                 \
        "jnz 1b; "                                                      \
        /* Clear MMX state and/or SFENCE, as needed */                  \
        emms"; "                                                        \
        /* Restore ECX and finish */                                    \
        "2: "                                                           \
        "pop "ECX";"

/*************************************************************************/

/* MMX- and SSE2-optimized swap/rotate routines.  These routines are
 * identical save for data size, so we use common macros to implement them,
 * with register names and data offsets replaced by parameters to the
 * macros. */

#define ASM_SIMD_MMX(name,size) \
    name((size), 64,                            \
         "movq", "movq", "movq", "",            \
         "%%mm0", "%%mm1", "%%mm2", "%%mm3",    \
         "%%mm4", "%%mm5", "%%mm6", "%%mm7")
#define ASM_SIMD_SSE2(name,size) \
    name((size), 128,                           \
         "movdqu", "movdqa", "movdqu", "",      \
         "%%xmm0", "%%xmm1", "%%xmm2", "%%xmm3",\
         "%%xmm4", "%%xmm5", "%%xmm6", "%%xmm7")
#define ASM_SIMD_SSE2_ALIGNED(name,size) \
    name((size), 128,                           \
         "movdqa", "movdqa", "movntdq", "sfence",\
         "%%xmm0", "%%xmm1", "%%xmm2", "%%xmm3",\
         "%%xmm4", "%%xmm5", "%%xmm6", "%%xmm7")

#define ASM_SWAP16_2_MMX(size)    ASM_SIMD_MMX(ASM_SWAP16_2_SIMD,(size))
#define ASM_SWAP16_2_SSE2(size)   ASM_SIMD_SSE2(ASM_SWAP16_2_SIMD,(size))
#define ASM_SWAP16_2_SSE2A(size)  ASM_SIMD_SSE2_ALIGNED(ASM_SWAP16_2_SIMD,(size))
#define ASM_SWAP32_MMX(size)      ASM_SIMD_MMX(ASM_SWAP32_SIMD,(size))
#define ASM_SWAP32_SSE2(size)     ASM_SIMD_SSE2(ASM_SWAP32_SIMD,(size))
#define ASM_SWAP32_SSE2A(size)    ASM_SIMD_SSE2_ALIGNED(ASM_SWAP32_SIMD,(size))
#define ASM_SWAP32_02_MMX(size)   ASM_SIMD_MMX(ASM_SWAP32_02_SIMD,(size))
#define ASM_SWAP32_02_SSE2(size)  ASM_SIMD_SSE2(ASM_SWAP32_02_SIMD,(size))
#define ASM_SWAP32_02_SSE2A(size) ASM_SIMD_SSE2_ALIGNED(ASM_SWAP32_02_SIMD,(size))
#define ASM_SWAP32_13_MMX(size)   ASM_SIMD_MMX(ASM_SWAP32_13_SIMD,(size))
#define ASM_SWAP32_13_SSE2(size)  ASM_SIMD_SSE2(ASM_SWAP32_13_SIMD,(size))
#define ASM_SWAP32_13_SSE2A(size) ASM_SIMD_SSE2_ALIGNED(ASM_SWAP32_13_SIMD,(size))
#define ASM_REV32_MMX(size)       ASM_SIMD_MMX(ASM_REV32_SIMD,(size))
#define ASM_REV32_SSE2(size)      ASM_SIMD_SSE2(ASM_REV32_SIMD,(size))
#define ASM_REV32_SSE2A(size)     ASM_SIMD_SSE2_ALIGNED(ASM_REV32_SIMD,(size))
#define ASM_ROL32_MMX(size)       ASM_SIMD_MMX(ASM_ROL32_SIMD,(size))
#define ASM_ROL32_SSE2(size)      ASM_SIMD_SSE2(ASM_ROL32_SIMD,(size))
#define ASM_ROL32_SSE2A(size)     ASM_SIMD_SSE2_ALIGNED(ASM_ROL32_SIMD,(size))
#define ASM_ROR32_MMX(size)       ASM_SIMD_MMX(ASM_ROR32_SIMD,(size))
#define ASM_ROR32_SSE2(size)      ASM_SIMD_SSE2(ASM_ROR32_SIMD,(size))
#define ASM_ROR32_SSE2A(size)     ASM_SIMD_SSE2_ALIGNED(ASM_ROR32_SIMD,(size))

/*************************************************************************/

/* Actual implementations.  Note that unrolling the SIMD loops doesn't seem
 * to be a win (only 2-3% improvement at most), and in fact can lose by a
 * bit in short loops. */

#define ASM_SWAP16_2_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ X86_SWAP16_2,                                  \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        psrlw $8, "MM0"                 # MM0: - 7 - 5 - 3 - 1          \n\
        psllw $8, "MM1"                 # MM1: 6 - 4 - 2 - 0 -          \n\
        por "MM1", "MM0"                # MM0: 6 7 4 5 2 3 0 1          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax", "edx")

#define ASM_SWAP32_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ X86_SWAP32,                                    \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        psrld $16, "MM0"                # MM0: - - 7 6 - - 3 2          \n\
        pslld $16, "MM1"                # MM1: 5 4 - - 1 0 - -          \n\
        por "MM1", "MM0"                # MM0: 5 4 7 6 1 0 3 2          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

#define ASM_SWAP32_02_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "push "EDX,                                    \
        /* pop_regs   */ "pop "EDX,                                     \
        /* small_loop */ X86_SWAP32_02,                                 \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM2"             # MM2: 7 6 5 4 3 2 1 0          \n\
        pand 16("EDX"), "MM1"           # MM1: - - - 4 - - - 0          \n\
        pslld $16, "MM1"                # MM1: - 4 - - - 0 - -          \n\
        pand 64("EDX"), "MM2"           # MM2: - 6 - - - 2 - -          \n\
        psrld $16, "MM2"                # MM2: - - - 6 - - - 2          \n\
        pand 160("EDX"), "MM0"          # MM0: 7 - 5 - 3 - 1 -          \n\
        por "MM1", "MM0"                # MM0: 7 4 5 - 3 0 1 -          \n\
        por "MM2", "MM0"                # MM0: 7 4 5 6 3 0 1 2          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size), "d" (&mask_data),    \
          "m" (mask_data)                                               \
        : "eax")

#define ASM_SWAP32_13_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "push "EDX,                                    \
        /* pop_regs   */ "pop "EDX,                                     \
        /* small_loop */ X86_SWAP32_13,                                 \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM2"             # MM2: 7 6 5 4 3 2 1 0          \n\
        pand 32("EDX"), "MM1"           # MM1: - - 5 - - - 1 -          \n\
        pslld $16, "MM1"                # MM1: 5 - - - 1 - - -          \n\
        pand 128("EDX"), "MM2"          # MM2: 7 - - - 3 - - -          \n\
        psrld $16, "MM2"                # MM2: - - 7 - - - 3 -          \n\
        pand 80("EDX"), "MM0"           # MM0: - 6 - 4 - 2 - 0          \n\
        por "MM1", "MM0"                # MM0: 5 6 - 4 1 2 - 0          \n\
        por "MM2", "MM0"                # MM0: 5 6 7 4 1 2 3 0          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size), "d" (&mask_data),    \
          "m" (mask_data)                                               \
        : "eax");

#define ASM_REV32_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ X86_REV32_BSWAP,                               \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM2"             # MM2: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM3"             # MM3: 7 6 5 4 3 2 1 0          \n\
        psrld $24, "MM0"                # MM0: - - - 7 - - - 3          \n\
        pand 32("EDX"), "MM2"           # MM2: - - 5 - - - 1 -          \n\
        psrld $8, "MM1"                 # MM1: - 7 6 5 - 3 2 1          \n\
        pand 32("EDX"), "MM1"           # MM1: - - 6 - - - 2 -          \n\
        pslld $8, "MM2"                 # MM2: - 5 - - - 1 - -          \n\
        pslld $24, "MM3"                # MM3: 4 - - - 0 - - -          \n\
        por "MM1", "MM0"                # MM0: - - 6 7 - - 2 3          \n\
        por "MM2", "MM0"                # MM0: - 5 6 7 - 1 2 3          \n\
        por "MM3", "MM0"                # MM0: 4 5 6 7 0 1 2 3          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size), "d" (&mask_data),    \
          "m" (mask_data)                                               \
        : "eax")

#define ASM_ROL32_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ X86_ROL32,                                     \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        pslld $8, "MM0"                 # MM0: 6 5 4 - 2 1 0 -          \n\
        psrld $24, "MM1"                # MM1: - - - 7 - - - 3          \n\
        por "MM1", "MM0"                # MM0: 6 5 4 7 2 1 0 3          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

#define ASM_ROR32_SIMD(size,regsize,ldq,movq,stq,sfence,MM0,MM1,MM2,MM3,MM4,MM5,MM6,MM7) \
    asm(SIMD_LOOP_WRAPPER(                                              \
        /* blocksize  */ (regsize)/32,                                  \
        /* push_regs  */ "",                                            \
        /* pop_regs   */ "",                                            \
        /* small_loop */ X86_ROR32,                                     \
        /* main_loop  */                                                \
         ldq" -("#regsize"/8)("ESI","ECX",4), "MM0"                     \n\
                                        # MM0: 7 6 5 4 3 2 1 0          \n\
        "movq" "MM0", "MM1"             # MM1: 7 6 5 4 3 2 1 0          \n\
        psrld $8, "MM0"                 # MM0: - 7 6 5 - 3 2 1          \n\
        pslld $24, "MM1"                # MM1: 4 - - - 0 - - -          \n\
        por "MM1", "MM0"                # MM0: 4 7 6 5 0 3 2 1          \n\
        "stq" "MM0", -("#regsize"/8)("EDI","ECX",4)",                   \
        /* emms */ "emms; "sfence)                                      \
        : /* no outputs */                                              \
        : "S" (src[0]), "D" (dest[0]), "c" (size)                       \
        : "eax")

/*************************************************************************/

/* SSE2 macros to load 8 24- or 32-bit RGB pixels into XMM0/1/2 (R/G/B) as
 * 16-bit values, used for RGB->YUV and RGB->grayscale conversions.
 * ZERO is the number of the XMM register containing all zeroes. */

#define SSE2_LOAD_RGB24(ZERO) \
        "movl -21("ESI","EBX"), %%eax                                   \n\
        movd %%eax, %%xmm0              # XMM0: ----- ----- ----- xBGR1 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xBGR1 ----- ----- ----- \n\
        movl -18("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xBGR1 ----- ----- xBGR2 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xBGR2 xBGR1 ----- ----- \n\
        movl -15("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xBGR2 xBGR1 ----- xBGR3 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xBGR3 xBGR2 xBGR1 ----- \n\
        movl -24("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xBGR3 xBGR2 xBGR1 xBGR0 \n\
        movl -9("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm1              # XMM1: ----- ----- ----- xBGR5 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xBGR5 ----- ----- ----- \n\
        movl -6("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xBGR5 ----- ----- xBGR6 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xBGR6 xBGR5 ----- ----- \n\
        movl -3("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xBGR6 xBGR5 ----- xBGR7 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xBGR7 xBGR6 xBGR5 ----- \n\
        movl -12("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xBGR7 xBGR6 xBGR5 xBGR4 \n"\
        SSE2_MASSAGE_RGBA32(ZERO)

#define SSE2_LOAD_BGR24(ZERO) \
        "movl -21("ESI","EBX"), %%eax                                   \n\
        movd %%eax, %%xmm0              # XMM0: ----- ----- ----- xRGB1 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xRGB1 ----- ----- ----- \n\
        movl -18("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xRGB1 ----- ----- xRGB2 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xRGB2 xRGB1 ----- ----- \n\
        movl -15("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xRGB2 xRGB1 ----- xRGB3 \n\
        pshufd $0x39, %%xmm0, %%xmm0    # XMM0: xRGB3 xRGB2 xRGB1 ----- \n\
        movl -24("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm0              # XMM0: xRGB3 xRGB2 xRGB1 xRGB0 \n\
        movl -9("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm1              # XMM1: ----- ----- ----- xRGB5 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xRGB5 ----- ----- ----- \n\
        movl -6("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xRGB5 ----- ----- xRGB6 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xRGB6 xRGB5 ----- ----- \n\
        movl -3("ESI","EBX"), %%eax                                     \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xRGB6 xRGB5 ----- xRGB7 \n\
        pshufd $0x39, %%xmm1, %%xmm1    # XMM1: xRGB7 xRGB6 xRGB5 ----- \n\
        movl -12("ESI","EBX"), %%eax                                    \n\
        movd %%eax, %%xmm2                                              \n\
        por %%xmm2, %%xmm1              # XMM1: xRGB7 xRGB6 xRGB5 xRGB4 \n"\
        SSE2_MASSAGE_BGRA32(ZERO)

#define SSE2_LOAD_RGBA32(ZERO) "\
        movdqu -32("ESI","ECX",4),%%xmm0 #XMM0: ABGR3 ABGR2 ABGR1 ABGR0 \n\
        movdqu -16("ESI","ECX",4),%%xmm1 #XMM1: ABGR7 ABGR6 ABGR5 ABGR4 \n"\
        SSE2_MASSAGE_RGBA32(ZERO)
#define SSE2_MASSAGE_RGBA32(ZERO) "\
        movdqa %%xmm0, %%xmm2           # XMM2: ABGR3 ABGR2 ABGR1 ABGR0 \n\
        punpcklbw %%xmm1, %%xmm0        # X0.l: A4 A0 B4 B0 G4 G0 R4 R0 \n\
        punpckhbw %%xmm1, %%xmm2        # X2.l: A6 A2 B6 B2 G6 G2 R6 R2 \n\
        movdqa %%xmm0, %%xmm1           # X1.l: A4 A0 B4 B0 G4 G0 R4 R0 \n\
        punpcklbw %%xmm2, %%xmm0        # X0.l: G6 G4 G2 G0 R6 R4 R2 R0 \n\
        punpckhbw %%xmm2, %%xmm1        # X1.l: G7 G5 G3 G1 R7 R5 R3 R1 \n\
        movdqa %%xmm0, %%xmm2           # X2.l: G6 G4 G2 G0 R6 R4 R2 R0 \n\
        punpcklbw %%xmm1, %%xmm0        # XMM0: G7.......G0 R7.......R0 \n\
        punpckhbw %%xmm1, %%xmm2        # XMM2: A7.......A0 B7.......B0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: G7.......G0 R7.......R0 \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: R7 R6 R5 R4 R3 R2 R1 R0 \n\
        punpckhbw %%xmm4, %%xmm1        # XMM1: G7 G6 G5 G4 G3 G2 G1 G0 \n\
        punpcklbw %%xmm4, %%xmm2        # XMM2: B7 B6 B5 B4 B3 B2 B1 B0 \n"

#define SSE2_LOAD_BGRA32(ZERO) "\
        movdqu -32("ESI","ECX",4),%%xmm0 #XMM0: ARGB3 ARGB2 ARGB1 ARGB0 \n\
        movdqu -16("ESI","ECX",4),%%xmm1 #XMM1: ARGB7 ARGB6 ARGB5 ARGB4 \n"\
        SSE2_MASSAGE_BGRA32(ZERO)
#define SSE2_MASSAGE_BGRA32(ZERO) "\
        movdqa %%xmm0, %%xmm2           # XMM2: ARGB3 ARGB2 ARGB1 ARGB0 \n\
        punpcklbw %%xmm1, %%xmm2        # X2.l: A4 A0 R4 R0 G4 G0 B4 B0 \n\
        punpckhbw %%xmm1, %%xmm0        # X0.l: A6 A2 R6 R2 G6 G2 B6 B2 \n\
        movdqa %%xmm2, %%xmm1           # X1.l: A4 A0 R4 R0 G4 G0 B4 B0 \n\
        punpcklbw %%xmm0, %%xmm2        # X2.l: G6 G4 G2 G0 B6 B4 B2 B0 \n\
        punpckhbw %%xmm0, %%xmm1        # X1.l: G7 G5 G3 G1 B7 B5 B3 B1 \n\
        movdqa %%xmm2, %%xmm0           # X0.l: G6 G4 G2 G0 B6 B4 B2 B0 \n\
        punpcklbw %%xmm1, %%xmm2        # XMM2: G7.......G0 B7.......B0 \n\
        punpckhbw %%xmm1, %%xmm0        # XMM0: A7.......A0 R7.......R0 \n\
        movdqa %%xmm2, %%xmm1           # XMM1: G7.......G0 B7.......B0 \n\
        punpcklbw %%xmm4, %%xmm0        # XMM0: R7 R6 R5 R4 R3 R2 R1 R0 \n\
        punpckhbw %%xmm4, %%xmm1        # XMM1: G7 G6 G5 G4 G3 G2 G1 G0 \n\
        punpcklbw %%xmm4, %%xmm2        # XMM2: B7 B6 B5 B4 B3 B2 B1 B0 \n"

#define SSE2_LOAD_ARGB32(ZERO) "\
        movdqu -32("ESI","ECX",4),%%xmm0 #XMM0: BGRA3 BGRA2 BGRA1 BGRA0 \n\
        movdqu -16("ESI","ECX",4),%%xmm1 #XMM1: BGRA7 BGRA6 BGRA5 BGRA4 \n"\
        SSE2_MASSAGE_ARGB32(ZERO)
#define SSE2_MASSAGE_ARGB32(ZERO) "\
        movdqa %%xmm0, %%xmm2           # XMM2: BGRA3 BGRA2 BGRA1 BGRA0 \n\
        punpcklbw %%xmm1, %%xmm0        # X0.l: B4 B0 G4 G0 R4 R0 A4 A0 \n\
        punpckhbw %%xmm1, %%xmm2        # X2.l: B6 B2 G6 G2 R6 R2 A6 A2 \n\
        movdqa %%xmm0, %%xmm1           # X1.l: B4 B0 G4 G0 R4 R0 A4 A0 \n\
        punpcklbw %%xmm2, %%xmm0        # X0.l: R6 R4 R2 R0 A6 A4 A2 A0 \n\
        punpckhbw %%xmm2, %%xmm1        # X1.l: R7 R5 R3 R1 A7 A5 A3 A1 \n\
        movdqa %%xmm0, %%xmm2           # X2.l: R6 R4 R2 R0 A6 A4 A2 A0 \n\
        punpcklbw %%xmm1, %%xmm0        # XMM0: R7.......G0 A7.......A0 \n\
        punpckhbw %%xmm1, %%xmm2        # XMM2: B7.......G0 G7.......G0 \n\
        movdqa %%xmm2, %%xmm1           # XMM1: B7.......B0 G7.......G0 \n\
        punpckhbw %%xmm4, %%xmm0        # XMM0: R7 R6 R5 R4 R3 R2 R1 R0 \n\
        punpcklbw %%xmm4, %%xmm1        # XMM1: G7 G6 G5 G4 G3 G2 G1 G0 \n\
        punpckhbw %%xmm4, %%xmm2        # XMM2: B7 B6 B5 B4 B3 B2 B1 B0 \n"

#define SSE2_LOAD_ABGR32(ZERO) "\
        movdqu -32("ESI","ECX",4),%%xmm0 #XMM0: RGBA3 RGBA2 RGBA1 RGBA0 \n\
        movdqu -16("ESI","ECX",4),%%xmm1 #XMM1: RGBA7 RGBA6 RGBA5 RGBA4 \n"\
        SSE2_MASSAGE_ABGR32(ZERO)
#define SSE2_MASSAGE_ABGR32(ZERO) "\
        movdqa %%xmm0, %%xmm2           # XMM2: RGBA3 RGBA2 RGBA1 RGBA0 \n\
        punpcklbw %%xmm1, %%xmm2        # X2.l: R4 R0 G4 G0 B4 B0 A4 A0 \n\
        punpckhbw %%xmm1, %%xmm0        # X0.l: R6 R2 G6 G2 B6 B2 A6 A2 \n\
        movdqa %%xmm2, %%xmm1           # X1.l: R4 R0 G4 G0 B4 B0 A4 A0 \n\
        punpcklbw %%xmm0, %%xmm2        # X2.l: B6 B4 B2 B0 A6 A4 A2 A0 \n\
        punpckhbw %%xmm0, %%xmm1        # X1.l: B7 B5 B3 B1 A7 A5 A3 A1 \n\
        movdqa %%xmm2, %%xmm0           # X0.l: B6 B4 B2 B0 A6 A4 A2 A0 \n\
        punpcklbw %%xmm1, %%xmm2        # XMM2: B7.......B0 A7.......A0 \n\
        punpckhbw %%xmm1, %%xmm0        # XMM0: R7.......R0 G7.......G0 \n\
        movdqa %%xmm0, %%xmm1           # XMM1: R7.......R0 G7.......G0 \n\
        punpckhbw %%xmm4, %%xmm0        # XMM0: R7 R6 R5 R4 R3 R2 R1 R0 \n\
        punpcklbw %%xmm4, %%xmm1        # XMM1: G7 G6 G5 G4 G3 G2 G1 G0 \n\
        punpckhbw %%xmm4, %%xmm2        # XMM2: B7 B6 B5 B4 B3 B2 B1 B0 \n"

/*************************************************************************/

#endif  /* ACLIB_IMG_X86_COMMON_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
