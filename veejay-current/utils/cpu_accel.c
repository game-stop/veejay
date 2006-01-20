/*
 * cpu_accel.c
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include "cpu_accel.h"

#ifdef HAVE_X86CPU 

#include "mjpeg_types.h"

/* Some miscelaneous stuff to allow checking whether SSE instructions cause
   illegal instruction errors.
*/

static sigjmp_buf sigill_recover;

static RETSIGTYPE sigillhandler(int sig )
{
	siglongjmp( sigill_recover, 1 );
}

typedef RETSIGTYPE (*__sig_t)(int);

static int testsseill()
{
	int illegal;
#if defined(__CYGWIN__)
	/* SSE causes a crash on CYGWIN, apparently.
	   Perhaps the wrong signal is being caught or something along
	   those line ;-) or maybe SSE itself won't work...
	*/
	illegal = 1;
#else
	__sig_t old_handler = signal( SIGILL, sigillhandler);
	if( sigsetjmp( sigill_recover, 1 ) == 0 )
	{
		asm ( "movups %xmm0, %xmm0" );
		illegal = 0;
	}
	else
		illegal = 1;
	signal( SIGILL, old_handler );
#endif
	return illegal;
}

static int x86_accel (void)
{
    int32_t eax, ebx, ecx, edx;
    int32_t AMD;
    int32_t caps;

#define cpuid(op,eax,ebx,ecx,edx)	\
    asm ("cpuid"			\
	 : "=a" (eax),			\
	   "=b" (ebx),			\
	   "=c" (ecx),			\
	   "=d" (edx)			\
	 : "a" (op)			\
	 : "cc")

    asm ("pushfl\n\t"
	 "popl %0\n\t"
	 "movl %0,%1\n\t"
	 "xorl $0x200000,%0\n\t"
	 "pushl %0\n\t"
	 "popfl\n\t"
	 "pushfl\n\t"
	 "popl %0"
         : "=a" (eax),
	       "=b" (ebx)
	 :
	 : "cc");


    if (eax == ebx)		// no cpuid
	return 0;

    cpuid (0x00000000, eax, ebx, ecx, edx);
    if (!eax)			// vendor string only
	return 0;

    AMD = (ebx == 0x68747541) && (ecx == 0x444d4163) && (edx == 0x69746e65);

    cpuid (0x00000001, eax, ebx, ecx, edx);
    if (! (edx & 0x00800000))	// no MMX
	return 0;

    caps = ACCEL_X86_MMX;
    /* If SSE capable CPU has same MMX extensions as AMD
	   and then some. However, to use SSE O.S. must have signalled
	   it use of FXSAVE/FXRSTOR through CR4.OSFXSR and hence FXSR (bit 24)
	   here
	*/
    if ((edx & 0x02000000))	
		caps = ACCEL_X86_MMX | ACCEL_X86_MMXEXT;
	if( (edx & 0x03000000) == 0x03000000 )
	{
		/* Check whether O.S. has SSE support... has to be done with
		   exception 'cos those Intel morons put the relevant bit
		   in a reg that is only accesible in ring 0... doh! 
		*/
		if( !testsseill() )
			caps |= ACCEL_X86_SSE;
	}

    cpuid (0x80000000, eax, ebx, ecx, edx);
    if (eax < 0x80000001)	// no extended capabilities
		return caps;

    cpuid (0x80000001, eax, ebx, ecx, edx);

    if (edx & 0x80000000)
	caps |= ACCEL_X86_3DNOW;

    if (AMD && (edx & 0x00400000))	// AMD MMX extensions
	{
		caps |= ACCEL_X86_MMXEXT;
	}

    return caps;
}
#endif

int cpu_accel (void)
{
#ifdef HAVE_X86CPU 
    static int got_accel = 0;
    static int accel;

    if (!got_accel) {
	got_accel = 1;
	accel = x86_accel ();
    }

    return accel;
#else
    return 0;
#endif
}
