#ifndef VJ_X86_H
#define VJ_X86_H



#include <stdlib.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len);

//#define veejay_memcpy(t,f,l) memcpy(t,f,l)

extern void mymemset_generic(void *s, char c, size_t count);

extern void vj_mem_init(void);

extern void *vj_malloc(unsigned int size);

#endif
