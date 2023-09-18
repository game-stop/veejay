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

#define BUFSIZE 1024


#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW)
#define HAVE_K6_2PLUS
#endif

/* definitions */
#define BLOCK_SIZE 4096
#define CONFUSION_FACTOR 0
//Feel free to fine-tune the above 2, it might be possible to get some speedup with them :)

static int selected_best_memcpy = 1;
static int selected_best_memset = 1;

static double get_time()
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
	if( vj_task_available() ) {
		uint8_t * t    = (uint8_t*) to;
		uint8_t *in[4] = { t, NULL,NULL,NULL };
		vj_task_run( in, in, NULL, NULL, 1, (performer_job_routine) &yuyv_plane_clear_job );
	}
	else {
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
}
#endif

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
#ifdef HAVE_ASM_SSE
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

#define is_aligned__(PTR,LEN) \
	(((uintptr_t)(const void*)(PTR)) % (LEN) == 0 )


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

void	yuyv_plane_init()
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
	if( vj_task_available() ) {
		int strides[4] = { len, 0,0, 0 };
		uint8_t *in[4] = { t, NULL,NULL,NULL};
		vj_task_run( in,in, NULL,strides, 1,(performer_job_routine) &yuyv_plane_clear_job );
		return;
	}	
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


/* for veejay, moving 128 bytes a time makes a difference */
static void *sse2_memcpy(void * to, const void * from, size_t len)
{
    void *retval = to;

    if(len >= 128)
    {
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
	/*	if(!is_aligned__(from,SSE_MMREG_SIZE)) {
			memcpy( to,from,len);
			return to;
		} */

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

		for(; i>0; i--)
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

#ifdef HAVE_ASM_AVX
static void * avx_memcpy(void * to, const void * from, size_t len)
{
  void *retval;
  size_t i;
  retval = to;

  /* PREFETCH has effect even for MOVSB instruction ;) */
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

  if(len >= MIN_LEN)
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
    i = len >> 7; /* len/128 */
    len&=127;
    if(((uintptr_t)from) & 31)
      /* if SRC is misaligned */
      for(; i>0; i--)
      {
        __asm__ __volatile__ (
        "prefetchnta 320(%0)\n"
        "prefetchnta 352(%0)\n"
        "prefetchnta 384(%0)\n"
        "prefetchnta 416(%0)\n"
        "vmovups    (%0), %%ymm0\n"
        "vmovups  32(%0), %%ymm1\n"
        "vmovups  64(%0), %%ymm2\n"
        "vmovups  96(%0), %%ymm3\n"
        "vmovntps %%ymm0,   (%1)\n"
        "vmovntps %%ymm1, 32(%1)\n"
        "vmovntps %%ymm2, 64(%1)\n"
        "vmovntps %%ymm3, 96(%1)\n"
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 128;
        to = ((unsigned char *)to) + 128;
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
        "prefetchnta 320(%0)\n"
        "prefetchnta 352(%0)\n"
        "prefetchnta 384(%0)\n"
        "prefetchnta 416(%0)\n"
        "vmovaps    (%0), %%ymm0\n"
        "vmovaps  32(%0), %%ymm1\n"
        "vmovaps  64(%0), %%ymm2\n"
        "vmovaps  96(%0), %%ymm3\n"
        "vmovntps %%ymm0,   (%1)\n"
        "vmovntps %%ymm1, 32(%1)\n"
        "vmovntps %%ymm2, 64(%1)\n"
        "vmovntps %%ymm3, 96(%1)\n"
        :: "r" (from), "r" (to) : "memory");
        from = ((const unsigned char *)from) + 128;
        to = ((unsigned char *)to) + 128;
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
				"movl (%0, %%"REG_a"), %%ecx 	\n\t"
				"movl 32(%0, %%"REG_a"), %%ecx 	\n\t"
				"movl 64(%0, %%"REG_a"), %%ecx 	\n\t"
				"movl 96(%0, %%"REG_a"), %%ecx 	\n\t"
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

void fast_memset_finish()
{
#ifdef HAVE_ASM_MMX2
	/* since movntq is weakly-ordered, a "sfence"
	 * is needed to become ordered again. */
	__asm__ __volatile__ ("sfence":::"memory");
#endif
#ifndef HAVE_ASM_SSE
    /* enables to use FPU */
    __asm__ __volatile__ (EMMS:::"memory");
#endif

}

void fast_memset_dirty(void * to, int val, size_t len)
{
#if defined ( HAVE_ASM_MMX ) || defined ( HAVE_ASM_SSE )
	size_t i;
	unsigned char mm_reg[AC_MMREG_SIZE], *pmm_reg;
	unsigned char *t = to;
    
  	if(len >= MIN_LEN)
	{
	  register unsigned long int delta;
      delta = ((unsigned long int)to)&(AC_MMREG_SIZE-1);
      if(delta)
	  {
	    delta=AC_MMREG_SIZE-delta;
	    len -= delta;
	    small_memset(t, val, delta);
	  }
	  i = len >> 7; /* len/128 */
	  len&=127;
	  pmm_reg = mm_reg;
	  small_memset(pmm_reg,val,sizeof(mm_reg));
#ifdef HAVE_ASM_SSE /* Only P3 (may be Cyrix3) */
	__asm__ __volatile__(
		"movups (%0), %%xmm0\n"
		:: "r"(mm_reg):"memory");
	for(; i>0; i--)
	{
		__asm__ __volatile__ (
		"movntps %%xmm0, (%0)\n"
		"movntps %%xmm0, 16(%0)\n"
		"movntps %%xmm0, 32(%0)\n"
		"movntps %%xmm0, 48(%0)\n"
		"movntps %%xmm0, 64(%0)\n"
		"movntps %%xmm0, 80(%0)\n"
		"movntps %%xmm0, 96(%0)\n"
		"movntps %%xmm0, 112(%0)\n"
		:: "r" (t) : "memory");
		t+=128;
	}
#else
	__asm__ __volatile__(
		"movq (%0), %%mm0\n"
		:: "r"(mm_reg):"memory");
	for(; i>0; i--)
	{
		__asm__ __volatile__ (
		MOVNTQ" %%mm0, (%0)\n"
		MOVNTQ" %%mm0, 8(%0)\n"
		MOVNTQ" %%mm0, 16(%0)\n"
		MOVNTQ" %%mm0, 24(%0)\n"
		MOVNTQ" %%mm0, 32(%0)\n"
		MOVNTQ" %%mm0, 40(%0)\n"
		MOVNTQ" %%mm0, 48(%0)\n"
		MOVNTQ" %%mm0, 56(%0)\n"
		MOVNTQ" %%mm0, 64(%0)\n"
		MOVNTQ" %%mm0, 72(%0)\n"
		MOVNTQ" %%mm0, 80(%0)\n"
		MOVNTQ" %%mm0, 88(%0)\n"
		MOVNTQ" %%mm0, 96(%0)\n"
		MOVNTQ" %%mm0, 104(%0)\n"
		MOVNTQ" %%mm0, 112(%0)\n"
		MOVNTQ" %%mm0, 120(%0)\n"
		:: "r" (t) : "memory");
		t+=128;
	}
#endif /* Have SSE */
	}
	/*
	 *	Now do the tail of the block
	 */
	if(len) small_memset(t, val, len);
#else
	memset(to,val,len);
#endif
}

#ifdef HAVE_ASM_SSE4_1
void *sse41_memset(void *to, uint8_t value, size_t len) {
  void *retval = to;

  // Check alignment and use aligned stores if possible
  uintptr_t delta = ((uintptr_t)to) & (SSE_MMREG_SIZE - 1);
  if (delta) {
    delta = SSE_MMREG_SIZE - delta;
    len -= delta;
    memset(to, value, delta);
    to = (void *)((char *)to + delta);

	if( len <= 0 ) {
		fprintf(stderr, "Alignment issue");
		return NULL;
	}
  }

  // Use larger prefetch distances for better performance
  _mm_prefetch((void *)((char *)to + 128), _MM_HINT_NTA);
  _mm_prefetch((void *)((char *)to + 256), _MM_HINT_NTA);

  __m128i xmm0 = _mm_set1_epi8(value);

  // Unroll the loop and use aligned stores when possible
  while (len >= 128) {
	if (((uintptr_t)to) & (SSE_MMREG_SIZE - 1)) {
      fprintf(stderr, "Warning: Alignment issue detected in loop operation.\n");
      break; // Print a warning but continue with the loop
    }
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

  // Copy the remaining bytes
  if (len >= 16) {
    memset(to, value, len);
  } else {
    // Avoid loop unrolling for small copies
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

        uint8x16_t data = vld1q_u8(src);

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

        uint8_t buffer[16];
        for (int i = 0; i < 16; i++) {
            buffer[i] = val;
        }

        for (; num_blocks > 0; num_blocks--) {
            memcpy_asimd_256v2(dst_bytes, buffer);
            dst_bytes += 256;
        }

        if (remaining_bytes) {
            memcpy_asimd_256v2(dst_bytes, buffer);
            dst_bytes += remaining_bytes;
        }
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

/* Fast memory set. See comments for fast_memcpy */
static void fast_memset(void * to, int val, size_t len)
{
	fast_memset_dirty( to, val , len );
	if(len >= MIN_LEN)
		fast_memset_finish();
}

#ifdef HAVE_ARM_ASIMD
void memset_asimd(void *dst, uint8_t val, size_t len) {
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
#endif

static struct {
     const char	*name;
     void *(*function)(void *to, const void *from, size_t len);
     double t;
     uint32_t cpu_require;
} memcpy_method[] =
{
     { NULL, NULL, 0},
	 /* standard memcpy */
     { "glibc memcpy()",  (void*) memcpy, 0,0 },
	 /* xine-lib memcpy: */
#if defined(ARCH_X86) || defined(ARCH_X86_64)
     { "linux kernel memcpy()", (void*)  linux_kernel_memcpy,0, 0},
#endif
#ifdef HAVE_ASM_AVX
     { "AVX optimized memcpy()", (void*) avx_memcpy, 0,AV_CPU_FLAG_AVX },
#endif
#ifdef HAVE_ASM_MMX
     { "MMX optimized memcpy()", (void*) mmx_memcpy, 0,AV_CPU_FLAG_MMX },
#endif
#ifdef HAVE_ASM_MMX2
     { "MMX2 optimized memcpy()", (void*) mmx2_memcpy, 0,AV_CPU_FLAG_MMX2 },
#endif
#ifdef HAVE_ASM_SSE
	 { "SSE optimized memcpy() (64)", (void*) sse_memcpy, 0,AV_CPU_FLAG_MMXEXT | AV_CPU_FLAG_SSE },
	 { "SSE optimized memcpy() (128)", (void*) sse_memcpy2, 0,AV_CPU_FLAG_MMXEXT | AV_CPU_FLAG_SSE },
#endif
  /* based on xine-lib + William Chan + other */
#if defined (__SSE2__)
	 { "SSE2 optimized memcpy() (128)", (void*) sse2_memcpy, 0,AV_CPU_FLAG_SSE2},
	 { "SSE2 optimized memcpy() (128) v2" , (void*) sse2_memcpy_unaligned, 0, AV_CPU_FLAG_SSE2},
#endif
#ifdef HAVE_ASM_SSE4_1
	{ "SSE4_1 optimized memcpy()" , (void*) sse41_memcpy, 0, AV_CPU_FLAG_SSE4},
#endif
#ifdef HAVE_ASM_SSE4_2
	{ "SSE4_2 optimized memcpy()" , (void*) sse42_memcpy, 0, AV_CPU_FLAG_SSE42},
#endif
	/* aclib_template.c: */
#if defined (HAVE_ASM_MMX) || defined( HAVE_ASM_SSE ) || defined( HAVE_ASM_MMX2)
     { "MMX/MMX2/SSE optimized memcpy() v1", (void*) fast_memcpy, 0,AV_CPU_FLAG_MMX |AV_CPU_FLAG_SSE |AV_CPU_FLAG_MMX2 },
#endif  
#if defined (HAVE_ARM_NEON)
	{ "NEON optimized memcpy()", (void*) memcpy_neon, 0, AV_CPU_FLAG_NEON },
#endif
#ifdef HAVE_ARM_ASIMD
	{ "Advanced SIMD ARMv8-A memcpy()", (void*) memcpy_asimd, 0, AV_CPU_FLAG_ARMV8 },
//	{ "Advanced SIMD ARMv8-A memcpy() v3", (void*) memcpy_asimd_v3, 0, AV_CPU_FLAG_ARMV8 },	
//	{ "Advanced SIMD ARMv8-A memcpy v2()", (void*) memcpy_asimdv2, 0, AV_CPU_FLAG_ARMV8 },
#endif
#ifdef HAVE_ARMV7A
	{ "new mempcy for cortex with line size of 32, preload offset of 192 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memcpy_new_line_size_32_preload_192,0,0 },
	{ "new memcpy for cortex with line size of 64, preload offset of 192 (C) Harm Hanemaaijer <fgenfb@yahoo.com>" ,(void*) memcpy_new_line_size_64_preload_192, 0, 0 },
    { "new memcpy for cortex with line size of 64, preload offset of 192, aligned access (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memcpy_new_line_size_64_preload_192_aligned_access, 0, 0 },
	{ "new memcpy for cortex with line size of 32, preload offset of 192, align 32", (void*) memcpy_new_line_size_32_preload_192_align_32,0,0},
	{ "new memcpy for cortex with line size of 32, preload offset of 96", (void*) memcpy_new_line_size_32_preload_96,0,0},
	{ "new memcpy for cortex with line size of 32, preload offset of 96, aligned access", (void*) memcpy_new_line_size_32_preload_96_aligned_access,0,0},
#endif
#ifdef HAVE_ARM_NEON
	{ "new memcpy for cortex using NEON with line size of 32, preload offset of 192 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memcpy_new_neon_line_size_32,0,AV_CPU_FLAG_NEON},
	{ "new memcpy for cortex using NEON with line size of 64, preload offset of 192 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memcpy_new_neon_line_size_64,0,AV_CPU_FLAG_NEON},
	{ "new mempcy for cortex using NEON with line size of 32, automatic prefetcher (C) Harm Hanemaaijer <fgenfb@yayhoo.com>", (void*) memcpy_new_neon_line_size_32_auto,0,AV_CPU_FLAG_NEON},
#endif
     { NULL, NULL, 0},
};

static struct {
	const char *name;
	void *(*function)(void *to, uint8_t c, size_t len);
	uint32_t cpu_require;
	double t;
} memset_method[] =
{
	{ NULL, NULL, 0,0},
	{ "glibc memset()",(void*)memset,0,0},
#if defined(HAVE_ASM_MMX) || defined(HAVE_ASM_MMX2) || defined(HAVE_ASM_SSE)
	{ "MMX/MMX2/SSE optimized memset()", (void*) fast_memset,0,AV_CPU_FLAG_MMX|AV_CPU_FLAG_SSE|AV_CPU_FLAG_MMX2 },
#endif 
#ifdef HAVE_ARM_NEON
	{ "memset_neon (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memset_neon,0, AV_CPU_FLAG_NEON },
#endif
#ifdef HAVE_ARM_ASIMD
	{ "Advanced SIMD memset()", (void*) memset_asimd, 0, AV_CPU_FLAG_ARMV8 },
	{ "Advanced SIMD memset() v4", (void*) memset_asimd_v4, 0, AV_CPU_FLAG_ARMV8 },	
//	{ "Advanced SIMD memset() v3", (void*) memset_asimd_v3, 0, AV_CPU_FLAG_ARMV8 },	
//	{ "Advanced SIMD memset() v2", (void*) memset_asimd_v2, 0, AV_CPU_FLAG_ARMV8 },
	
#endif
#ifdef HAVE_ARM7A
	{ "memset align 0 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memset_new_align_0,0,0 },
	{ "memset align 8 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memset_new_align_8,0,0 },
	{ "memset align 32 (C) Harm Hanemaaijer <fgenfb@yahoo.com>", (void*) memset_new_align_32,0,0 },
#endif
#ifdef HAVE_ASM_SSE4_1
  //  { "SSE4_1 memset()", (void*) sse41_memset,0, AV_CPU_FLAG_SSE4},
#endif
	{ NULL, NULL, 0, 0},
};


void	memcpy_report()
{
	int i;
	fprintf(stdout,"SIMD benchmark results:\n");
	for( i = 1; memset_method[i].name; i ++ ) {
		fprintf(stdout,"\t%g : %s\n",memset_method[i].t,  memset_method[i].name );
	}
	for( i = 1; memcpy_method[i].name; i ++ ) {
		fprintf(stdout,"\t%g : %s\n",memcpy_method[i].t,  memcpy_method[i].name );
	}

}

void *(* veejay_memcpy)(void *to, const void *from, size_t len) = 0;

void *(* veejay_memset)(void *what, uint8_t val, size_t len ) = 0;

char *get_memcpy_descr()
{
	return strdup( memcpy_method[selected_best_memcpy].name );
}

char *get_memset_descr()
{
	return strdup( memset_method[selected_best_memset].name );
}

static int set_user_selected_memcpy()
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
		veejay_msg(VEEJAY_MSG_ERROR, "No valid memcpy method selected, please use one of the following:");
		for( i = 1; memcpy_method[i].name; i ++ ) {
			veejay_msg(VEEJAY_MSG_ERROR, "\t\"%s\"", memcpy_method[i].name);
		}
		veejay_msg(VEEJAY_MSG_ERROR, "Using memcpy method '%s'", memcpy_method[1].name );
	}
	return 0;
}
static int set_user_selected_memset()
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
		veejay_msg(VEEJAY_MSG_ERROR, "No valid memset method selected, please use one of the following:");
		for( i = 1; memset_method[i].name; i ++ ) {
			veejay_msg(VEEJAY_MSG_ERROR, "\t\"%s\"", memset_method[i].name);
		}
		veejay_msg(VEEJAY_MSG_ERROR, "Using memset method '%s'", memset_method[1].name );
	}
	return 0;
}

void find_best_memcpy()
{
	int best = set_user_selected_memcpy();
	if( best > 0 )
		goto set_best_memcpy_method;

     double t;
     char *buf1, *buf2;
     int i, k;
     int bufsize = 720 * 576 * 4;

     if (!(buf1 = (char*) malloc( bufsize * sizeof(char))))
          return;

     if (!(buf2 = (char*) malloc( bufsize * sizeof(char)))) {
          free( buf1 );
          return;
     }

     int cpu_flags = av_get_cpu_flags();
	
	veejay_msg(VEEJAY_MSG_DEBUG, "Finding best memcpy ..." );	

     memset(buf1,0, bufsize);
     memset(buf2,0, bufsize);

     /* make sure buffers are present on physical memory */
     memcpy( buf1, buf2, bufsize);
     memcpy( buf2, buf1, bufsize );

	for( i = 1; memcpy_method[i].name; i ++ ) {
		
		if( memcpy_method[i].cpu_require && !(cpu_flags & memcpy_method[i].cpu_require ) ) {
			memcpy_method[i].t = 0.0;
			continue;
		}

		t = get_time();
		for( k = 0; k < 1024; k ++ ) {
			memcpy_method[i].function( buf1,buf2, bufsize );
		}
		t = get_time() - t;
		memcpy_method[i].t = t;
	}

	for( i = 1; memcpy_method[i].name; i ++ ) {
		if(best == 0 ) { 
			best = i;
		    t = memcpy_method[i].t;	
			continue;
		}

		if( memcpy_method[i].t < t && memcpy_method[i].t > 0 ) {
			t = memcpy_method[i].t;
			best = i;
		}
	}

    free( buf1 );
    free( buf2 );

set_best_memcpy_method:
	if (best) {
		veejay_memcpy = memcpy_method[best].function;
    } else {
		veejay_memcpy = memcpy;
	}

	selected_best_memcpy = best;
}

void find_best_memset()
{
	int best = set_user_selected_memset();
	if( best > 0 )
		goto set_best_memset_method;


	double t;
	char *buf1, *buf2;
	int i, k;
	int bufsize = 720 * 576 * 4;
	int cpu_flags = av_get_cpu_flags();
	
	if (!(buf1 = (char*) malloc( bufsize * sizeof(char) )))
        	return;

	if (!(buf2 = (char*) malloc( bufsize * sizeof(char) ))) {
		free( buf1 );
		return;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Finding best memset..." );

	memset( buf1, 0, bufsize * sizeof(char));
	memset( buf2, 0, bufsize * sizeof(char));

	for (i=1; memset_method[i].name; i++)
	{
		if( memset_method[i].cpu_require && !(cpu_flags & memset_method[i].cpu_require ) ) {
			memset_method[i].t= 0;
			continue;
	}

	t = get_time();
	for( k = 0; k < 1024; k ++ ) {
		memset_method[i].function( buf1 , 0 , bufsize );
	}
	t = get_time() - t;
 
	memset_method[i].t = t;

	if (best == 0 || t < memset_method[best].t)
		best = i;
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
	vj_task_run( input, output, NULL, strides,4,(performer_job_routine) &vj_frame_copy_job );
}

static void	vj_frame_clearN( uint8_t **input, int *strides, unsigned int val )
{
	vj_task_set_param( val,0 );
	vj_task_run( input, input, NULL, strides,3, (performer_job_routine) &vj_frame_clear_job );
}

static inline void vj_frame_slow1( uint8_t *dst, uint8_t *a, uint8_t *b, const int len, const float frac )
{
#ifndef HAVE_ASM_MMX
	int i;
	for( i = 0; i < len; i ++  ) {
		dst[i] = a[i] + ( frac * ( b[i] - a[i] ) ); 
	}
#else
	uint32_t ialpha = (256 * frac);
    unsigned int i;

    ialpha |= ialpha << 16;

    __asm __volatile
				("\n\t pxor %%mm6, %%mm6"
                ::);

	for (i = 0; i < len; i += 4) {
		__asm __volatile
			("\n\t movd %[alpha], %%mm3"
			"\n\t movd %[src2], %%mm0"
			"\n\t psllq $32, %%mm3"
			"\n\t movd %[alpha], %%mm2"
			"\n\t movd %[src1], %%mm1"
			"\n\t por %%mm3, %%mm2"
			"\n\t punpcklbw %%mm6, %%mm0"  
			"\n\t punpcklbw %%mm6, %%mm1"  
			"\n\t psubsw %%mm1, %%mm0"     
			"\n\t pmullw %%mm2, %%mm0"     
			"\n\t psrlw $8, %%mm0"        
			"\n\t paddb %%mm1, %%mm0"     
			"\n\t packuswb %%mm0, %%mm0\n\t"
			"\n\t movd %%mm0, %[dest]\n\t"
			: [dest] "=m" (*(dst + i))
			: [src1] "m" (*(a + i))
			, [src2] "m" (*(b + i))
			, [alpha] "m" (ialpha));
	}
#endif
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
		vj_frame_slow1(d,a,b,len,frac );	
	}


}

static void	vj_frame_slow_single( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac )
{
	vj_frame_slow1(img[0],p0_buffer[0],p1_buffer[0],len,frac );	
	vj_frame_slow1(img[1],p0_buffer[1],p1_buffer[1],uv_len,frac );	
	vj_frame_slow1(img[2],p0_buffer[2],p1_buffer[2],uv_len,frac );	
}


void	vj_frame_slow_threaded( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac )
{
	if( vj_task_available() ) {
		int strides[4] = { len, uv_len, uv_len, 0 };
		vj_task_set_float( frac );
		vj_task_run( p0_buffer, img, p1_buffer,strides, 4,(performer_job_routine) &vj_frame_slow_job );
	} 
	else {
		vj_frame_slow_single( p0_buffer, p1_buffer, img, len, uv_len, frac );
	}
   
#ifdef HAVE_ASM_MMX
	__asm __volatile(_EMMS"       \n\t"
              		 SFENCE"     \n\t"
                	:::"memory");	
	
#endif
/*
		int i;
		if( uv_len != len ) { 
			for( i  = 0; i < len ; i ++ ) {
				img[0][i] = p0_buffer[0][i] + ( frac * (p1_buffer[0][i] - p0_buffer[0][i]));
			}
			for( i  = 0; i < uv_len ; i ++ ) {
				img[1][i] = p0_buffer[1][i] + ( frac * (p1_buffer[1][i] - p0_buffer[1][i]));
				img[2][i] = p0_buffer[2][i] + ( frac * (p1_buffer[2][i] - p0_buffer[2][i]));
			}
		} else {
			for( i  = 0; i < len ; i ++ ) {
				img[0][i] = p0_buffer[0][i] + ( frac * (p1_buffer[0][i] - p0_buffer[0][i]));
				img[1][i] = p0_buffer[1][i] + ( frac * (p1_buffer[1][i] - p0_buffer[1][i]));
				img[2][i] = p0_buffer[2][i] + ( frac * (p1_buffer[2][i] - p0_buffer[2][i]));
			}
		}
	*/

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
	float work_size = (planes[0] + planes[1] + planes[2] + planes[3]) / 1048576.0f;

	veejay_msg(VEEJAY_MSG_INFO, "run test '%s' (%dx) on chunks of %2.2f MB:", str, N, work_size );

	for( i = 0; i < N; i ++ )
	{
		stats[i] = f( n_frames, n_tasks, source, dest, planes );
		if( stats[i] > fastest )
			fastest = stats[i];
	}
	
	double sum = 0.0;
	double slowest=fastest;
	for( i = 0; i < N; i ++ )
	{
		if( stats[i] < fastest ) {
			fastest = stats[i];
		}	
		sum += stats[i];
	}

	double average = (sum / N);

	veejay_msg(VEEJAY_MSG_INFO, "run done: best score for %s is %g, worst is %g, average is %g",str, fastest, slowest, average );
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

	run_benchmark_test( n_tasks, benchmark_single_copy, "single-threaded memory copy", n_frames, dest, source, planes );
	run_benchmark_test( n_tasks, benchmark_single_slow, "single-threaded slow frame", n_frames, dest, source, planes );

	if( n_tasks > 1 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Using %d tasks", n_tasks );
		task_start(n_tasks);
		run_benchmark_test( n_tasks, benchmark_threaded_slow, "multi-threaded slow frame", n_frames, dest, source, planes );
		run_benchmark_test( n_tasks, benchmark_threaded_copy, "multi-threaded memory copy", n_frames, dest, source, planes );
		task_stop(n_tasks);
	}

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

	int n_tasks = task_num_cpus();
	char *str2 = getenv( "VEEJAY_MULTITHREAD_TASKS" );
	if( str2 ) {
		n_tasks = atoi(str2);
	}

	veejay_msg(VEEJAY_MSG_INFO, "VEEJAY_MULTITHREAD_TASKS=%d", n_tasks );

	init_parallel_tasks( n_tasks );

	benchmark_tasks( n_tasks,100,w,h );
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
// https://stackoverflow.com/questions/4351371/c-performance-challenge-integer-to-stdstring-conversion
// fast int to string function by user434507
// modified to append a space at the end instead of null-terminator
char *vj_sprintf(char* c, int n) {
    int sign = (n < 0);
    unsigned int val = (n ^ sign) - sign;

    int size = 0;

    if (val == 0) {
        *c++ = '0';
        *c++ = ' ';
        return c;
    }

    while (val > 0) {
        *c++ = '0' + (val % 10);
        val /= 10;
        size++;
    }

    if (sign) {
        *c++ = '-';
        size++;
    }

    int spaces = 12 - size;  
	while (spaces-- > 0) {
        *c++ = ' ';
    }

    char *start = c - size - (sign ? 1 : 0);
    char *end = c - 1;

    while (start < end) {
        char temp = *start;
        *start = *end;
        *end = temp;
        start++;
        end--;
    }

    return c;
}

/*

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

*/