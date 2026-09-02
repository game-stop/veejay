dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([lvdcrop-plugin],[0.0.1],[veejay-users@lists.sourceforge.net])
AC_PREREQ([2.71])
AC_CONFIG_SRCDIR([plugins/lvd_crop.c])

LVDGMICMAJOR_VERSION=0
LVDGMICMINOR_VERSION=0
LVDGMICMICRO_VERSION=1
LVDGMICVERSION=$LVDGMICMAJOR_VERSION.$LVDGMICMINOR_VERSION.$LVDGMICMICRO_VERSION
LVDGMICCODENAME="Livido crop filter - build $LVDGMICMINOR_VERSION $LVDGMICMICRO_VERSION"
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE

dnl Build modes below own optimization and debug flags; preserve only caller input here.
CFLAGS="${CFLAGS-}"
CXXFLAGS="${CXXFLAGS-}"

LT_INIT

AC_DEFINE(VEEJAYUTIL,1,[Building Livido crop filter])
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
arch_target="generic"
AC_ARG_WITH([arch-target],
			AS_HELP_STRING([--with-arch-target=generic, native, auto or CPU],
						   [Use portable defaults, native tuning, or a compiler-validated CPU target]),
			[arch_target="$withval"])


AC_USE_SYSTEM_EXTENSIONS

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
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


have_gavl=false
PKG_CHECK_MODULES(GAVL, [gavl >= 0.2.3], have_gavl=true,have_gavl=false)
AM_CONDITIONAL(HAVE_GAVL, test x$have_gavl = xtrue)

AC_SUBST(LIBAVUTIL_CFLAGS)
AC_SUBST(LIBAVUTIL_LIBS)

AC_SUBST(HAVE_GAVL)
AC_SUBST(GAVL_CFLAGS)
AC_SUBST(GAVL_LIBS)

AC_SUBST(LIBGETOPT_LIB)

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

dnl *********************************************************************
dnl Test whether we're linux or not. If so, define HAVE_V4L and build the
dnl video4linux/hardware dependent tools. Else, undefine HAVE_V4L
have_v4l=false
have_linux=false
have_darwin=false
case $host in
  *-*-linux*)
	AC_CHECK_HEADER(linux/version.h,
		[have_linux=true
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


have_asm_nasm=false
have_asm_mmx=false
have_asm_sse=false
have_asm_sse2=false
have_asm_mmx2=false
have_asm_3dnow=false
have_cmov=false
have_x86cpu=false
have_x86_64cpu=false
have_altivec=false
have_mips=false
have_ppccpu=false
have_ps2=false
have_arm=false
have_armv7a=false

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

	AC_MSG_RESULT([x86])
    ;;
  x86_64*|k8-*)
    	AC_DEFINE(HAVE_X86_CPU,1, [Compiling for x86-64 architecture CPU])
    	AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true

	AC_MSG_RESULT([x86_64])
    ;;
  aarch64*|arm64*)
      AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
      have_arm=true
      AC_MSG_RESULT([aarch64])
      ;;
  armv7*|armv8l)
      AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
      AC_DEFINE(HAVE_ARMV7A,1,[Compiling for ARMv7-A CPU])
      have_arm=true
      have_armv7a=true
      AC_MSG_RESULT([armv7])
      ;;
  arm*)
      AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
      have_arm=true
      AC_MSG_RESULT([arm])
      ;;
  powerpc | powerpc64 | powerpc64le)
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
   *)

dnl If you get here, you can change AC_MSG_ERROR to AC_MSG_RESULT
dnl and uncomment setting the CFLAGS below to have it compile anyway on your system
	AC_MSG_ERROR([unknown])

    ;;
esac

AC_SUBST(PROGRAM_NOPIC)

CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES"

AC_MSG_CHECKING([sub-architecture settings])
ARCHFLAGS=""
case $arch_target in
	generic)
		AC_MSG_RESULT([portable compiler defaults])
		;;
	auto|native)
		if test x$have_ppccpu = xtrue || test x$have_arm = xtrue; then requested_arch_flag="-mcpu=native"; else requested_arch_flag="-march=native"; fi
		saved_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $requested_arch_flag"
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [ARCHFLAGS="$requested_arch_flag"],
			[AC_MSG_ERROR([the compiler does not support $requested_arch_flag])])
		CFLAGS="$saved_CFLAGS"
		AC_MSG_RESULT([$ARCHFLAGS])
		;;
	*)
		if test x$have_ppccpu = xtrue || test x$have_arm = xtrue; then requested_arch_flag="-mcpu=$arch_target"; else requested_arch_flag="-march=$arch_target"; fi
		saved_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $requested_arch_flag"
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [ARCHFLAGS="$requested_arch_flag"],
			[AC_MSG_ERROR([the compiler does not support $requested_arch_flag])])
		CFLAGS="$saved_CFLAGS"
		AC_MSG_RESULT([$ARCHFLAGS])
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

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED,,
                    [Defined if building against uninstalled FFmpeg source])

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



AC_MSG_RESULT($CFLAGS)

OPT_CFLAGS="-O3 -ftree-vectorize -ffast-math"
cflags_without_optimization=""
for flag in $CFLAGS; do
	case $flag in
		-O*) ;;
		*) cflags_without_optimization="$cflags_without_optimization $flag" ;;
	esac
done
cxxflags_without_optimization=""
for flag in $CXXFLAGS; do
	case $flag in
		-O*) ;;
		*) cxxflags_without_optimization="$cxxflags_without_optimization $flag" ;;
	esac
done
CFLAGS="$cflags_without_optimization $ARCHFLAGS $OPT_CFLAGS"
CXXFLAGS="$cxxflags_without_optimization $ARCHFLAGS $OPT_CFLAGS"


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
		CXXFLAGS="$debugCFLAGS $CXXFLAGS $warnCXXFLAGS"
  else
 	CFLAGS="$CFLAGS $warnCFLAGS -DSILENT"
		CXXFLAGS="$CXXFLAGS $warnCXXFLAGS"
  fi
  cflags_set=yes
fi


if test x"$with_extra_cflags" != "x"; then
   CFLAGS="$CFLAGS $with_extra_cflags"
   CXXFLAGS="$CXXFLAGS $with_extra_cflags"
fi
dnl Check for Veejay
have_veejaycore=false
PKG_CHECK_MODULES( VEEJAYCORE, [veejaycore >= 1.5.65 ],
	[	
		AC_SUBST(VEEJAYCORE_CFLAGS)
		AC_SUBST(VEEJAYCORE_LIBS)
		have_veejaycore=true
		AC_DEFINE(HAVE_VEEJAYCORE,1, [Veejay Core 1.5.65 installed])]
		,
		[have_veejaycore=false])

if test x$have_veejaycore != xtrue ; then
   AC_MSG_ERROR([Cannot find Veejay Core 1.5.65 or later])
fi
AC_CONFIG_FILES([
Makefile
plugins/Makefile
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Livido crop filter ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Architecture: ${host_cpu} ])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([])
if test "$have_arm" = "true" ; then
AC_MSG_NOTICE([   arm])
AC_MSG_NOTICE([    NEON    enabled     : ${ac_cv_flag_neon}])
fi

if test "$have_mips" = "true" ; then
AC_MSG_NOTICE([   mips])
AC_MSG_NOTICE([    PS/2 enabled        : ${have_ps2}])
fi
AC_MSG_NOTICE([])

AC_MSG_NOTICE([  - FFmpeg/AV Swscaler            : ${have_swscale} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVUtil              : ${have_avutil} ])
AC_MSG_NOTICE([  - Veejay Core                   : ${have_veejaycore} ])
if test "$have_linux" = "true" ; then
AC_MSG_NOTICE([   Platform: Linux])
fi
AC_MSG_NOTICE([  - GAVL                          : ${have_gavl} ])
if test "x$have_gavl" = "false" ; then
AC_MSG_NOTICE([    Get gavl from http://gmerlin.sourceforge.net])
fi
