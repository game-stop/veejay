#undef HAVE_MOVTAR
#undef HAVE_CATGETS
#undef HAVE_GETTEXT
#undef HAVE_LC_MESSAGES
#undef HAVE_STPCPY
#undef HAVE_LIBSM
#undef ENABLE_NLS
#undef PACKAGE_LOCALE_DIR
#undef PACKAGE_DATA_DIR
#undef PACKAGE_SOURCE_DIR
#undef HAVE_XML2
#undef HAVE_DIRECTFB

#define _GNU_SOURCE 1			/* We make some use of C9X and POSIX and GNU
								 facilities... */

#define VERSION   "0.6.0"		/* vj tools release version */

/* Large file support ? */
#undef _FILE_OFFSET_BITS
#undef _LARGEFILE_SOURCE
#undef _LARGEFILE64_SOURCE

#undef ARCH_PPC

/* Define pthread lib stack size */
#undef HAVE_PTHREADSTACKSIZE

/* Define for an Intel based CPU */
#undef HAVE_X86CPU 	

/* For HAVEX86CPU: Define for availability of CMOV instruction (P6, P7
 * and Athlon cores).*/
#undef HAVE_CMOV

/* For HAVEX86CPU: Define if the installed GCC tool chain can generate
 * MMX instructions */
#undef HAVE_ASM_MMX

#undef HAVE_ASM_SSE

#undef HAVE_MMX

#undef HAVE_ASM_MMX2

#undef ARCH_X86

#undef HAVE_JACK

#undef HAVE_NCURSES

/* For HAVEX86CPU: Define if the installed GCC tool-chain can generate
 * 3DNow instructions */
#undef HAVE_ASM_3DNOW

/* For HAVEX86CPU: Define if the nasm assembler is available */
#undef HAVE_ASM_NASM

/* For X86 compatible machines */
#undef ARCH_X86

#undef HAVE_DIRECTFB

/* whether we're in linux or not (video4linux?) */
#undef HAVE_V4L

/* Define if the libpthread library is present */
#undef HAVE_LIBPTHREAD

/* Define if the quicktime for linux library is present */
#undef HAVE_LIBQUICKTIME
#undef HAVE_OPENQUICKTIME

/* Define if the libXxf86dga library is available */
#undef HAVE_LIBXXF86DGA

/* Define if the libmovtar library is available */
#undef HAVE_LIBMOVTAR

/* do we have some cool thing for 64bits integers? */
#undef PRID64_STRING_FORMAT

#undef BUILD_VEEJAY

/* Define for libDV and possibly YV12 support */
#undef LIBDV_PAL_YV12
#undef SUPPORT_READ_DV2
#undef SUPPORT_READ_YUV420
#undef LIBDV_PRE_0_9_9

#undef HAVE_SDL

/* Name of package */
#define PACKAGE "veejay"
#define VEEJAY 1

/* Define if disable assert() */
#undef NDEBUG
