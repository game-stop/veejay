dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([lvdasciiart-plugin],[0.0.1],[veejay-users@lists.sourceforge.net])
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([plugins/lvd_aa.c])

LVDGMICMAJOR_VERSION=0
LVDGMICMINOR_VERSION=0
LVDGMICMICRO_VERSION=1
LVDGMICVERSION=$LVDGMICMAJOR_VERSION.$LVDGMICMINOR_VERSION.$LVDGMICMICRO_VERSION
LVDGMICCODENAME="Livido AsciiArt filter - build $LVDGMICMINOR_VERSION $LVDGMICMICRO_VERSION"
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE

AC_DEFINE(VEEJAYUTIL,1,[Building Livido AsciiArt filter])
AC_DEFINE(HAVE_CONFIG_H,1,[Include config.h])
# libtool versioning
LT_RELEASE=$LVDGMICMAJOR_VERSION.$LVDGMICMINOR_VERSION
LT_CURRENT=$LVDGMICMICRO_VERSION
LT_REVISION=$LVDGMICMICRO_VERSION
LT_AGE=$LVDGMICMICRO_VERSION
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
dnl **********************************************************************
dnl Options

AC_CONFIG_MACRO_DIR([m4])

dnl kill CFLAGS
CFLAGS=""
AC_ARG_ENABLE([strict],
		AC_HELP_STRING([--enable-strict],
			     [Compile in paranoia assertion checking]))
AC_ARG_ENABLE([debug],
		AC_HELP_STRING([--enable-debug],
			     [Compile in debugging information]))
AC_ARG_ENABLE([profile],
		AC_HELP_STRING([--enable-profiling],
			     [Compile in profiling information]))
AC_ARG_ENABLE([compile-warnings],     
              AC_HELP_STRING([--enable-compile-warnings],
                             [Turn on compiler warnings.]))
AC_ARG_ENABLE([warnings_as_errors],   
              AC_HELP_STRING([--enable-warnings_as_errors],
                             [Compiler warnings are errors.]))
AC_ARG_ENABLE([simd-accel],
              AC_HELP_STRING([--enable-simd-accel],
                             [use SIMD multimedia instructions such as MMX, AltiVec, etc.if possible]))
AC_ARG_WITH([extra-cflags],
            AC_HELP_STRING([--with-extra-cflags=flags],
                           [Options to be added to CFLAGS (optional)]))
arch_target="auto"
AC_ARG_WITH([arch-target], AS_HELP_STRING( [--with-arch-target=generic or auto],
					[Build a generic binary or auto-detect current cpu type (defaults to auto-detect)]),
					[ arch_target="$withval"])

user_specified_target=
AC_ARG_WITH([arch-target], AS_HELP_STRING( [--with-arch-target=CPU_TYPE],
                                         [Set a specific sub-architecture (defaults to native)]),
                [ user_specified_target="$withval" ])



AC_GNU_SOURCE

dnl Initialize libtool
AC_PROG_LIBTOOL

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_STDC
AC_PROG_YACC
AM_PROG_LEX
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_C_BIGENDIAN
AC_C_CONST
AC_C_INLINE
AC_SYS_LARGEFILE

dnl configure AS and ASFLAGS...
AM_PROG_AS

AC_CHECK_HEADERS([fenv.h stdint.h inttypes.h sys/types.h alloca.h])

AC_CHECK_FUNCS([posix_memalign memalign fmax lround pow bzero gettimeofday memset mmap strndup strstr strncasecmp])
AC_CHECK_FUNCS([sched_get_priority_max])
AC_CHECK_FUNCS([select socket getpagesize memcpy])

AC_CHECK_FUNC(getopt_long,
              [AC_DEFINE(HAVE_GETOPT_LONG, 1, [long getopt support])],
              [ # FreeBSD and BSD/OS have a gnugetopt library for this:
               AC_CHECK_LIB([gnugetopt], [getopt_long],
                            [AC_DEFINE(HAVE_GETOPT_LONG, 1,
                                       [long getopt support])
                             LIBGETOPT_LIB="-lgnugetopt"])
              ])

AC_CHECK_SIZEOF([int])
AC_CHECK_SIZEOF([long int])
AC_CHECK_SIZEOF([size_t])
AC_CHECK_SIZEOF([float])

have_aalib=false
AC_CHECK_HEADER(aalib.h, have_aalib=true, have_aalib=false)
if test x$have_aalib == xfalse; then
    AC_MSG_ERROR([aalib headers not found. Please install aalib-dev])
fi

AA_LIBS="-laa"
AC_SUBST(AA_LIBS)
AC_DEFINE(HAVE_AALIB,1,[Compile with aalib support])

AC_TYPE_SIGNAL

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

AC_SUBST(LIBM_LIBS)

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,,[Compile with dlopen support])
 have_dl_dlopen=true
fi

AC_MSG_CHECKING(whether to compile in debugging information)
debugCFLAGS=""
if test "x$enable_debug" = "xyes" ; then
 debugCFLAGS="-g"
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi
AC_MSG_CHECKING(whether to compile in profiling information)
if test "x$enable_profiling" = "xyes" ; then
 debugCFLAGS="$debugCFLAGS -fprofile-arcs -ftest-coverage"
 DEBUGLIBS="-lgcov"
 AC_SUBST(DEBUGLIBS)
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

   	dnl If the host is pentium, align on double word boundary
    	case $host_cpu in
		i686)
            if test "x$enable_debug" = "xno" ; then
			    CFLAGS="-ffast-math -O3 -ftree-vectorize"
            fi
		;;
	*)
		;;
	esac 
	
    ;;
  x86_64*|k8-*)
    	AC_DEFINE(HAVE_X86_CPU,1, [Compiling for x86-64 architecture CPU])
    	AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true
        if test "x$enable_debug" = "xno" ; then
	        CFLAGS="-fPIC -DPIC -m64 -ftree-vectorize -O3 -ffast-math"
        fi
    ;;
  aarch64* |arm)
     AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	 if test "x$enable_debug" != "xyes" ; then
	    VJE_CFLAGS="-O3 -ffast-math -ftree-vectorize -m64 -fPIC -DPIC"
	    SUBSAMPLE_CFLAGS="-fPIC -DPIC"
	 fi
	 CFLAGS="$CFLAGS -fPIC -DPIC -fopenmp"
	 have_arm=true
	 AC_MSG_RESULT([arm])
     ;;
  arm7* )
     AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	 AC_DEFINE(HAVE_ARMV7A,1,[Compiling for ARM7L CPU])
	 if test "x$enable_debug" != "xyes" ; then
	    VJE_CFLAGS="-O3 -ffast-math -ftree-vectorize -fPIC -DPIC"
	    SUBSAMPLE_CFLAGS="-fPIC -DPIC"
	 fi
	 CFLAGS="$CFLAGS -fPIC -DPIC -fopenmp"
	 have_arm=true
	 have_armv7a=true
	 AC_MSG_RESULT([arm7])
	 ;;
  powerpc | powerpc64)
	AC_DEFINE(HAVE_PPCCPU,1, [Compiling for PowerPC CPU])
	AC_DEFINE(ARCH_PPC,1,[Compiling for PowerPC])
	have_ppccpu=true

	case $host in
	      *-apple-darwin*)
		PROGRAM_NOPIC="-mdynamic-no-pic" # dynamic-no-pic is an Apple gcc option

	        ;;
     		 *)
   		PROGRAM_NOPIC=""
      		;;
    	esac
    ;;
   mips | mipsel)
	AC_DEFINE(ARCH_MIPS,,[Compiling for MIPS CPU])
	AC_DEFINE(HAVE_MIPS,,[Compiling for MIPS CPU])
	have_mips=true
	AC_MSG_CHECKING([if we are compiling on playstation2 hardware])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],
		[[#include <linux/ps2/dev.h>
		  #include <linux/ps2/gs.h>
                ]])],
		[have_ps2=true],
		[have_ps2=false])

	if test x$have_ps2 = xtrue ; then
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PS2,1,[Compile for playstation2])
	else
		AC_MSG_RESULT(no)
	fi

	;;
  *)
    AC_MSG_RESULT([unknown])
    ;;
esac

AC_SUBST(PROGRAM_NOPIC)

AC_MSG_RESULT($CFLAGS)

CFLAGS="$ARCHFLAGS $CFLAGS"
CXXFLAGS="$ARCHFLAGS $CXXFLAGS"


if test "x${GCC}" != "xyes" ; then
  enable_compile_warnings=no
fi

AC_MSG_CHECKING(what warning flags to pass to the C compiler)
warnCFLAGS=
warnCXXFLAGS=
if test "x$enable_compile_warnings" != "xno" ; then
 if test "x$GCC" = "xyes" ; then
    case "$CFLAGS" in
     *-Wall*) ;;
     *) warnCFLAGS="-Wall -Wunused " ;;
    esac
  if test "x$enable_compile_warnings" = "xyes" ; then
    warnCFLAGS="$warnCFLAGS -Wmissing-prototypes -Wundef -Wshadow -Wbad-function-cast -Wconversion -Wpadded -Wunreachable-code -Wmissing-declarations -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual"
    warnCXXFLAGS="$warnCXXFLAGS -Wmissing-prototypes  -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual"
  fi
  if test "x$enable_warnings_as_errors" = "xyes" ; then
    warnCFLAGS="$warnCFLAGS -Werror"
    warnCXXFLAGS="$warnCXXFLAGS -Werror"
  fi
 fi
fi

if test "x$cflags_set" != "xyes" ; then
  if test "x$enable_debug" = "xyes" ; then
  	CFLAGS="$debugCFLAGS $warnCFLAGS $CFLAGS"
  else
 	CFLAGS="$CFLAGS $warnCFLAGS -DSILENT"
  fi
  CXXFLAGS="$CXXFLAGS $warnCXXFLAGS"
  cflags_set=yes
fi


if test x"$with_extra_cflags" != "x"; then
   CFLAGS="$CFLAGS $with_extra_cflags"
   CXXFLAGS="$CXXFLAGS $with_extra_cflags"
fi

have_freetype2=false
PKG_CHECK_MODULES( FREETYPE2, [freetype2], [
      AC_SUBST(FREETYPE2_CFLAGS)
      AC_SUBST(FREETYPE2_LIBS)
      AC_DEFINE(HAVE_FREETYPE,,[compiling with freetype])
      have_freetype2=true],
      [have_freetype2=false] )


PKG_CHECK_MODULES(LIBAVUTIL, [libavutil >= 49.7.0],have_avutil=true,have_avutil=false)
PKG_CHECK_MODULES(LIBSWSCALE,[libswscale >= 0.7.1],have_swscale=true,have_swscale=false)
if test x$have_swscale = xfalse;
then
        AC_MSG_ERROR([libswscale not found.])
fi
if test x$have_avutil = xfalse;
then
        AC_MSG_ERROR([libavutil not found.])
fi

FFMPEG_CFLAGS="${LIBAVUTIL_CFLAGS} ${LIBSWSCALE_CFLAGS}"
FFMPEG_LIBS="${LIBAVUTIL_LIBS} ${LIBSWSCALE_LIBS}"
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

AC_CONFIG_FILES([
Makefile
plugins/Makefile
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Livido aalib filter ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Architecture: ${host_cpu} ])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([  - AALib                         : ${have_aalib}])
AC_MSG_NOTICE([  - FreeType2                     : ${have_freetype2}])
AC_MSG_NOTICE([  - FFmpeg/AV Swscaler            : ${have_swscale} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVUtil              : ${have_avutil} ])

