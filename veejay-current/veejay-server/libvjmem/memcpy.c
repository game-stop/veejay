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
#include <libvje/vje.h>
#include <veejay/vj-task.h>
#include <libavutil/cpu.h>
#define BUFSIZE 1024


#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW)
#define HAVE_K6_2PLUS
#endif

/* definitions */
#define BLOCK_SIZE 4096
#define CONFUSION_FACTOR 0
//Feel free to fine-tune the above 2, it might be possible to get some speedup with them :)

#ifdef HAVE_POSIX_TIMERS
static int64_t _x_gettime(void)
{
	struct timespec tm;
	return (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&tm) == -1 )
			? times(NULL)
			: (int64_t) tm.tv_sec * 1e9 + tm.tv_nsec;
}
#define rdtsc(x) _x_gettime()
#elif (defined(ARCH_X86) || defined(ARCH_X86_64)) && defined(HAVE_SYS_TIMES_H)
static int64_t rdtsc(int cpu_flags)
{
	int64_t x;
	if( cpu_flags & AV_CPU_FLAGS_MMX ) {
			__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
			return x;
	} else {
			return times(NULL);
	}
}
#else
static uint64_t rdtsc(int cpu_flags)
{
#ifdef HAVE_SYS_TIMES_H
		struct tms tp;
		return times(&tp);
#else
		return clock();
#endif
}
#endif /* HAVE_SYS_TIMES_H */

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
		int 	strides[4] = { len, 0,0,0 };
		vj_task_run( in, in, NULL, NULL, 1, &yuyv_plane_clear_job );
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
#if HAVE_MMX && !HAVE_MMX2 && !HAVE_AMD3DNOW && !HAVE_SSE
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
#if HAVE_MMX2
#define MOVNTQ "movntq"
#else
#define MOVNTQ "movq"
#endif

#define _MMX1_MIN_LEN 0x800 /* 2k blocks */

#ifdef HAVE_ASM_3DNOW
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#define is_aligned__(PTR,LEN) \
	(((uintptr_t)(const void*)(PTR)) % (LEN) == 0 )

static int selected_best_memcpy = 1;
static int selected_best_memset = 1;

char	*veejay_strncpy( char *dest, const char *src, size_t n )
{
	dest[n] = '\0';
	if( n < 0xff ) {
		small_memcpy( dest,src, n );
	} else if ( n < 512 ) {
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
static uint8_t yuyv_mmreg_[AC_MMREG_SIZE];

void	yuyv_plane_init()
{
	unsigned int i;
	for( i = 0; i < AC_MMREG_SIZE ;i ++ )
		yuyv_mmreg_[i] = ( (i%2) ? 128: 0 );
}


static void	yuyv_plane_clear_job( void *arg )
{
	vj_task_arg_t *v = (vj_task_arg_t*) arg;
	int len = v->strides[0];
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
}



/* Fast memory set. See comments for fast_memcpy */
void *fast_memset(void * to, int val, size_t len)
{
	fast_memset_dirty( to, val , len );
	if(len >= MIN_LEN)
		fast_memset_finish();
}

static void *linux_kernel_memcpy(void *to, const void *from, size_t len) {
     return __memcpy(to,from,len);
}

#endif

static struct {
     char                 *name;
     void               *(*function)(void *to, const void *from, size_t len);
     uint64_t	   time;
     uint32_t		cpu_require;
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
	/* aclib_template.c: */
#if defined (HAVE_ASM_MMX) || defined( HAVE_ASM_SSE ) || defined( HAVE_ASM_MMX2)
     { "MMX/MMX2/SSE optimized memcpy() v1", (void*) fast_memcpy, 0,AV_CPU_FLAG_MMX |AV_CPU_FLAG_SSE |AV_CPU_FLAG_MMX2 },
#endif  
     { NULL, NULL, 0},
};

static struct {
     char                 *name;
     void                *(*function)(void *to, uint8_t c, size_t len);
	 uint64_t	    time;
     uint32_t		cpu_require;
} memset_method[] =
{
     { NULL, NULL, 0,0},
     { "glibc memset()",            (void*)memset, 0,0},
#if defined(HAVE_ASM_MMX) || defined(HAVE_ASM_MMX2) || defined(HAVE_ASM_SSE)
     { "MMX/MMX2/SSE optimized memset()", (void*)   fast_memset, 0, AV_CPU_FLAG_MMX|AV_CPU_FLAG_SSE|AV_CPU_FLAG_MMX2},
#endif 
       { NULL, NULL, 0,0},
};


void	memcpy_report()
{
	int i;
	fprintf(stdout,"SIMD benchmark results:\n");
	for( i = 1; memset_method[i].name; i ++ ) {
		fprintf(stdout,"\t%8ld : %s\n",(long) memset_method[i].time,  memset_method[i].name );
	}
	for( i = 1; memcpy_method[i].name; i ++ ) {
		fprintf(stdout,"\t%8ld : %s\n",(long) memcpy_method[i].time,  memcpy_method[i].name );
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

void find_best_memcpy()
{
     uint64_t t;
     char *buf1, *buf2;
     int i, best = 0,k;
     int bufsize = 720 * 576 * 3;

     if (!(buf1 = (char*) malloc( bufsize * sizeof(char) )))
          return;

     if (!(buf2 = (char*) malloc( bufsize * sizeof(char) ))) {
          free( buf1 );
          return;
     }

     int cpu_flags = av_get_cpu_flags();
	
     memset(buf1,0, bufsize);
     memset(buf2,0, bufsize);

     /* make sure buffers are present on physical memory */
     memcpy( buf1, buf2, bufsize);
     memcpy( buf2, buf1, bufsize );

	for( i = 1; memcpy_method[i].name; i ++ ) {
		
		t = rdtsc(cpu_flags);
		if( memcpy_method[i].cpu_require && !(cpu_flags & memcpy_method[i].cpu_require ) ) {
			memcpy_method[i].time = 0;
			continue;
		}

		for( k = 0; k < 128; k ++ ) {
			memcpy_method[i].function( buf1,buf2, bufsize );
		}
		t = rdtsc(cpu_flags) - t;
		memcpy_method[i].time = t;
	}

	for( i = 1; memcpy_method[i].name; i ++ ) {
		if(best == 0 ) { 
			best = i;
		    t = memcpy_method[i].time;	
			continue;
		}

		if( memcpy_method[i].time < t && memcpy_method[i].time > 0 ) {
			t = memcpy_method[i].time;
			best = i;
		}
	}

	if (best) {
		veejay_memcpy = memcpy_method[best].function;
    } else {
		veejay_memcpy = memcpy;
	}

	selected_best_memcpy = best;

    free( buf1 );
    free( buf2 );
}

void find_best_memset()
{
    uint64_t t;
    char *buf1, *buf2;
    int i, best = 0,k;
	int bufsize = 720 * 576 * 3;
	int cpu_flags = av_get_cpu_flags();
	
     if (!(buf1 = (char*) malloc( bufsize * sizeof(char) )))
          return;

     if (!(buf2 = (char*) malloc( bufsize * sizeof(char) ))) {
          free( buf1 );
          return;
     }
	
	memset( buf1, 0, bufsize * sizeof(char));
	memset( buf2, 0, bufsize * sizeof(char));

     for (i=1; memset_method[i].name; i++)
     {
		if( memset_method[i].cpu_require && !(cpu_flags & memset_method[i].cpu_require ) ) {
			memset_method[i].time= 0;
			continue;
		}

        t = rdtsc(cpu_flags);
		for( k = 0; k < 128; k ++ ) {
			memset_method[i].function( buf1 , 0 , bufsize );
		}
        t = rdtsc(cpu_flags) - t;
	  
        memset_method[i].time = t;

        if (best == 0 || t < memset_method[best].time)
        	best = i;
     }

     if (best) {
          veejay_memset = memset_method[best].function;
     } else {
		  veejay_memset = memset_method[1].function;
	 }

	selected_best_memset = best;

	 free( buf1 );
     free( buf2 );

}

static	void	vj_frame_copy_job( void *arg ) {
	int i;
	vj_task_arg_t *info = (vj_task_arg_t*) arg;
	for( i = 0; i < 4; i ++ ) {
		if( info->strides[i] <= 0 || info->output[i] == NULL || info->output[i] == NULL )
			continue;
		veejay_memcpy( info->output[i], info->input[i], info->strides[i] );
	}
}

static	void	vj_frame_clear_job( void *arg ) {
	int i;
	vj_task_arg_t *info = (vj_task_arg_t*) arg;
	for( i = 0; i < 4; i ++ ) {
		if( info->strides[i] > 0  )
			veejay_memset( info->input[i], info->iparam, info->strides[i] );
	}
}

static void	vj_frame_copyN( uint8_t **input, uint8_t **output, int *strides )
{
	vj_task_run( input, output, NULL, strides,4,(performer_job_routine) &vj_frame_copy_job );
}

static void	vj_frame_clearN( uint8_t **input, int *strides, unsigned int val )
{
	vj_task_set_int( val );
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
	int i,j;
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

void	vj_frame_slow_single( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac )
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

void	vj_frame_simple_clear(  uint8_t **input, int *strides, int v )
{
	int i;
	for( i = 0; i < 4; i ++ ) {
		if( input[i] == NULL || strides[i] == 0 )
			continue;
		veejay_memset( input[i], v , strides[i] );
	}
}


void	vj_frame_simple_copy(  uint8_t **input, uint8_t **output, int *strides  )
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

static unsigned long benchmark_single_slow(long c, int n_tasks, uint8_t **source, uint8_t **dest, int *planes)
{
	uint64_t k;
	uint64_t stats[c];
	uint64_t bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		uint64_t t = rdtsc(0);
		vj_frame_slow_single( source, source, dest, planes[0], planes[1]/2, 0.67f );
		t = rdtsc(0) - t;
		stats[k] = t;
	}

	uint64_t sum = 0,j;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	uint64_t best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %2.2f ms",
			(float)(bytes /1048576.0f), (best_time/1000.0f));

	return best_time;
}


static unsigned long benchmark_threaded_slow(long c, int n_tasks, uint8_t **source, uint8_t **dest, int *planes)
{
	uint64_t k;
	uint64_t stats[c];
	uint64_t bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		uint64_t t = rdtsc(0);
		vj_frame_slow_threaded( source, source, dest, planes[0], planes[1]/2, 0.67f );
		t = rdtsc(0) - t;
		stats[k] = t;
	}

	uint64_t sum = 0,j;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	uint64_t best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %2.2f ms",
			(float)(bytes /1048576.0f), (best_time/1000.0f));

	return best_time;
}


static unsigned long benchmark_threaded_copy(long c, int n_tasks, uint8_t **dest, uint8_t **source, int *planes)
{
	uint64_t k;
	uint64_t stats[c];
	uint64_t bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ )	
	{
		uint64_t t = rdtsc(0);
		vj_frame_copyN( source,dest,planes );
		t = rdtsc(0) - t;
		stats[k] = t;
	}

	uint64_t sum = 0,j;
	for( k = 0; k < c ;k ++ )
		sum += stats[k];

	uint64_t best_time = (sum / c );

	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %2.2f ms",
			(float)(bytes /1048576.0f), (best_time/1000.0f));

	return best_time;
}

static unsigned long benchmark_single_copy(long c,int dummy, uint8_t **dest, uint8_t **source, int *planes)
{
	uint64_t k; int j;
	uint64_t stats[c];
	uint64_t bytes = ( planes[0] + planes[1] + planes[2] + planes[3] );

	for( k = 0; k < c; k ++ ) {
		uint64_t t = rdtsc(0);
		for( j = 0; j < 4; j ++ ) {
			veejay_memcpy( dest[j], source[j], planes[j] );
		}
		t = rdtsc(0) - t;
		stats[k] = t;
	}

	uint64_t sum = 0;
	for( k = 0; k < c; k ++ ) 
		sum += stats[k];

	uint64_t best_time = (sum/c);
	
	veejay_msg(VEEJAY_MSG_DEBUG, "%.2f MB data in %2.2f ms",
			(float)(bytes /1048576.0f), (best_time/1000.0f));

	return best_time;
}

typedef unsigned long (*benchmark_func)(long c, int dummy, uint8_t **dest, uint8_t **source, int *planes);


void run_benchmark_test(int n_tasks, benchmark_func f, char *str, int n_frames, uint8_t **dest, uint8_t **source, int *planes )
{
	uint32_t N = 8;
	uint64_t stats[N];	
	uint32_t i;
	uint64_t fastest = 0;
	float work_size = (planes[0] + planes[1] + planes[2] + planes[3]) / 1048576.0f;

	veejay_msg(VEEJAY_MSG_INFO, "run test '%s' (%dx) on chunks of %2.2f MB:", str, N, work_size );

	for( i = 0; i < N; i ++ )
	{
		stats[i] = f( n_frames, n_tasks, source, dest, planes );
		if( stats[i] > fastest )
			fastest = stats[i];
	}
	
	uint64_t sum = 0;
	uint64_t slowest=fastest;
	for( i = 0; i < N; i ++ )
	{
		if( stats[i] < fastest ) {
			fastest = stats[i];
		}	
		sum += stats[i];
	}

	float average = (sum / N);

	veejay_msg(VEEJAY_MSG_INFO, "run done: best score for %s is %2.4f ms, worst is %2.4f ms, average is %2.4f ms", 
		str, fastest/1000.0f, slowest/1000.0f, average/1000.0f );
}

void benchmark_tasks(int n_tasks, long n_frames, int w, int h)
{
	unsigned int len = w * h;
	unsigned int uv_len = (w/2) * h;
	unsigned int total = len + uv_len + uv_len;
	uint8_t *src = (uint8_t*) vj_malloc(sizeof(uint8_t) * total );
	uint8_t *dst = (uint8_t*) vj_malloc(sizeof(uint8_t) * total );

	int planes[4] = { len, uv_len, uv_len , 0 };
	int i;
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
	if( n_tasks > 1 ) {	
		vj_frame_copy = (frame_copy_routine) vj_frame_copyN;
		vj_frame_clear= (frame_clear_routine) vj_frame_clearN;
	}
	else {
		vj_frame_copy = (frame_copy_routine) vj_frame_simple_copy;
		vj_frame_clear = (frame_clear_routine) vj_frame_simple_clear;
	}
}

void	benchmark_veejay(int w, int h)
{
	if( w < 64  )
		w = 64;
	if( h < 64)
		h = 64;

	int n_tasks = task_num_cpus();
	init_parallel_tasks( n_tasks );
	char *str2 = getenv( "VEEJAY_MULTITHREAD_TASKS" );
	if( str2 ) {
		n_tasks = atoi(str2);
	}
	
	int n_frames = 100;
	veejay_msg(VEEJAY_MSG_INFO, "Benchmark %dx%d YUVP 4:2:2 (%d frames)", w,h,n_frames);
	benchmark_tasks( n_tasks, n_frames,w,h );
}

void	*vj_hmalloc(size_t sze, const char *name)
{
	void *data = vj_malloc( sze );
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
	size_t sl  = strlen(s);
	size_t len = sl + 1;
	char *ptr  = vj_malloc( len );
	ptr[sl] = '\0';
	return ptr ? memcpy( ptr, s, sl ) : NULL;
}

char	*vj_strndup( const char *s, size_t n )
{
	size_t len = n + 1;
	char *ptr = vj_malloc( len );
	ptr[n] = '\0';
	return ptr ? memcpy( ptr,s,n ) : NULL;
}


char	*vj_sprintf( char *dst, int value )
{
	char number[16];
	int n;
	snprintf(number,sizeof(number),"%d", value);
	n = strlen( number );
	*dst = '\0';
	strcat( dst, number );
	return dst + n;
}

