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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvevo/libvevo.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <veejaycore/veejaycore.h>
#include <locale.h>

extern void find_best_memcpy(void);
extern void find_best_memset(void);
extern void yuyv_plane_init(void);
extern void benchmark_tasks(int n_tasks, long n_frames, int w, int h);
extern void init_parallel_tasks(int n_tasks);

static int CACHE_LINE_SIZE = 64;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define VJ_MEM_KIB 1024ULL
#define VJ_MEM_MIB (1024ULL * 1024ULL)

static int vj_mem_kib_line(const char *line, const char *key, uint64_t *bytes)
{
	const size_t key_len = strlen(key);

	if(strncmp(line, key, key_len) != 0 || line[key_len] != ':')
		return 0;

	const char *value_text = line + key_len + 1;
	while(isspace((unsigned char)*value_text))
		value_text++;

	errno = 0;
	char *end = NULL;
	unsigned long long value = strtoull(value_text, &end, 10);
	if(errno != 0 || end == value_text || value > UINT64_MAX / VJ_MEM_KIB)
		return 0;

	*bytes = (uint64_t)value * VJ_MEM_KIB;
	return 1;
}

static int vj_mem_proc_info(uint64_t *total, uint64_t *available,
							uint64_t *virtual_size)
{
	FILE *file = fopen("/proc/meminfo", "r");
	char line[256];
	uint64_t free_bytes = 0;
	uint64_t buffers = 0;
	uint64_t cached = 0;
	int have_total = 0;
	int have_available = 0;

	if(file) {
		while(fgets(line, sizeof(line), file)) {
			if(!have_total && vj_mem_kib_line(line, "MemTotal", total))
				have_total = 1;
			else if(!have_available && vj_mem_kib_line(line, "MemAvailable", available))
				have_available = 1;
			else if(free_bytes == 0)
				vj_mem_kib_line(line, "MemFree", &free_bytes);
			if(buffers == 0)
				vj_mem_kib_line(line, "Buffers", &buffers);
			if(cached == 0)
				vj_mem_kib_line(line, "Cached", &cached);
		}
		fclose(file);
	}

	if(!have_available && free_bytes <= UINT64_MAX - buffers &&
	   free_bytes + buffers <= UINT64_MAX - cached)
	{
		*available = free_bytes + buffers + cached;
		have_available = *available > 0;
	}

	if(virtual_size) {
		*virtual_size = 0;
		file = fopen("/proc/self/status", "r");
		if(file) {
			while(fgets(line, sizeof(line), file)) {
				if(vj_mem_kib_line(line, "VmSize", virtual_size))
					break;
			}
			fclose(file);
		}
	}

	return have_total && have_available;
}

static int vj_mem_sysconf_info(uint64_t *total, uint64_t *available)
{
	const long page_size = sysconf(_SC_PAGESIZE);
	const long total_pages = sysconf(_SC_PHYS_PAGES);
	const long available_pages = sysconf(_SC_AVPHYS_PAGES);

	if(page_size <= 0 || total_pages <= 0 || available_pages < 0 ||
	   (uint64_t)total_pages > UINT64_MAX / (uint64_t)page_size ||
	   (uint64_t)available_pages > UINT64_MAX / (uint64_t)page_size)
		return 0;

	*total = (uint64_t)total_pages * (uint64_t)page_size;
	*available = (uint64_t)available_pages * (uint64_t)page_size;
	return 1;
}

static int vj_mem_read_u64(const char *path, uint64_t *value,
                           int allow_max, int *is_max)
{
	FILE *file = fopen(path, "r");
	char text[128];

	if(is_max)
		*is_max = 0;

	if(!file)
		return 0;
	if(!fgets(text, sizeof(text), file)) {
		fclose(file);
		return 0;
	}
	fclose(file);

	if(allow_max && strncmp(text, "max", 3) == 0) {
		char *end = text + 3;
		while(isspace((unsigned char)*end))
			end++;
		if(*end != '\0')
			return 0;
		if(is_max)
			*is_max = 1;
		return 1;
	}

	errno = 0;
	char *end = NULL;
	unsigned long long parsed = strtoull(text, &end, 10);
	if(errno != 0 || end == text)
		return 0;

	while(isspace((unsigned char)*end))
		end++;
	if(*end != '\0')
		return 0;

	*value = (uint64_t)parsed;
	return 1;
}

static int vj_mem_controller_has_memory(const char *controllers, size_t length)
{
	const char *cursor = controllers;
	const char *end = controllers + length;

	while(cursor < end) {
		const char *comma = memchr(cursor, ',', (size_t)(end - cursor));
		const char *token_end = comma ? comma : end;
		if((size_t)(token_end - cursor) == 6 &&
		   strncmp(cursor, "memory", 6) == 0)
			return 1;
		cursor = comma ? comma + 1 : end;
	}
	return 0;
}

static void vj_mem_cgroup_paths(char *v2, size_t v2_size,
								char *v1, size_t v1_size)
{
	FILE *file = fopen("/proc/self/cgroup", "r");
	char line[PATH_MAX + 128];

	v2[0] = '\0';
	v1[0] = '\0';
	if(!file)
		return;

	while(fgets(line, sizeof(line), file)) {
		char *first = strchr(line, ':');
		char *second = first ? strchr(first + 1, ':') : NULL;
		if(!first || !second)
			continue;

		char *path = second + 1;
		path[strcspn(path, "\r\n")] = '\0';
		if(first + 1 == second) {
			snprintf(v2, v2_size, "%s", path);
		}
		else if(vj_mem_controller_has_memory(first + 1,
											  (size_t)(second - first - 1)))
		{
			snprintf(v1, v1_size, "%s", path);
		}
	}
	fclose(file);
}

static int vj_mem_cgroup_file(char *path, size_t path_size,
							  const char *base, const char *group,
							  const char *name)
{
	int result;

	if(group && group[0] != '\0' && strcmp(group, "/") != 0)
		result = snprintf(path, path_size, "%s%s/%s", base, group, name);
	else
		result = snprintf(path, path_size, "%s/%s", base, name);

	return result > 0 && (size_t)result < path_size;
}

static int vj_mem_cgroup_constraint(const char *base, const char *group,
									const char *limit_name,
									const char *usage_name,
									int text_max,
									uint64_t *best_limit,
									uint64_t *best_available,
									int *found)
{
	char path[PATH_MAX];
	uint64_t limit = 0;
	uint64_t usage = 0;
	int unlimited = 0;

	if(!vj_mem_cgroup_file(path, sizeof(path), base, group, limit_name) ||
	   !vj_mem_read_u64(path, &limit, text_max, &unlimited))
		return 0;
	if(unlimited || limit >= (1ULL << 60))
		return 1;
	if(!vj_mem_cgroup_file(path, sizeof(path), base, group, usage_name) ||
	   !vj_mem_read_u64(path, &usage, 0, NULL))
		return 0;

	const uint64_t available = usage < limit ? limit - usage : 0;
	if(!*found || limit < *best_limit)
		*best_limit = limit;
	if(!*found || available < *best_available)
		*best_available = available;
	*found = 1;
	return 1;
}

static int vj_mem_cgroup_parent(char *group)
{
	if(!group || group[0] == '\0' || strcmp(group, "/") == 0)
		return 0;

	char *slash = strrchr(group, '/');
	if(!slash || slash == group)
		group[1] = '\0';
	else
		*slash = '\0';
	return 1;
}

static int vj_mem_cgroup_hierarchy(const char *base, const char *group,
									const char *limit_name,
									const char *usage_name,
									int text_max,
									uint64_t *limit,
									uint64_t *available)
{
	char current[PATH_MAX];
	int found = 0;

	if(!group || group[0] == '\0')
		return 0;
	snprintf(current, sizeof(current), "%s", group);

	for(;;) {
		vj_mem_cgroup_constraint(base, current, limit_name, usage_name,
								 text_max, limit, available, &found);
		if(!vj_mem_cgroup_parent(current))
			break;
	}
	return found;
}

static int vj_mem_cgroup_info(uint64_t *limit, uint64_t *available)
{
	char v2_group[PATH_MAX];
	char v1_group[PATH_MAX];

	vj_mem_cgroup_paths(v2_group, sizeof(v2_group),
						v1_group, sizeof(v1_group));

	int found = vj_mem_cgroup_hierarchy("/sys/fs/cgroup", v2_group,
										"memory.max", "memory.current", 1,
										limit, available);
	if(!found)
		found = vj_mem_cgroup_hierarchy("/sys/fs/cgroup/memory", v1_group,
											"memory.limit_in_bytes",
											"memory.usage_in_bytes", 0,
											limit, available);
	return found;
}

int vj_mem_get_info(vj_mem_info_t *info)
{
	uint64_t total = 0;
	uint64_t available = 0;
	uint64_t virtual_size = 0;
	uint64_t constrained_total = 0;
	uint64_t constrained_available = 0;

	if(!info)
		return 0;
	info->total_bytes = 0;
	info->available_bytes = 0;

	if(!vj_mem_proc_info(&total, &available, &virtual_size) &&
	   !vj_mem_sysconf_info(&total, &available))
		return 0;

	if(vj_mem_cgroup_info(&constrained_total, &constrained_available)) {
		if(constrained_total < total)
			total = constrained_total;
		if(constrained_available < available)
			available = constrained_available;
	}

	struct rlimit address_limit;
	if(getrlimit(RLIMIT_AS, &address_limit) == 0 &&
	   address_limit.rlim_cur != RLIM_INFINITY)
	{
		const uint64_t limit = (uint64_t)address_limit.rlim_cur;
		const uint64_t remaining = virtual_size < limit ?
								   limit - virtual_size : 0;
		if(limit < total)
			total = limit;
		if(remaining < available)
			available = remaining;
	}

	info->total_bytes = total;
	info->available_bytes = available;
	return total > 0;
}

int vj_mem_parse_env_mb(const char *name, size_t *bytes)
{
	const char *text;

	if(!name || !bytes)
		return -1;
	*bytes = 0;
	text = getenv(name);
	if(!text || !*text || strcasecmp(text, "auto") == 0)
		return 0;
	if(strcasecmp(text, "off") == 0 || strcmp(text, "0") == 0)
		return 1;

	for(const char *cursor = text; *cursor; cursor++)
		if(!isdigit((unsigned char)*cursor))
			return -1;

	errno = 0;
	char *end = NULL;
	unsigned long long value = strtoull(text, &end, 10);
	if(errno != 0 || end == text || *end != '\0' ||
	   value > SIZE_MAX / VJ_MEM_MIB)
		return -1;

	*bytes = (size_t)value * (size_t)VJ_MEM_MIB;
	return 1;
}

#if defined(HAVE_ASM_AVX2)
#define MEM_ALIGNMENT_SIZE 64
#elif defined(HAVE_ASM_AVX)
#define MEM_ALIGNMENT_SIZE 32
#elif defined(HAVE_ASM_SSE) || defined(HAVE_ASM_SSE2) || defined(__SSE4_2__) || defined(__SSE4_1__)
#define MEM_ALIGNMENT_SIZE 16
#elif defined(__ARM_ARCH_8A__)
#define MEM_ALIGNMENT_SIZE 16
#elif defined(__ARM_ARCH_7A__)
#define MEM_ALIGNMENT_SIZE 8
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
static int get_cache_line_size(void) {
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
	long detected_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
	if (detected_size > 0 && detected_size <= INT_MAX) {
		return (int)detected_size;
	}
#endif
	return 64;
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

static int	get_cache_line_size(void)
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

int	cpu_get_cacheline_size(void)
{
	return CACHE_LINE_SIZE;
}

int	mem_align_size(void)
{
	return MEM_ALIGNMENT_SIZE;
}

void vj_mem_init(int w, int h)
{
#if defined(ARCH_X86) || defined(ARCH_X86_64) || defined(HAVE_ARM)
    int detected_size = get_cache_line_size();

    if (detected_size > 0) {
        CACHE_LINE_SIZE = detected_size;
    } else {
        // Fallback to 64, which is standard for almost all modern x86_64
        CACHE_LINE_SIZE = 64;
        veejay_msg(VEEJAY_MSG_DEBUG, "CPU Cache line detection failed, defaulting to 64");
    }
#endif
	int lim = w * h;

    setenv("OMP_PROC_BIND", ( lim <= 414720? "close" : "spread"), 1);
    setenv("OMP_PLACES", "cores", 1);
    setenv("OMP_WAIT_POLICY", "active", 1);

	setlocale(LC_NUMERIC, "C");

#if defined (HAVE_ASM_MMX) || defined (HAVE_ASM_SSE)
	yuyv_plane_init();
#endif
	//find_best_memcpy();	
	//find_best_memset();
	vj_mem_set_defaults(w,h);
}

void vj_mem_optimize(void) {
#ifndef STRICT_CHECKING
	//find_best_memcpy();
	//find_best_memset();
#endif
}

int	vj_mem_threaded_init(int w, int h)
{
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
	uintptr_t misalignment = ((uintptr_t)ptr + offset) % alignment;
	size_t padding = ( misalignment != 0 ) ? ( alignment - misalignment ) : 0;
	return ptr + offset + padding;
}
