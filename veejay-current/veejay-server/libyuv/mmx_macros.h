#ifndef MMX_MACROS_H
#define MMX_MACROS_H

#define BLOCK_SIZE 4096

#undef PREFETCH
#undef EMMS
#undef MOVNTQ
#undef SFENCE
#undef MIN_LEN

#if HAVE_ASM_3DNOW
#define PREFETCH  "prefetch"
#elif HAVE_ASM_MMX2
#define PREFETCH "prefetchnta"
#else
#define PREFETCH  " # nop"
#endif

#if HAVE_ASM_MMX2
#define MOVNTQ "movntq"
#define SFENCE "sfence"
#else
#define MOVNTQ "movq"
#define SFENCE " # nop"
#endif

#if HAVE_ASM_MMX
#define MIN_LEN 0x800  
#else
#define MIN_LEN 0x40 
#endif

#if ARCH_X86_64
#define REG_a "rax"
typedef int64_t x86_reg;
#elif ARCH_X86
#define REG_a "eax"
typedef int32_t x86_reg;
#else
typedef int x86_reg;
#endif

#ifdef HAVE_ASM_3DNOW
#define _EMMS     "femms"
#else
#define _EMMS     "emms"
#endif


#endif

