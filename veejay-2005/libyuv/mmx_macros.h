#include <stdlib.h> /* size_t */
#include <config.h>

/* MMX memcpy stuff taken from MPlayer (http://www.mplayerhq.hu) */

#define BLOCK_SIZE 4096
#define CONFUSION_FACTOR 0
//Feel free to fine-tune the above 2, it might be possible to get some speedup with them :)

#undef HAVE_MMX1
#ifndef MMXEXT
/*  means: mmx v.1. Note: Since we added alignment of destinition it speedups
    of memory copying on PentMMX, Celeron-1 and P2 upto 12% versus
    standard (non MMX-optimized) version.
    Note: on K6-2+ it speedups memory copying upto 25% and
          on K7 and P3 about 500% (5 times). */
#define HAVE_MMX1
#endif

#undef MMREG_SIZE
#define MMREG_SIZE 64 //8

#undef PREFETCH
#undef EMMS

#ifdef MMXEXT
#define PREFETCH "prefetchnta"
#else
#define PREFETCH "/nop"
#endif

#define EMMS     "emms"

#undef MOVNTQ
#ifdef MMXEXT
#define MOVNTQ "movntq"
#else
#define MOVNTQ "movq"
#endif

#undef MIN_LEN
#ifdef HAVE_MMX1
#define MIN_LEN 0x800  /* 2K blocks */
#else
#define MIN_LEN 0x40  /* 64-byte blocks */
#endif
