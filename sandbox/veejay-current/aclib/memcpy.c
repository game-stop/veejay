/*
 * memcpy.c - optimized memcpy() routines for aclib
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "ac.h"
#include "ac_internal.h"
#include <string.h>

/* Use memmove because memcpy isn't guaranteed to be ascending */
static void *(*memcpy_ptr)(void *, const void *, size_t) = memmove;

/*************************************************************************/

/* External interface */

void *ac_memcpy(void *dest, const void *src, size_t size)
{
    return (*memcpy_ptr)(dest, src, size);
}

/*************************************************************************/
/*************************************************************************/

/* Note the check for ARCH_X86 here: this is to prevent compilation of this
 * code on x86_64, since all x86_64 processors support SSE2, and because
 * this code is not set up to use the 64-bit registers for addressing on
 * x86_64. */

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)

/* MMX-optimized routine, intended for PMMX/PII processors.
 * Nonstandard instructions used:
 *     (CPUID.MMX)   MOVQ
 */

static void *memcpy_mmx(void *dest, const void *src, size_t bytes)
{
    asm("\
PENTIUM_LINE_SIZE = 32          # PMMX/PII cache line size              \n\
PENTIUM_CACHE_SIZE = 8192       # PMMX/PII total cache size             \n\
# Use only half because writes may touch the cache too (PII)            \n\
PENTIUM_CACHE_BLOCK = (PENTIUM_CACHE_SIZE/2 - PENTIUM_LINE_SIZE)        \n\
                                                                        \n\
        push %%ebx              # Save PIC register                     \n\
        push %%edi              # Save destination for return value     \n\
        cld                     # MOVS* should ascend                   \n\
                                                                        \n\
        mov $64, %%ebx          # Constant                              \n\
                                                                        \n\
        cmp %%ebx, %%ecx                                                \n\
        jb mmx.memcpy_last      # Just use movs if <64 bytes            \n\
                                                                        \n\
        # First align destination address to a multiple of 8 bytes      \n\
        mov $8, %%eax           # EAX <- (8-dest) & 7                   \n\
        sub %%edi, %%eax                                                \n\
        and $0b111, %%eax       # ... which is the number of bytes to copy\n\
        lea 0f, %%edx           # Use a computed jump--faster than a loop\n\
        sub %%eax, %%edx                                                \n\
        jmp *%%edx              # Execute 0-7 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
0:      sub %%eax, %%ecx        # Update count                          \n\
                                                                        \n\
        # Now copy data in blocks                                       \n\
0:      mov %%ecx, %%edx        # EDX <- ECX >> 6 (cache lines to copy) \n\
        shr $6, %%edx                                                   \n\
        jz mmx.memcpy_last      # <64 bytes left?  Skip to end          \n\
        cmp $PENTIUM_CACHE_BLOCK/64, %%edx                              \n\
        jb 1f                   # Limit size of block                   \n\
        mov $PENTIUM_CACHE_BLOCK/64, %%edx                              \n\
1:      mov %%edx, %%eax        # EAX <- EDX << 6 (bytes to copy)       \n\
        shl $6, %%eax                                                   \n\
        sub %%eax, %%ecx        # Update remaining count                \n\
        add %%eax, %%esi        # Point to end of region to be block-copied\n\
2:      test %%eax, -32(%%esi)  # Touch each cache line in reverse order\n\
        test %%eax, -64(%%esi)                                          \n\
        sub %%ebx, %%esi        # Update pointer                        \n\
        sub %%ebx, %%eax        # And loop                              \n\
        jnz 2b                                                          \n\
        # Note that ESI now points to the beginning of the block        \n\
3:      movq   (%%esi), %%mm0   # Do the actual copy, 64 bytes at a time\n\
        movq  8(%%esi), %%mm1                                           \n\
        movq 16(%%esi), %%mm2                                           \n\
        movq 24(%%esi), %%mm3                                           \n\
        movq 32(%%esi), %%mm4                                           \n\
        movq 40(%%esi), %%mm5                                           \n\
        movq 48(%%esi), %%mm6                                           \n\
        movq 56(%%esi), %%mm7                                           \n\
        movq %%mm0,   (%%edi)                                           \n\
        movq %%mm1,  8(%%edi)                                           \n\
        movq %%mm2, 16(%%edi)                                           \n\
        movq %%mm3, 24(%%edi)                                           \n\
        movq %%mm4, 32(%%edi)                                           \n\
        movq %%mm5, 40(%%edi)                                           \n\
        movq %%mm6, 48(%%edi)                                           \n\
        movq %%mm7, 56(%%edi)                                           \n\
        add %%ebx, %%esi        # Update pointers                       \n\
        add %%ebx, %%edi                                                \n\
        dec %%edx               # And loop                              \n\
        jnz 3b                                                          \n\
        jmp 0b                                                          \n\
                                                                        \n\
mmx.memcpy_last:                                                        \n\
        # Copy last <64 bytes, using the computed jump trick            \n\
        mov %%ecx, %%eax        # EAX <- ECX>>2                         \n\
        shr $2, %%eax                                                   \n\
        lea 0f, %%edx                                                   \n\
        sub %%eax, %%edx                                                \n\
        jmp *%%edx              # Execute 0-15 MOVSD's                  \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
0:      and $0b11, %%ecx        # ECX <- ECX & 3                        \n\
        lea 0f, %%edx                                                   \n\
        sub %%ecx, %%edx                                                \n\
        jmp *%%edx              # Execute 0-3 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
0:                                                                      \n\
        # All done!                                                     \n\
        emms                    # Clean up MMX state                    \n\
        pop %%edi               # Restore destination (return value)    \n\
        pop %%ebx               # Restore PIC register                  \n\
    " : /* no outputs */
      : "D" (dest), "S" (src), "c" (bytes)
      : "%eax", "%edx"
    );
    return dest;
}

#endif  /* HAVE_ASM_MMX && ARCH_X86 */

/*************************************************************************/

#if defined(HAVE_ASM_SSE) && defined(ARCH_X86)

/* SSE-optimized routine.  Backported from AMD64 routine below.
 * Nonstandard instructions used:
 *     (CPUID.CMOVE) CMOVA
 *     (CPUID.MMX)   MOVQ
 *     (CPUID.SSE)   MOVNTQ
 */

static void *memcpy_sse(void *dest, const void *src, size_t bytes)
{
    asm("\
        push %%ebx              # Save PIC register                     \n\
        push %%edi              # Save destination for return value     \n\
        cld                     # MOVS* should ascend                   \n\
                                                                        \n\
        cmp $64, %%ecx          # Skip block copy for small blocks      \n\
        jb sse.memcpy_last                                              \n\
                                                                        \n\
        mov $128, %%ebx         # Constant used later                   \n\
                                                                        \n\
        # First align destination address to a multiple of 8 bytes      \n\
        mov $8, %%eax           # EAX <- (8-dest) & 7                   \n\
        sub %%edi, %%eax                                                \n\
        and $0b111, %%eax       # ... which is the number of bytes to copy\n\
        lea 0f, %%edx           # Use a computed jump--faster than a loop\n\
        sub %%eax, %%edx                                                \n\
        jmp *%%edx              # Execute 0-7 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
0:      sub %%eax, %%ecx        # Update count                          \n\
                                                                        \n\
        cmp $0x10040, %%ecx     # Is this a large block? (0x10040 is an \n\
                                # arbitrary value where prefetching and \n\
                                # write combining seem to start becoming\n\
                                # faster)                               \n\
        jae sse.memcpy_bp       # Yup, use prefetch copy                \n\
                                                                        \n\
sse.memcpy_small:               # Small block copy routine--no prefetch \n"
#if 0
"       mov %%ecx, %%edx        # EDX <- bytes to copy / 8              \n\
        shr $3, %%edx                                                   \n\
        mov %%edx, %%eax        # Leave remainder in ECX for later      \n\
        shl $3, %%eax                                                   \n\
        sub %%eax, %%ecx                                                \n\
        .align 16                                                       \n\
0:      movq (%%esi), %%mm0     # Copy 8 bytes of data                  \n\
        movq %%mm0, (%%edi)                                             \n\
        add $8, %%esi           # Update pointers                       \n\
        add $8, %%edi                                                   \n\
        dec %%edx               # And loop                              \n\
        jg 0b                                                           \n\
        jmp sse.memcpy_last     # Copy any remaining bytes              \n\
                                                                        \n\
        nop                     # Align loops below                     \n"
#else
"       # It appears that a simple rep movs is faster than cleverness   \n\
        # with movq...                                                  \n\
        mov %%ecx, %%edx        # EDX <- ECX & 3                        \n\
        and $0b11, %%edx                                                \n\
        shr $2, %%ecx           # ECX <- ECX >> 2                       \n\
        rep movsl               # Copy away!                            \n\
        mov %%edx, %%ecx        # Take care of last 0-3 bytes           \n\
        rep movsb                                                       \n\
        jmp sse.memcpy_end      # And exit                              \n\
                                                                        \n\
        .align 16                                                       \n\
        nop                                                             \n\
        nop                                                             \n"
#endif
"sse.memcpy_bp:                 # Block prefetch copy routine           \n\
0:      mov %%ecx, %%edx        # EDX: temp counter                     \n\
        shr $6, %%edx           # Divide by cache line size (64 bytes)  \n\
        cmp %%ebx, %%edx        # ... and cap at 128 (8192 bytes)       \n\
        cmova %%ebx, %%edx                                              \n\
        shl $3, %%edx           # EDX <- cache lines to copy * 8        \n\
        mov %%edx, %%eax        # EAX <- cache lines to preload * 8     \n\
                                #        (also used as memory offset)   \n\
1:      test %%eax, -64(%%esi,%%eax,8)  # Preload cache lines in pairs  \n\
        test %%eax, -128(%%esi,%%eax,8) # (going backwards)             \n\
        # (note that test %%eax,... seems to be faster than prefetchnta \n\
        #  on x86)                                                      \n\
        sub $16, %%eax          # And loop                              \n\
        jg 1b                                                           \n\
                                                                        \n\
        # Then copy--forward, which seems to be faster than reverse for \n\
        # certain alignments                                            \n\
        xor %%eax, %%eax                                                \n\
2:      movq (%%esi,%%eax,8), %%mm0 # Copy 8 bytes and loop             \n\
        movntq %%mm0, (%%edi,%%eax,8)                                   \n\
        inc %%eax                                                       \n\
        cmp %%edx, %%eax                                                \n\
        jb 2b                                                           \n\
                                                                        \n\
        # Finally, update pointers and count, and loop                  \n\
        shl $3, %%edx           # EDX <- bytes copied                   \n\
        add %%edx, %%esi                                                \n\
        add %%edx, %%edi                                                \n\
        sub %%edx, %%ecx                                                \n\
        cmp $64, %%ecx          # At least one cache line left?         \n\
        jae 0b                  # Yup, loop                             \n\
                                                                        \n\
sse.memcpy_last:                                                        \n\
        # Copy last <64 bytes, using the computed jump trick            \n\
        mov %%ecx, %%eax        # EAX <- ECX>>2                         \n\
        shr $2, %%eax                                                   \n\
        lea 0f, %%edx                                                   \n\
        sub %%eax, %%edx                                                \n\
        jmp *%%edx              # Execute 0-15 MOVSD's                  \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
        movsd                                                           \n\
0:      and $0b11, %%ecx        # ECX <- ECX & 3                        \n\
        lea sse.memcpy_end, %%edx                                       \n\
        sub %%ecx, %%edx                                                \n\
        jmp *%%edx              # Execute 0-3 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
                                                                        \n\
sse.memcpy_end:                                                         \n\
        # All done!                                                     \n\
        emms                    # Clean up after MMX instructions       \n\
        sfence                  # Flush the write buffer                \n\
        pop %%edi               # Restore destination (return value)    \n\
        pop %%ebx               # Restore PIC register                  \n\
    " : /* no outputs */
      : "D" (dest), "S" (src), "c" (bytes)
      : "%eax", "%edx"
    );
    return dest;
}

#endif  /* HAVE_ASM_SSE && ARCH_X86 */

/*************************************************************************/

#if defined(HAVE_ASM_SSE2) && defined(ARCH_X86_64)

/* AMD64-optimized routine, using SSE2.  Derived from AMD64 optimization
 * guide section 5.13: Appropriate Memory Copying Routines.
 * Nonstandard instructions used:
 *     (CPUID.CMOVE) CMOVA
 *     (CPUID.SSE2)  MOVDQA, MOVDQU, MOVNTDQ
 *
 * Note that this routine will also run more or less as-is (modulo register
 * names and label(%%rip) references) on x86 CPUs, but tests have shown the
 * SSE1 version above to be faster.
 */

/* The block copying code--macroized because we use two versions of it
 * depending on whether the source is 16-byte-aligned or not.  Pass either
 * movdqa or movdqu (unquoted) for the parameter. */
#define AMD64_BLOCK_MEMCPY(movdq) \
"       # First prefetch (note that if we end on an odd number of cache \n\
        # lines, we skip prefetching the last one--faster that way than \n\
        # prefetching line by line or treating it as a special case)    \n\
0:      mov %%ecx, %%edx        # EDX: temp counter (always <32 bits)   \n\
        shr $6, %%edx           # Divide by cache line size (64 bytes)  \n\
        cmp %%ebx, %%edx        # ... and cap at 128 (8192 bytes)       \n\
        cmova %%ebx, %%edx                                              \n\
        shl $3, %%edx           # EDX <- cache lines to copy * 8        \n\
        mov %%edx, %%eax        # EAX <- cache lines to preload * 8     \n\
                                #        (also used as memory offset)   \n\
1:      prefetchnta -64(%%rsi,%%rax,8)  # Preload cache lines in pairs  \n\
        prefetchnta -128(%%rsi,%%rax,8) # (going backwards)             \n\
        sub $16, %%eax          # And loop                              \n\
        jg 1b                                                           \n\
                                                                        \n\
        # Then copy--forward, which seems to be faster than reverse for \n\
        # certain alignments                                            \n\
        xor %%eax, %%eax                                                \n\
2:      " #movdq " (%%rsi,%%rax,8), %%xmm0 # Copy 16 bytes and loop     \n\
        movntdq %%xmm0, (%%rdi,%%rax,8)                                 \n\
        add $2, %%eax                                                   \n\
        cmp %%edx, %%eax                                                \n\
        jb 2b                                                           \n\
                                                                        \n\
        # Finally, update pointers and count, and loop                  \n\
        shl $3, %%edx           # EDX <- bytes copied                   \n\
        add %%rdx, %%rsi                                                \n\
        add %%rdx, %%rdi                                                \n\
        sub %%rdx, %%rcx                                                \n\
        cmp $64, %%rcx          # At least one cache line left?         \n\
        jae 0b                  # Yup, loop                             \n"

static void *memcpy_amd64(void *dest, const void *src, size_t bytes)
{
    asm("\
        push %%rdi              # Save destination for return value     \n\
        cld                     # MOVS* should ascend                   \n\
                                                                        \n\
        cmp $64, %%rcx          # Skip block copy for small blocks      \n\
        jb amd64.memcpy_last                                            \n\
                                                                        \n\
        mov $128, %%ebx         # Constant used later                   \n\
                                                                        \n\
        # First align destination address to a multiple of 16 bytes     \n\
        mov $8, %%eax           # EAX <- (8-dest) & 7                   \n\
        sub %%edi, %%eax        # (we don't care about the top 32 bits) \n\
        and $0b111, %%eax       # ... which is the number of bytes to copy\n\
        lea 0f(%%rip), %%rdx    # Use a computed jump--faster than a loop\n\
        sub %%rax, %%rdx                                                \n\
        jmp *%%rdx              # Execute 0-7 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
0:      sub %%rax, %%rcx        # Update count                          \n\
        test $0b1000, %%edi     # Is destination not 16-byte aligned?   \n\
        je 1f                                                           \n\
        movsq                   # Then move 8 bytes to align it         \n\
        sub $8, %%rcx                                                   \n\
                                                                        \n\
1:      cmp $0x38000, %%rcx     # Is this a large block? (0x38000 is an \n\
                                # arbitrary value where prefetching and \n\
                                # write combining seem to start becoming\n\
                                # faster)                               \n\
        jb amd64.memcpy_small   # Nope, use small copy (no prefetch/WC) \n\
        test $0b1111, %%esi     # Is source also 16-byte aligned?       \n\
                                # (use ESI to save a REX prefix byte)   \n\
        jnz amd64.memcpy_normal_bp # Nope, use slow copy                \n\
        jmp amd64.memcpy_fast_bp # Yup, use fast copy                   \n\
                                                                        \n\
amd64.memcpy_small:             # Small block copy routine--no prefetch \n\
        mov %%ecx, %%edx        # EDX <- bytes to copy / 16             \n\
        shr $4, %%edx           # (count known to fit in 32 bits)       \n\
        mov %%edx, %%eax        # Leave remainder in ECX for later      \n\
        shl $4, %%eax                                                   \n\
        sub %%eax, %%ecx                                                \n\
        .align 16                                                       \n\
0:      movdqu (%%rsi), %%xmm0  # Copy 16 bytes of data                 \n\
        movdqa %%xmm0, (%%rdi)                                          \n\
        add $16, %%rsi          # Update pointers                       \n\
        add $16, %%rdi                                                  \n\
        dec %%edx               # And loop                              \n\
        jnz 0b                                                          \n\
        jmp amd64.memcpy_last   # Copy any remaining bytes              \n\
                                                                        \n\
        .align 16                                                       \n\
        nop                                                             \n\
        nop                                                             \n\
amd64.memcpy_fast_bp:           # Fast block prefetch loop              \n"
AMD64_BLOCK_MEMCPY(movdqa)
"       jmp amd64.memcpy_last   # Copy any remaining bytes              \n\
                                                                        \n\
        .align 16                                                       \n\
        nop                                                             \n\
        nop                                                             \n\
amd64.memcpy_normal_bp:         # Normal (unaligned) block prefetch loop\n"
AMD64_BLOCK_MEMCPY(movdqu)
"                                                                       \n\
amd64.memcpy_last:                                                      \n\
        # Copy last <64 bytes, using the computed jump trick            \n\
        mov %%ecx, %%eax        # EAX <- ECX>>3                         \n\
        shr $3, %%eax                                                   \n\
        lea 0f(%%rip), %%rdx                                            \n\
        add %%eax, %%eax        # Watch out, MOVSQ is 2 bytes!          \n\
        sub %%rax, %%rdx                                                \n\
        jmp *%%rdx              # Execute 0-7 MOVSQ's                   \n\
        movsq                                                           \n\
        movsq                                                           \n\
        movsq                                                           \n\
        movsq                                                           \n\
        movsq                                                           \n\
        movsq                                                           \n\
        movsq                                                           \n\
0:      and $0b111, %%ecx       # ECX <- ECX & 7                        \n\
        lea 0f(%%rip), %%rdx                                            \n\
        sub %%rcx, %%rdx                                                \n\
        jmp *%%rdx              # Execute 0-7 MOVSB's                   \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
        movsb                                                           \n\
0:                                                                      \n\
        # All done!                                                     \n\
        emms                    # Clean up after MMX instructions       \n\
        sfence                  # Flush the write buffer                \n\
        pop %%rdi               # Restore destination (return value)    \n\
    " : /* no outputs */
      : "D" (dest), "S" (src), "c" (bytes)
      : "%rax", "%rbx", "%rdx"
    );
    return dest;
}

#endif  /* HAVE_ASM_SSE2 && ARCH_X86_64 */

/*************************************************************************/

/* Initialization routine. */

int ac_memcpy_init(int accel)
{
    memcpy_ptr = memmove;

#if defined(HAVE_ASM_MMX) && defined(ARCH_X86)
    if (HAS_ACCEL(accel, AC_MMX))
        memcpy_ptr = memcpy_mmx;
#endif

#if defined(HAVE_ASM_SSE) && defined(ARCH_X86)
    if (HAS_ACCEL(accel, AC_CMOVE|AC_SSE))
        memcpy_ptr = memcpy_sse;
#endif

#if defined(HAVE_ASM_SSE2) && defined(ARCH_X86_64)
    if (HAS_ACCEL(accel, AC_CMOVE|AC_SSE2))
        memcpy_ptr = memcpy_amd64;
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
