dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejay-utils],[1.1.8],[veejay-users@lists.sourceforge.net])
AC_PREREQ([2.69])
AC_CONFIG_SRCDIR([src/sayVIMS.c])

VEEJAY_MAJOR_VERSION=1
VEEJAY_MINOR_VERSION=1
VEEJAY_MICRO_VERSION=8
VEEJAY_VERSION=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION.$VEEJAY_MICRO_VERSION
VEEJAY_CODENAME="Veejay Utilities - build $VEEJAY_MINOR_VERSION $VEEJAY_MICRO_VERSION"
AC_CONFIG_HEADERS([config.h])
dnl AX_CONFIG_PREFIXED_HEADER([veejay-config.h], [VEEJAY], [config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.7])
AM_MAINTAINER_MODE

AC_DEFINE(VEEJAYUTIL,1,[Building Veejay Utilities])
AC_DEFINE(HAVE_CONFIG_H,1,[Include config.h])
# libtool versioning
LT_RELEASE=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION
LT_CURRENT=$VEEJAY_MICRO_VERSION
LT_REVISION=$VEEJAY_MICRO_VERSION
LT_AGE=$VEEJAY_MICRO_VERSION
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
dnl **********************************************************************
dnl Options

dnl AC_CONFIG_MACRO_DIR([m4])

dnl kill CFLAGS
CFLAGS=""
AC_ARG_ENABLE([strict],
		AS_HELP_STRING([--enable-strict],[Compile in paranoia assertion checking]))
AC_ARG_ENABLE([debug],
		AS_HELP_STRING([--enable-debug],[Compile in debugging information]))
AC_ARG_ENABLE([profile],
		AS_HELP_STRING([--enable-profiling],[Compile in profiling information]))
AC_ARG_ENABLE([compile-warnings],     
              AS_HELP_STRING([--enable-compile-warnings],[Turn on compiler warnings.]))
AC_ARG_ENABLE([warnings_as_errors],   
              AS_HELP_STRING([--enable-warnings_as_errors],[Compiler warnings are errors.]))
AC_ARG_ENABLE([simd-accel],
              AS_HELP_STRING([--enable-simd-accel],[use SIMD multimedia instructions such as MMX, AltiVec, etc.if possible]))
AC_ARG_WITH([extra-cflags],
            AS_HELP_STRING([--with-extra-cflags=flags],[Options to be added to CFLAGS (optional)]))


user_specified_target=
AC_ARG_WITH([arch-target], AS_HELP_STRING( [--with-arch-target=CPU_TYPE],
                                         [Set a specific sub-architecture (defaults to native)]),
                [ user_specified_target="$withval" ])



AC_USE_SYSTEM_EXTENSIONS

dnl Initialize libtool
LT_INIT

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_YACC
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

  
PKG_CHECK_MODULES(LIBAVUTIL, [libavutil >= 49.7.0],have_avutil=true,have_avutil=false)

if test x$have_avutil = xtrue;
then
	AC_DEFINE( BASE64_AVUTIL,1,[Use avutil base64])
fi

AC_SUBST(LIBAVUTIL_CFLAGS)
AC_SUBST(LIBAVUTIL_LIBS)

AC_SUBST(LIBGETOPT_LIB)


AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

dnl *********************************************************************
dnl Test whether we're linux or not. If so, define HAVE_V4L and build the
dnl video4linux/hardware dependent tools. Else, undefine HAVE_V4L
have_v4l=false
have_linux=false
have_darwin=false
use_gveejayreloaded=false
dnl use_gveejay=false
case $host in
  *-*-linux*)
	AC_CHECK_HEADER(linux/version.h,
		[have_linux=true
		if test x$with_gveejayreloaded != xno ; then
			 use_gveejayreloaded=true
		fi
dnl		if test x$with_gveejay != xno ; then
dnl			 use_gveejay=true
dnl		fi
	         AC_DEFINE(HAVE_LINUX,,[Linux platform])
		],
		[AC_MSG_ERROR([version.h not found - please install the linux kernel headers])
		])
		LIBM_LIBS="-lm"
	;;
   *-apple-darwin*)
		AC_DEFINE(HAVE_DARWIN,1,[MAC OS X Darin])
		dnl OS-X/Darwin needs no-cpp-precomp
		CFLAGS="$CFLAGS -no-cpp-precomp"
		have_darwin=true
		LIBM_LIBS=""
	;;	
  *)
	AC_MSG_WARN([Alien platform - Good Luck!])
	LIBM_LIBS=""
	;;
esac


dnl ====== check for PKG_CONFIG_PATH
if test x"$PKG_CONFIG_PATH" = x ; then
	AC_MSG_NOTICE([The PKG_CONFIG_PATH variable was not set])
	AC_MSG_NOTICE([You should set it to the directories that contain the .pc files])
	PKG_CONFIG_PATH=/usr/lib/pkgconfig
fi

AC_SUBST(LIBM_LIBS)

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,,[Compile with dlopen support])
 have_dl_dlopen=true
fi

PKG_CHECK_MODULES( VEEJAYCORE, [veejaycore >= 1.5.67],
		[	
		AC_SUBST(VEEJAYCORE_CFLAGS)
		AC_SUBST(VEEJAYCORE_LIBS)
		have_veejaycore=true
		AC_DEFINE( HAVE_VEEJAYCORE,1, [Veejay Core Installed])],
			[have_veejaycore=false])

if test x$have_veejaycore != xtrue; then
	AC_MSG_ERROR( [Cannot find Veejay Core >= 1.5.67])
fi


PKG_CHECK_MODULES( VEEJAY, [veejay >= 1.5.67],
		[	
		AC_SUBST(VEEJAY_CFLAGS)
		AC_SUBST(VEEJAY_LIBS)
		have_veejay=true
		AC_DEFINE( HAVE_VEEJAY,1, [Veejay Core Installed])],
			[have_veejay=false])

if test x$have_veejay != xtrue; then
	AC_MSG_ERROR( [Cannot find Veejay Lib >= 1.5.67])
fi

dnl *********************************************************************
dnl Check for mjpegtools
dnl   (creates MJPEGYTOOLS_CFLAGS, 
dnl *********************************************************************

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
AC_MSG_CHECKING(whether to compile in assertion checking)
if test "x$enable_strict" = "xyes" ; then
 debugCFLAGS="$debugCFLAGS -DSTRICT_CHECKING"
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi

have_x86cpu=false
have_x86_64cpu=false
have_mips=false
have_ppccpu=false
have_ps2=false
have_arm=false

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"
	CFLAGS="$CFLAGS -O3"

	AC_MSG_RESULT([x86])
    ;;
  x86_64*|k8-*)
    	AC_DEFINE(HAVE_X86_CPU,1, [Compiling for x86-64 architecture CPU])
    	AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true
		CFLAGS="$CFLAGS -m64 -fPIC -DPIC -O3"

	AC_MSG_RESULT([x86_64])
    ;;
  aarch64* |arm)
     AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	 if test "x$enable_debug" != "xyes" ; then
	    VJE_CFLAGS="-O3 -ffast-math -ftree-vectorize -fPIC -DPIC"
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
dnl     CFLAGS="$CFLAGS -no-cpp-precomp"

	        ;;
     		 *)
   		PROGRAM_NOPIC=""
      		;;
    	esac

	AC_MSG_RESULT([powerpc])
    ;;
   mips | mipsel)
	AC_DEFINE(ARCH_MIPS,1,[Compiling for MIPS CPU])
	AC_DEFINE(HAVE_MIPS,1,[Compiling for MIPS CPU])
	have_mips=true
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],
		[[#include <linux/ps2/dev.h>
		  #include <linux/ps2/gs.h>
                ]])],
		[have_ps2=true],
		[have_ps2=false])

	AC_MSG_RESULT([mips])

	AC_MSG_CHECKING([if we are compiling on playstation2 hardware])
	if test x$have_ps2 = xtrue ; then
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PS2,1,[Compile for playstation2])
	else
		AC_MSG_RESULT(no)
	fi

	;;
   arm|armv7l)
	AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	have_arm=true
	AC_MSG_RESULT(arm)
	CFLAGS="$CFLAGS -O3"
	;;

  *)

	CFLAGS="$CFLAGS"

    ;;
esac

AC_SUBST(PROGRAM_NOPIC)

if test x$host_alias != x; then
	dnl Cross compiling
	AC_MSG_CHECKING(sub-architecture settings)
	if test x$have_x86cpu = xtrue; then
		host_mod_cpu=`echo $host_cpu|tr _ -`
		ARCHFLAGS="-march=$host_mod_cpu -mcpu=$host_mod_cpu"
		AC_MSG_RESULT($ARCHFLAGS)
	fi
else
	AC_MSG_CHECKING(sub-architecture settings)
	
	chmod +x $srcdir/cpuinfo.sh

	if test "$arch_target" = "auto"; then
		TMP=`$srcdir/cpuinfo.sh`
		ARCHFLAGS=`cat veejay.arch`
	else
       		ARCHFLAGS="-mtune=generic"
	fi
		AC_MSG_RESULT($ARCHFLAGS)
fi

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED,,
                    [Defined if building against uninstalled FFmpeg source])


dnl **********************************************************************
dnl All the conditional stuff for the Makefiles
AM_CONDITIONAL(ARCH_PPC, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(ARCH_X86, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(ARCH_X86_64, test x$have_x86_64cpu = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)

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
	warnCFLAGS="$warnCFLAGS -Wundef -Wshadow -Wbad-function-cast -Wunreachable-code -Wmissing-declarations -Wpointer-arith -Wcast-align -Wwrite-strings -Wall"
    warnCXXFLAGS="$warnCXXFLAGS -Wpointer-arith -Wcast-align -Wwrite-strings "
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
 	CFLAGS="$CFLAGS $warnCFLAGS"
  fi
  CXXFLAGS="$CXXFLAGS $warnCXXFLAGS"
  cflags_set=yes
fi


if test x"$with_extra_cflags" != "x"; then
   CFLAGS="$CFLAGS $with_extra_cflags"
   CXXFLAGS="$CXXFLAGS $with_extra_cflags"
fi


AC_CONFIG_FILES([
Makefile
man/Makefile
src/Makefile
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay-Utilities ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Architecture: ${host_cpu} ])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Required dependencies:])
AC_MSG_NOTICE([  - Veejay Core                   : ${have_veejaycore}])
AC_MSG_NOTICE([  - Veejay Lib                    : ${have_veejay}])

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Optional dependencies:])
AC_MSG_NOTICE([  - AVUtil                        : ${have_avutil}])
