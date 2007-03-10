#ifndef VJ_X86_H
#define VJ_X86_H


#include <stdint.h>
#include <stdlib.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

extern void *(* veejay_memset)(void *to, uint8_t val, size_t len);

extern void mymemset_generic(void *s, char c, size_t count);

extern void vj_mem_init(void);

extern char *get_memcpy_descr( void );

extern void *vj_malloc(unsigned int size);

extern	void *vj_calloc(unsigned int size );

extern void *vj_yuvalloc( unsigned int w, unsigned int h );

extern void fast_memset_dirty(void * to, int val, size_t len);

extern void fast_memset_finish();

extern void	packed_plane_clear( size_t len, void *to );

extern void	yuyv_plane_clear( size_t len, void *to );

extern int	cpu_cache_size();

extern char	*veejay_strncat( char *s1, char *s2, size_t n );

extern void    yuyv_plane_init();

extern void    yuyv_plane_clear( size_t len, void *to );

#endif
