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

#endif
