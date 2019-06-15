/*
 * Copyright 2013 Harm Hanemaaijer <fgenfb@yahoo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifdef CONFIG_THUMB
#define W(instr) instr.w
#define THUMB(instr...)	instr
#define ARM(instr...)
#else
#define W(instr) instr
#define THUMB(instr...)
#define ARM(instr...) instr
#endif

/*
 * In practice, because the way NEON is configured on most systems,
 * specifying alignment hints for NEON instructions doesn't seem
 * to improve performance, or even degrade performance in some cases.
 * However, actually having the address aligned to an element
 * boundary or greater is beneficial.
 */
#define NEON_ALIGN(n)
/* #define NEON_ALIGN(n) :n */

/* Prevent the stack from becoming executable */
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

.text
.syntax unified
.arch armv7a
.fpu neon

.macro asm_function function_name
    .global \function_name
.func \function_name
.type \function_name, function
ARM(    .p2align 5      )
THUMB(  .p2align 2      )
\function_name:
.endm

/*
 * The following memcpy implementation is optimized with a fast path
 * for common, word aligned cases and optionally use unaligned access for
 * small sizes.
 *
 * - line_size is the cache line size used for prefetches. Must be 64 or 32.
 * - prefetch_distance is the number of cache lines to look ahead and must be
 *   >= 2.
 * - write_align is the write alignment enforced before the main loop for larger
 *   sizes (word aligned case) and must be 0, 16, 32, or 64.
 * - aligned_access must be 0 or 1. When enabled, no unaligned memory accesses
 *   will occur. Both small size tresholds for unaligned access are not used
 *   in this case.
 */

/* The threshold size for using the fast path for the word-aligned case. */
#define FAST_PATH_THRESHOLD 256
/* The threshold size for using the small size path for the word-aligned case. */
#define SMALL_SIZE_THRESHOLD 15
/*
 * The threshold size for using the small size path for the unaligned case.
 * Unaligned memory accesses will be generated for requests smaller or equal to
 * this size.
 */
#define UNALIGNED_SMALL_SIZE_THRESHOLD 64
/*
 * The threshold size for using the small size path when both the source and
 * the destination are unaligned. Unaligned memory accesses will be generated
 * for requests smaller of equal to this size.
 */
#define BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD 32

/*
 * For a code-reduced version, define all four of the above constants to 0,
 * eliminating the fast path and small size special cases. With Thumb2
 * enabled, this resulted in a reduction in code size from 1150 to 824 bytes,
 * at the cost of lower performance for smaller sizes.
 */
// #define FAST_PATH_THRESHOLD 0
// #define SMALL_SIZE_THRESHOLD 0
// #define UNALIGNED_SMALL_SIZE_THRESHOLD 0
// #define BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD 0

/*
 * EARLY_PREFETCHES is used in the fast path implementation.
 * The optimal value for EARLY_PREFETCHES was determined empirically.
 * It is equal to prefetch_distance + 1 for line_size 32.
 *            and prefetch_distance - 1 for line_size 64.
 */
#define EARLY_PREFETCHES (\prefetch_distance - (\line_size / 32) * 2 + 3)

#if FAST_PATH_THRESHOLD > 0
#define FAST_PATH(instr...) instr
#define NO_FAST_PATH(instr...)
#else
#define FAST_PATH(instr...)
#define NO_FAST_PATH(instr...) instr
#endif


/* Helper macro for the fast-path implementation. */

.macro copy_16_bytes bytes_to_go, line_size, prefetch_distance
#ifdef CONFIG_THUMB
                /*
                 * When Thumb2 mode is enabled, the ldmia/stmia instructions
                 * will be 16-bit, and the preload instruction will be
                 * 32-bit, so we only need one 32-bit wide nop instruction
                 * when there's no preload, for a total size of two words.
                 */
.if \bytes_to_go >= (EARLY_PREFETCHES * \line_size) && \
 (\bytes_to_go % \line_size) == 0
        	pld     [r1, ip]
                ldmia   r1!, {r3, r4, r5, r6}
                stmia   r0!, {r3, r4, r5, r6}
.else
                ldmia   r1!, {r3, r4, r5, r6}
        W(	nop	)
                stmia   r0!, {r3, r4, r5, r6}
.endif
#else
                /*
                 * When ARM mode is enabled, every instruction is one word,
                 * so make sure the entire block is four instructions.
                 */
.if \bytes_to_go >= (EARLY_PREFETCHES * \line_size) && \
(\bytes_to_go % \line_size) == 0
       		pld     [r1, ip]
.else
                nop
.endif
                ldmia   r1!, {r3, r4, r5, r6}
                nop
                stmia   r0!, {r3, r4, r5, r6}
#endif
.endm


/* Helper macro implementing unaligned copy. */

.macro unaligned_copy shift, line_size, prefetch_distance, write_align, \
aligned_access
		/*
		 * ip is the aligned source base address.
		 * r3 is a word of data from the source.
		 */
.if \write_align > 0
		cmp	r2, #(32 + \write_align - 4)
.else
		cmp	r2, #32
.endif
		push 	{r5}
		blt	55f
		subs	r2, r2, #32

		/* Handle write alignment. */
.if \write_align > 0
.if \write_align == 8
		tst	r0, #4
	        mov	r4, r3, lsr #\shift
                ldrne	r3, [r1], #4
		subne	r2, r2, #4
                orrne   r4, r4, r3, lsl #(32 - \shift)
                strne   r4, [r0], #4
.else
		ands	r5, r0, #(\write_align - 1)
		rsb	r5, r5, #\write_align
		beq	59f
		sub	r2, r2, r5

58:             movs    r4, r3, lsr #\shift
                ldr	r3, [r1], #4
                subs    r5, r5, #4
                orr     r4, r4, r3, lsl #(32 - \shift)
                str     r4, [r0], #4
                bgt     58b
59:
.endif
.endif

                /*
                 * Assume a preload at aligned base + line_size will
                 * be useful.
                 */
		pld     [ip, #\line_size]
		push	{r6-r11}
		mov	r11, r3

		mov	r4, ip
                add     r5, r1, #(\prefetch_distance * \line_size)
                subs    r2, r2, #(\prefetch_distance * \line_size)
                bic     r3, r5, #31
                add     r4, r4, #(2 * \line_size)
                blt     54f
                cmp     r4, r3
                sub     ip, r3, r1
                /*
                 * "Catch-up" the early preloads (which have been performed up
                 * to aligned source address + line_size) to the preload offset
                 * used in the main loop.
                 */
                bge     52f
51:             adds    r4, r4, #\line_size		/* Thumb16 */
                cmp     r4, r3
                pld     [r4, #(- \line_size)]
                blt     51b
52:
		/*
		 * Note that when L1_CACHE_BYTES is 64, we are
		 * prefetching every 32 bytes. Although not optimal
		 * there doesn't seem to be big penalty for the extra
		 * preload instructions and it prevents greater
		 * code size and complexity.
		 */
53:		pld	[r1, ip]
54:
		ldmia	r1!, {r4-r7}
		mov	r3, r11, lsr #\shift
		ldmia	r1!, {r8-r11}
		orr	r3, r3, r4, lsl #(32 - \shift)
		movs	r4, r4, lsr #\shift	/* Thumb16 */
		orr	r4, r4, r5, lsl #(32 - \shift)
		movs	r5, r5, lsr #\shift	/* Thumb16 */
		orr	r5, r5, r6, lsl #(32 - \shift)
		movs	r6, r6, lsr #\shift	/* Thumb16 */
		orr	r6, r6, r7, lsl #(32 - \shift)
		movs	r7, r7, lsr #\shift	/* Thumb16 */
		orr	r7, r7, r8, lsl #(32 - \shift)
		mov	r8, r8, lsr #\shift
		orr	r8, r8, r9, lsl #(32 - \shift)
		mov	r9, r9, lsr #\shift
		orr	r9, r9, r10, lsl #(32 - \shift)
		mov	r10, r10, lsr #\shift
		orr	r10, r10, r11, lsl #(32 - \shift)
		subs	r2, r2, #32
		stmia	r0!, {r3-r10}
		bge	53b
		cmn	r2, #(\prefetch_distance * \line_size)
		bge	54b
		/* Correct the count. */
		adds	r2, r2, #(\prefetch_distance * \line_size + 32)

		mov	r3, r11
		pop	{r6-r11}

55:		bics	r5, r2, #3
		beq	57f

56:             movs    r4, r3, lsr #\shift
                ldr	r3, [r1], #4
                subs    r5, r5, #4
                orr     r4, r4, r3, lsl #(32 - \shift)
                str     r4, [r0], #4
                bgt     56b

57:		pop	{r5}
		pop	{r4}
		subs	r1, r1, #((32 - \shift) / 8)
.if \aligned_access == 1
		b	7b
.else
		b	3b
.endif
.endm


/* The main memcpy function macro. */

.macro memcpy_variant line_size, prefetch_distance, write_align, \
aligned_access

.if \aligned_access == 1
		cmp	r2, #3
.else
NO_FAST_PATH(	cmp	r2, #3	)
.endif
		orr	r3, r0, r1
.if \aligned_access == 1
		push	{r0}
		ble	7f
.else
NO_FAST_PATH(	push	{r0}	)
NO_FAST_PATH(	ble	3f	)
.endif
		bic	ip, r1, #(\line_size - 1)
		tst	r3, #3
		pld	[ip]
.if \aligned_access == 1
FAST_PATH(	bne	30f	)
.else
FAST_PATH(	push	{r0}	)
FAST_PATH(	bne	7f	)	/* Unaligned source or destination. */
.endif
FAST_PATH(	cmp	r2, #FAST_PATH_THRESHOLD )
FAST_PATH(	bgt     10f	)
NO_FAST_PATH(	bne	30f	)
#if FAST_PATH_THRESHOLD == 0
		/*
		 * When the fast path is disabled, check whether there are
		 * enough bytes for alignment, and jump to the main handling
		 * code for larger sizes.
		 */
.if \write_align > 0
 		cmp	r2, #(\write_align - 4)
		bge	10f
.endif
		push	{r4}
		b	18f
#endif

		/*
		 * Fast path for aligned copies of size <= FAST_PATH_THRESHOLD.
		 */
#if FAST_PATH_THRESHOLD > 0
#if SMALL_SIZE_THRESHOLD == 15
                bics    r3, r2, #15
                pld     [ip, #\line_size]
		/* Jump for small sizes <= 15 bytes. */
		beq	5f
#else
		cmp	r2, #SMALL_SIZE_THRESHOLD
                pld     [ip, #\line_size]
		/* Jump for small sizes <= SMALL_SIZE_THRESHOLD bytes. */
		ble	5f
		bic	r3, r2, #15
#endif

9:		/*
		 * This is the entry-point into the fast path from
		 * an unaligned request that has been aligned.
		 */
		push	{r4, r5, r6}

                /*
                 * Use a heuristic to determine whether the preload
                 * at aligned_base + 2 * line_size will be useful.
                 */
.if EARLY_PREFETCHES >= 3
                cmp     r2, #(2 * \line_size - \line_size / 2)
.endif
                add     r5, ip, #(EARLY_PREFETCHES * \line_size)
.if EARLY_PREFETCHES >= 3
                blt     1f
.endif
.if EARLY_PREFETCHES == 3
                pld     [ip, #(2 * \line_size)] )
.endif
.if EARLY_PREFETCHES == 4
                cmp     r2, #(3 * \line_size - \line_size / 2)
                pld     [ip, #(2 * \line_size)]
                blt     1f
                pld     [ip, #(3 * \line_size)]
.endif
.if EARLY_PREFETCHES == 5
                cmp     r2, #(3 * \line_size - \line_size / 2)
                pld     [ip, #(2 * \line_size)]
                blt     1f
                cmp     r2, #(4 * \line_size - \line_size / 2)
                pld     [ip, #(3 * \line_size)]
                blt     1f
                pld     [ip, #(4 * \line_size)]
.endif

1:              /*
                 * Set r5 so that the next preload will occur
                 * exactly at aligned_base + EARLY_PREFETCHES *
                 * line_size. For example, if line_size is 64
                 * and the number of bytes is 240, the next preload
                 * will occur after processing 48 bytes, which is derived
                 * from the formula r3 & (line_size - 1),
                 * where r3 is equal to number_of_bytes & (~15).
                 */
                rsb     r4, r3, #256
        	subs    r5, r5, r1
        	and     ip, r3, #(\line_size - 1)
                subs    r2, r2, r3		/* Thumb16 */
THUMB(		lsrs    r4, r4, #1	)	/* Thumb16 */
        	sub     ip, r5, ip
                add     pc, pc, r4
                nop
                /* >= 256 bytes to go. */
                copy_16_bytes 256, \line_size, \prefetch_distance
                /* >= 240 bytes go. */
                copy_16_bytes 240, \line_size, \prefetch_distance
                /* >= 224 bytes to go. */
                copy_16_bytes 224, \line_size, \prefetch_distance
                /* >= 204 bytes go. */
                copy_16_bytes 204, \line_size, \prefetch_distance
                /* >= 192 bytes to go. */
                copy_16_bytes 192, \line_size, \prefetch_distance
                /* >= 176 bytes go. */
                copy_16_bytes 176, \line_size, \prefetch_distance
                /* >= 160 bytes to go. */
                copy_16_bytes 160, \line_size, \prefetch_distance
                /* >= 144 bytes go. */
                copy_16_bytes 144, \line_size, \prefetch_distance
                /* >= 128 bytes to go. */
                copy_16_bytes 128, \line_size, \prefetch_distance
                /* >= 112 bytes go. */
                copy_16_bytes 112, \line_size, \prefetch_distance
                /* >= 96 bytes to go. */
                copy_16_bytes 96, \line_size, \prefetch_distance
                /* >= 80 bytes to go. */
                copy_16_bytes 80, \line_size, \prefetch_distance
                /* >= 64 bytes to go. */
                copy_16_bytes 64, \line_size, \prefetch_distance
                /* >= 48 bytes to go. */
                copy_16_bytes 48, \line_size, \prefetch_distance
                /* >= 32 bytes to go. */
                copy_16_bytes 32, \line_size, \prefetch_distance
                /* At this point there are 16 to 31 bytes to go. */
                tst     r2, #15
                ldmia   r1!, {r3, r4, r5, r6}
                cmpne   r2, #8
                /*
                 * If r2 == 8, we need to clear the eq flag while
                 * making sure carry remains set.
                 */
                tsteq   r2, #15
                stmia   r0!, {r3, r4, r5, r6}
                /*
                 * The equal flag is set if there are no bytes left.
                 * The carry flag is set is there are >= 8 bytes left.
                 */
		pop	{r4, r5, r6}
                beq     4f

2:
		/*
		 * ARM mode imposes restrictions on the registers used
		 * in double-word loads and stored so we have to use
		 * single-word operations.
		 */
.if \aligned_access == 0
	ARM(	ldrcs	r3, [r1], #4	)
	ARM(	ldrcs	ip, [r1], #4	)
	ARM(	strcs	r3, [r0], #4	)
	ARM(	strcs	ip, [r0], #4	)
	THUMB(	ldrdcs  r3, ip, [r1], #8	)
	THUMB(	strdcs  r3, ip, [r0], #8	)
.else
		ldrcs	r3, [r1], #4
		ldrcs	ip, [r1], #4
		strcs	r3, [r0], #4
		strcs	ip, [r0], #4
.endif
                tst     r2, #4
                ldrne   ip, [r1], #4
                strne   ip, [r0], #4
                tst     r2, #3
                popeq	{r0}
                bxeq	lr

	        /*
		 * Handle the last up to three bytes. Unaligned access
		 * make take place if source or destination is not
		 * half-word aligned.
		 */
3:		movs    r2, r2, lsl #31
                ldrhcs  r3, [r1], #2
                strhcs  r3, [r0], #2
                ldrbne  r3, [r1], #1
                strbne  r3, [r0], #1
4:		pop	{r0}
		bx	lr

5:		/*
		 * Sizes <= SMALL_SIZE_THRESHOLD bytes, both source and
		 * destination aligned.
		 */
#if SMALL_SIZE_THRESHOLD <= 15
		cmp	r2, #8		/* cs if r2 >= 8. */
		b	2b
#else
101:		tst	r2, #4
		ldrne	r3, [r1], #4
		subne	r2, r2, #4
		strne	r3, [r0], #4
		cmp	r2, #8
		blt	3b
6:		cmp	r2, #16
		ldr	r3, [r1], #4
		ldr	ip, [r1], #4
		str	r3, [r0], #4
		sub	r2, r2, #8
		str	ip, [r0], #4
		bge	6b
		cmp	r2, #0
		popeq	{r0}
		bxeq	lr
		b	3b
#endif

#endif	/* FAST_PATH_THRESHOLD > 0 */

.if \aligned_access == 1
		/*
		 * Handle the last up to three bytes avoiding
		 * unaligned memory access.
		 */
7:		movs    r2, r2, lsl #31
                ldrbcs  r3, [r1], #1
                ldrbcs  ip, [r1], #1
                strbcs  r3, [r0], #1
                strbcs  ip, [r0], #1
                ldrbne  r3, [r1], #1
                strbne  r3, [r0], #1
		pop	{r0}
		bx	lr
.endif

#if FAST_PATH_THRESHOLD > 0
.if \aligned_access == 0
7:		/*
		 * Unaligned source or destination. There are seperate small
		 * size thresholds for when both source and destination are
		 * unaligned and the other case.
		 */
		tst	r0, #3
		mov	r3, #UNALIGNED_SMALL_SIZE_THRESHOLD
		tstne	r1, #3
		movne	r3, #BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD
		cmp	r2, r3
		bgt	30f

		/* Small sizes, unaligned case. Use single word load/stores. */
#if SMALL_SIZE_THRESHOLD >= 16
		/* Use the identical code path already defined above. */
		b	101b
#else
		tst	r2, #4
		ldrne	r3, [r1], #4
		subne	r2, r2, #4
		strne	r3, [r0], #4
		cmp	r2, #8
		blt	3b
8:		cmp	r2, #16
		ldr	r3, [r1], #4
		ldr	ip, [r1], #4
		str	r3, [r0], #4
		sub	r2, r2, #8
		str	ip, [r0], #4
		bge	8b
		b	3b
#endif
.endif
#endif		/* FAST_PATH_THRESHOLD > 0 */

10:		/*
		 * This is the start of the handling of larger sizes for
		 * aligned copies.
		 *
		 * Size > FAST_PATH_THRESHOLD (256).
		 * ip is the line_sized aligned source address for preloads.
		 */

.if \write_align >= 16
		ands	r3, r0, #(\write_align - 1)
		push	{r4}
		rsb	r3, r3, #\write_align
		beq	17f
		push	{lr}
		bl	20f
		pop	{lr}
17:
.elseif \write_align == 8
		/*
		 * For write alignment of 8, it is quickest to do a simple
		 * conditional load/store.
		 */
		tst	r0, #4
		push	{r4}
		ldrne	r3, [r1], #4
		subne	r2, r2, #4
		strne	r3, [r0], #4
.else
		push	{r4}
.endif

18:
.if (FAST_PATH_THRESHOLD - (\write_align - 4)) < \line_size
		cmp	r2, #\line_size
		blt	15f
.endif
		subs	r2, r2, #\line_size

16:		/*
		 * This is the entry-point when source and destination were
		 * initially unaligned but are now aligned because they had
		 * the same alignment within a word. Write alignment and
		 * size check has already been handled.
		 */

		push	{r5-r11}

                /*
                 * Assume a preload at aligned base + line_size will
                 * be useful.
                 */
		mov	r4, ip
		pld     [ip, #\line_size]
                add     r5, r1, #(\prefetch_distance * \line_size)
                subs    r2, r2, #(\prefetch_distance * \line_size)
                bic     r3, r5, #(\line_size - 1)
                add     r4, r4, #(2 * \line_size)
                blt     14f
                cmp     r4, r3
                sub     ip, r3, r1
                /*
                 * "Catch-up" the early preloads (which have been performed up
                 * to aligned source address + line_size) to the preload offset
                 * used in the main loop.
                 */
                bge     12f
11:             adds    r4, r4, #\line_size		/* Thumb16 */
                cmp     r4, r3
                pld     [r4, #(- \line_size)]
                blt     11b
12:

		/*
		 * The main loop for large sizes. Copy 32 bytes at a time
		 * using ldmia/stmia while prefetching a 32-byte aligned
		 * address for line size 32, or 64 bytes at a time while
		 * prefetching a 64-byte aligned address for line size 64.
		 */
13:		pld     [r1, ip]
14:
.if \line_size == 32
		ldmia   r1!, {r4-r7}
		subs    r2, r2, #32
		ldmia   r1!, {r8-r11}
		stmia   r0!, {r4-r7}
		stmia   r0!, {r8-r11}
.else
		ldmia   r1!, {r4-r11}
		subs    r2, r2, #64
		stmia   r0!, {r4-r11}
		ldmia   r1!, {r4-r11}
		stmia   r0!, {r4-r11}
.endif
		bge	13b
		cmn	r2, #(\prefetch_distance * \line_size)
		bge	14b
		/* Correct the count. */
		adds	r2, r2, #((\prefetch_distance + 1) * \line_size)
		pop	{r5-r11}

15:		ands	r3, r2, #60
.if \write_align <= 8
		/*
		 * When the subroutine is not used for write alignment, the
		 * subroutine will only be called once, so branch without
		 * linking.
		 */
		bne	20f
19:
.else
		mov	ip, lr
		blne	20f
		mov	lr, ip
.endif
		pop	{r4}
#if FAST_PATH_THRESHOLD > 0
		cmp	r2, #0
		bne	3b
#else
	ARM(	cmp	r2, #0	)
	ARM(	beq	4f	)
	THUMB(	cbz	r2, 4f	)
	        /* Handle the last up to three bytes. */
3:		movs    r2, r2, lsl #31
                ldrhcs  r3, [r1], #2
                strhcs  r3, [r0], #2
                ldrbne  r3, [r1], #1
                strbne  r3, [r0], #1
4:
#endif
		pop	{r0}
		bx	lr

                /*
                 * Subroutine that copies a multiple of 4 bytes of size
                 * r3 from 0 to 64 or 32 bytes. r2 is decremented by the
		 * number of bytes copied.
                 */
20:		tst     r3, #4
                sub     r2, r2, r3
                ldrne   r4, [r1], #4
                subne   r3, r3, #4
                strne   r4, [r0], #4
.if \write_align <= 32 && \line_size == 32
                rsb     r3, r3, #32
.else
                rsb     r3, r3, #64
.endif
		/*
		 * These ldmia/stmia instructions are 16-bit on Thumb2,
		 * 32-bit on ARM.
		 */
	THUMB(	lsrs    r3, r3, #1	)
                add     pc, pc, r3
                nop
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
.if \write_align > 32 || \line_size > 32
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
                ldmia   r1!, {r3, r4}
                stmia   r0!, {r3, r4}
.endif
.if \write_align <= 8
		b	19b
.else
		mov	pc, lr
.endif

30:		/*
		 * Unaligned case. Align the destination.
		 * Number of bytes is > UNALIGNED_SMALL_SIZE_THRESHOLD.
		 * Note: This may use unaligned access.
		 * ip is the line_size aligned source address for preloads.
		 */
		ands	r3, r0, #3
		push	{r4}
		andeq	r3, r1, #3
		beq	40f	/* Destination is aligned but source is not. */
		/* Align the destination. */
		cmp	r3, #2
.if \aligned_access == 1
                ldrble  r4, [r1], #1
		ldrble	r3, [r1], #1
		suble	r2, r2, #2
		strble	r4, [r0], #1
		strble	r3, [r0], #1
.else
                ldrhle  r4, [r1], #2
		suble	r2, r2, #2
		strhle	r4, [r0], #2
.endif
		ldrbne	r4, [r1], #1
		subne	r2, r2, #1
		strbne	r4, [r0], #1
		ands	r3, r1, #3
		bne	40f	/* Destination is aligned but source is not. */

#if 0 && FAST_PATH_THRESHOLD > 0
		/*
		 * Source and destination are now aligned.
		 * Now recreate the situation of a word-aligned memcpy
		 * with the current source and destination,
		 * which may require an extra preload instruction.
		 *
		 * This path is currently disabled disabled in favour
		 * of the one below this which does write alignment and
		 * jumps into the main loop for larger sizes.
		 */
		bic	r3, r1, #(\line_size - 1)
		pop	{r4}
		cmp	r3, ip
	THUMB(	pldne	[r3]				)
	THUMB(	cmp	r2, #FAST_PATH_THRESHOLD	)
	THUMB(	mov	ip, r3				)
	ARM(	beq	31f				)
	ARM(	pld	[r3]				)
	ARM(	mov	ip, r3				)
31:	ARM(	cmp	r2, #FAST_PATH_THRESHOLD	)
		bgt	10b

		/*
		 * Recreate the fast path small size check here,
		 * but only if it necessary.
		 */
.if (BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD + 1 - 3) <= SMALL_SIZE_THRESHOLD ||
\aligned_access == 1
		cmp	r2, #SMALL_SIZE_THRESHOLD
                pld     [ip, #\line_size]
		/* Jump for small sizes <= SMALL_SIZE_THRESHOLD bytes. */
		ble	5b
.else
		pld	[ip, #\line_size]
.endif
		bic	r3, r2, #15
		b	9b

#else
		/*
		 * Source and destination are now aligned. Check carefully
		 * whether there are enough bytes to do alignment.
		 */
.if \write_align > 0
.if (BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD + 1 - 3) < (\write_align - 4) \
|| \aligned_access == 1
		cmp	r2, #(\write_align - 4)
		blt	31f
.endif
.if \write_align == 8
		/*
		 * For write alignment of 8, it is quickest to do a simple
		 * conditional load/store.
		 */
		tst	r0, #4
		ldrne	r3, [r1], #4
		subne	r2, r2, #4
		strne	r3, [r0], #4
.else
		ands	r3, r0, #(\write_align - 1)
		rsb	r3, r3, #\write_align
		beq	31f
		push	{lr}
		bl	20b
		pop	{lr}
.endif

31:		/*
		 * Check whether there are enough bytes to do one iteration
		 * of the main loop.
		 */
.if (BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD + 1 - 3 - (\write_align - 4)) < \line_size \
|| \aligned_access == 1
		cmp	r2, #\line_size
		blt	15b
.endif
		subs	r2, r2, #\line_size
.else
		/*
		 * No write alignment. Only have to check for enough bytes to
		 * do one iteration of the main loop.
		 */

.if (BOTH_UNALIGNED_SMALL_SIZE_THRESHOLD + 1 - 3) < \line_size \
|| \aligned_access == 1
		cmp	r2, #\line_size
		blt	15b
.endif
		subs	r2, r2, #\line_size
.endif
		b	16b
#endif

40:		/*
		 * Unaligned case. Size is > SMALL_SIZE_THRESHOLD - 3.
		 */
		bic	r1, r1, #3
		cmp	r3, #2
		ldr	r3, [r1], #4
		beq	41f
		bgt	42f

		unaligned_copy 8, \line_size, \prefetch_distance, \
			\write_align, \aligned_access

41:		unaligned_copy 16, \line_size, \prefetch_distance, \
			\write_align, \aligned_access

42:		unaligned_copy 24, \line_size, \prefetch_distance, \
			\write_align, \aligned_access

.endm

/*
 * The following is a NEON-based memcpy implementation that may use unaligned
 * access, but NEON instruction addresses are always at least element aligned.
 * It is optimized for both Thumb2 (CONFIG_THUMB) and ARM mode.
 *
 * - line_size is the cache line size used for prefetches. Must be 64 or 32.
 * - prefetch_distance is the number of cache lines to look ahead and must be
 *   >= 2, or 0 to disable prefetching in the main copying loop.
 * - early_prefetch indicates whether to perform early preloads. Must be 0 or 1.
 *   When prefetch_distance > 0, early_prefetch should be 1. To remove all PLD
 *   instructions altogether, set both prefetch_distance and early_prefetch
 *   to 0.
 */

.macro neon_memcpy_variant line_size, prefetch_distance, early_prefetch

		cmp	r2, #3
.if \prefetch_distance > 0 || \early_prefetch == 1
		push	{r0}
.else
		mov	ip, r0
.endif
		orr	r3, r0, r1
		ble	8f
.if \prefetch_distance > 0 || \early_prefetch == 1
		bic	ip, r1, #(\line_size - 1)
.endif
		tst	r3, #3
.if \early_prefetch == 1
		pld	[ip]
.endif
		bne	10f		/* Unaligned source or destination. */
		push	{r4}

		/* Aligned source and destination. */
1:		cmp	r2, #256
		/*
		 * Jump to word-aligned NEON fast path <= 256 bytes.
                 */
		ble	18f
		subs	r2, r2, #\line_size

                /* Align to a 32-byte boundary. */
#ifdef CONFIG_THUMB
		/*
		 * Use conditional NEON instructions when
		 * available (Thumb2 mode)
		 */
                ands    r4, r0, #31
                rsb     r4, r4, #32
                beq     31f
                tst     r4, #4
                sub     r2, r2, r4
                ldrne   r3, [r1 :32], #4
                strne   r3, [r0 :32], #4
                tst     r4, #8
		vld1ne.32 {d0}, [r1]!
		vst1ne.64 {d0}, [r0 NEON_ALIGN(64)]!
                cmp     r4, #16
		vld1ge.32 {d2, d3}, [r1]!
		vst1ge.64 {d2, d3}, [r0 NEON_ALIGN(128)]!
#else
		/*
		 * Otherwise, branch into a series of single
		 * loads/stores.
	 	 */
                ands    r4, r0, #31
                beq     31f
		rsb	r3, r4, #32
		lsl	r4, r4, #1
		sub	r2, r2, r3
		add	pc, pc, r4
		nop
		ldr	r3, [r1], #4
		str	r3, [r0], #4
		ldr	r4, [r1], #4
		str	r4, [r0], #4
		ldr	r3, [r1], #4
		str	r3, [r0], #4
		ldr	r4, [r1], #4
		str	r4, [r0], #4
		ldr	r3, [r1], #4
		str	r3, [r0], #4
		ldr	r4, [r1], #4
		str	r4, [r0], #4
		ldr	r3, [r1], #4
		str	r3, [r0], #4
		ldr	r4, [r1], #4
		str	r4, [r0], #4
#endif
                cmp     r2, #0
		addlt	r2, r2, \line_size
                blt     6f

31:
.if \early_prefetch == 1
		pld     [ip, #\line_size]
.endif
.if \prefetch_distance > 0
                /*
                 * Assume a preload at aligned base + line_size will
                 * be useful.
                 */
		push	{r5}
		mov	r4, ip
                add     r5, r1, #(\prefetch_distance * \line_size)
                subs    r2, r2, #(\prefetch_distance * \line_size)
                bic     r3, r5, #(\line_size - 1)
                add     r4, r4, #(2 * \line_size)
                blt     5f
                cmp     r4, r3
                sub     ip, r3, r1
                /*
                 * "Catch-up" the early preloads (which have been performed up
                 * to aligned source address + line_size) to the preload offset
                 * used in the main loop.
                 */
                bge     3f
2:              adds    r4, r4, #\line_size		/* Thumb16 */
                cmp     r4, r3
                pld     [r4, #(- \line_size)]
                blt     2b
3:
.endif

		sub	ip, ip, #\line_size
4:
                /*
                 * Since the destination is 32-byte aligned,
                 * specify 256-bit alignment for the NEON stores.
                 */
.if \line_size == 32
		vld1.32 {d0-d3}, [r1]!
                subs    r2, r2, #32
.if \prefetch_distance > 0
		pld	[r1, ip]
.endif
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
.else	/* line_size == 64 */
		vld1.32 {d0-d3}, [r1]!
                vld1.32 {d4-d7}, [r1]!
.if \prefetch_distance > 0
		pld	[r1, ip]
.endif
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
                subs    r2, r2, #64
                vst1.64 {d4-d7}, [r0 NEON_ALIGN(256)]!
.endif
                bge     4b
.if \prefetch_distance > 0
5:
.if \line_size == 32
		vld1.32 {d0-d3}, [r1]!
                subs    r2, r2, #32
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
.else	/* line_size == 64 */
		vld1.32 {d0-d3}, [r1]!
                vld1.32 {d4-d7}, [r1]!
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
                subs    r2, r2, #64
                vst1.64 {d4-d7}, [r0 NEON_ALIGN(256)]!
.endif
        	cmn     r2, #(\prefetch_distance * \line_size)
        	bge     5b
.endif
                /* Correct the count. */
23:     	adds    r2, r2, #((\prefetch_distance + 1) * \line_size)
.if \prefetch_distance > 0
		pop	{r5}
.endif

                /*
		 * Process the last 0-(line_size - 1) bytes, destination
		 * 32-byte aligned, source word aligned.
		 */
6:
#ifdef CONFIG_THUMB
		/*
		 * Use conditional NEON instructions when
		 * available (Thumb2 mode).
		 */
.if \line_size == 64
		cmp     r2, #32
                vld1ge.32 {d0-d3}, [r1]!
                vst1ge.64 {d0-d3}, [r0 NEON_ALIGN(128)]!
                tst     r2, #16
                vld1ne.32 {d0, d1}, [r1]!
                vst1ne.64 {d0, d1}, [r0 NEON_ALIGN(128)]!
.else
                cmp     r2, #16
                vld1ge.32 {d0, d1}, [r1]!
                vst1ge.64 {d0, d1}, [r0 NEON_ALIGN(128)]!
.endif
                tst     r2, #8
                vld1ne.32 {d2}, [r1]!
                vst1ne.64 {d2}, [r0 NEON_ALIGN(64)]!
                tst     r2, #4
                ldrne   r3, [r1], #4
                strne   r3, [r0 :32], #4

                pop     {r4}
#else
		/*
		 * Just use the world-aligned tail code if we
                 * don't have Thumb2.
		 */
		b	17f
#endif

	        /*
		 * Handle the last up to three bytes. Unaligned access
		 * may take place if source or destination is not
		 * half-word aligned.
		 */
8:		movs    r2, r2, lsl #31
                ldrhcs  r3, [r1], #2
                strhcs  r3, [r0], #2
                ldrbne  r3, [r1], #1
                strbne  r3, [r0]
9:
.if \prefetch_distance > 0 || \early_prefetch == 1
		pop	{r0}
.else
		mov	r0, ip
.endif
		bx	lr

10:             /*
                 * Unaligned case. Align the destination.
                 * Number of bytes is > 3.
                 * Note: This may use unaligned access.
                 * ip is the line_size aligned source address for preloads.
                 */
		cmp	r2, #64
                push    {r4}
		/* For small sizes < 64 bytes just use the unaligned tail code. */
		blt	16f
		ands	r3, r0, #3
                beq     11f     /* Destination is aligned but source is not. */
                /* Align the destination. */
		cmp	r3, #2
                ldrbne  r4, [r1], #1
                subne   r2, r2, #1
                strbne  r4, [r0], #1
                ldrhle  r4, [r1], #2
                suble   r2, r2, #2
                strhle  r4, [r0], #2
                tst	r1, #3
                beq     1b      /* Destination and source are now aligned. */
		/* Destination is now aligned to a word boundary. */
11:
		cmp	r2, #64
		/*
		 * Jump to non-aligned NEON tail code for <= 64 bytes.
                 */
		ble	16f
		subs	r2, r2, #\line_size

                /* Align destination to a 32-byte boundary. */
                ands    r4, r0, #31
                rsb     r4, r4, #32
                beq     20f
                tst     r4, #4
                sub     r2, r2, r4
                ldrne   r3, [r1 :8], #4         /* Unaligned access. */
                strne   r3, [r0 :32], #4
                tst     r4, #8
#ifdef CONFIG_THUMB
		/*
		 * Use conditional NEON instructions when
		 * available (Thumb2 mode)
		 */
                vld1ne.8 {d0}, [r1]!
                vst1ne.64 {d0}, [r0 NEON_ALIGN(64)]!
                cmp     r4, #16
                vld1ge.8 {d2, d3}, [r1]!
                vst1ge.64 {d2, d3}, [r0 NEON_ALIGN(128)]!
#else
		beq	31f
                vld1.8 {d0}, [r1]!
                vst1.64 {d0}, [r0 NEON_ALIGN(64)]!
31:             cmp     r4, #16
		blt	32f
                vld1.8 {d2, d3}, [r1]!
                vst1.64 {d2, d3}, [r0 NEON_ALIGN(128)]!
32:
#endif
                cmp     r2, #0
                addlt   r2, r2, #\line_size
                blt     16f
20:

.if \early_prefetch == 1
		pld     [ip, #\line_size]
.endif
.if \prefetch_distance > 0
                /*
                 * Assume a preload at aligned base + line_size will
                 * be useful.
                 */
		push	{r5}
		mov	r4, ip
                add     r5, r1, #(\prefetch_distance * \line_size)
                subs    r2, r2, #(\prefetch_distance * \line_size)
                bic     r3, r5, #(\line_size - 1)
                add     r4, r4, #(2 * \line_size)
                blt     15f
                cmp     r4, r3
                sub     ip, r3, r1
                /*
                 * "Catch-up" the early preloads (which have been performed up
                 * to aligned source address + line_size) to the preload offset
                 * used in the main loop.
                 */
                bge     13f
12:             adds    r4, r4, #\line_size		/* Thumb16 */
                cmp     r4, r3
                pld     [r4, #(- \line_size)]
                blt     12b
.endif

13:
                /*
                 * Process 64 unaligned bytes from source at a time and copy
                 * them to the 32-byte aligned destination.
                 */
14:
.if \prefetch_distance > 0
		pld     [r1, ip]
.endif
15:
.if \line_size == 32
		vld1.8  {d0-d3}, [r1]!
                subs    r2, r2, #32
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
.else	/* line_size == 64 */
		vld1.8  {d0-d3}, [r1]!
                vld1.8  {d4-d7}, [r1]!
                vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
                subs    r2, r2, #64
                vst1.64 {d4-d7}, [r0 NEON_ALIGN(256)]!
.endif
                bge     14b
.if \prefetch_distance > 0
		cmn     r2, #(\prefetch_distance * \line_size)
        	bge     15b
.endif
                /* Correct the count. */
                adds    r2, r2, #((\prefetch_distance + 1) * \line_size)
.if \prefetch_distance > 0
        	pop     {r5}
.endif

		/*
		 * Handle last 0-(line_size - 1) bytes (destination 32-byte
		 * aligned source unaligned).
		 */
#ifdef CONFIG_THUMB
		/*
		 * Use conditional NEON instructions when
		 * available (Thumb2 mode)
		 */
.if \line_size == 64
		cmp     r2, #32
                vld1ge.8 {d0-d3}, [r1]!
                vst1ge.64 {d0-d3}, [r0 NEON_ALIGN(128)]!
                tst     r2, #16
                vld1ne.8 {d0, d1}, [r1]!
                vst1ne.64 {d0, d1}, [r0 NEON_ALIGN(128)]!
.else
                cmp     r2, #16
                vld1ge.8 {d0, d1}, [r1]!
                vst1ge.64 {d0, d1}, [r0 NEON_ALIGN(128)]!
.endif
                tst     r2, #8
                vld1ne.8 {d2}, [r1]!
                vst1ne.64 {d2}, [r0 NEON_ALIGN(64)]!
                tst     r2, #4
                ldrne   r3, [r1], #4
                strne   r3, [r0 :32], #4

                pop     {r4}
		b	8b
#else
		/*
		 * Fall through to the code below. It is not entirely
		 * optimal because it does not indicate the destination
		 * is word aligned.
		 */
#endif

                /* Handle small size of 0-63 bytes, unaligned. */
16:		bic	r3, r2, #7
		rsb	r4, r3, #64
		tst	r2, #7
		add	pc, pc, r4
		nop
		vld1.8 {d0}, [r1]!
		vst1.8 {d0}, [r0]!
		vld1.8 {d1}, [r1]!
		vst1.8 {d1}, [r0]!
		vld1.8 {d0}, [r1]!
		vst1.8 {d0}, [r0]!
		vld1.8 {d1}, [r1]!
		vst1.8 {d1}, [r0]!
		vld1.8 {d0}, [r1]!
		vst1.8 {d0}, [r0]!
		vld1.8 {d1}, [r1]!
		vst1.8 {d1}, [r0]!
		vld1.8 {d0}, [r1]!
		vst1.8 {d0}, [r0]!
		vld1.8 {d1}, [r1]!
		vst1.8 {d1}, [r0]!
                pop     {r4}
		beq	9b
                tst     r2, #4
                ldrne   r3, [r1 :8], #4         /* Unaligned access. */
                strne   r3, [r0], #4
		b	8b

                /* Handle small size of 0-63 bytes, word aligned. */
17:
#ifdef CONFIG_THUMB
		cmp     r2, #32
                vld1ge.32 {d0-d3}, [r1]!
                vst1ge.32 {d0-d3}, [r0]!
                tst     r2, #16
                vld1ne.32 {d0, d1}, [r1]!
                vst1ne.32 {d0, d1}, [r0]!
                tst     r2, #8
                vld1ne.32 {d2}, [r1]!
                vst1ne.32 {d2}, [r0]!
		tst	r2, #7
#else
		bic	r3, r2, #7
		rsb	r4, r3, #64
		tst	r2, #7
		add	pc, pc, r4
		nop
		vld1.32 {d0}, [r1]!
		vst1.32 {d0}, [r0]!
		vld1.32 {d1}, [r1]!
		vst1.32 {d1}, [r0]!
		vld1.32 {d0}, [r1]!
		vst1.32 {d0}, [r0]!
		vld1.32 {d1}, [r1]!
		vst1.32 {d1}, [r0]!
		vld1.32 {d0}, [r1]!
		vst1.32 {d0}, [r0]!
		vld1.32 {d1}, [r1]!
		vst1.32 {d1}, [r0]!
		vld1.32 {d0}, [r1]!
		vst1.32 {d0}, [r0]!
		vld1.32 {d1}, [r1]!
		vst1.32 {d1}, [r0]!
#endif
                pop     {r4}
		beq	9b
		tst	r2, #4
		ldrne	r3, [r1], #4
		strne	r3, [r0], #4
		b	8b

		/*
		 * Fast path for <= 256 bytes, word aligned.
		 * This is hardcoded for a preload offset of 128 bytes,
		 * which seems to work well in practice for small sizes.
		 */
18:		bics	r3, r2, #31
.if \early_prefetch == 1
		pld	[ip, #32]
		beq	21f
		pld	[ip, #64]
		pld	[ip, #96]
.endif
		rsb	r4, r3, #256
		ands	r2, r2, #31
		/*
		 * Each code block handling 32 bytes is
                 * 12 bytes long.
		 */
		lsr	r4, r4, #2
                add     ip, ip, #128
		add	r4, r4, r4, lsr #1
		sub	ip, ip, r1
		add	pc, pc, r4
		nop
		pld	[r1, ip]
		vld1.32 {d0-d3}, [r1]!
		vst1.32 {d0-d3}, [r0]!
		pld	[r1, ip]
		vld1.32 {d4-d7}, [r1]!
		vst1.32 {d4-d7}, [r0]!
		pld	[r1, ip]
		vld1.32 {d0-d3}, [r1]!
		vst1.32 {d0-d3}, [r0]!
		pld	[r1, ip]
		vld1.32 {d4-d7}, [r1]!
		vst1.32 {d4-d7}, [r0]!
		pld	[r1, ip]
		vld1.32 {d0-d3}, [r1]!
		vst1.32 {d0-d3}, [r0]!
		W(nop)
		vld1.32 {d4-d7}, [r1]!
		vst1.32 {d4-d7}, [r0]!
		W(nop)
		vld1.32 {d0-d3}, [r1]!
		vst1.32 {d0-d3}, [r0]!
		W(nop)
		vld1.32 {d4-d7}, [r1]!
		vst1.32 {d4-d7}, [r0]!
		beq	19f
21:
#ifdef CONFIG_THUMB
		cmp	r2, #16
		vld1ge.32 {d0-d1}, [r1]!
		vst1ge.32 {d0-d1}, [r0]!
		tst	r2, #8
		vld1ne.32 {d0}, [r1]!
		vst1ne.32 {d0}, [r0]!
#else
		cmp	r2, #16
		ldmiage r1!, {r3, r4}
		stmiage r0!, {r3, r4}
		ldmiage r1!, {r3, r4}
		stmiage r0!, {r3, r4}
		tst	r2, #8
		ldmiane r1!, {r3, r4}
		stmiane r0!, {r3, r4}
#endif
                tst     r2, #4
		pop	{r4}
                ldrne   r3, [r1], #4
                strne   r3, [r0 :32], #4
		and	r2, r2, #3
		b	8b
19:
		pop	{r4}
.if \prefetch_distance > 0 || \early_prefetch == 1
		pop	{r0}
.else
		mov	r0, ip
.endif
		bx	lr
.endm


#if defined(MEMCPY_REPLACEMENT_RPI) || defined(MEMCPY_REPLACEMENT_ARMV7_32) \
|| defined(MEMCPY_REPLACEMENT_ARMV7_64) || defined(MEMCPY_REPLACEMENT_NEON_32) \
|| defined(MEMCPY_REPLACEMENT_NEON_64)

#ifdef MEMCPY_REPLACEMENT_RPI
asm_function memcpy
		memcpy_variant 32, 3, 8, 0
.endfunc
#endif

#ifdef MEMCPY_REPLACEMENT_ARMV7_32
asm_function memcpy
		memcpy_variant 32, 6, 0, 0
.endfunc
#endif

#ifdef MEMCPY_REPLACEMENT_ARMV7_64
asm_function memcpy
		memcpy_variant 64, 3, 0, 0
.endfunc
#endif

#ifdef MEMCPY_REPLACEMENT_NEON_32
asm_function memcpy
		neon_memcpy_variant 32, 6, 1
.endfunc
#endif

#ifdef MEMCPY_REPLACEMENT_NEON_64
asm_function memcpy
		neon_memcpy_variant 64, 3, 1
.endfunc
#endif

#ifdef MEMCPY_REPLACEMENT_NEON_AUTO
asm_function memcpy
		neon_memcpy_variant 32, 0, 1
.endfunc
#endif

#else

asm_function memcpy_new_line_size_64_preload_192
		memcpy_variant 64, 3, 0, 0
.endfunc

asm_function memcpy_new_line_size_64_preload_192_align_32
		memcpy_variant 64, 3, 32, 0
.endfunc

asm_function memcpy_new_line_size_64_preload_192_aligned_access
		memcpy_variant 64, 3, 0, 1
.endfunc

asm_function memcpy_new_line_size_32_preload_192
		memcpy_variant 32, 6, 0, 0
.endfunc

asm_function memcpy_new_line_size_32_preload_192_align_32
		memcpy_variant 32, 6, 32, 0
.endfunc

asm_function memcpy_new_line_size_32_preload_96
		memcpy_variant 32, 3, 8, 0
.endfunc

asm_function memcpy_new_line_size_32_preload_96_aligned_access
		memcpy_variant 32, 3, 8, 1
.endfunc

asm_function memcpy_new_neon_line_size_64
		neon_memcpy_variant 64, 3, 1
.endfunc

asm_function memcpy_new_neon_line_size_32
		neon_memcpy_variant 32, 6, 1
.endfunc

asm_function memcpy_new_neon_line_size_32_auto
		neon_memcpy_variant 32, 0, 1
.endfunc

#endif

/*
 * Macro for memset replacement.
 * write_align must be 0, 8, or 32.
 * use_neon must be 0 or 1.
 */

.macro memset_variant write_align, use_neon
.if \use_neon == 1
	.fpu neon
.endif
	ands	r3, r0, #3
	mov	ip, r0
	bne	7f

	/* Destination is word aligned. */
1:	orr	r1, r1, r1, lsl #8
.if \use_neon == 1
	cmp	r2, #16
.else
	cmp	r2, #8
.endif
	orr	r1, r1, r1, lsl #16
.if \use_neon == 1
	blt	13f
        vmov	d0, r1, r1
	vmov	d1, r1, r1
.else
	blt	5f
	mov	r3, r1
.endif

	cmp	r2, #64
	push 	{r4}
.if \use_neon == 1
	blt	10f
.else
	ble	10f
.endif
.if \write_align > 0
	ands	r4, r0, #(\write_align - 1)
.if \use_neon == 1
#ifndef CONFIG_THUMB
	add	r3, r4, #7
#endif
.endif
	/* Let r4 be equal to the number of bytes to align.  */
	rsb	r4, r4, #\write_align
	/*
	 * At this point r4 contains the number of bytes to align
	 * if eq is not set. The eq flag is set if there are no bytes
	 * to align.
	 */
.if \write_align == 8
	subne	r2, r2, r4
	strne	r1, [r0], #4
.elseif \write_align == 32
	beq	2f
	tst     r4, #4
	sub	r2, r2, r4
	strne	r1, [r0], #4
.if \use_neon == 1
#ifdef CONFIG_THUMB
	tst     r4, #8
	vst1ne.64 {d0}, [r0 NEON_ALIGN(64)]!
	cmp	r4, #16
	vst1ge.64 {d0, d1}, [r0 NEON_ALIGN(128)]!
#else
	bic	r4, r3, #7
	lsr	r4, r4, #1
	add	pc, pc, r4
	nop
	vst1.64 {d0}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0}, [r0 NEON_ALIGN(64)]!
#endif
.else
	tst     r4, #8
	stmiane r0!, {r1, r3}
	cmp	r4, #16
	stmiage r0!, {r1, r3}
	stmiage r0!, {r1, r3}
.endif
.endif	/* \write_align == 32 */
	cmp	r2, #64
	blt	4f
.endif	/* \write_align > 0 */

2:
.if \use_neon == 1
        /*
         * When NEON is enabled, \write_align is
         * equal to 32 so specify 256-bit alignment in the
         * NEON store instructions.
         */
	subs	r2, r2, #64
        vmov	q1, q0
3:	vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
	subs	r2, r2, #64
        vst1.64 {d0-d3}, [r0 NEON_ALIGN(256)]!
        bge     3b
	adds	r2, r2, #64
.else
	mov	r4, r1
	subs	r2, r2, #64
	push	{r5}
	mov	r5, r1

3:	stmia	r0!, {r1, r3, r4, r5}
	subs	r2, r2, #64		/* Thumb16 */
	stmia	r0!, {r1, r3, r4, r5}
	stmia	r0!, {r1, r3, r4, r5}
	stmia	r0!, {r1, r3, r4, r5}
	bge	3b
	adds	r2, r2, #64		/* Thumb16 */

	pop	{r5}
.endif
	/* Early exit if there are 0 bytes left. */
/* THUMB(	cbz	r2, 9f	) */
THUMB(	cmp	r2, #0	)
THUMB(	beq	9f	)
ARM(	teq	r2, #0	)
ARM(	beq	9f	)
	/*
	 * Handle 8-64 bytes (or 16-63 bytes in case of NEON).
	 * In case of NEON, destination must be 8-byte aligned.
	 */
4:
.if \use_neon == 1
#ifdef CONFIG_THUMB
	vmov	q1, q0
	cmp	r2, #32
	vst1ge.64 {d0-d3}, [r0 NEON_ALIGN(64)]!
	tst	r2, #16
	vst1ne.64 {d0, d1}, [r0 NEON_ALIGN(64)]!
	tst	r2, #8
	vst1ne.64 {d0}, [r0 NEON_ALIGN(64)]!
	and	r2, r2, #7
#else
	bic	r4, r2, #15
	subs	r2, r2, r4
	rsb	r4, r4, #64
	/*
	 * When using NEON, the vst instruction
	 * (storing 16 bytes) is always 32-bit.
	 */
	lsr	r4, r4, #2
	add	pc, pc, r4
	nop
	vst1.64 {d0, d1}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0, d1}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0, d1}, [r0 NEON_ALIGN(64)]!
	vst1.64 {d0, d1}, [r0 NEON_ALIGN(64)]!
	cmp	r2, #8
	strge	r1, [r0], #4
	strge	r1, [r0], #4
	subge	r2, r2, #8
#endif
.else	/* use_neon == 0 */
	bic	r4, r2, #7
	subs	r2, r2, r4
	rsb	r4, r4, #64
	/*
	 * The stmia instruction (storing 8 bytes) is 32-bit for ARM,
	 * 16-bit for Thumb2.
	 */
THUMB(	lsrs	r4, r4, #2	)
ARM(	lsr	r4, r4, #1	)
	add	pc, pc, r4
	nop
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
	stmia	r0!, {r1, r3}
.endif
14:	pop	{r4}

5:	cmp	r2, #4
	strge	r1, [r0], #4
	/* Early exit for multiple of 4 size. */
	ands	r2, r2, #3
	moveq	r0, ip
	bxeq	lr

	/*
	 * At this point there are 1, 2 or 3 bytes,
	 * and the destination is aligned.
	 */
6:	cmp	r2, #2
	strhge	r1, [r0], #2
	strbne	r1, [r0]
	mov	r0, ip
	bx	lr

.if \use_neon == 1
	/* 0-15 bytes left, word aligned. */
13:	cmp	r2, #8
	strge	r1, [r0]
	strge	r1, [r0, #4]
	addge	r0, r0, #8
	subge	r2, r2, #8
	b	5b
.endif

	/* Unaligned case. */
7:	cmp	r2, #4
	blt	8f
#ifdef CONFIG_THUMB
.if \use_neon == 1
	/*
	 * When Thumb2 is enabled with NEON, use the optimized
	 * unaligned NEON code path for small sizes.
	 */
	cmp 	r2, #64
	blt	11f
.endif
#endif
	/* Align the destination. */
	cmp	r3, #2
	sub	r2, r2, #4
	strble	r1, [r0]
	strble	r1, [r0, #1]
	addle	r0, r0, #2
	add	r2, r2, r3
	strbne	r1, [r0], #1
	b	1b

	/* 0 to 3 bytes left. */
8:	cmp	r2, #2
	strbge  r1, [r0]
	strbge  r1, [r0, #1]
	addge	r0, r0, #2
	tst	r2, #1
	strbne  r1, [r0]
	mov	r0, ip
	bx	lr

9:	pop	{r4}
	mov	r0, ip
	bx	lr

	/*
	 * Word aligned 8 <= size <= 64
	 * (16 <= size <= 63 in case of NEON).
	 */
10:
	/* Align the destination to an 8 byte boundary. */
	tst     r0, #4
	strne	r1, [r0], #4
	subne	r2, r2, #4
.if \use_neon == 1
	cmp	r2, #16
	poplt	{r4}
	blt	13b
.else
	cmp	r2, #8
	blt	14b
.endif
	b	4b

#ifdef CONFIG_THUMB
.if \use_neon == 1
	/*
	 * Handle 4 <= size <= 63 bytes, unaligned.
	 * Use unaligned NEON instructions with Thumb2.
	 */
11:
	orr	r1, r1, r1, lsl #8
	tst	r2, #8
	orr	r1, r1, r1, lsl #16
        vmov	d0, r1, r1
	vst1ne.8 {d0}, [r0]!
	vmov	d1, r1, r1
	tst	r2, #16
	vst1ne.8 {d0, d1}, [r0]!
	vmov	q1, q0
	cmp	r2, #32
	and	r2, r2, #7
	vst1ge.8 {d0-d3}, [r0]!
	cmp	r2, #4
	/* The following store is unaligned. */
	strge	r1, [r0], #4
	subge	r2, r2, #4
	b	8b
.endif
#endif
.endm

#if defined(MEMSET_REPLACEMENT_RPI) || defined(MEMSET_REPLACEMENT_ARMV7_32) \
|| defined(MEMSET_REPLACEMENT_ARMV7_64) || defined(MEMSET_REPLACEMENT_NEON_32) \
|| defined(MEMSET_REPLACEMENT_NEON_64)

#ifdef MEMSET_REPLACEMENT_RPI
asm_function memset
		memset_variant 32, 0
.endfunc
#endif

#if defined(MEMSET_REPLACEMENT_ARMV7_32) || defined(MEMSET_REPLACEMENT_ARMV7_64)
asm_function memset
		memset_variant 8, 0
.endfunc
#endif

#if defined(MEMSET_REPLACEMENT_NEON_32) || defined(MEMSET_REPLACEMENT_NEON_64)
asm_function memset
		memset_variant 32, 1
.endfunc
#endif

#else

asm_function memset_new_align_0
		memset_variant 0, 0
.endfunc

asm_function memset_new_align_8
		memset_variant 8, 0
.endfunc

asm_function memset_new_align_32
		memset_variant 32, 0
.endfunc

asm_function memset_neon
		memset_variant 32, 1
.endfunc

#endif
