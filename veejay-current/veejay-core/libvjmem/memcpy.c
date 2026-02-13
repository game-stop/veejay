/*
   (c) Copyright 2000-2002  convergence integrated media GmbH.
   (c) Copyright 2002       convergence GmbH.
   
   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Hundt <andi@fischlustig.de> and
              Sven Neumann <sven@convergence.de>.

   Fast memcpy code was taken from xine (see below).

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

/*
 * Copyright (C) 2001 the xine project
 *
 * This file is part of xine, a unix video player.
 *
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * These are the MMX/MMX2/SSE optimized versions of memcpy
 *
 * This code was adapted from Linux Kernel sources by Nick Kurshev to
 * the mplayer program. (http://mplayer.sourceforge.net)
 *
 * Miguel Freitas split the #ifdefs into several specialized functions that
 * are benchmarked at runtime by xine. Some original comments from Nick
 * have been preserved documenting some MMX/SSE oddities.
 * Also added kernel memcpy function that seems faster than glibc one.
 *
 */

/* Original comments from mplayer (file: aclib.c) This part of code
   was taken by me from Linux-2.4.3 and slightly modified for MMX, MMX2,
   SSE instruction set. I have done it since linux uses page aligned
   blocks but mplayer uses weakly ordered data and original sources can
   not speedup them. Only using PREFETCHNTA and MOVNTQ together have
   effect!

   From IA-32 Intel Architecture Software Developer's Manual Volume 1,

  Order Number 245470:
  "10.4.6. Cacheability Control, Prefetch, and Memory Ordering Instructions"

  Data referenced by a program can be temporal (data will be used
  again) or non-temporal (data will be referenced once and not reused
  in the immediate future). To make efficient use of the processor's
  caches, it is generally desirable to cache temporal data and not
  cache non-temporal data. Overloading the processor's caches with
  non-temporal data is sometimes referred to as "polluting the
  caches".  The non-temporal data is written to memory with
  Write-Combining semantics.

  The PREFETCHh instructions permits a program to load data into the
  processor at a suggested cache level, so that it is closer to the
  processors load and store unit when it is needed. If the data is
  already present in a level of the cache hierarchy that is closer to
  the processor, the PREFETCHh instruction will not result in any data
  movement.  But we should you PREFETCHNTA: Non-temporal data fetch
  data into location close to the processor, minimizing cache
  pollution.

  The MOVNTQ (store quadword using non-temporal hint) instruction
  stores packed integer data from an MMX register to memory, using a
  non-temporal hint.  The MOVNTPS (store packed single-precision
  floating-point values using non-temporal hint) instruction stores
  packed floating-point data from an XMM register to memory, using a
  non-temporal hint.

  The SFENCE (Store Fence) instruction controls write ordering by
  creating a fence for memory store operations. This instruction
  guarantees that the results of every store instruction that precedes
  the store fence in program order is globally visible before any
  store instruction that follows the fence. The SFENCE instruction
  provides an efficient way of ensuring ordering between procedures
  that produce weakly-ordered data and procedures that consume that
  data.

  If you have questions please contact with me: Nick Kurshev:
  nickols_k@mail.ru.
*/

/*  mmx v.1 Note: Since we added alignment of destinition it speedups
    of memory copying on PentMMX, Celeron-1 and P2 upto 12% versus
    standard (non MMX-optimized) version.
    Note: on K6-2+ it speedups memory copying upto 25% and
          on K7 and P3 about 500% (5 times).
*/

/* Additional notes on gcc assembly and processors: [MF]
   prefetch is specific for AMD processors, the intel ones should be
   prefetch0, prefetch1, prefetch2 which are not recognized by my gcc.
   prefetchnta is supported both on athlon and pentium 3.

   therefore i will take off prefetchnta instructions from the mmx1
   version to avoid problems on pentium mmx and k6-2.

   quote of the day:
    "Using prefetches efficiently is more of an art than a science"
*/
#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <time.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libyuv/mmx.h>
#include <libyuv/mmx_macros.h>
#include <veejaycore/defs.h>
#include <libyuv/yuvconv.h>
#include <veejaycore/veejaycore.h>
#include <libavutil/cpu.h>
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif
#ifdef HAVE_ARM_NEON
#include <fastarm/new_arm.h>
#endif
#ifdef HAVE_ARM_ASIMD
#include <arm_neon.h>
#include <arm_sve.h>
#endif
#if defined (__SSE2__) || defined(__SSE4_2__) || defined(_SSE4_1__)
#include <immintrin.h>
#endif

#ifdef HAVE_ASM_SSE2
#include <smmintrin.h>
#endif
#define BUFSIZE 1024


#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW)
#define HAVE_K6_2PLUS
#endif

/* definitions */
#define BLOCK_SIZE 4096
#define CONFUSION_FACTOR 0
//Feel free to fine-tune the above 2, it might be possible to get some speedup with them :)

#define FIND_BEST_MAX_ITERATIONS 1024


#define is_aligned__(PTR,LEN) \
	(((uintptr_t)(const void*)(PTR)) % (LEN) == 0 )

static int selected_best_memcpy = 1;
static int selected_best_memset = 1;

static double get_time(void)
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double) ts.tv_sec + (double) ts.tv_nsec / 1000000000.0;
}

#if defined(ARCH_X86) || defined (ARCH_X86_64)
/* for small memory blocks (<256 bytes) this version is faster */
#define small_memcpy(to,from,n)\
{\
register uintptr_t dummy;\
__asm__ __volatile__(\
  "rep; movsb"\
  :"=&D"(to), "=&S"(from), "=&c"(dummy)\
  :"0" (to), "1" (from),"2" (n)\
  : "memory");\
}

/* for small memory blocks (<256 bytes) this version is faster */
#define small_memset(to,val,n)\
{\
register unsigned long int dummy;\
__asm__ __volatile__(\
	"rep; stosb"\
	:"=&D"(to), "=&c"(dummy)\
        :"0" (to), "1" (n), "a"((char)val)\
	:"memory");\
}

#else
#define	small_memcpy(to,from,n) memcpy( to,from,n )
#define small_memset(to,val,n) memset(to,val,n)
char	*veejay_strncpy( char *dest, const char *src, size_t n )
{
	veejay_memcpy ( dest, src, n );
	dest[n] = '\0';
	return dest;
}

char	*veejay_strncat( char *s1, char *s2, size_t n )
{
	return strncat( s1,s2, n);
}


static void	yuyv_plane_clear_job( void *arg )
{
	vj_task_arg_t *v = (vj_task_arg_t*) arg;
	int len = v->strides[0];
	uint8_t *t = v->input[0];
	unsigned int i;
	i = len;
	for( ; i > 0 ; i -- )
	{
		t[0] = 0;
		t[1] = 128;
		t[2] = 0;
		t[3] = 128;
		t += 4;
	}
}


void	yuyv_plane_clear( size_t len, void *to )
{
	uint8_t *t = (uint8_t*) to;
	unsigned int i;
	i = len;
	for( ; i > 0 ; i -- )
	{
		t[0] = 0;
		t[1] = 128;
		t[2] = 0;
		t[3] = 128;
		t += 4;
	}

}
#endif

static int BENCHMARK_WID = 1920;
static int BENCHMARK_HEI = 1080;


#if defined(ARCH_X86) || defined (ARCH_X86_64)
static __inline__ void * __memcpy(void * to, const void * from, size_t n)
{
     int d0, d1, d2;
     if ( n < 4 ) { 
          small_memcpy(to,from,n);
     }
     else
          __asm__ __volatile__(
            "rep ; movsl\n\t"
            "testb $2,%b4\n\t"
            "je 1f\n\t"
            "movsw\n"
            "1:\ttestb $1,%b4\n\t"
            "je 2f\n\t"
            "movsb\n"
            "2:"
            : "=&c" (d0), "=&D" (d1), "=&S" (d2)
            :"0" (n/4), "q" (n),"1" ((uintptr_t) to),"2" ((uintptr_t) from)
            : "memory");

     return(to);
}

#ifdef HAVE_ASM_AVX
#define AVX_MMREG_SIZE 32
#endif
#if defined(HAVE_ASM_SSE) || defined(HAVE_ASM_SSE2) || defined(__SSE4_1__)
#define SSE_MMREG_SIZE 16
#endif
#ifdef HAVE_ASM_MMX
#define MMX_MMREG_SIZE 8
#endif

#undef _MMREG_SIZE
#ifdef HAVE_ASM_SSE
#define AC_MMREG_SIZE 16
#elif HAVE_ASM_MMX
#define AC_MMREG_SIZE 64
#endif

#undef HAVE_ONLY_MMX1
#if HAVE_ASM_MMX && !HAVE_ASM_MMX2 && !HAVE_ASM_3DNOW && !HAVE_ASM_SSE
/*  means: mmx v.1. Note: Since we added alignment of destinition it speedups
    of memory copying on PentMMX, Celeron-1 and P2 upto 12% versus
    standard (non MMX-optimized) version.
    Note: on K6-2+ it speedups memory copying upto 25% and
          on K7 and P3 about 500% (5 times). */
#define HAVE_ONLY_MMX1
#endif

#undef PREFETCH
#undef EMMS

#ifdef HAVE_ASM_MMX2
#define PREFETCH "prefetchnta"
#elif defined ( HAVE_ASM_3DNOW )
#define PREFETCH  "prefetch"
#else
#define PREFETCH " # nop"
#endif

#undef MOVNTQ
#if HAVE_ASM_MMX2
#define MOVNTQ "movntq"
#else
#define MOVNTQ "movq"
#endif

#define _MMX1_MIN_LEN 0x800 /* 2k blocks */

//#ifdef HAVE_ASM_3DNOW
//#define EMMS     "femms"
//#else
#define EMMS     "emms"
//#endif

char	*veejay_strncpy( char *dest, const char *src, size_t n )
{
	dest[n] = '\0';
	if( n < 0xff ) {
		small_memcpy( dest,src, n );
	} else {
		return veejay_memcpy( dest,src, n );
	}
	return dest;
}

char	*veejay_strncat( char *s1, char *s2, size_t n )
{
	return strncat( s1,s2, n);
}

static uint8_t ppmask[16] = { 0,128,128,0, 128,128,0,128, 128,0,128,128,0,128,128, 0 };
#if defined ( HAVE_ASM_MMX ) || defined ( HAVE_ASM_SSE )
static uint8_t yuyv_mmreg_[AC_MMREG_SIZE];
#endif

void	yuyv_plane_init(void)
{
#if defined ( HAVE_ASM_MMX ) || defined ( HAVE_ASM_SSE )
	unsigned int i;
	for( i = 0; i < AC_MMREG_SIZE ;i ++ )
		yuyv_mmreg_[i] = ( (i%2) ? 128: 0 );
#endif
}


static void	yuyv_plane_clear_job( void *arg )
{
	vj_task_arg_t *v = (vj_task_arg_t*) arg;
	unsigned int len = v->strides[0];
	uint8_t *t = v->input[0];
	unsigned int i;
	
#ifdef HAVE_ASM_MMX2
	__asm __volatile(
		"movq	(%0),	%%mm0\n"
		:: "r" (yuyv_mmreg_) : "memory" );

	i = len >> 7;
	len = len % 128;

	for(; i > 0 ; i -- )
	{
		__asm __volatile(
			PREFETCH" 320(%0)\n"
			MOVNTQ"	%%mm0,	(%0)\n"
			MOVNTQ"	%%mm0,	8(%0)\n"
			MOVNTQ"	%%mm0,	16(%0)\n"
			MOVNTQ"	%%mm0,	24(%0)\n"
			MOVNTQ"	%%mm0,	32(%0)\n"
			MOVNTQ"	%%mm0,	40(%0)\n"
			MOVNTQ"	%%mm0,	48(%0)\n"
			MOVNTQ"	%%mm0,	56(%0)\n"
			MOVNTQ"	%%mm0,	64(%0)\n"
			MOVNTQ"	%%mm0,	72(%0)\n"
			MOVNTQ"	%%mm0,	80(%0)\n"
			MOVNTQ"	%%mm0,	88(%0)\n"
			MOVNTQ"	%%mm0,	96(%0)\n"
			MOVNTQ"	%%mm0,	104(%0)\n"
			MOVNTQ"	%%mm0,	112(%0)\n"
			MOVNTQ" %%mm0,  120(%0)\n"
		:: "r" (t) : "memory" );
		t += 128;
	}
#else
#ifdef HAVE_ASM_MMX
	__asm __volatile(
		"movq (%0),	%%mm0\n\t"
		:: "r" (yuyv_mmreg_): "memory");
	i = len >> 6;
	len = len % 64;

	for(; i > 0 ; i -- )
	{
		__asm__ __volatile__ (
			"movq	%%mm0,	(%0)\n"
			"movq	%%mm0,	8(%0)\n"
			"movq	%%mm0, 16(%0)\n"
			"movq	%%mm0, 24(%0)\n"
			"movq	%%mm0, 32(%0)\n"
			"movq	%%mm0, 40(%0)\n"
			"movq   %%mm0, 48(%0)\n"
			"movq	%%mm0, 56(%0)\n"
		:: "r" (t) : "memory");
		t += 64;
	}
#endif
#endif
#ifdef HAVE_ASM_MMX
	i = len >> 3;
	len = i % 8;
	for( ; i > 0; i -- )
	{
		__asm__ __volatile__ (
			"movq	%%mm0, (%0)\n"
		:: "r" (t) : "memory" );
		t += 8;
	}
#endif
	i = len;
	for( ; i > 0 ; i -- )
	{
		t[0] = 0;
		t[1] = 128;
		t[2] = 0;
		t[3] = 128;
		t += 4;
	}
}


void	yuyv_plane_clear( size_t len, void *to )
{
	uint8_t *t = (uint8_t*) to;
	unsigned int i;
#ifdef HAVE_ASM_MMX2
	__asm __volatile(
		"movq	(%0),	%%mm0\n"
		:: "r" (yuyv_mmreg_) : "memory" );

	i = len >> 7;
	len = len % 128;

	for(; i > 0 ; i -- )
	{
		__asm __volatile(
			PREFETCH" 320(%0)\n"
			MOVNTQ"	%%mm0,	(%0)\n"
			MOVNTQ"	%%mm0,	8(%0)\n"
			MOVNTQ"	%%mm0,	16(%0)\n"
			MOVNTQ"	%%mm0,	24(%0)\n"
			MOVNTQ"	%%mm0,	32(%0)\n"
			MOVNTQ"	%%mm0,	40(%0)\n"
			MOVNTQ"	%%mm0,	48(%0)\n"
			MOVNTQ"	%%mm0,	56(%0)\n"
			MOVNTQ"	%%mm0,	64(%0)\n"
			MOVNTQ"	%%mm0,	72(%0)\n"
			MOVNTQ"	%%mm0,	80(%0)\n"
			MOVNTQ"	%%mm0,	88(%0)\n"
			MOVNTQ"	%%mm0,	96(%0)\n"
			MOVNTQ"	%%mm0,	104(%0)\n"
			MOVNTQ"	%%mm0,	112(%0)\n"
			MOVNTQ" %%mm0,  120(%0)\n"
		:: "r" (t) : "memory" );
		t += 128;
	}
#else
#ifdef HAVE_ASM_MMX
	__asm __volatile(
		"movq (%0),	%%mm0\n\t"
		:: "r" (yuyv_mmreg_): "memory");
	i = len >> 6;
	len = len % 64;

	for(; i > 0 ; i -- )
	{
		__asm__ __volatile__ (
			"movq	%%mm0,	(%0)\n"
			"movq	%%mm0,	8(%0)\n"
			"movq	%%mm0, 16(%0)\n"
			"movq	%%mm0, 24(%0)\n"
			"movq	%%mm0, 32(%0)\n"
			"movq	%%mm0, 40(%0)\n"
			"movq   %%mm0, 48(%0)\n"
			"movq	%%mm0, 56(%0)\n"
		:: "r" (t) : "memory");
		t += 64;
	}
#endif
#endif
#ifdef HAVE_ASM_MMX
	i = len >> 3;
	len = i % 8;
	for( ; i > 0; i -- )
	{
		__asm__ __volatile__ (
			"movq	%%mm0, (%0)\n"
		:: "r" (t) : "memory" );
		t += 8;
	}
#endif
	i = len;
	for( ; i > 0 ; i -- )
	{
		t[0] = 0;
		t[1] = 128;
		t[2] = 0;
		t[3] = 128;
		t += 4;
	}
}

void	packed_plane_clear( size_t len, void *to )
{
	uint8_t *t = (uint8_t*) to;
	unsigned int i;
	uint8_t *m = (uint8_t*) &ppmask;
#ifdef HAVE_ASM_MMX
	__asm __volatile(
		"movq (%0),	%%mm0\n\t"
		:: "r" (m));
	i = len / 64;
	len = len % 64;

	for(; i > 0 ; i -- )
	{
		__asm__ __volatile__ (
			"movq	%%mm0,	(%0)\n"
			"movq	%%mm0,	8(%0)\n"
			"movq	%%mm0, 16(%0)\n"
			"movq	%%mm0, 24(%0)\n"
			"movq	%%mm0, 32(%0)\n"
			"movq	%%mm0, 40(%0)\n"
			"movq   %%mm0, 48(%0)\n"
			"movq	%%mm0, 56(%0)\n"
		:: "r" (t) : "memory");
		t += 64;
	}
#endif	
	i = len;
	for( ; i > 0 ; i -- )
	{
		t[0] = 0;
		t[1] = 128;
		t[2] = 0;
		t[3] = 128;
		t += 4;
	}
}

#if defined (__SSE4_1__)
static void *sse41_memcpy(void *to, const void *from, size_t len) {
  void *retval = to;

  if (len >= 128) {
	if(!is_aligned__(from,SSE_MMREG_SIZE)) {
		memcpy( to,from,len);
		return to;
	} 

    register uintptr_t delta;
    /* Align destination to SSE_MMREG_SIZE -boundary */
    delta = ((uintptr_t)to) & (SSE_MMREG_SIZE - 1);
    if (delta) {
      delta = SSE_MMREG_SIZE - delta;
      len -= delta;
      __memcpy(to, from, delta);
    }

    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

    // Prefetch data
    _mm_prefetch((char *)from + 128, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 160, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 192, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 224, _MM_HINT_NTA);

    // Load data into registers
    xmm0 = _mm_load_si128((__m128i *)from);
    xmm1 = _mm_load_si128((__m128i *)((char *)from + 16));
    xmm2 = _mm_load_si128((__m128i *)((char *)from + 32));
    xmm3 = _mm_load_si128((__m128i *)((char *)from + 48));
    xmm4 = _mm_load_si128((__m128i *)((char *)from + 64));
    xmm5 = _mm_load_si128((__m128i *)((char *)from + 80));
    xmm6 = _mm_load_si128((__m128i *)((char *)from + 96));
    xmm7 = _mm_load_si128((__m128i *)((char *)from + 112));

    // Store data from registers to destination
    _mm_store_si128((__m128i *)to, xmm0);
    _mm_store_si128((__m128i *)((char *)to + 16), xmm1);
    _mm_store_si128((__m128i *)((char *)to + 32), xmm2);
    _mm_store_si128((__m128i *)((char *)to + 48), xmm3);
    _mm_store_si128((__m128i *)((char *)to + 64), xmm4);
    _mm_store_si128((__m128i *)((char *)to + 80), xmm5);
    _mm_store_si128((__m128i *)((char *)to + 96), xmm6);
    _mm_store_si128((__m128i *)((char *)to + 112), xmm7);

    // Increment pointers
    from += 128;
    to += 128;
    len -= 128;
  }

  // Copy the remaining bytes
  if (len) {
    __memcpy(to, from, len);
  }

  return retval;
}
#endif

#if defined (__SSE4_2__ )
static void *sse42_memcpy(void *to, const void *from, size_t len) {
  void *retval = to;

  if (len >= 128) {
	if(!is_aligned__(from,SSE_MMREG_SIZE)) {
		memcpy( to,from,len);
		return to;
	} 

    register uintptr_t delta;
    /* Align destination to SSE_MMREG_SIZE -boundary */
    delta = ((uintptr_t)to) & (SSE_MMREG_SIZE - 1);
    if (delta) {
      delta = SSE_MMREG_SIZE - delta;
      len -= delta;
      __memcpy(to, from, delta);
    }

    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

    // Prefetch data
    _mm_prefetch((char *)from + 128, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 160, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 192, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 224, _MM_HINT_NTA);

    // Load data into registers
    xmm0 = _mm_load_si128((__m128i *)from);
    xmm1 = _mm_load_si128((__m128i *)((char *)from + 16));
    xmm2 = _mm_load_si128((__m128i *)((char *)from + 32));
    xmm3 = _mm_load_si128((__m128i *)((char *)from + 48));
    xmm4 = _mm_load_si128((__m128i *)((char *)from + 64));
    xmm5 = _mm_load_si128((__m128i *)((char *)from + 80));
    xmm6 = _mm_load_si128((__m128i *)((char *)from + 96));
    xmm7 = _mm_load_si128((__m128i *)((char *)from + 112));

    // Store data from registers to destination
    _mm_storeu_si128((__m128i *)to, xmm0);
    _mm_storeu_si128((__m128i *)((char *)to + 16), xmm1);
    _mm_storeu_si128((__m128i *)((char *)to + 32), xmm2);
    _mm_storeu_si128((__m128i *)((char *)to + 48), xmm3);
    _mm_storeu_si128((__m128i *)((char *)to + 64), xmm4);
    _mm_storeu_si128((__m128i *)((char *)to + 80), xmm5);
    _mm_storeu_si128((__m128i *)((char *)to + 96), xmm6);
    _mm_storeu_si128((__m128i *)((char *)to + 112), xmm7);

    // Increment pointers
    from += 128;
    to += 128;
    len -= 128;
  }

  // Copy the remaining bytes
  if (len) {
    __memcpy(to, from, len);
  }

  return retval;
}

#endif

#if defined (__SSE2__)

/* Xine + William Chan + Others
 * https://duckduckgo.com/?q=william+chan+sse2+memcpy&t=ffsb&ia=web
 * https://stackoverflow.com/questions/1715224/very-fast-memcpy-for-image-processing
 * http://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html#ss5.4
 */

static void *sse2_memcpy(void * to, const void * from, size_t len)
{
    void *retval = to;

    if(len >= 128)
    {
		if(!is_aligned__(from,SSE_MMREG_SIZE)) {
			memcpy( to,from,len);
			return to;
		} 

        register uintptr_t delta;
        /* Align destination to SSE_MMREG_SIZE -boundary */
        delta = ((uintptr_t)to)&(SSE_MMREG_SIZE-1);
        if(delta)
        {
            delta=SSE_MMREG_SIZE-delta;
            len -= delta;
            small_memcpy(to, from, delta);
        }
        __asm__ /*__volatile__ */(

            "mov %2, %%ebx\n"        //ebx is our counter
            "shr $0x7, %%ebx\n"      //divide by 128 (8 * 128bit registers)
            "jz loop_copy_end\n"
        "loop_copy:\n"

            "prefetchnta 128(%0)\n" //SSE2 prefetch
            "prefetchnta 160(%0)\n"
            "prefetchnta 192(%0)\n"
            "prefetchnta 224(%0)\n"
// check align is ! source or dest ???? with movdqa
// 16-byte boundary
            "movdqu (%0),    %%xmm0\n" //move data from src to registers
            "movdqu 16(%0),  %%xmm1\n"
            "movdqu 32(%0),  %%xmm2\n"
            "movdqu 48(%0),  %%xmm3\n"
            "movdqu 64(%0),  %%xmm4\n"
            "movdqu 80(%0),  %%xmm5\n"
            "movdqu 96(%0),  %%xmm6\n"
            "movdqu 112(%0), %%xmm7\n"

            "movntdq %%xmm0, (%1)  \n" //move data from registers to dest
            "movntdq %%xmm1, 16(%1)\n"
            "movntdq %%xmm2, 32(%1)\n"
            "movntdq %%xmm3, 48(%1)\n"
            "movntdq %%xmm4, 64(%1)\n"
            "movntdq %%xmm5, 80(%1)\n"
            "movntdq %%xmm6, 96(%1)\n"
            "movntdq %%xmm7,112(%1)\n"

            "add $128, %0\n"
            "add $128, %1\n"
            "dec %%ebx\n"

            "jnz loop_copy\n" //loop please
        "loop_copy_end:"

            : : "r" (from) ,
            "r" (to),
            "m" (len)
            : "ebx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"
        );

        len&=127; //get the tail size
    }

    /* Now do the tail of the block */
    if(len) __memcpy(to, from, len);
    return retval;
}

static void *sse2_memcpy_unaligned(void *to, const void *from, size_t len) {
  void *retval = to;

  if (len >= 128) {
    register uintptr_t delta;
    /* Align destination to SSE_MMREG_SIZE -boundary */
    delta = ((uintptr_t)to) & (SSE_MMREG_SIZE - 1);
    if (delta) {
      delta = SSE_MMREG_SIZE - delta;
      len -= delta;
      __memcpy(to, from, delta);
    }

    __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;

    // Prefetch data
    _mm_prefetch((char *)from + 128, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 160, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 192, _MM_HINT_NTA);
    _mm_prefetch((char *)from + 224, _MM_HINT_NTA);

    // Load data into registers
    xmm0 = _mm_loadu_si128((__m128i *)from);
    xmm1 = _mm_loadu_si128((__m128i *)((char *)from + 16));
    xmm2 = _mm_loadu_si128((__m128i *)((char *)from + 32));
    xmm3 = _mm_loadu_si128((__m128i *)((char *)from + 48));
    xmm4 = _mm_loadu_si128((__m128i *)((char *)from + 64));
    xmm5 = _mm_loadu_si128((__m128i *)((char *)from + 80));
    xmm6 = _mm_loadu_si128((__m128i *)((char *)from + 96));
    xmm7 = _mm_loadu_si128((__m128i *)((char *)from + 112));

    // Store data from registers to destination
    _mm_storeu_si128((__m128i *)to, xmm0);
    _mm_storeu_si128((__m128i *)((char *)to + 16), xmm1);
    _mm_storeu_si128((__m128i *)((char *)to + 32), xmm2);
    _mm_storeu_si128((__m128i *)((char *)to + 48), xmm3);
    _mm_storeu_si128((__m128i *)((char *)to + 64), xmm4);
    _mm_storeu_si128((__m128i *)((char *)to + 80), xmm5);
    _mm_storeu_si128((__m128i *)((char *)to + 96), xmm6);
    _mm_storeu_si128((__m128i *)((char *)to + 112), xmm7);

    // Increment pointers
    from += 128;
    to += 128;
    len -= 128;
  }

  // Copy the remaining bytes
  if (len) {
    __memcpy(to, from, len);
  }

  return retval;
}

#endif

#ifdef HAVE_ASM_SSE
/* for veejay, moving 128 bytes a time makes a difference */
static void *sse_memcpy2(void * to, const void * from, size_t len)
{
	void *retval = to;
	size_t i;
	
	if(len >= 128)
	{
		if(!is_aligned__(from,SSE_MMREG_SIZE)) {
			memcpy( to,from,len);
			return to;
		} 

		register uintptr_t delta;
   
		/* Align destination to SSE_MMREG_SIZE -boundary */
		delta = ((uintptr_t)to)&(SSE_MMREG_SIZE-1);
		if(delta)
		{
			delta=SSE_MMREG_SIZE-delta;
			len -= delta;
			small_memcpy(to, from, delta);
		}
		i = len >> 7; /* len/128 */
		len&=127;

		for(; i>1; i--)
		{   
			__asm__ __volatile__ (
				"movups (%0),  %%xmm0\n"
				"movups 16(%0),%%xmm1\n"
				"movups 32(%0),%%xmm2\n"
				"movups 48(%0),%%xmm3\n"
				"movups 64(%0),%%xmm4\n"
				"movups 80(%0),%%xmm5\n"
				"movups 96(%0),%%xmm6\n"
				"movups 112(%0),%%xmm7\n"
				"movntps %%xmm0, (%1)\n"
				"movntps %%xmm1,16(%1)\n"
				"movntps %%xmm2,32(%1)\n"
				"movntps %%xmm3,48(%1)\n"
				"movntps %%xmm4,64(%1)\n"
				"movntps %%xmm5,80(%1)\n"
				"movntps %%xmm6,96(%1)\n"
				"movntps %%xmm7,112(%1)\n"
				:: "r" (from), "r" (to) : "memory");
			from = ((const unsigned char *)from) + 128;
			to = ((unsigned char *)to) + 128;
		}
		
		if (len >= SSE_MMREG_SIZE) {
            __asm__ __volatile__ (
                "movups (%0),  %%xmm0\n"
                "movntps %%xmm0, (%1)\n"
                :: "r" (from), "r" (to) : "memory");
            from = ((const unsigned char *)from) + 16;
            to = ((unsigned char *)to) + 16;
        }
		if (len) {
            memcpy(to, from, len);
        }
		/* since movntq is weakly-ordered, a "sfence"
		 * is needed to become ordered again. */
		__asm__ __volatile__ ("sfence":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) __memcpy(to, from, len);
  return retval;

}
//https://raw.githubusercontent.com/huceke/xine-lib-vaapi/master/src/xine-utils/memcpy.c
/* SSE note: i tried to move 128 bytes a time instead of 64 but it
didn't make any measureable difference. i'm using 64 for the sake of
simplicity. [MF] */
static void * sse_memcpy(void * to, const void * from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  /* PREFETCH has effect even for MOVSB instruction ;) */
  /*
  __asm__ __volatile__ (
    "   prefetchnta (%0)\n"
    "   prefetchnta 32(%0)\n"
    "   prefetchnta 64(%0)\n"
    "   prefetchnta 96(%0)\n"
    "   prefetchnta 128(%0)\n"
    "   prefetchnta 160(%0)\n"
    "   prefetchnta 192(%0)\n"
    "   prefetchnta 224(%0)\n"
    "   prefetchnta 256(%0)\n"
    "   prefetchnta 288(%0)\n"
    : : "r" (from) );
	*/
  if(len >= MIN_LEN)
  {
    register uintptr_t delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((uintptr_t)to)&(SSE_MMREG_SIZE-1);
    if(delta)
    {
      delta=SSE_MMREG_SIZE-delta;
      len -= delta;
      small_memcpy(to, from, delta);
    }
    i = len >> 6; /* len/64 */
    len&=63;
    if(((uintptr_t)from) & 15)
      /* if SRC is misaligned */
      for(; i>0; i--)
      {
        __asm__ __volatile__ (
//        "prefetchnta 320(%0)\n"
//        "prefetchnta 352(%0)\n"
        "movups (%0), %%xmm0\n"
        "movups 16(%0), %%xmm1\n"
        "movups 32(%0), %%xmm2\n"
        "movups 48(%0), %%xmm3\n"
        "movntps %%xmm0, (%1)\n"
        "movntps %%xmm1, 16(%1)\n"
        "movntps %%xmm2, 32(%1)\n"
        "movntps %%xmm3, 48(%1)\n"
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 64;
        to = ((unsigned char *)to) + 64;
      }
    else
      /*
         Only if SRC is aligned on 16-byte boundary.
         It allows to use movaps instead of movups, which required data
         to be aligned or a general-protection exception (#GP) is generated.
      */
      for(; i>0; i--)
      {
        __asm__ __volatile__ (
//        "prefetchnta 320(%0)\n"
//        "prefetchnta 352(%0)\n"
        "movaps (%0), %%xmm0\n"
        "movaps 16(%0), %%xmm1\n"
        "movaps 32(%0), %%xmm2\n"
        "movaps 48(%0), %%xmm3\n"
        "movntps %%xmm0, (%1)\n"
        "movntps %%xmm1, 16(%1)\n"
        "movntps %%xmm2, 32(%1)\n"
        "movntps %%xmm3, 48(%1)\n"
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 64;
        to = ((unsigned char *)to) + 64;
      }
    /* since movntq is weakly-ordered, a "sfence"
     * is needed to become ordered again. */
    __asm__ __volatile__ ("sfence":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) __memcpy(to, from, len);
  return retval;
}
#endif

#ifdef HAVE_ASM_AVX2
void *avx2_memcpy(void *dst0, const void *src0, size_t len0) {
    void *retval = dst0;

    const char *src = (const char *)src0;
    char *dst = (char *)dst0;
    size_t len = len0;

	// the avx2 memcpy is undefined if src or dst overlap
    /*if ((dst < src + len) && (src < dst + len)) {
        // use memmove for overlapping regions
        memmove(dst, src, len);
        return retval;
    }*/

    if (len >= 128) {
        uintptr_t misalign = ((uintptr_t)dst) & 31;
        if (misalign) {
            size_t delta = 32 - misalign; // bytes to copy to reach 32-byte alignment
            if (delta > len) delta = len; // defensive, though len >= 128 here
            memcpy(dst, src, delta);
            dst += delta;
            src += delta;
            len -= delta;
        }

        size_t blocks = len / 128;
        len = len % 128;

        int src_aligned = (((uintptr_t)src) & 31) == 0;
        if(src_aligned) {
          for (size_t i = 0; i < blocks; i++) {
              // prefetch next 256-512 bytes
              _mm_prefetch((const char *)src + 256, _MM_HINT_NTA);
              _mm_prefetch((const char *)src + 512, _MM_HINT_NTA);
              __m256i ymm0, ymm1, ymm2, ymm3;
              ymm0 = _mm256_load_si256((const __m256i *)(src + 0));
              ymm1 = _mm256_load_si256((const __m256i *)(src + 32));
              ymm2 = _mm256_load_si256((const __m256i *)(src + 64));
              ymm3 = _mm256_load_si256((const __m256i *)(src + 96));
              _mm256_stream_si256((__m256i *)(dst + 0), ymm0);
              _mm256_stream_si256((__m256i *)(dst + 32), ymm1);
              _mm256_stream_si256((__m256i *)(dst + 64), ymm2);
              _mm256_stream_si256((__m256i *)(dst + 96), ymm3);

              src += 128;
              dst += 128;
          }
        } else {
            for (size_t i = 0; i < blocks; i++) {
                _mm_prefetch((const char *)src + 256, _MM_HINT_NTA);
                _mm_prefetch((const char *)src + 512, _MM_HINT_NTA);
                __m256i ymm0, ymm1, ymm2, ymm3;
                ymm0 = _mm256_loadu_si256((const __m256i *)(src + 0));
                ymm1 = _mm256_loadu_si256((const __m256i *)(src + 32));
                ymm2 = _mm256_loadu_si256((const __m256i *)(src + 64));
                ymm3 = _mm256_loadu_si256((const __m256i *)(src + 96));
                _mm256_stream_si256((__m256i *)(dst + 0), ymm0);
                _mm256_stream_si256((__m256i *)(dst + 32), ymm1);
                _mm256_stream_si256((__m256i *)(dst + 64), ymm2);
                _mm256_stream_si256((__m256i *)(dst + 96), ymm3);
                src += 128;
                dst += 128;
            }
        }

        _mm_sfence(); // ensure stores are visible
    }

    if (len) {
        memcpy(dst, src, len);
    }

    return retval;
}

void *avx2_memset(void *dst0, int c, size_t len0) {
    void *retval = dst0;
    uint8_t *dst = (uint8_t *)dst0;
    size_t len = len0;

    if (len == 0) return retval;

    if (len < 128) {
        small_memset(dst, c, len);
        return retval;
    }

    uintptr_t misalign = (uintptr_t)dst & 31;
    if (misalign) {
        size_t delta = 32 - misalign;
        if (delta > len) delta = len;
        memset(dst, c, delta);
        dst += delta;
        len -= delta;
    }

    __m256i ymm_val = _mm256_set1_epi8((char)c);

    size_t blocks = len / 128;
    size_t remaining = len % 128;

    for (size_t i = 0; i < blocks; i++) {
        _mm_prefetch((const char *)(dst + 256), _MM_HINT_NTA);
        _mm_prefetch((const char *)(dst + 512), _MM_HINT_NTA);

        _mm256_stream_si256((__m256i *)(dst + 0), ymm_val);
        _mm256_stream_si256((__m256i *)(dst + 32), ymm_val);
        _mm256_stream_si256((__m256i *)(dst + 64), ymm_val);
        _mm256_stream_si256((__m256i *)(dst + 96), ymm_val);

        dst += 128;
    }

    _mm_sfence();
    if (remaining) {
        small_memset(dst, c, remaining);
    }

    return retval;
}
#endif

#ifdef HAVE_ASM_AVX
void *avx_memset(void *dst0, int c, size_t len0) {
    void *retval = dst0;
    uint8_t *dst = (uint8_t *)dst0;
    size_t len = len0;

    if (len == 0) return retval;

    if (len < 128) {
        small_memset(dst, c, len);
        return retval;
    }

    uintptr_t misalign = (uintptr_t)dst & 31;
    if (misalign) {
        size_t delta = 32 - misalign;
        if (delta > len) delta = len;
        memset(dst, c, delta);
        dst += delta;
        len -= delta;
    }

    __m256i ymm_val = _mm256_set1_epi8((char)c);

    size_t blocks = len / 128;
    size_t remaining = len % 128;

    for (size_t i = 0; i < blocks; i++) {
        _mm_prefetch((const char *)(dst + 256), _MM_HINT_NTA);
        _mm_prefetch((const char *)(dst + 512), _MM_HINT_NTA);
        _mm256_store_si256((__m256i *)(dst + 0), ymm_val);
        _mm256_store_si256((__m256i *)(dst + 32), ymm_val);
        _mm256_store_si256((__m256i *)(dst + 64), ymm_val);
        _mm256_store_si256((__m256i *)(dst + 96), ymm_val);

        dst += 128;
    }

    if (remaining) {
        small_memset(dst, c, remaining);
    }

    return retval;
}
#endif

#ifdef HAVE_ASM_AVX512
#define AVX512_MMREG_SIZE 64  // 512-bit register = 64 bytes
static void * avx512_memcpy(void *to, const void *from, size_t len) {
    void *retval = to;
    size_t i;
    __asm__ __volatile__ (
        "   prefetchnta (%0)\n"
        "   prefetchnta 64(%0)\n"
        "   prefetchnta 128(%0)\n"
        "   prefetchnta 192(%0)\n"
        "   prefetchnta 256(%0)\n"
        "   prefetchnta 320(%0)\n"
        "   prefetchnta 384(%0)\n"
        "   prefetchnta 448(%0)\n"
        "   prefetchnta 512(%0)\n"
        "   prefetchnta 576(%0)\n"
        "   prefetchnta 640(%0)\n"
        :: "r" (from));

    if (len >= 512) {
        register uintptr_t delta;
        delta = ((uintptr_t)to) & (AVX512_MMREG_SIZE - 1);
        if (delta) {
            delta = AVX512_MMREG_SIZE - delta;
            len -= delta;
            memcpy(to, from, delta);
            from = (char *)from + delta;
            to = (char *)to + delta;
        }

        i = len >> 8; // len / 256
        len &= 255;
        if (((uintptr_t)from) & 63) {
            for (; i > 0; i--) {
                __asm__ __volatile__ (
                "vmovups    (%0), %%ymm0\n"
                "vmovups  32(%0), %%ymm1\n"
                "vmovups  64(%0), %%ymm2\n"
                "vmovups  96(%0), %%ymm3\n"
                "vmovups  128(%0), %%ymm4\n"
                "vmovups  160(%0), %%ymm5\n"
                "vmovups  192(%0), %%ymm6\n"
                "vmovups  224(%0), %%ymm7\n"
                "vmovntps %%ymm0,   (%1)\n"
                "vmovntps %%ymm1, 32(%1)\n"
                "vmovntps %%ymm2, 64(%1)\n"
                "vmovntps %%ymm3, 96(%1)\n"
                "vmovntps %%ymm4, 128(%1)\n"
                "vmovntps %%ymm5, 160(%1)\n"
                "vmovntps %%ymm6, 192(%1)\n"
                "vmovntps %%ymm7, 224(%1)\n"
                :: "r" (from), "r" (to) : "memory");
                from = ((const unsigned char *)from) + 256;
                to = ((unsigned char *)to) + 256;
            }
        } else {
            for (; i > 0; i--) {
                __asm__ __volatile__ (
                "vmovaps    (%0), %%ymm0\n"
                "vmovaps  32(%0), %%ymm1\n"
                "vmovaps  64(%0), %%ymm2\n"
                "vmovaps  96(%0), %%ymm3\n"
                "vmovaps  128(%0), %%ymm4\n"
                "vmovaps  160(%0), %%ymm5\n"
                "vmovaps  192(%0), %%ymm6\n"
                "vmovaps  224(%0), %%ymm7\n"
                "vmovntps %%ymm0,   (%1)\n"
                "vmovntps %%ymm1, 32(%1)\n"
                "vmovntps %%ymm2, 64(%1)\n"
                "vmovntps %%ymm3, 96(%1)\n"
                "vmovntps %%ymm4, 128(%1)\n"
                "vmovntps %%ymm5, 160(%1)\n"
                "vmovntps %%ymm6, 192(%1)\n"
                "vmovntps %%ymm7, 224(%1)\n"
                :: "r" (from), "r" (to) : "memory");
                from = ((const unsigned char *)from) + 256;
                to = ((unsigned char *)to) + 256;
            }
        }
        __asm__ __volatile__ ("sfence" ::: "memory");
    }

    if (len) memcpy(to, from, len);

    return retval;
}
#endif

#ifdef HAVE_ASM_AVX

static void* avx_memcpy(void *destination, const void *source, size_t size)
{
	unsigned char *dst = (unsigned char*)destination;
	const unsigned char *src = (const unsigned char*)source;
	static size_t cachesize = 0x200000; // L3-cache size
	size_t padding;

	if (size <= 256) {
		__memcpy(dst, src, size);
		_mm256_zeroupper();
		return destination;
	}

	// align destination to 16 bytes boundary
	padding = (32 - (((size_t)dst) & 31)) & 31;
	__m256i head = _mm256_loadu_si256((const __m256i*)src);
	_mm256_storeu_si256((__m256i*)dst, head);
	dst += padding;
	src += padding;
	size -= padding;

	// medium size copy
	if (size <= cachesize) {
		__m256i c0, c1, c2, c3, c4, c5, c6, c7;

		for (; size >= 256; size -= 256) {
			c0 = _mm256_loadu_si256(((const __m256i*)src) + 0);
			c1 = _mm256_loadu_si256(((const __m256i*)src) + 1);
			c2 = _mm256_loadu_si256(((const __m256i*)src) + 2);
			c3 = _mm256_loadu_si256(((const __m256i*)src) + 3);
			c4 = _mm256_loadu_si256(((const __m256i*)src) + 4);
			c5 = _mm256_loadu_si256(((const __m256i*)src) + 5);
			c6 = _mm256_loadu_si256(((const __m256i*)src) + 6);
			c7 = _mm256_loadu_si256(((const __m256i*)src) + 7);
			_mm_prefetch((const char*)(src + 512), _MM_HINT_NTA);
			src += 256;
			_mm256_storeu_si256((((__m256i*)dst) + 0), c0);
			_mm256_storeu_si256((((__m256i*)dst) + 1), c1);
			_mm256_storeu_si256((((__m256i*)dst) + 2), c2);
			_mm256_storeu_si256((((__m256i*)dst) + 3), c3);
			_mm256_storeu_si256((((__m256i*)dst) + 4), c4);
			_mm256_storeu_si256((((__m256i*)dst) + 5), c5);
			_mm256_storeu_si256((((__m256i*)dst) + 6), c6);
			_mm256_storeu_si256((((__m256i*)dst) + 7), c7);
			dst += 256;
		}
	}
	else {		// big memory copy
		__m256i c0, c1, c2, c3, c4, c5, c6, c7;
		/* __m256i c0, c1, c2, c3, c4, c5, c6, c7; */

		_mm_prefetch((const char*)(src), _MM_HINT_NTA);

		if ((((size_t)src) & 31) == 0) {	// source aligned
			for (; size >= 256; size -= 256) {
				c0 = _mm256_load_si256(((const __m256i*)src) + 0);
				c1 = _mm256_load_si256(((const __m256i*)src) + 1);
				c2 = _mm256_load_si256(((const __m256i*)src) + 2);
				c3 = _mm256_load_si256(((const __m256i*)src) + 3);
				c4 = _mm256_load_si256(((const __m256i*)src) + 4);
				c5 = _mm256_load_si256(((const __m256i*)src) + 5);
				c6 = _mm256_load_si256(((const __m256i*)src) + 6);
				c7 = _mm256_load_si256(((const __m256i*)src) + 7);
				_mm_prefetch((const char*)(src + 512), _MM_HINT_NTA);
				src += 256;
				_mm256_stream_si256((((__m256i*)dst) + 0), c0);
				_mm256_stream_si256((((__m256i*)dst) + 1), c1);
				_mm256_stream_si256((((__m256i*)dst) + 2), c2);
				_mm256_stream_si256((((__m256i*)dst) + 3), c3);
				_mm256_stream_si256((((__m256i*)dst) + 4), c4);
				_mm256_stream_si256((((__m256i*)dst) + 5), c5);
				_mm256_stream_si256((((__m256i*)dst) + 6), c6);
				_mm256_stream_si256((((__m256i*)dst) + 7), c7);
				dst += 256;
			}
		}
		else {							// source unaligned
			for (; size >= 256; size -= 256) {
				c0 = _mm256_loadu_si256(((const __m256i*)src) + 0);
				c1 = _mm256_loadu_si256(((const __m256i*)src) + 1);
				c2 = _mm256_loadu_si256(((const __m256i*)src) + 2);
				c3 = _mm256_loadu_si256(((const __m256i*)src) + 3);
				c4 = _mm256_loadu_si256(((const __m256i*)src) + 4);
				c5 = _mm256_loadu_si256(((const __m256i*)src) + 5);
				c6 = _mm256_loadu_si256(((const __m256i*)src) + 6);
				c7 = _mm256_loadu_si256(((const __m256i*)src) + 7);
				_mm_prefetch((const char*)(src + 512), _MM_HINT_NTA);
				src += 256;
				_mm256_stream_si256((((__m256i*)dst) + 0), c0);
				_mm256_stream_si256((((__m256i*)dst) + 1), c1);
				_mm256_stream_si256((((__m256i*)dst) + 2), c2);
				_mm256_stream_si256((((__m256i*)dst) + 3), c3);
				_mm256_stream_si256((((__m256i*)dst) + 4), c4);
				_mm256_stream_si256((((__m256i*)dst) + 5), c5);
				_mm256_stream_si256((((__m256i*)dst) + 6), c6);
				_mm256_stream_si256((((__m256i*)dst) + 7), c7);
				dst += 256;
			}
		}
		_mm_sfence();
	}

	__memcpy(dst, src, size);
	_mm256_zeroupper();

	return destination;
}


static void * avx_memcpy2(void * to, const void * from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  if(len >= 256)
  {
    register uintptr_t delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((uintptr_t)to)&(AVX_MMREG_SIZE-1);
    if(delta)
    {
      delta=AVX_MMREG_SIZE-delta;
      len -= delta;
      small_memcpy(to, from, delta);
    }
    i = len >> 8; 
    len&=255;

    __asm__ __volatile__ (
      "prefetchnta 64(%0)\n"
      "prefetchnta 128(%0)\n"
      "prefetchnta 192(%0)\n"
      "prefetchnta 256(%0)\n"
      : : "r" (from)
    );

    if(((uintptr_t)from) & 31)
      for(; i>0; i--)
      {
        __asm__ __volatile__ (
        "vmovups    (%0), %%ymm0\n"
        "vmovups  32(%0), %%ymm1\n"
        "vmovups  64(%0), %%ymm2\n"
        "vmovups  96(%0), %%ymm3\n"
        "vmovups  128(%0), %%ymm4\n"
        "vmovups  160(%0), %%ymm5\n"
        "vmovups  192(%0), %%ymm6\n"
        "vmovups  224(%0), %%ymm7\n"	
        "vmovntps %%ymm0,   (%1)\n"
        "vmovntps %%ymm1, 32(%1)\n"
        "vmovntps %%ymm2, 64(%1)\n"
        "vmovntps %%ymm3, 96(%1)\n"
        "vmovntps %%ymm4, 128(%1)\n"
        "vmovntps %%ymm5, 160(%1)\n"
        "vmovntps %%ymm6, 192(%1)\n"
        "vmovntps %%ymm7, 224(%1)\n"	
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 256;
        to = ((unsigned char *)to) + 256;
      }
    else
      for(; i>0; i--)
      {
        __asm__ __volatile__ (
        "vmovaps    (%0), %%ymm0\n"
        "vmovaps  32(%0), %%ymm1\n"
        "vmovaps  64(%0), %%ymm2\n"
        "vmovaps  96(%0), %%ymm3\n"
        "vmovaps  128(%0), %%ymm4\n"
        "vmovaps  160(%0), %%ymm5\n"
        "vmovaps  192(%0), %%ymm6\n"
        "vmovaps  224(%0), %%ymm7\n"	
        "vmovntps %%ymm0,   (%1)\n"
        "vmovntps %%ymm1, 32(%1)\n"
        "vmovntps %%ymm2, 64(%1)\n"
        "vmovntps %%ymm3, 96(%1)\n"
        "vmovntps %%ymm4, 128(%1)\n"
        "vmovntps %%ymm5, 160(%1)\n"
        "vmovntps %%ymm6, 192(%1)\n"
        "vmovntps %%ymm7, 224(%1)\n"	
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 256;
        to = ((unsigned char *)to) + 256;
      }
    /* since movntq is weakly-ordered, a "sfence"
     * is needed to become ordered again. */
    __asm__ __volatile__ ("sfence":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) __memcpy(to, from, len);
  return retval;
}
#endif /* HAVE_ASM_AVX */

#ifdef HAVE_ASM_MMX
static void * mmx_memcpy(void * to, const void * from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  if(len >= _MMX1_MIN_LEN)
  {
    register uintptr_t delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((uintptr_t)to)&(MMX_MMREG_SIZE-1);
    if(delta)
    {
      delta=MMX_MMREG_SIZE-delta;
      len -= delta;
      small_memcpy(to, from, delta);
    }
    i = len >> 6; /* len/64 */
    len&=63;
    for(; i>0; i--)
    {
      __asm__ __volatile__ (
      "movq (%0), %%mm0\n"
      "movq 8(%0), %%mm1\n"
      "movq 16(%0), %%mm2\n"
      "movq 24(%0), %%mm3\n"
      "movq 32(%0), %%mm4\n"
      "movq 40(%0), %%mm5\n"
      "movq 48(%0), %%mm6\n"
      "movq 56(%0), %%mm7\n"
      "movq %%mm0, (%1)\n"
      "movq %%mm1, 8(%1)\n"
      "movq %%mm2, 16(%1)\n"
      "movq %%mm3, 24(%1)\n"
      "movq %%mm4, 32(%1)\n"
      "movq %%mm5, 40(%1)\n"
      "movq %%mm6, 48(%1)\n"
      "movq %%mm7, 56(%1)\n"
      :: "r" (from), "r" (to) : "memory");
      from = ((const unsigned char *)from) + 64;
      to = ((unsigned char *)to) + 64;
    }
    __asm__ __volatile__ ("emms":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) __memcpy(to, from, len);
  return retval;
}
#endif

#ifdef HAVE_ASM_MMX2
static void * mmx2_memcpy(void * to, const void * from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  /* PREFETCH has effect even for MOVSB instruction ;) */
/*  __asm__ __volatile__ (
    "   prefetchnta (%0)\n"
    "   prefetchnta 32(%0)\n"
    "   prefetchnta 64(%0)\n"
    "   prefetchnta 96(%0)\n"
    "   prefetchnta 128(%0)\n"
    "   prefetchnta 160(%0)\n"
    "   prefetchnta 192(%0)\n"
    "   prefetchnta 224(%0)\n"
    "   prefetchnta 256(%0)\n"
    "   prefetchnta 288(%0)\n"
    : : "r" (from) ); */

  if(len >= MIN_LEN)
  {
    register uintptr_t delta;
    /* Align destinition to MMREG_SIZE -boundary */
    delta = ((uintptr_t)to)&(MMX_MMREG_SIZE-1);
    if(delta)
    {
      delta=MMX_MMREG_SIZE-delta;
      len -= delta;
      small_memcpy(to, from, delta);
    }
    i = len >> 6; /* len/64 */
    len&=63;
    for(; i>0; i--)
    {
      __asm__ __volatile__ (
//      "prefetchnta 320(%0)\n"
//      "prefetchnta 352(%0)\n"
			  
      "movq (%0), %%mm0\n"
      "movq 8(%0), %%mm1\n"
      "movq 16(%0), %%mm2\n"
      "movq 24(%0), %%mm3\n"
      "movq 32(%0), %%mm4\n"
      "movq 40(%0), %%mm5\n"
      "movq 48(%0), %%mm6\n"
      "movq 56(%0), %%mm7\n"
      "movntq %%mm0, (%1)\n"
      "movntq %%mm1, 8(%1)\n"
      "movntq %%mm2, 16(%1)\n"
      "movntq %%mm3, 24(%1)\n"
      "movntq %%mm4, 32(%1)\n"
      "movntq %%mm5, 40(%1)\n"
      "movntq %%mm6, 48(%1)\n"
      "movntq %%mm7, 56(%1)\n"
      :: "r" (from), "r" (to) : "memory");
      from = ((const unsigned char *)from) + 64;
      to = ((unsigned char *)to) + 64;
    }
     /* since movntq is weakly-ordered, a "sfence"
     * is needed to become ordered again. */
    __asm__ __volatile__ ("sfence":::"memory");
    __asm__ __volatile__ ("emms":::"memory");
  }
  /*
   *	Now do the tail of the block
   */
  if(len) __memcpy(to, from, len);
  return retval;
}
#endif

#if defined (HAVE_ASM_MMX) || defined( HAVE_ASM_SSE ) || defined( HAVE_ASM_MMX2 )
static void *fast_memcpy(void * to, const void * from, size_t len)
{
	void *retval;
	size_t i;
	retval = to;
#ifndef HAVE_ONLY_MMX1
        /* PREFETCH has effect even for MOVSB instruction ;) */
	__asm__ volatile (
			PREFETCH" (%0)\n"
			PREFETCH" 64(%0)\n"
			PREFETCH" 128(%0)\n"
			PREFETCH" 192(%0)\n"
			PREFETCH" 256(%0)\n"
		: : "r" (from) );
#endif
	
	if(len >= MIN_LEN)
	{
		register x86_reg delta;
        /* Align destinition to MMREG_SIZE -boundary */
        delta = ((intptr_t)to)&(AC_MMREG_SIZE-1);
        if(delta)
		{
			delta=AC_MMREG_SIZE-delta;
			len -= delta;
			small_memcpy(to, from, delta);
		}
		i = len >> 6; /* len/64 */
		len&=63;
        /*
           This algorithm is top effective when the code consequently
           reads and writes blocks which have size of cache line.
           Size of cache line is processor-dependent.
           It will, however, be a minimum of 32 bytes on any processors.
           It would be better to have a number of instructions which
           perform reading and writing to be multiple to a number of
           processor's decoders, but it's not always possible.
        */
#if HAVE_ASM_SSE /* Only P3 (may be Cyrix3) */
		if(((intptr_t)from) & 15)
		/* if SRC is misaligned */
		for(; i>0; i--)
		{
			__asm__ volatile (
			PREFETCH" 320(%0)\n"
			"movups (%0), %%xmm0\n"
			"movups 16(%0), %%xmm1\n"
			"movups 32(%0), %%xmm2\n"
			"movups 48(%0), %%xmm3\n"
			"movntps %%xmm0, (%1)\n"
			"movntps %%xmm1, 16(%1)\n"
			"movntps %%xmm2, 32(%1)\n"
			"movntps %%xmm3, 48(%1)\n"
			:: "r" (from), "r" (to) : "memory");
			from=((const unsigned char *) from)+64;
			to=((unsigned char *)to)+64;
		}
		else
		/*
		   Only if SRC is aligned on 16-byte boundary.
		   It allows to use movaps instead of movups, which required data
		   to be aligned or a general-protection exception (#GP) is generated.
		*/
		for(; i>0; i--)
		{
			__asm__ volatile (
			PREFETCH" 320(%0)\n"
			"movaps (%0), %%xmm0\n"
			"movaps 16(%0), %%xmm1\n"
			"movaps 32(%0), %%xmm2\n"
			"movaps 48(%0), %%xmm3\n"
			"movntps %%xmm0, (%1)\n"
			"movntps %%xmm1, 16(%1)\n"
			"movntps %%xmm2, 32(%1)\n"
			"movntps %%xmm3, 48(%1)\n"
			:: "r" (from), "r" (to) : "memory");
			from=((const unsigned char *)from)+64;
			to=((unsigned char *)to)+64;
		}
#else
		// Align destination at BLOCK_SIZE boundary
		for(; ((intptr_t)to & (BLOCK_SIZE-1)) && i>0; i--)
		{
			__asm__ volatile (
#ifndef HAVE_ONLY_MMX1
			PREFETCH" 320(%0)\n"
#endif
			"movq (%0), %%mm0\n"
			"movq 8(%0), %%mm1\n"
			"movq 16(%0), %%mm2\n"
			"movq 24(%0), %%mm3\n"
			"movq 32(%0), %%mm4\n"
			"movq 40(%0), %%mm5\n"
			"movq 48(%0), %%mm6\n"
			"movq 56(%0), %%mm7\n"
			MOVNTQ" %%mm0, (%1)\n"
			MOVNTQ" %%mm1, 8(%1)\n"
			MOVNTQ" %%mm2, 16(%1)\n"
			MOVNTQ" %%mm3, 24(%1)\n"
			MOVNTQ" %%mm4, 32(%1)\n"
			MOVNTQ" %%mm5, 40(%1)\n"
			MOVNTQ" %%mm6, 48(%1)\n"
			MOVNTQ" %%mm7, 56(%1)\n"
			:: "r" (from), "r" (to) : "memory");
			from=((const unsigned char *)from)+64;
			to=((unsigned char *)to)+64;
		}

		//	printf(" %d %d\n", (int)from&1023, (int)to&1023);
		// Pure Assembly cuz gcc is a bit unpredictable ;)
		if(i>=BLOCK_SIZE/64)
			__asm__ volatile(
				"xor %%"REG_a", %%"REG_a"	\n\t"
				ASMALIGN(4)
				"1:			\n\t"
				"movl (%0, %%"REG_a"), %%ecx	\n\t"
				"movl 32(%0, %%"REG_a"), %%ecx	\n\t"
				"movl 64(%0, %%"REG_a"), %%ecx	\n\t"
				"movl 96(%0, %%"REG_a"), %%ecx	\n\t"
				"add $128, %%"REG_a"		\n\t"
				"cmp %3, %%"REG_a"		\n\t"
				" jb 1b				\n\t"
				"xor %%"REG_a", %%"REG_a"	\n\t"
				ASMALIGN(4)
				"2:			\n\t"
				"movq (%0, %%"REG_a"), %%mm0\n"
				"movq 8(%0, %%"REG_a"), %%mm1\n"
				"movq 16(%0, %%"REG_a"), %%mm2\n"
				"movq 24(%0, %%"REG_a"), %%mm3\n"
				"movq 32(%0, %%"REG_a"), %%mm4\n"
				"movq 40(%0, %%"REG_a"), %%mm5\n"
				"movq 48(%0, %%"REG_a"), %%mm6\n"
				"movq 56(%0, %%"REG_a"), %%mm7\n"
				MOVNTQ" %%mm0, (%1, %%"REG_a")\n"
				MOVNTQ" %%mm1, 8(%1, %%"REG_a")\n"
				MOVNTQ" %%mm2, 16(%1, %%"REG_a")\n"
				MOVNTQ" %%mm3, 24(%1, %%"REG_a")\n"
				MOVNTQ" %%mm4, 32(%1, %%"REG_a")\n"
				MOVNTQ" %%mm5, 40(%1, %%"REG_a")\n"
				MOVNTQ" %%mm6, 48(%1, %%"REG_a")\n"
				MOVNTQ" %%mm7, 56(%1, %%"REG_a")\n"
				"add $64, %%"REG_a"		\n\t"
				"cmp %3, %%"REG_a"		\n\t"
				"jb 2b				\n\t"
#if CONFUSION_FACTOR > 0
				// a few percent speedup on out of order executing CPUs
				"mov %5, %%"REG_a"		\n\t"
				"2:			\n\t"
				"movl (%0), %%ecx	\n\t"
				"movl (%0), %%ecx	\n\t"
				"movl (%0), %%ecx	\n\t"
				"movl (%0), %%ecx	\n\t"
				"dec %%"REG_a"		\n\t"
				" jnz 2b		\n\t"
#endif

				"xor %%"REG_a", %%"REG_a"	\n\t"
				"add %3, %0		\n\t"
				"add %3, %1		\n\t"
				"sub %4, %2		\n\t"
				"cmp %4, %2		\n\t"
				" jae 1b		\n\t"
					: "+r" (from), "+r" (to), "+r" (i)
					: "r" ((x86_reg)BLOCK_SIZE), "i" (BLOCK_SIZE/64), "i" ((x86_reg)CONFUSION_FACTOR)
					: "%"REG_a, "%ecx"
			);

			for(; i>0; i--)
			{
				__asm__ volatile (
#ifndef HAVE_ONLY_MMX1
				PREFETCH" 320(%0)\n"
#endif
				"movq (%0), %%mm0\n"
				"movq 8(%0), %%mm1\n"
				"movq 16(%0), %%mm2\n"
				"movq 24(%0), %%mm3\n"
				"movq 32(%0), %%mm4\n"
				"movq 40(%0), %%mm5\n"
				"movq 48(%0), %%mm6\n"
				"movq 56(%0), %%mm7\n"
				MOVNTQ" %%mm0, (%1)\n"
				MOVNTQ" %%mm1, 8(%1)\n"
				MOVNTQ" %%mm2, 16(%1)\n"
				MOVNTQ" %%mm3, 24(%1)\n"
				MOVNTQ" %%mm4, 32(%1)\n"
				MOVNTQ" %%mm5, 40(%1)\n"
				MOVNTQ" %%mm6, 48(%1)\n"
				MOVNTQ" %%mm7, 56(%1)\n"
				:: "r" (from), "r" (to) : "memory");
				from=((const unsigned char *)from)+64;
				to=((unsigned char *)to)+64;
			}
#endif /* Have SSE */

#ifdef HAVE_ASM_MMX2
        /* since movntq is weakly-ordered, a "sfence"
		 * is needed to become ordered again. */
		__asm__ volatile ("sfence":::"memory");
#endif

#ifndef HAVE_ASM_SSE
		/* enables to use FPU */
		__asm__ volatile (EMMS:::"memory");
#endif
	}
	/*
	 *	Now do the tail of the block
	 */
	if(len) small_memcpy(to, from, len);
	return retval;
}
#endif

#ifdef HAVE_ASM_SSE4_2
void *sse42_memset(void *ptr, uint8_t value, size_t num) {
    uint8_t *dest = (uint8_t *)ptr;
    uint8_t byte_value = (uint8_t)value;

    if (num < 128) {
        return memset(dest, byte_value, num);
    }

    size_t alignment_offset = (size_t)dest % 16;
    size_t offset = 16 - alignment_offset;

    if (alignment_offset > 0) {
        memset(dest, byte_value, offset);
        dest += offset;
        num -= offset;
    }

    __m128i xmm_value = _mm_set1_epi8(byte_value);
    size_t num_xmm_sets = num / 16;

    for (size_t i = 0; i < num_xmm_sets; i++) {
        _mm_storeu_si128((__m128i *)dest, xmm_value);
        dest += 16;
    }

    size_t remaining_bytes = num % 16;
    if (remaining_bytes > 0) {
        memset(dest, byte_value, remaining_bytes);
    }

    return ptr;
}
void *sse42_aligned_memset(void *to, uint8_t value, size_t len) {

    if (len < 128) {
        return memset(to, value, len);
    }

	if(! ( ((uintptr_t) to % 16 == 0) && (len % 16 == 0))) {
		return sse42_memset(to,value,len);
	}

	void *retval = to;

	__m128i xmm_value = _mm_set1_epi8(value);

	while (len >= 16) {
		_mm_store_si128((__m128i *)to, xmm_value);
		to = (void *)((char *)to + 16);
		len -= 16;
	}

	if (len > 0) {
		memset(to, value, len);
	}

	return retval;
}
#endif
#ifdef HAVE_ASM_SSE4_1
void *sse41_memset_v2(void *to, uint8_t value, size_t len) {
  void *retval = to;
  size_t remainder = (uintptr_t)to % 16;

  if (remainder) {
    size_t offset = 16 - remainder;
    if (offset > len) offset = len;
    small_memset(to, value, offset);
    to = (void *)((char *)to + offset);
    len -= offset;
  }

  __m128i xmm_value = _mm_set1_epi8(value);

  while (len >= 16) {
    if ((uintptr_t)to % 16 == 0) {
      _mm_store_si128((__m128i *)to, xmm_value);
    } else {
      memcpy(to, &xmm_value, 16);
    }
    to = (void *)((char *)to + 16);
    len -= 16;
  }

  if (len > 0) {
    small_memset(to, value, len);
  }

  return retval;
}

void *sse41_memset(void *to, uint8_t value, size_t len) {

	if (len < 128) {
        return memset(to, value, len);
    }

	void *retval = to;

	uintptr_t delta = ((uintptr_t)to) & (SSE_MMREG_SIZE - 1);
	if (delta) {
		delta = SSE_MMREG_SIZE - delta;
		len -= delta;
		memset(to, value, delta);
		to = (void *)((char *)to + delta);
	}

	_mm_prefetch((void *)((char *)to + 128), _MM_HINT_NTA);
	_mm_prefetch((void *)((char *)to + 256), _MM_HINT_NTA);

	__m128i xmm0 = _mm_set1_epi8(value);

	while (len >= 128) {
		_mm_store_si128((__m128i *)to, xmm0);
		_mm_store_si128((__m128i *)((char *)to + 16), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 32), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 48), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 64), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 80), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 96), xmm0);
		_mm_store_si128((__m128i *)((char *)to + 112), xmm0);
		
		to = (void *)((char *)to + 128);
		len -= 128;
	}

	if (len >= 16) {
		memset(to, value, len);
	} else {
		for (size_t i = 0; i < len; i++) {
			*((uint8_t *)to + i) = value;
		}
	}

	return retval;
}
#endif


static void *linux_kernel_memcpy(void *to, const void *from, size_t len) {
     return __memcpy(to,from,len);
}

#endif

#ifdef HAVE_ARM_NEON
static inline void memcpy_neon_256( uint8_t *dst, const uint8_t *src )
{
	__asm__ volatile(	"pld [%[src], #64]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #128]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #192]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #256]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #320]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #384]" :: [src] "r" (src));
	__asm__ volatile(	"pld [%[src], #448]" :: [src] "r" (src));

	__asm__ volatile(	
				"vld1.8 {d0-d3}, [%[src]]!\n\t"
				"vld1.8 {d4-d7}, [%[src]]!\n\t"
				"vld1.8 {d8-d11},[%[src]]!\n\t"
				"vld1.8 {d12-d15},[%[src]]!\n\t"
				"vld1.8 {d16-d19}, [%[src]]!\n\t"
				"vld1.8 {d20-d23}, [%[src]]!\n\t"
				"vld1.8 {d24-d27},[%[src]]!\n\t"
				"vld1.8 {d28-d31},[%[src]]\n\t"
				"vst1.8 {d0-d3}, [%[dst]]!\n\t"
				"vst1.8 {d4-d7}, [%[dst]]!\n\t"
				"vst1.8 {d8-d11}, [%[dst]]!\n\t"
				"vst1.8 {d12-d15}, [%[dst]]!\n\t"
				"vst1.8 {d16-d19}, [%[dst]]!\n\t"
				"vst1.8 {d20-d23}, [%[dst]]!\n\t"
				"vst1.8 {d24-d27}, [%[dst]]!\n\t"
				"vst1.8 {d28-d31}, [%[dst]]!\n\t"
				
				: [src] "+r" (src), [dst] "+r" (dst)
				:: "memory" , 
						"d0", "d1", "d2", "d3", "d4", "d5", "d6" , "d7",
						"d8", "d9", "d10","d11","d12","d13","d14", "d15",
						"d16","d17","d18","d19","d20","d21","d22", "d23",
						"d24","d23","d24","d25","d26","d27","d28", "d29",
						"d30","d31"
				 );

}

static void *memcpy_neon( void *to, const void *from, size_t n )
{
	void *retval = to;

	if( n < 16 ) {
		memcpy( to,from,n );
		return retval;
	}

	size_t i = n >> 8;
	size_t r = n & 255;

	uint8_t *src = (uint8_t*) from;
	uint8_t *dst = (uint8_t*) to;

	for( ; i > 0; i -- ) {
		memcpy_neon_256( dst, src );
		src += 256;
		dst += 256;	
	}

	if( r ) {
		memcpy(dst,src,r);
	}

	return retval;
}
#endif
#ifdef HAVE_ARM_ASIMD
static inline void memcpy_neon_256(uint8_t *dst, const uint8_t *src) {
    __asm__ volatile(
        "prfm pldl1keep, [%[src], #64]\n\t"
        "prfm pldl1keep, [%[src], #128]\n\t"
        "prfm pldl1keep, [%[src], #192]\n\t"
        "prfm pldl1keep, [%[src], #256]\n\t"
        "prfm pldl1keep, [%[src], #320]\n\t"
        "prfm pldl1keep, [%[src], #384]\n\t"
        "prfm pldl1keep, [%[src], #448]\n\t"
        "ld1 {v0.8b, v1.8b, v2.8b, v3.8b}, [%[src]], #32\n\t"
        "ld1 {v4.8b, v5.8b, v6.8b, v7.8b}, [%[src]], #32\n\t"
        "ld1 {v8.8b, v9.8b, v10.8b, v11.8b}, [%[src]], #32\n\t"
        "ld1 {v12.8b, v13.8b, v14.8b, v15.8b}, [%[src]], #32\n\t"
        "ld1 {v16.8b, v17.8b, v18.8b, v19.8b}, [%[src]], #32\n\t"
        "ld1 {v20.8b, v21.8b, v22.8b, v23.8b}, [%[src]], #32\n\t"
        "ld1 {v24.8b, v25.8b, v26.8b, v27.8b}, [%[src]], #32\n\t"
        "ld1 {v28.8b, v29.8b, v30.8b, v31.8b}, [%[src]]\n\t"
        "st1 {v0.8b, v1.8b, v2.8b, v3.8b}, [%[dst]], #32\n\t"
        "st1 {v4.8b, v5.8b, v6.8b, v7.8b}, [%[dst]], #32\n\t"
        "st1 {v8.8b, v9.8b, v10.8b, v11.8b}, [%[dst]], #32\n\t"
        "st1 {v12.8b, v13.8b, v14.8b, v15.8b}, [%[dst]], #32\n\t"
        "st1 {v16.8b, v17.8b, v18.8b, v19.8b}, [%[dst]], #32\n\t"
        "st1 {v20.8b, v21.8b, v22.8b, v23.8b}, [%[dst]], #32\n\t"
        "st1 {v24.8b, v25.8b, v26.8b, v27.8b}, [%[dst]], #32\n\t"
        "st1 {v28.8b, v29.8b, v30.8b, v31.8b}, [%[dst]]\n\t"
        : [src] "+r"(src), [dst] "+r"(dst)
        :
        : "memory", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
          "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
          "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
          "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
    );
}

static void *memcpy_neon(void *to, const void *from, size_t n) {
    void *retval = to;

    if (n < 16) {
        memcpy(to, from, n);
        return retval;
    }

    size_t i = n >> 8;
    size_t r = n & 255;

    uint8_t *src = (uint8_t *)from;
    uint8_t *dst = (uint8_t *)to;

    for (; i > 0; i--) {
        memcpy_neon_256(dst, src);
        src += 256;
        dst += 256;
    }

    if (r) {
        memcpy(dst, src, r);
    }

    return retval;
}


static void memcpy_asimd_256_v3(uint8_t *dst, const uint8_t *src) {
    uint8x16_t data;

    data = vld1q_u8(src);

    for (int i = 0; i < 16; ++i) {
        vst1q_u8(dst, data);
        src += 16;
        dst += 16;
    }
}

static void *memcpy_asimd_v3(void *to, const void *from, size_t n) {
    void *retval = to;
    const uint8_t *src = (const uint8_t *)from;
    uint8_t *dst = (uint8_t *)to;

    if (n >= 256) {
        size_t i = n >> 8;
        size_t r = n & 255;

        for (; i > 0; i--) {
            memcpy_asimd_256_v3(dst, src);
            src += 256;
            dst += 256;
        }

        if (r) {
            memcpy(dst, src, r);
        }
    } else {
        memcpy(to, from, n);
    }

    return retval;
}
static void memcpy_asimd_256(uint8_t *dst, const uint8_t *src) {
    uint8x16_t data;
    for (int i = 0; i < 16; ++i) {
        data = vld1q_u8(src);
        vst1q_u8(dst, data);
        src += 16;
        dst += 16;
    }
}
static void *memcpy_asimd(void *to, const void *from, size_t n) {
    void *retval = to;
    uint8_t *src = (uint8_t *)from;
    uint8_t *dst = (uint8_t *)to;

    if (n >= 256) {
        size_t i = n >> 8;
        size_t r = n & 255;

        for (; i > 0; i--) {
            memcpy_asimd_256(dst, src);
            src += 256;
            dst += 256;
        }

        if (r) {
            memcpy(dst, src, r);
        }
    } else {
        memcpy(to, from, n);
    }

    return retval;
}

static void memcpy_asimd_256v2(uint8_t *dst, const uint8_t *src) {
    uint8x16_t data;
    for (int i = 0; i < 16; ++i) {
        data = vld1q_u8(src);
        vst1q_u8(dst, data);
        src += 16;
        dst += 16;
    }
}
static void *memcpy_asimdv2(void *to, const void *from, size_t n) {
    void *retval = to;
    uint8_t *src = (uint8_t *)from;
    uint8_t *dst = (uint8_t *)to;

    if (n >= 256) {
        size_t i = n >> 8;
        size_t r = n & 255;

        for (; i > 0; i--) {
            memcpy_asimd_256v2(dst, src);
            src += 256;
            dst += 256;
        }

        if (r) {
            memcpy(dst, src, r);
        }
    } else {
        memcpy(to, from, n);
    }

    return retval;
}
void *memset_asimd_v3(void *dst, uint8_t val, size_t n) {
    void *retval = dst;
    uint8_t *dst_bytes = (uint8_t *)dst;
    uint8x16_t value = vdupq_n_u8(val); 
	
    if (n >= 256) {
        size_t num_blocks = n >> 8;
        size_t remaining_bytes = n & 255;
		uint8x16_t val_vec = vdupq_n_u8(val);
        uint8_t buffer[256];
		uint8_t *p = buffer;

		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;

		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;
		vst1q_u8(p, val_vec); p += 16;

        for (; num_blocks > 0; num_blocks--) {
            memcpy_asimd_256v2(dst_bytes, buffer);
            dst_bytes += 256;
        }

		for( size_t i = 0; i < remaining_bytes; i ++ )
			dst_bytes[i] = val;

    } else {
		while (n >= 16) {
            vst1q_u8(dst_bytes, value);
            dst_bytes += 16;
            n -= 16;
        }

        while (n > 0) {
            *dst_bytes++ = val;
            n--;
        }
    }
    return retval;
}


#endif

#ifdef HAVE_ARM_ASIMD
void memset_asimd(void *dst, uint8_t val, size_t len) {

	if( len == 0 || NULL == dst ) 
		return;

    uint8x16_t value = vdupq_n_u8(val);
	uint8_t *dst_bytes = (uint8_t *)dst;
	size_t num_blocks = len / 16;

    for (size_t i = 0; i < num_blocks; i++) {
        vst1q_u8(dst_bytes, value);
        dst_bytes += 16;
    }

    size_t remaining_bytes = len % 16;
	for (size_t i = 0; i < remaining_bytes; i++) {
        *dst_bytes++ = val;
    }
}
void memset_asimd_v2(void *dst, uint8_t val, size_t len) {

	if( len == 0 || NULL == dst ) 
		return;

    uint8x16_t value = vdupq_n_u8(val);
    uint8_t *dst_bytes = (uint8_t *)dst;
    size_t num_blocks = len / 16;

    size_t alignment_offset = (size_t)dst_bytes & 0xF;
    size_t start_offset = (alignment_offset > 0) ? (16 - alignment_offset) : 0;

    for (size_t i = 0; i < start_offset; i++) {
        *dst_bytes++ = val;
    }

    for (size_t i = 0; i < num_blocks; i += 4) {
        vst1q_u8(dst_bytes, value);
        vst1q_u8(dst_bytes + 16, value);
        vst1q_u8(dst_bytes + 32, value);
        vst1q_u8(dst_bytes + 48, value);

        dst_bytes += 64;
    }

    size_t remaining_bytes = len % 16;
    for (size_t i = 0; i < remaining_bytes; i++) {
        *dst_bytes++ = val;
    }
}
void memset_asimd_v4(void *dst, uint8_t val, size_t len) {
  if( len == 0 || NULL == dst ) 
	return;

  uint8x16_t v = vdupq_n_u8(val);
  size_t multiple_of_16 = len & ~0xF;

  uint8_t *dst8 = (uint8_t *)dst;
  for (size_t i = 0; i < multiple_of_16; i += 16) {
    vst1q_u8(dst8 + i, v);
  }

  for (size_t i = multiple_of_16; i < len; i++) {
    dst8[i] = val;
  }
}

void memset_asimd_64(uint8_t *dst, uint8_t value, size_t size) {

	if( size == 0 || NULL == dst ) 
		return;

	uint8x16_t value_v = vdupq_n_u8(value);

    size_t num_blocks = size / 64;
    size_t remaining_bytes = size % 64;

    for (size_t i = 0; i < num_blocks; i++) {
        vst1q_u8(dst, value_v); 
		dst += 16;
		vst1q_u8(dst, value_v);
		dst += 16; 
		vst1q_u8(dst, value_v);
		dst += 16;
		vst1q_u8(dst, value_v);
		dst += 16;
	}

	while (remaining_bytes >= 16) {
        vst1q_u8(dst, value_v);
        dst += 16;
        remaining_bytes -= 16;
    }

    if(remaining_bytes >= 8) {
		uint64_t val64 = (uint64_t)value;
    	val64 |= val64 << 8;
        val64 |= val64 << 16;
        val64 |= val64 << 32;	
        uint64x1_t value_u64 = vdup_n_u64(val64);
        vst1_u8(dst, vreinterpret_u8_u64(value_u64));
        dst += 8;
        remaining_bytes -= 8;
    }

    while (remaining_bytes > 0) {
        *dst = value;
        dst++;
        remaining_bytes--;
    }

}

void memset_asimd_32(uint8_t *dst, uint8_t value, size_t size) {
	if( size == 0 || dst == NULL ) 
		return;

	uint8x16_t value_v = vdupq_n_u8(value);

    size_t num_blocks = size / 32;
    size_t remaining_bytes = size % 32;

    for (size_t i = 0; i < num_blocks; i++) {
        vst1q_u8(dst, value_v); 
		dst += 16;
		vst1q_u8(dst, value_v);
		dst += 16; 
	}

	while (remaining_bytes >= 16) {
        vst1q_u8(dst, value_v);
        dst += 16;
        remaining_bytes -= 16;
    }

    if(remaining_bytes >= 8) {
		uint64_t val64 = (uint64_t) value;
		val64 |= val64 << 8;
		val64 |= val64 << 16;
		val64 |= val64 << 32;
        uint64x1_t value_u64 = vdup_n_u64(val64);
        vst1_u8(dst, vreinterpret_u8_u64(value_u64));
        dst += 8;
        remaining_bytes -= 8;
    }

    while (remaining_bytes > 0) {
        *dst = value;
        dst++;
        remaining_bytes--;
    }
}

#endif

void *memset_64(void *ptr, int value, size_t num) {

	if( num == 0 || ptr == NULL ) 
		return ptr;

    uint8_t *dest = (uint8_t *)ptr;
    uint8_t byte_value = (uint8_t)value;
    size_t num_bytes = num;

    size_t num_words = num_bytes / sizeof(size_t);
    size_t remainder = num_bytes % sizeof(size_t);
    size_t pattern = 0;

    for (size_t i = 0; i < sizeof(size_t); i++) {
        pattern = (pattern << 8) | byte_value;
    }

    for (size_t i = 0; i < num_words; i++) {
        *((size_t *)(dest + i * sizeof(size_t))) = pattern;
    }

    for (size_t i = 0; i < remainder; i++) {
        dest[num_words * sizeof(size_t) + i] = byte_value;
    }

    return ptr;
}

static struct {
    const char *name;
    void *(*function)(void *to, const void *from, size_t len);
    double t;
    uint32_t cpu_require;
} memcpy_method[] = {
    { NULL, NULL, 0 },
    { "glibc memcpy()",  (void*) memcpy, 0, 0 },
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    { "linux kernel memcpy()", (void*) linux_kernel_memcpy, 0, 0 },
#endif
#ifdef HAVE_ASM_AVX512
    { "AVX-512 optimized memcpy()", (void*) avx512_memcpy, 0, AV_CPU_FLAG_AVX512 },
#endif
#ifdef HAVE_ASM_AVX2
    { "AVX2 optimized memcpy()", (void*) avx2_memcpy, 0, AV_CPU_FLAG_AVX2 },
#endif
#ifdef HAVE_ASM_AVX
    { "AVX optimized memcpy()", (void*) avx_memcpy, 0, AV_CPU_FLAG_AVX },
    { "AVX simple memcpy()", (void*) avx_memcpy2, 0, AV_CPU_FLAG_AVX },
#endif
#if defined (__SSE4_1__)
    { "SSE4_1 optimized memcpy()", (void*) sse41_memcpy, 0, AV_CPU_FLAG_SSE4 },
#endif
#if defined (__SSE4_2__)
    { "SSE4_2 optimized memcpy()", (void*) sse42_memcpy, 0, AV_CPU_FLAG_SSE42 },
#endif
#if defined (__SSE2__)
    { "SSE2 optimized memcpy() (128)", (void*) sse2_memcpy, 0, AV_CPU_FLAG_SSE2 },
    { "SSE2 optimized memcpy() (128) v2", (void*) sse2_memcpy_unaligned, 0, AV_CPU_FLAG_SSE2 },
#endif
#ifdef HAVE_ASM_SSE
    { "SSE optimized memcpy() (64)", (void*) sse_memcpy, 0, AV_CPU_FLAG_MMXEXT | AV_CPU_FLAG_SSE },
    { "SSE optimized memcpy() (128)", (void*) sse_memcpy2, 0, AV_CPU_FLAG_MMXEXT | AV_CPU_FLAG_SSE },
#endif
#ifdef HAVE_ASM_MMX
    { "MMX optimized memcpy()", (void*) mmx_memcpy, 0, AV_CPU_FLAG_MMX },
#endif
#ifdef HAVE_ASM_MMX2
    { "MMX2 optimized memcpy()", (void*) mmx2_memcpy, 0, AV_CPU_FLAG_MMX2 },
#endif
#if defined (HAVE_ASM_MMX) || defined(HAVE_ASM_SSE) || defined(HAVE_ASM_MMX2)
    { "MMX/MMX2/SSE optimized memcpy() v1", (void*) fast_memcpy, 0, AV_CPU_FLAG_MMX | AV_CPU_FLAG_SSE | AV_CPU_FLAG_MMX2 },
#endif
#if defined(HAVE_ARM_NEON)
    { "NEON optimized memcpy()", (void*) memcpy_neon, 0, AV_CPU_FLAG_NEON },
    { "new memcpy for cortex using NEON with line size of 32, preload offset of 192", (void*) memcpy_new_neon_line_size_32, 0, AV_CPU_FLAG_NEON },
    { "new memcpy for cortex using NEON with line size of 64, preload offset of 192", (void*) memcpy_new_neon_line_size_64, 0, AV_CPU_FLAG_NEON },
    { "new memcpy for cortex using NEON line 32 auto-prefetch", (void*) memcpy_new_neon_line_size_32_auto, 0, AV_CPU_FLAG_NEON },
#endif
#ifdef HAVE_ARM_ASIMD
    { "Advanced SIMD ARMv8-A memcpy()", (void*) memcpy_asimd, 0, AV_CPU_FLAG_ARMV8 },
#endif
#ifdef HAVE_ARMV7A
    { "new memcpy cortex line 32, preload 192", (void*) memcpy_new_line_size_32_preload_192, 0, 0 },
    { "new memcpy cortex line 64, preload 192", (void*) memcpy_new_line_size_64_preload_192, 0, 0 },
    { "new memcpy cortex line 64, preload 192 aligned", (void*) memcpy_new_line_size_64_preload_192_aligned_access, 0, 0 },
    { "new memcpy cortex line 32, preload 192 align32", (void*) memcpy_new_line_size_32_preload_192_align_32, 0, 0 },
    { "new memcpy cortex line 32, preload 96", (void*) memcpy_new_line_size_32_preload_96, 0, 0 },
    { "new memcpy cortex line 32, preload 96 aligned", (void*) memcpy_new_line_size_32_preload_96_aligned_access, 0, 0 },
#endif
    { NULL, NULL, 0 }
};

static struct {
    const char *name;
    void *(*function)(void *to, uint8_t c, size_t len);
    uint32_t cpu_require;
    double t;
} memset_method[] = {
    { NULL, NULL, 0, 0 },
    { "glibc memset()", (void*) memset, 0, 0},
#ifdef HAVE_ASM_AVX2
    { "AVX2 optimized memset()", (void*) avx2_memset, 0, AV_CPU_FLAG_AVX2 },
#endif
#ifdef HAVE_ASM_AVX
    { "AVX optimized memset()", (void*) avx_memset, 0, AV_CPU_FLAG_AVX },
#endif
#if defined (__SSE4_1__)
    { "SSE4_1 memset()", (void*) sse41_memset, 0, AV_CPU_FLAG_SSE4 },
    { "SSE4_1 memset() v2", (void*) sse41_memset_v2, 0, AV_CPU_FLAG_SSE4 },
#endif
#if defined (__SSE4_2__)
    { "SSE4_2 unaligned memset()", (void*) sse42_memset, 0, AV_CPU_FLAG_SSE42 },
    { "SSE4_2 aligned memset()", (void*) sse42_aligned_memset, 0, AV_CPU_FLAG_SSE42 },
#endif
#ifdef HAVE_ARM_NEON
    { "memset_neon", (void*) memset_neon, 0, AV_CPU_FLAG_NEON },
#endif
#ifdef HAVE_ARM_ASIMD
    { "Advanced SIMD memset()", (void*) memset_asimd, 0, AV_CPU_FLAG_ARMV8 },
    { "Advanced SIMD memset() v4", (void*) memset_asimd_v4, 0, AV_CPU_FLAG_ARMV8 },
    { "Advanced SIMD memset() 64-line", (void*) memset_asimd_64, 0, AV_CPU_FLAG_ARMV8 },
    { "Advanced SIMD memset() 32-line", (void*) memset_asimd_32, 0, AV_CPU_FLAG_ARMV8 },
    { "Advanced SIMD memset() v3", (void*) memset_asimd_v3, 0, AV_CPU_FLAG_ARMV8 },
#endif
#ifdef HAVE_ARM7A
    { "memset align 0", (void*) memset_new_align_0, 0, 0 },
    { "memset align 8", (void*) memset_new_align_8, 0, 0 },
    { "memset align 32", (void*) memset_new_align_32, 0, 0 },
#endif
    { "64-bit word memset()", (void*) memset_64, 0, 0 },
    { NULL, NULL, 0, 0 } 
};


void	memcpy_report(void)
{
	int i;
	fprintf(stdout,"\n\nSIMD benchmark results:\n");
	for( i = 1; memset_method[i].name; i ++ ) {
		fprintf(stdout,"\t%g : %s\n",memset_method[i].t,  memset_method[i].name );
	}
	for( i = 1; memcpy_method[i].name; i ++ ) {
		fprintf(stdout,"\t%g : %s\n",memcpy_method[i].t,  memcpy_method[i].name );
	}

	fprintf(stdout, "\n");
	fprintf(stdout, "best memcpy(): %s\n", memcpy_method[ selected_best_memcpy ].name );
	fprintf(stdout, "best memset(): %s\n" ,memset_method[ selected_best_memset ].name );

}

void *(* veejay_memcpy)(void *to, const void *from, size_t len) = 0;
void *(* veejay_memset)(void *what, uint8_t val, size_t len ) = 0;

static int set_user_selected_memcpy(void)
{
	char *mm = getenv( "VEEJAY_MEMCPY_METHOD" );
	if( mm ) {
		int i;
		for( i = 1; memcpy_method[i].name; i ++ ) {
			if( strcasecmp( memcpy_method[i].name, mm ) == 0 ) {
				veejay_msg(VEEJAY_MSG_INFO, "Using user selected memcpy method '%s'",
							memcpy_method[i].name );
				return i;
			}
		}
		veejay_msg(VEEJAY_MSG_INFO, "Using memcpy method '%s'", memcpy_method[1].name );
	}
	return 0;
}
static int set_user_selected_memset(void)
{
	char *mm = getenv( "VEEJAY_MEMSET_METHOD" );
	if( mm ) {
		int i;
		for( i = 1; memset_method[i].name; i ++ ) {
			if( strcasecmp( memset_method[i].name, mm ) == 0 ) {
				veejay_msg(VEEJAY_MSG_INFO, "Using user selected memset method '%s'",
							memset_method[i].name );
				return i;
			}
		}
		veejay_msg(VEEJAY_MSG_ERROR, "Using memset method '%s'", memset_method[1].name );
	}
	return 0;
}

static void mem_fill_block(uint8_t *dst, size_t len) {
	int i;
	for( i = 0; i < len ; i ++ ) {
		dst[i] = i % 256;
	}
}

static int mem_verify( uint8_t *source, uint8_t *good, size_t len) {
	if( memcmp(source,good,len) == 0 )
		return 1;
	return 0;
}

static int mem_validate( uint8_t *buffer, uint8_t *validation_buffer, size_t len, int index) {
	if(!mem_verify(buffer, validation_buffer, len ) ) {
		veejay_msg(0,"Function failed test, skip");
		return 0;
	}
	return 1;
}


#define TIMING_TOLERANCE 0.005  // 0.5% tolerance

static void lock_buffers(void *buf, size_t size) {
    if (mlock(buf, size) != 0) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to lock memory (!)");
    }
}

void find_best_memcpy(void)
{
	int best = set_user_selected_memcpy();
	if( best > 0 )
		goto set_best_memcpy_method;

  double t;
  uint8_t *buf1, *buf2, *validbuf;
  int i, k;
  int bufsize = (BENCHMARK_WID * BENCHMARK_HEI * 3);
  if (!(buf1 = (uint8_t*) vj_malloc( bufsize * sizeof(uint8_t))))
       return;

  if (!(buf2 = (uint8_t*) vj_malloc( bufsize * sizeof(uint8_t)))) {
       free( buf1 );
       return;
  }

  if (!(validbuf = (uint8_t*) vj_malloc( bufsize * sizeof(uint8_t)))) {
      free( buf1 );
      free( buf2 );
      return;
  }

  lock_buffers(buf1, bufsize);
  lock_buffers(buf2, bufsize);
  lock_buffers(validbuf, bufsize);

  int cpu_flags = av_get_cpu_flags();
  veejay_msg(VEEJAY_MSG_INFO, "Finding best memcpy ... (copy %ld blocks of %2.2f Mb)",FIND_BEST_MAX_ITERATIONS, bufsize / 1048576.0f );

  memset(buf1,0, bufsize);
  memset(buf2,0, bufsize);
	 
  /* make sure buffers are present on physical memory */
  memcpy( buf1, buf2, bufsize);
  memcpy( buf2, buf1, bufsize );

  /* fill */
  mem_fill_block(buf1, bufsize);
  memset(buf2, 0, bufsize);
  mem_fill_block(validbuf, bufsize);
	 
	for( i = 1; memcpy_method[i].name != NULL; i ++ ) {
	
		veejay_msg(VEEJAY_MSG_INFO, "Testing method %s", memcpy_method[i].name );

		if( memcpy_method[i].cpu_require && !(cpu_flags & memcpy_method[i].cpu_require ) ) {
			memcpy_method[i].t = 0.0;
			continue;
		}

		t = get_time();
		for( k = 0; k < FIND_BEST_MAX_ITERATIONS; k ++ ) {
			memcpy_method[i].function( buf2,buf1, bufsize );
		}
		t = get_time() - t;
		
		if(!mem_verify(buf2,validbuf, bufsize)) {
			t = 0;			
		}

		if( t > 0 )
			veejay_msg(VEEJAY_MSG_INFO, "method '%s' completed in %g seconds", memcpy_method[i].name, t );
		else
			veejay_msg(VEEJAY_MSG_WARNING, "method '%s' fails validation");

		memcpy_method[i].t = t;
	}

  best = 0;
  double best_time = 0.0;
  for (i = 1; memcpy_method[i].name != NULL; i++) {
      if (memcpy_method[i].t <= 0.0) continue;
      if (best == 0) {
          best = i;
          best_time = memcpy_method[i].t;
      } else if (memcpy_method[i].t + (best_time * TIMING_TOLERANCE) < best_time) {
          best = i;
          best_time = memcpy_method[i].t;
      }
  }

  free( buf1 );
  free( buf2 );
	free( validbuf );

set_best_memcpy_method:
	if (best) {
		veejay_memcpy = memcpy_method[best].function;
    } else {
		veejay_memcpy = memcpy;
	}

	selected_best_memcpy = best;

	veejay_msg(VEEJAY_MSG_INFO, "Selected best: %s", memcpy_method[best].name);
	veejay_msg(VEEJAY_MSG_WARNING, "export VEEJAY_MEMCPY_METHOD=\"%s\"", memcpy_method[best].name );
}

static volatile unsigned char sink;

static inline void consume_buffer(const unsigned char *buf, size_t n)
{
    for (size_t i = 0; i < n; i += 4096)
        sink ^= buf[i];
}

void find_best_memset(void)
{
	int best = set_user_selected_memset();
	if( best > 0 )
		goto set_best_memset_method;


	double t;
	char *buf1, *buf2;
	int i, k;
	int bufsize = (BENCHMARK_WID * BENCHMARK_HEI * 3);
	int cpu_flags = av_get_cpu_flags();
	
	if (!(buf1 = (char*) vj_malloc( bufsize * sizeof(char) )))
			return;

	if (!(buf2 = (char*) vj_malloc( bufsize * sizeof(char) ))) {
		free( buf1 );
		return;
	}

  lock_buffers(buf1, bufsize);
  lock_buffers(buf2, bufsize);

	veejay_msg(VEEJAY_MSG_INFO, "Finding best memset... (clear %d blocks of %2.2f Mb)", FIND_BEST_MAX_ITERATIONS, bufsize / 1048576.0f );

	memset( buf1, 0, bufsize * sizeof(char));
	memset( buf2, 0, bufsize * sizeof(char));

	consume_buffer((unsigned char *)buf1, bufsize);
	consume_buffer((unsigned char *)buf2, bufsize);

  for (i = 1; memset_method[i].name != NULL; i++) {
      if (memset_method[i].cpu_require && !(cpu_flags & memset_method[i].cpu_require)) {
          memset_method[i].t = 0.0;
          continue;
      }

      t = get_time();
      for (k = 0; k < FIND_BEST_MAX_ITERATIONS; k++) {
          memset_method[i].function(buf1, 0, bufsize);
      }
      t = get_time() - t;

      if (t <= 0.0) continue;

      memset_method[i].t = t;
      veejay_msg(VEEJAY_MSG_INFO, "method '%s' completed in %g seconds", memset_method[i].name, t);
  }

  best = 0;
  double best_time = 0.0;
  for (i = 1; memset_method[i].name != NULL; i++) {
      if (memset_method[i].t <= 0.0) continue;

      if (best == 0) {
          best = i;
          best_time = memset_method[i].t;
      } else if (memset_method[i].t + (best_time * TIMING_TOLERANCE) < best_time) {
          best = i;
          best_time = memset_method[i].t;
      }
  }

	free( buf1 );
	free( buf2 );

set_best_memset_method:
	if (best) {
		veejay_memset = memset_method[best].function;
	} 
	else {
	  veejay_memset = memset_method[1].function;
	}

	selected_best_memset = best;
	veejay_msg(VEEJAY_MSG_INFO, "Selected %s", memset_method[best].name);
	veejay_msg(VEEJAY_MSG_WARNING, "export VEEJAY_MEMSET_METHOD=\"%s\"", memset_method[best].name );

}

void    vj_mem_set_defaults(int w, int h) {

	if( w > 0 ) 
		BENCHMARK_WID = w;
	if( h > 0 )
		BENCHMARK_HEI = h;

	veejay_memset = memset_method[1].function;
	veejay_memcpy = memcpy_method[1].function;

	set_user_selected_memcpy();
	set_user_selected_memset();
}


static	void	vj_frame_copy_job( void *arg ) {
	int i;
	vj_task_arg_t *info = (vj_task_arg_t*) arg;
	for( i = 0; i < 4; i ++ ) {
		if( info->strides[i] <= 0 || info->input[i] == NULL || info->output[i] == NULL )
			continue;
		veejay_memcpy( info->output[i], info->input[i], info->strides[i] );
	}
}

static	void	vj_frame_clear_job( void *arg ) {
	int i;
	vj_task_arg_t *info = (vj_task_arg_t*) arg;
	for( i = 0; i < 4; i ++ ) {
		if( info->strides[i] > 0  )
			veejay_memset( info->input[i], info->iparams[0], info->strides[i] );
	}
}

static void	vj_frame_copyN( uint8_t **input, uint8_t **output, int *strides )
{
	vj_task_run( input, output, NULL, strides,4,(performer_job_routine) &vj_frame_copy_job,0 );
}

static void	vj_frame_clearN( uint8_t **input, int *strides, unsigned int val )
{
	vj_task_set_param( val,0 );
	vj_task_run( input, input, NULL, strides,3, (performer_job_routine) &vj_frame_clear_job,0 );
}

static void	vj_frame_slow_job( void *arg )
{
	vj_task_arg_t *job = (vj_task_arg_t*) arg;
	unsigned int i;
	uint8_t **img = job->output;
	uint8_t **p0_buffer = job->input;
	uint8_t **p1_buffer = job->temp;
	const float frac = job->fparam;
	
	for ( i = 0; i < 3; i ++ ) {
		uint8_t *a = p0_buffer[i];
		uint8_t *b = p1_buffer[i];
		uint8_t *d = img[i];
		const unsigned int len = job->strides[i];
		yuv_interpolate_frames(d,a,b,len,frac );	
	}


}

void	vj_frame_slow_single( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac )
{
	yuv_interpolate_frames(img[0],p0_buffer[0],p1_buffer[0],len,frac );	
	yuv_interpolate_frames(img[1],p0_buffer[1],p1_buffer[1],uv_len,frac );	
	yuv_interpolate_frames(img[2],p0_buffer[2],p1_buffer[2],uv_len,frac );	
}


void	vj_frame_slow_threaded( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac )
{
	if( vj_task_get_workers() > 1 ) {
		int strides[4] = { len, uv_len, uv_len, 0 };
		vj_task_set_float( frac );
		vj_task_run( p0_buffer, img, p1_buffer,strides, 4,(performer_job_routine) &vj_frame_slow_job, 0 );
	} 
	else {
		vj_frame_slow_single( p0_buffer, p1_buffer, img, len, uv_len, frac );
	}
}

static void	vj_frame_simple_clear(  uint8_t **input, int *strides, int v )
{
	int i;
	for( i = 0; i < 4; i ++ ) {
		if( input[i] == NULL || strides[i] == 0 )
			continue;
		veejay_memset( input[i], v , strides[i] );
	}
}


static void	vj_frame_simple_copy(  uint8_t **input, uint8_t **output, int *strides  )
{
	int i;
	for( i = 0; i < 4; i ++ )  {
		if( input[i] != NULL && output[i] != NULL && strides[i] > 0 )
			veejay_memcpy( output[i],input[i], strides[i] );
	}
}

typedef void *(*frame_copy_routine)( uint8_t **input, uint8_t **output, int *strides );
typedef void *(*frame_clear_routine)( uint8_t **input, int *strides, unsigned int val );

frame_copy_routine vj_frame_copy = 0;
frame_clear_routine vj_frame_clear = 0;

void	vj_frame_copy1( uint8_t *input, uint8_t *output, int size )
{
	uint8_t *in[4] = { input, NULL,NULL,NULL };
	uint8_t *ou[4] = { output,NULL,NULL,NULL };
	int     strides[4] = { size,0,0,0 };
	vj_frame_copy( in, ou, strides );
}

void	vj_frame_clear1( uint8_t *input, unsigned int val, int size )
{
	uint8_t *in[4] = { input, NULL,NULL,NULL };
	int     strides[4] = { size,0,0,0 };
	vj_frame_clear( in, strides, val );
}

static double benchmark_single_slow(long c, int n_tasks, uint8_t **source, uint8_t **dest, int *planes)
{
	int k;
	double stats[c];
	uint64_t bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		double t = get_time();
		vj_frame_slow_single( source, source, dest, planes[0], planes[1]/2, 0.67f );
		t = get_time() - t;
		stats[k] = t;
	}

	double sum = 0.0;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	double best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %g",(float)((bytes*c) /1048576.0f), best_time);

	return best_time;
}


static double benchmark_threaded_slow(long c, int n_tasks, uint8_t **source, uint8_t **dest, int *planes)
{
	int k;
	double stats[c];
	int bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		double t = get_time();
		vj_frame_slow_threaded( source, source, dest, planes[0], planes[1]/2, 0.67f );
		t = get_time() - t;
		stats[k] = t;
	}

	double sum = 0.0;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	double best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %g",(float)((bytes*c) /1048576.0f), best_time);

	return best_time;
}


static double benchmark_threaded_copy(long c, int n_tasks, uint8_t **dest, uint8_t **source, int *planes)
{
	int k;
	double stats[c];
	int bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		double t = get_time();
		vj_frame_copyN( source,dest,planes );
		t = get_time() - t;
		stats[k] = t;
	}

	double sum = 0.0;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	double best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %g",(float)((bytes*c) /1048576.0f), best_time);

	return best_time;
}

static double benchmark_single_copy(long c,int dummy, uint8_t **dest, uint8_t **source, int *planes)
{
	int k; int j;
	double stats[c];
	int bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ ) {
		double t = get_time();
		for( j = 0; j < 4; j ++ ) {
			veejay_memcpy( dest[j], source[j], planes[j] );
		}
		t = get_time() - t;
		stats[k] = t;
	}

	double sum = 0.0;
	for( k = 0; k < c; k ++ ) 
		sum += stats[k];

	double best_time = (sum/c);
	
	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %g",(float)((bytes*c) /1048576.0f), best_time);

	return best_time;
}

typedef double (*benchmark_func)(long c, int dummy, uint8_t **dest, uint8_t **source, int *planes);

static void run_benchmark_test(int n_tasks, benchmark_func f, const char *str, int n_frames, uint8_t **dest, uint8_t **source, int *planes )
{
	int N = 8;
	double stats[N];	
	uint32_t i;
	double fastest = 0.0;
	double slowest = 0.0;
	float work_size = (planes[0] + planes[1] + planes[2] + planes[3]) / 1048576.0f;

	veejay_msg(VEEJAY_MSG_INFO, "run %dx test '%s' on chunks of %2.2f MB:", N, str, work_size );

	for( i = 0; i < N; i ++ )
	{
		stats[i] = f( n_frames, n_tasks, source, dest, planes );
		if(i == 0 || stats[i] < fastest )
			fastest = stats[i];

		if( stats[i] > slowest )
			slowest = stats[i];
	}
	
	double sum = 0.0;
	for( i = 0; i < N; i ++ )
	{
		sum += stats[i];
	}

	double average = (sum / N);

	double fastest_ms = fastest * 1000000.0;
	double slowest_ms = slowest * 1000000.0;
	double average_ms = average * 1000000.0;

	veejay_msg(VEEJAY_MSG_INFO, "run done: best score for %s is %gms, worst is %gms, average is %gms",str, fastest_ms, slowest_ms, average_ms );
}

static void benchmark_tasks(unsigned int n_tasks, long n_frames, int w, int h)
{
	int len = w * h;
	int uv_len = (w/2) * h;
	int total = len + uv_len + uv_len;
	uint8_t *src = (uint8_t*) vj_malloc(sizeof(uint8_t) * total );
	uint8_t *dst = (uint8_t*) vj_malloc(sizeof(uint8_t) * total );

	int planes[4] = { len, uv_len, uv_len , 0 };
	uint8_t *source[4] = { src, src + len, src + len + uv_len, NULL };
	uint8_t *dest[4] = { dst,dst + len, dst + len + uv_len, NULL};

	memset( src, 16, sizeof(uint8_t) * total );
	memset( dst, 240, sizeof(uint8_t) * total );

  consume_buffer(src, total);
	consume_buffer(dst, total);

  run_benchmark_test( n_tasks, benchmark_single_copy, "single-threaded memory copy", n_frames, dest, source, planes );
	run_benchmark_test( n_tasks, benchmark_single_slow, "single-threaded slow frame", n_frames, dest, source, planes );

	run_benchmark_test( n_tasks, benchmark_threaded_slow, "multi-threaded slow frame", n_frames, dest, source, planes );
	//run_benchmark_test( n_tasks, benchmark_threaded_copy, "multi-threaded memory copy", n_frames, dest, source, planes );
	

	free(src);
	free(dst);
}

void	init_parallel_tasks(int n_tasks) 
{
//	if( n_tasks > 1 ) {	
//		vj_frame_copy = (frame_copy_routine) vj_frame_copyN;
//		vj_frame_clear= (frame_clear_routine) vj_frame_clearN;
//	}
//	else {
		vj_frame_copy = (frame_copy_routine) vj_frame_simple_copy;
		vj_frame_clear = (frame_clear_routine) vj_frame_simple_clear;
//	}
}

void	benchmark_veejay(int w, int h)
{
	if( w < 64  )
		w = 64;
	if( h < 64)
		h = 64;

	veejay_msg(VEEJAY_MSG_INFO, "Starting benchmark %dx%d YUVP 4:2:2 (100 frames)", w,h);
  veejay_msg(VEEJAY_MSG_INFO, "   you can set envvar VEEJAY_MEMCPY_METHOD or VEEJAY_MEMSET_METHOD to skip this");


	find_best_memcpy();
	find_best_memset();

	//init_parallel_tasks( 0 );
	//benchmark_tasks( 0,100,w,h );
}

void	*vj_hmalloc(size_t sze, const char *name)
{
	void *data = vj_calloc( sze );
	if( data == NULL ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to allocate memory (needed %ld bytes)", (long) sze );
		return NULL;
	}
	int tiedtoram = 1;
	if( mlock( data,sze ) != 0 )
		tiedtoram = 0;

	veejay_msg(VEEJAY_MSG_DEBUG,"Using %.2f MB RAM %s (memory %s paged to the swap area)",
			((float) sze/1048576.0f),
			name,
			(tiedtoram ? "is not going to be" : "may be" )
			);
	return data;
}

char	*vj_strdup( const char *s )
{
	/*size_t sl  = strlen(s);
	size_t len = sl + 1;
	char *ptr  = vj_malloc( len );
	ptr[sl] = '\0';
	return ptr ? memcpy( ptr, s, sl ) : NULL;*/
    return strdup(s);
}

char	*vj_strndup( const char *s, size_t n )
{
	size_t len = n + 1;
	char *ptr = vj_malloc( len );
	ptr[n] = '\0';
	return ptr ? memcpy( ptr,s,n ) : NULL;
}

// https://stackoverflow.com/questions/4351371/c-performance-challenge-integer-to-stdstring-conversion
// fast int to string function by user434507
// modified to append a space at the end instead of null-terminator
static const char digit_pairs[201] = {
  "00010203040506070809"
  "10111213141516171819"
  "20212223242526272829"
  "30313233343536373839"
  "40414243444546474849"
  "50515253545556575859"
  "60616263646566676869"
  "70717273747576777879"
  "80818283848586878889"
  "90919293949596979899"
};
char *vj_sprintf(char* c, int n) {
    int sign = -(n<0);
    unsigned int val = (n^sign)-sign;

    int size;
    if(val>=10000) {
        if(val>=10000000) {
            if(val>=1000000000) {
                size=10;
            }
            else if(val>=100000000) {
                size=9;
            }
            else size=8;
        }
        else {
            if(val>=1000000) {
                size=7;
            }
            else if(val>=100000) {
                size=6;
            }
            else size=5;
        }
    }
    else {
        if(val>=100) {
            if(val>=1000) {
                size=4;
            }
            else size=3;
        }
        else {
            if(val>=10) {
                size=2;
            }
            else if(n==0) {
                c[0]='0';
                c[1] = ' ';
                return c + 2;
            }
            else size=1;
        }
    }
    size -= sign;
    if(sign)
      *c='-';

    c += size-1;
    while(val>=100) {
        int pos = val % 100;
        val /= 100;
        *(short*)(c-1)=*(short*)(digit_pairs+2*pos); 
        c-=2;
    }
    while(val>0) {
        *c--='0' + (val % 10);
        val /= 10;
    }
    
    if(sign) { 
        c[size] = ' ';
        return c + size + 1;
    }
    else {
        c[size + 1] = ' ';
    }

    return c + size + 2;
}
