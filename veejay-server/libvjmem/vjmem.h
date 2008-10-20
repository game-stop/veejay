/* veejay - Linux VeeJay
 * 	     (C) 2002-2007 Niels Elburg <nelburg@looze.net> 
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

#include <config.h>
#include <stdint.h>
#include <stdlib.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);
extern void *(* veejay_memset)(void *to, uint8_t val, size_t len);
extern void vj_mem_init(void);
extern char *get_memcpy_descr( void );
extern void *vj_malloc_(unsigned int size);
#define vj_malloc(i) vj_malloc_(i)
extern  void *vj_calloc_(unsigned int size );
#define vj_calloc(i) vj_calloc_(i)
extern void *vj_yuvalloc( unsigned int w, unsigned int h );
extern void fast_memset_dirty(void * to, int val, size_t len);
extern void fast_memset_finish();
extern void	packed_plane_clear( size_t len, void *to );
extern void	yuyv_plane_clear( size_t len, void *to );
extern int	cpu_cache_size();
extern int	get_cache_line_size();
extern char	*veejay_strncat( char *s1, char *s2, size_t n );
extern char	*veejay_strncpy( char *s1, const char *s2, size_t n );
extern void    yuyv_plane_init();
extern void    yuyv_plane_clear( size_t len, void *to );

#endif
