/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Compiling for MIPS CPU */
/* #undef ARCH_MIPS */

/* Compiling for PowerPC */
/* #undef ARCH_PPC */

/* Compiling for x86 architecture */
#define ARCH_X86 1

/* Compiling for x86-64 architecture CPU */
/* #undef ARCH_X86_64 */

/* */
#define AVCODEC_INC <ffmpeg/avcodec.h>

/* */
#define AVUTIL_INC <ffmpeg/avutil.h>

/* Define to 1 if you have the <alloca.h> header file. */
#define HAVE_ALLOCA_H 1

/* Whether or not we have alsa */
#define HAVE_ALSA 1

/* Inline PPC Altivec primitives available */
/* #undef HAVE_ALTIVEC */

/* Compiling in 3Dnow */
/* #undef HAVE_ASM_3DNOW */

/* Compiling in MMX support */
#define HAVE_ASM_MMX 

/* Compiling in MMX2 */
#define HAVE_ASM_MMX2 

/* Compiling in MMXEXT */
#define HAVE_ASM_MMXEXT 

/* Compiling in SSE support */
#define HAVE_ASM_SSE 

/* Compiling in SSE2 support */
#define HAVE_ASM_SSE2 

/* use avcodec */
#define HAVE_AVCODEC 1

/* Define to 1 if you have the `bzero' function. */
#define HAVE_BZERO 1

/* Compiling in CMOV */
#define HAVE_CMOV 

/* Include config.h */
#define HAVE_CONFIG_H 1

/* MAC OS X Darin */
/* #undef HAVE_DARWIN */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Compile with dlopen support */
#define HAVE_DL_DLOPEN 

/* Define to 1 if you have the <fenv.h> header file. */
#define HAVE_FENV_H 1

/* Defined if building against uninstalled FFmpeg source */
#define HAVE_FFMPEG_UNINSTALLED 

/* Define to 1 if you have the `fmax' function. */
/* #undef HAVE_FMAX */

/* long getopt support */
#define HAVE_GETOPT_LONG 1

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `dl' library (-ldl). */
#define HAVE_LIBDL 1

/* Compiling with pthread library */
#define HAVE_LIBPTHREAD 

/* Linux platform */
#define HAVE_LINUX 

/* Define to 1 if you have the `lround' function. */
/* #undef HAVE_LROUND */

/* Define to 1 if you have the `memalign' function. */
#define HAVE_MEMALIGN 1

/* Define to 1 if you have the `memcpy' function. */
#define HAVE_MEMCPY 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Compiling for MIPS CPU */
/* #undef HAVE_MIPS */

/* MJPEGTools installed */
#define HAVE_MJPEGTOOLS 1

/* Define to 1 if you have the `mmap' function. */
#define HAVE_MMAP 1

/* Compiling in MMX support */
#define HAVE_MMX 

/* Compiling in MMX2 */
#define HAVE_MMX2 

/* Define to 1 if you have the `posix_memalign' function. */
#define HAVE_POSIX_MEMALIGN 1

/* Define to 1 if you have the `pow' function. */
/* #undef HAVE_POW */

/* Compiling for PowerPC CPU */
/* #undef HAVE_PPCCPU */

/* Compile for playstation2 */
/* #undef HAVE_PS2 */

/* Using pthread stack size */
/* #undef HAVE_PTHREADSTACKSIZE */

/* Define to 1 if you have the `sched_get_priority_max' function. */
#define HAVE_SCHED_GET_PRIORITY_MAX 1

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* Compiling in SSE support */
#define HAVE_SSE 

/* Compiling in SSE2 support */
#define HAVE_SSE2 

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the `strndup' function. */
#define HAVE_STRNDUP 1

/* Define to 1 if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* use swscaler */
#define HAVE_SWSCALER 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Veejay installed */
#define HAVE_VEEJAY 1

/* Compiling for x86 architecture CPU */
#define HAVE_X86CPU 

/* Compiling for x86-64 architecture CPU */
/* #undef HAVE_X86_CPU */

/* Is __progname defined by system? */
#define HAVE___PROGNAME 1

/* Name of package */
#define PACKAGE "gveejay"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "veejay-users@lists.sourceforge.net"

/* Define to the full name of this package. */
#define PACKAGE_NAME "gveejay"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "gveejay 1.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "gveejay"

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.2"

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* The size of `float', as computed by sizeof. */
#define SIZEOF_FLOAT 4

/* The size of `int', as computed by sizeof. */
#define SIZEOF_INT 4

/* The size of `long int', as computed by sizeof. */
#define SIZEOF_LONG_INT 4

/* The size of `size_t', as computed by sizeof. */
#define SIZEOF_SIZE_T 4

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* */
#define SWSCALE_INC <ffmpeg/swscale.h>

/* use gdk image load / save */
#define USE_GDK_PIXBUF 1

/* GtkCairo widget - Cairo */
#define USE_GTKCAIRO 

/* Version number of package */
#define VERSION "1.2"

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */

/* Define to 1 if the X Window System is missing or not being used. */
/* #undef X_DISPLAY_MISSING */

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Number of bits in a file offset, on hosts where this is settable. */
#define _FILE_OFFSET_BITS 64

/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif
