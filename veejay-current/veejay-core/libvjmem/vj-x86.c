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
#include <libvevo/libvevo.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <veejaycore/veejaycore.h>
extern void find_best_memcpy(void);
extern void find_best_memset(void);
extern void yuyv_plane_init();
extern void benchmark_tasks(int n_tasks, long n_frames, int w, int h);
extern void init_parallel_tasks(int n_tasks);

static int CACHE_LINE_SIZE = 64;

#if defined(HAVE_ASM_SSE) || defined(HAVE_ASM_SSE2) || defined(__SSE4_2__) || defined(__SSE4_1__)
#define MEM_ALIGNMENT_SIZE 16
#elif defined (HAVE_ASM_AVX2)
#define MEM_ALIGNMENT_SIZE 64
#elif defined (HAVE_ASM_AVX)
#define MEM_ALIGNMENT_SIZE 32
#elif defined (__ARM_ARCH_7A__)
#define MEM_ALIGNMENT_SIZE 8
#elif defined (__ARM_ARCH_8A__ )
#define MEM_ALIGNMENT_SIZE 16
#else
#define MEM_ALIGNMENT_SIZE 8
#endif

static int has_cpuid(void)
{
#ifdef ARCH_X86_64
	return 1;
#endif
#ifdef ARCH_X86
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
#endif
	return 0;
}

#ifdef HAVE_ARM
static int get_cache_line_size() {
    int ctr_el0;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));
    int cwgr_val = (ctr_el0 >> 32) & 0x7;
    int cache_line_size = 64 << cwgr_val;
    return cache_line_size;
}
#endif
#if defined(ARCH_X86_64) || defined(ARCH_X86)
// copied from Mplayer (want to have cache line size detection ;) )
static void do_cpuid(unsigned int ax, unsigned int *p)
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

static int	get_cache_line_size()
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
#endif

int	cpu_cache_size()
{
	return CACHE_LINE_SIZE;
}

int	mem_align_size()
{
	return MEM_ALIGNMENT_SIZE;
}

void vj_mem_init(void)
{
#if defined(ARCH_X86) || defined(ARCH_X86_X64) || defined(HAVE_ARM) 
	CACHE_LINE_SIZE = get_cache_line_size();
#endif

	
#if defined (HAVE_ASM_MMX) || defined (HAVE_ASM_SSE)
	yuyv_plane_init();
#endif
	//find_best_memcpy();	
	//find_best_memset();
	vj_mem_set_defaults();
}

void vj_mem_optimize() {
#ifndef STRICT_CHECKING
	//find_best_memcpy();	
	//find_best_memset();
#endif
}


void	vj_mem_destroy()
{
}

int	vj_mem_threaded_init(int w, int h)
{
	task_init( w , h );

	init_parallel_tasks( 0 ); // sets functions pointer to single/multi threaded versions
	
	return 1;
}


void *vj_malloc_(size_t size)
{
	if( size <= 0 )
		return NULL;
	void *ptr = NULL;
#ifdef HAVE_POSIX_MEMALIGN
    size_t aligned_size = (size + MEM_ALIGNMENT_SIZE - 1) & ~(MEM_ALIGNMENT_SIZE - 1);
    
    int err = posix_memalign(&ptr, MEM_ALIGNMENT_SIZE, aligned_size);
    if (err == EINVAL) {
        veejay_msg(0, "Error: Memory size is not a multiple of %zu: %zu\n", MEM_ALIGNMENT_SIZE, aligned_size);
        return NULL;
    } else if (err == ENOMEM) {
        veejay_msg(0, "Error: Unable to allocate %zu bytes of memory\n", size);
        return NULL;
    }
#else
#ifdef HAVE_MEMALIGN
    ptr = memalign(MEM_ALIGNMENT_SIZE, size);
#else
    ptr = malloc(size);
#endif
#endif

    if (!ptr) {
        veejay_msg(0, "Error: Failed to allocate %zu bytes of memory\n", size);
        return NULL;
    }

    return ptr;
}

void	*vj_calloc_( size_t size )
{
	void *ptr = vj_malloc_(size);
	if(ptr)
		memset(ptr,0,size);
	return ptr;
}

typedef struct 
{
	size_t len;
	void	*addr;
	size_t cur;
} v_simple_pool_t;

void	*vj_simple_pool_init( size_t s )
{
	v_simple_pool_t *pool = (v_simple_pool_t*) vj_malloc( sizeof(v_simple_pool_t) );
	if(!pool)
		return NULL;
	void *addr = vj_calloc_( s + 16 );
	if(!addr) {
		free(pool);
		return NULL;
	}
	pool->addr = addr;
	pool->cur = 0;
	pool->len = s;
	return (void*) pool;
}

void	*vj_simple_pool_alloc( void *ptr, size_t s )
{
	v_simple_pool_t *pool = (v_simple_pool_t*) ptr;
	if( s > pool->len || (pool->cur + s) > pool->len ) {
		return NULL;
	}
	uint8_t *addr = (uint8_t*) pool->addr + (pool->cur);

	pool->cur += s;

	return (void*) ( addr + pool->cur );
}

void	vj_simple_pool_reset( void *ptr )
{
	v_simple_pool_t *pool = (v_simple_pool_t*) ptr;
	pool->cur = 0;
}

void	vj_simple_pool_free( void *ptr )
{
	v_simple_pool_t *pool = (v_simple_pool_t*) ptr;
	if( pool ) { 
		if( pool->addr )
			free(pool->addr);
		free(pool);
	}
}

static size_t get_alignment(void* ptr) {
    uintptr_t address = (uintptr_t)ptr;
    size_t alignment = 1;

    while ((address & 1) == 0) {
        alignment <<= 1;
        address >>= 1;
    }

    return alignment;
}
   
int	check_desired_alignment( void *ptr ) {
	size_t align = mem_align_size();
	if( ptr == NULL )
	    return 1;
	if( (uintptr_t) ptr % align != 0 ) {
		veejay_msg(VEEJAY_MSG_WARNING, "Data %p is not aligned at %u bytes but at %u bytes", ptr, align, get_alignment(ptr));
		return 0;
	}	
	return 1;
}

uint8_t *realign_buffer( uint8_t *ptr, size_t offset ) {
	size_t alignment = mem_align_size();
	size_t misalignment = (size_t)( ptr + offset ) % alignment;
	size_t padding = ( misalignment != 0 ) ? ( alignment - misalignment ) : 0;
	return ptr + offset + padding;
}
