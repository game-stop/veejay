/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */
#include <config.h>
#include <stdlib.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <aclib/ac.h>
#include <aclib/imgconvert.h>
extern void find_best_memcpy(void);
extern void find_best_memset(void);
extern int find_best_threaded_memcpy(int w, int h);
extern void yuyv_plane_init();

static int MEM_ALIGNMENT_SIZE = 0;
static int CACHE_LINE_SIZE = 16;


#ifdef ARCH_X86
int has_cpuid(void)
{
        int a, c;

// code from libavcodec:
    __asm__ __volatile__ (
                          /* See if CPUID instruction is supported ... */
                          /* ... Get copies of EFLAGS into eax and ecx */
                          "pushf\n\t"
                          "popl %0\n\t"
                          "movl %0, %1\n\t"
                          
                          /* ... Toggle the ID bit in one copy and store */
                          /*     to the EFLAGS reg */
                          "xorl $0x200000, %0\n\t"
                          "push %0\n\t"
                          "popf\n\t"
                          
                          /* ... Get the (hopefully modified) EFLAGS */
                          "pushf\n\t"
                          "popl %0\n\t"
                          : "=a" (a), "=c" (c)
                          :
                          : "cc" 
                          );

        return (a!=c);
}

// copied from Mplayer (want to have cache line size detection ;) )
void
do_cpuid(unsigned int ax, unsigned int *p)
{
// code from libavcodec:
    __asm __volatile
        ("movl %%ebx, %%esi\n\t"
         "cpuid\n\t"
         "xchgl %%ebx, %%esi"
         : "=a" (p[0]), "=S" (p[1]), 
           "=c" (p[2]), "=d" (p[3])
         : "0" (ax));
}

int	get_cache_line_size()
{
	unsigned int regs[4];
	unsigned int regs2[4];
	unsigned int ret = 32; // default cache line size

	if(!has_cpuid())
	{
		return ret;
	}

	do_cpuid( 0x00000000, regs); // get _max_ cpuid level and vendor name
	if( regs[0] >= 0x00000001)
	{
		do_cpuid(  0x00000001, regs2 );
		ret = (( regs2[1] >> 8) & 0xff) * 8;
		return ret;
	}
	do_cpuid(0x80000000, regs );
	if( regs[0] >= 0x80000006) {
		do_cpuid( 0x80000001, regs2 );
		ret = (regs[2] & 0xff);
		return ret;
	}
	return ret;
}


void mymemset_generic(void * s, char c,size_t count)
{
int d0, d1;
__asm__ __volatile__(
	"rep\n\t"
	"stosb"
	: "=&c" (d0), "=&D" (d1)
	:"a" (c),"1" (s),"0" (count)
	:"memory");
}

#else 
void mymemset_generic(void *s, char c, size_t cc )
{
	memset(s,c,cc);
} 
#endif

int	cpu_cache_size()
{
	return CACHE_LINE_SIZE;
}

void vj_mem_init(void)
{
	ac_init( AC_ALL );

	ac_imgconvert_init(AC_ALL);


#ifdef ARCH_X86 
	CACHE_LINE_SIZE = get_cache_line_size();
#endif
	if(MEM_ALIGNMENT_SIZE == 0)
		MEM_ALIGNMENT_SIZE = getpagesize();
#if defined (HAVE_ASM_MMX) || defined (HAVE_ASM_SSE)
	yuyv_plane_init();
#endif
	find_best_memcpy();	
	find_best_memset();
}

int	vj_mem_threaded_init(int w, int h)
{
	int n = find_best_threaded_memcpy(w, h);
	if( n > 1 ) {
		int res = task_start( n );
		if( res != n ) {
			veejay_msg(0, "Failed to initialize threadpool of %d threads.", n );
			return 0;
		}
	}
	if( n == - 1)
		return 0;

	return 1;
}

void	vj_mem_threaded_stop()
{
	int tasks = num_threaded_tasks();
	if( tasks > 0 )
		task_stop( tasks );
}

void *vj_malloc_(size_t size)
{
	if( size <= 0 )
		return NULL;
	void *ptr = NULL;
#ifdef HAVE_POSIX_MEMALIGN
	posix_memalign( &ptr, MEM_ALIGNMENT_SIZE, size );
#else
#ifdef HAVE_MEMALIGN
	ptr = memalign( MEM_ALIGNMENT_SIZE, size );
#else	
	ptr = malloc ( size ) ;
#endif
#endif
	if(!ptr)
		return NULL;

	return ptr;
}
void	*vj_strict_malloc( unsigned int size, const char *f, int line )
{	
	veejay_msg(0, "%d\t\tbytes\t\tin %s:%d",size,f,line);
	return vj_malloc_( size );
}
void	*vj_calloc_( unsigned int size )
{
	void *ptr = vj_malloc_( size );
	if(ptr)
		veejay_memset( ptr, 0, size );
	return ptr;	
}
void	*vj_strict_calloc( unsigned int size, const char *f, int line )
{
	veejay_msg(0, "%d\t\tbytes\t\tin %s:%d", size,f, line );
	return vj_calloc_(size);
}
#define    RUP8(num)(((num)+8)&~8)


