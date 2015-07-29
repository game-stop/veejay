/* veejay - Linux VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef VJ_X86_H
#define VJ_X86_H

#define MAX_WORKERS 256 

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);
extern void *(* veejay_memset)(void *to, uint8_t val, size_t len);
extern void vj_mem_init(void);
extern int 	vj_mem_threaded_init(int w, int h);
extern void vj_mem_threaded_stop(void);
extern char *get_memcpy_descr( void );
extern char	*get_memset_descr( void );
extern void	*vj_malloc_(size_t size);
extern void *vj_calloc_(size_t size );
extern void	memcpy_report();

#define vj_malloc(i) vj_malloc_(i)
#define vj_calloc(i) vj_calloc_(i)

extern void fast_memset_dirty(void * to, int val, size_t len);
extern void fast_memset_finish();
extern void	packed_plane_clear( size_t len, void *to );
extern void	yuyv_plane_clear( size_t len, void *to );
extern int	cpu_cache_size();
extern int	get_cache_line_size();
extern char	*veejay_strncat( char *s1, char *s2, size_t n );
extern char	*veejay_strncpy( char *s1, const char *s2, size_t n );
extern void yuyv_plane_init();
extern void yuyv_plane_clear( size_t len, void *to );
extern char	**vje_build_param_list( int num, ... );
extern void	*(*vj_frame_copy)( uint8_t **input, uint8_t **output, int *strides );
extern void	*(*vj_frame_clear)( uint8_t **input, int *strides, unsigned int val );
extern void	vj_frame_copy1( uint8_t *input, uint8_t *output, int size );
extern void	vj_frame_clear1( uint8_t *input, unsigned int value, int size );
extern uint8_t	num_threaded_tasks();
extern void	vj_frame_slow_threaded( uint8_t **p0_buffer, uint8_t **p1_buffer, uint8_t **img, int len, int uv_len,const float frac );
extern void	benchmark_veejay(int w, int h);
extern void	*vj_hmalloc(size_t sze, const char *name);
extern char	*vj_strdup(const char *s);
extern char	*vj_strndup(const char *s, size_t n );
extern char *vj_sprintf(char *dst, int value);
extern void	vj_simple_pool_free( void *ptr );
extern void	*vj_simple_pool_alloc( void *ptr, size_t s );
extern void	*vj_simple_pool_init( size_t s );
extern void	vj_simple_pool_reset( void *ptr );
extern void	vj_mem_destroy();
#endif
