#ifndef VJ_X86_H
#define VJ_X86_H

#include <config.h>
#include <stdint.h>
#include <stdlib.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

extern void *(* veejay_memset)(void *to, uint8_t val, size_t len);

extern void mymemset_generic(void *s, char c, size_t count);

extern void vj_mem_init(void);

extern char *get_memcpy_descr( void );

/*#ifdef STRICT_CHECKING
extern void *vj_strict_malloc(unsigned int size, const char *f, int line );
extern void *vj_strict_calloc(unsigned int size, const char *f, int line );
#define vj_malloc(i) vj_strict_malloc(i, __FUNCTION__,__LINE__)
#define vj_calloc(i) vj_strict_calloc(i, __FUNCTION__,__LINE__)
#else
extern void *vj_malloc_(unsigned int size);
#define vj_malloc(i) vj_malloc_(i)
extern	void *vj_calloc_(unsigned int size );
#define vj_calloc(i) vj_calloc_(i)
#endif*/

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

extern char	*veejay_strncat( char *s1, char *s2, size_t n );

extern void    yuyv_plane_init();

extern void    yuyv_plane_clear( size_t len, void *to );

#endif
