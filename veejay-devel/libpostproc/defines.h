
#ifndef _DEFINES_H
#define _DEFINES_H

#include <config.h>

#ifdef ARCH_X86_64
#  define REGa    rax
#  define REGb    rbx
#  define REGSP   rsp
#  define REG_a  "rax"
#  define REG_b  "rbx"
#  define REG_c  "rcx"
#  define REG_d  "rdx"
#  define REG_S  "rsi"
#  define REG_D  "rdi"
#  define REG_SP "rsp"
#  define REG_BP "rbp"
#else
#  define REGa    eax
#  define REGb    ebx
#  define REGSP   esp
#  define REG_a  "eax"
#  define REG_b  "ebx"
#  define REG_c  "ecx"
#  define REG_d  "edx"
#  define REG_S  "esi"
#  define REG_D  "edi"
#  define REG_SP "esp"
#  define REG_BP "ebp"
#endif


/* Feel free to add more to the list, eg. a.out IMO */
#if defined(__CYGWIN__) || defined(__MINGW32__) || defined(__OS2__) || \
   (defined(__OpenBSD__) && !defined(__ELF__))
#define MANGLE(a) "_" #a
#else
#define MANGLE(a) #a
#endif

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#else

#include <inttypes.h>

#ifdef ARCH_X86_64
#  define LEGACY_REGS "=Q"
#else
#  define LEGACY_REGS "=q"
#endif

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static inline uint16_t ByteSwap16(uint16_t x)
{
  __asm("xchgb %b0,%h0"	:
        LEGACY_REGS (x)	:
        "0" (x));
    return x;
}
#define bswap_16(x) ByteSwap16(x)

static inline uint32_t ByteSwap32(uint32_t x)
{
#if __CPU__ > 386
 __asm("bswap	%0":
      "=r" (x)     :
#else
 __asm("xchgb	%b0,%h0\n"
      "	rorl	$16,%0\n"
      "	xchgb	%b0,%h0":
      LEGACY_REGS (x)		:
#endif
      "0" (x));
  return x;
}
#define bswap_32(x) ByteSwap32(x)

static inline uint64_t ByteSwap64(uint64_t x)
{
#ifdef ARCH_X86_64
  __asm("bswap	%0":
        "=r" (x)     :
        "0" (x));
  return x;
#else
  register union { __extension__ uint64_t __ll;
          uint32_t __l[2]; } __x;
  asm("xchgl	%0,%1":
      "=r"(__x.__l[0]),"=r"(__x.__l[1]):
      "0"(bswap_32((unsigned long)x)),"1"(bswap_32((unsigned long)(x>>32))));
  return __x.__ll;
#endif
}
#define bswap_64(x) ByteSwap64(x)

#elif defined(ARCH_SH4)

static inline uint16_t ByteSwap16(uint16_t x) {
	__asm__("swap.b %0,%0":"=r"(x):"0"(x));
	return x;
}

static inline uint32_t ByteSwap32(uint32_t x) {
	__asm__(
	"swap.b %0,%0\n"
	"swap.w %0,%0\n"
	"swap.b %0,%0\n"
	:"=r"(x):"0"(x));
	return x;
}

#define bswap_16(x) ByteSwap16(x)
#define bswap_32(x) ByteSwap32(x)

static inline uint64_t ByteSwap64(uint64_t x)
{
    union { 
        uint64_t ll;
        struct {
           uint32_t l,h;
        } l;
    } r;
    r.l.l = bswap_32 (x);
    r.l.h = bswap_32 (x>>32);
    return r.ll;
}
#define bswap_64(x) ByteSwap64(x)

#else

#define bswap_16(x) (((x) & 0x00ff) << 8 | ((x) & 0xff00) >> 8)
			

// code from bits/byteswap.h (C) 1997, 1998 Free Software Foundation, Inc.
#define bswap_32(x) \
     ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
      (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

static inline uint64_t ByteSwap64(uint64_t x)
{
    union { 
        uint64_t ll;
        uint32_t l[2]; 
    } w, r;
    w.ll = x;
    r.l[0] = bswap_32 (w.l[1]);
    r.l[1] = bswap_32 (w.l[0]);
    return r.ll;
}
#define bswap_64(x) ByteSwap64(x)

#endif	/* !ARCH_X86 */

#endif	/* !HAVE_BYTESWAP_H */

// be2me ... BigEndian to MachineEndian
// le2me ... LittleEndian to MachineEndian

#ifdef WORDS_BIGENDIAN
#define be2me_16(x) (x)
#define be2me_32(x) (x)
#define be2me_64(x) (x)
#define le2me_16(x) bswap_16(x)
#define le2me_32(x) bswap_32(x)
#define le2me_64(x) bswap_64(x)
#else
#define be2me_16(x) bswap_16(x)
#define be2me_32(x) bswap_32(x)
#define be2me_64(x) bswap_64(x)
#define le2me_16(x) (x)
#define le2me_32(x) (x)
#define le2me_64(x) (x)
#endif


#endif
