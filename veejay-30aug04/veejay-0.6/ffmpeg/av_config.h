/* Map transcode configure defines to libavcodecs */
#ifndef __AV_CONFIG_H
#define __AV_CONFIG_H

/* Kludge to see if we had HAVE_MMX defined before we included
 * config.h - see below ... */
#ifndef HAVE_MMX
#define HAD_MMX_FALSE
#endif

#include "config.h"

/* These come from our config.h */
/* #define ARCH_PPC */
/* #define ARCH_X86 */
/* #define HAVE_MMX */
/* #define HAVE_LRINTF */
/* #define HAVE_STRPTIME */
/* #define HAVE_MEMALIGN */
/* #define HAVE_MALLOC_H */

#ifdef ARCH_PPC
#  define ARCH_POWERPC
#endif

/* We use HAVE_MMX, but for ffmpeg the Makefile's set it, so
 * unset it here, else ffmpeg use MMX constructs, even if we
 * have --disable-mmx */
#if defined(HAD_MMX_FALSE) && defined(HAVE_MMX)
#undef HAVE_MMX
#endif

#ifdef HAVE_DLOPEN
#  define CONFIG_HAVE_DLOPEN 1
#endif

#ifdef HAVE_DLFCN_H
#  define CONFIG_HAVE_DLFCN 1
#endif

#ifdef SYSTEM_DARWIN
#  define CONFIG_DARWIN 1
#endif

#ifdef CAN_COMPILE_C_ALTIVEC
#  define HAVE_ALTIVEC 1
#endif

#define CONFIG_ENCODERS 1
#define CONFIG_DECODERS 1
#define CONFIG_MPEGAUDIO_HP 0
#define CONFIG_VIDEO4LINUX 0
#define CONFIG_DV1394 1
#define CONFIG_AUDIO_OSS 0
#define CONFIG_NETWORK 0

#define CONFIG_RISKY 1

#define CONFIG_ZLIB 1
#define SIMPLE_IDCT 1
#define restrict __restrict__

#endif /* __AV_CONFIG_H */
