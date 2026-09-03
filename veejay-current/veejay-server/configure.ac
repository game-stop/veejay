dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejay],[1.6.0],[http://www.veejayhq.net])
AC_PREREQ([2.69])

USER_CFLAGS="$CFLAGS"
dnl Build modes below own optimization and debug flags; preserve only caller input here.
CFLAGS="${CFLAGS-}"
CXXFLAGS="${CXXFLAGS-}"

AC_USE_SYSTEM_EXTENSIONS

AC_CONFIG_SRCDIR([veejay/veejay.c])

VEEJAY_MAJOR_VERSION=1
VEEJAY_MINOR_VERSION=6
VEEJAY_MICRO_VERSION=0
VEEJAY_VERSION=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION.$VEEJAY_MICRO_VERSION
VEEJAY_CODENAME="Veejay Classic - build $VEEJAY_MINOR_VERSION $VEEJAY_MICRO_VERSION"
AC_CONFIG_HEADERS([config.h])
dnl AX_CONFIG_PREFIXED_HEADER([veejay-config.h], [VEEJAY], [config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE

AC_DEFINE(VEEJAY,1,[Building Veejay])
AC_DEFINE(HAVE_CONFIG_H,1,[Include config.h])
# libtool versioning
LT_RELEASE=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION
LT_CURRENT=$VEEJAY_MAJOR_VERSION
LT_REVISION=$VEEJAY_MINOR_VERSION
LT_AGE=$VEEJAY_MAJOR_VERSION
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
dnl **********************************************************************
dnl Options

dnl AC_CONFIG_MACRO_DIR([m4])

dnl enable auto vectorization for libvjmem, libvje and libyuv

AC_ARG_ENABLE([strict],
		AS_HELP_STRING([--enable-strict],[Compile in paranoia assertion checking]))

AC_ARG_ENABLE([sanitizer],
        AS_HELP_STRING([--enable-sanitizer],[Enable AddressSanitizer]))

AC_ARG_ENABLE([debug],
		AS_HELP_STRING([--enable-debug],[Compile in debugging information]))
AC_ARG_ENABLE([profile],
		AS_HELP_STRING([--enable-profiling],[Compile in profiling information]))
AC_ARG_ENABLE([compile-warnings],     
              AS_HELP_STRING([--enable-compile-warnings],[Turn on compiler warnings.]))
AC_ARG_ENABLE([warnings_as_errors],   
              AS_HELP_STRING([--enable-warnings-as-errors],[Compiler warnings are errors.]))
AC_ARG_WITH([extra-cflags],
            AS_HELP_STRING([--with-extra-cflags=flags],[Options to be added to CFLAGS (optional)]))
AC_ARG_WITH([libdv],
            AS_HELP_STRING([--without-libdv],[Do not use libdv.]),
            [], [with_libdv=yes])

AC_ARG_WITH([pixbuf],
            AS_HELP_STRING([--without-pixbuf],[Do not use GDK pixbuf]),
            [], [with_pixbuf=yes])

AC_ARG_WITH([playthread],
            AS_HELP_STRING([--without-playthread],[Do not use playback thread]),
            [], [with_playthread=yes])

AC_ARG_WITH([sdl],
	     AS_HELP_STRING([--without-sdl],[Do not use SDL.]),
	        [], [with_sdl=yes])
AC_ARG_WITH([jack],
		AS_HELP_STRING([--without-jack],[Do not use jack.]),
		[], [with_jack=yes])

AC_ARG_ENABLE([v4l1],
		AS_HELP_STRING([--enable-v4l1],[Compile with video4linux instead of video4linux 2]))

AC_ARG_WITH([xml2],
    AS_HELP_STRING([--with-xml2],[Use the mandatory libxml2 project-file support]),
		[], [with_xml2=yes])

AC_ARG_WITH([jpeg],
		AS_HELP_STRING([--without-jpeg],[Do not JPEG support]),
		[],	[with_libjpeg=yes])

AC_ARG_WITH([jpeg-mmx],
            AS_HELP_STRING([--with-jpegmmx=PFX],[Prefix where jpeg-mmx is installed (optional).]))

AC_ARG_WITH([v4l2], AS_HELP_STRING([--without-v4l2],[Do not use Video4Linux]),
			[],	[with_v4l2=yes])

AC_ARG_WITH([nvjpeg],
	[AS_HELP_STRING([--with-nvjpeg@<:@=DIR@:>@],
		[enable optional nvJPEG MJPEG decode; look for CUDA headers, nvcc and libraries below DIR])],
	[with_nvjpeg="$withval"], [with_nvjpeg=auto])
AC_ARG_WITH([mediamtx],
  [AS_HELP_STRING([--with-mediamtx@<:@=PATH@:>@],
    [enable optional MediaMTX gateway integration; use the executable at PATH])],
  [with_mediamtx="$withval"], [with_mediamtx=auto])
AC_ARG_VAR([CUDA_HOME], [CUDA toolkit prefix used for nvJPEG detection])
AC_ARG_VAR([CUDA_PATH], [CUDA toolkit prefix used for nvJPEG detection])
AC_ARG_VAR([NVCC], [path to the NVIDIA CUDA compiler])
AC_ARG_VAR([MEDIAMTX], [path to the MediaMTX executable])

arch_target="generic"
AC_ARG_WITH(arch-target, AS_HELP_STRING( [--with-arch-target=generic, native, auto or user defined],
          [Build a generic binary, target the build host with native/auto, or use a specific -march setting)]),
					[ arch_target="$withval"])

AC_USE_SYSTEM_EXTENSIONS

dnl Initialize libtool
LT_INIT

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_OPENMP
have_openmp=false
OPENMP_PRAGMA_CFLAGS=""
if test "x$enable_openmp" != "xno" &&
   test -n "$ac_cv_prog_c_openmp" &&
   test "x$ac_cv_prog_c_openmp" != "xunsupported"; then
  have_openmp=true
else
  AC_CACHE_CHECK([for a flag to ignore disabled OpenMP pragmas],
           [vj_cv_c_openmp_pragma_flag], [
    vj_save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS -Werror -Wunknown-pragmas -Wno-unknown-pragmas"
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
int main(void)
{
#pragma omp parallel
  { }
  return 0;
}
]])], [vj_cv_c_openmp_pragma_flag="-Wno-unknown-pragmas"],
   [vj_cv_c_openmp_pragma_flag="none"])
    CFLAGS="$vj_save_CFLAGS"
  ])
  if test "x$vj_cv_c_openmp_pragma_flag" != "xnone"; then
    OPENMP_PRAGMA_CFLAGS="$vj_cv_c_openmp_pragma_flag"
  fi
  AC_MSG_NOTICE([OpenMP unavailable or disabled; building serial code paths])
fi
dnl AM_PROG_CC_STDC
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

AC_CHECK_HEADERS([stdatomic.h fenv.h stdint.h inttypes.h sys/types.h alloca.h sys/time.h sys/times.h])

AC_MSG_CHECKING([for C11 standard support (mandatory for atomics)])
_VEEJAY_CFLAGS_SAVE="$CFLAGS"
AC_CACHE_VAL([ac_cv_c_c11], [
  # Try C11 first, testing with stdatomic.h to ensure header is present
  CFLAGS="$CFLAGS -std=c11"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <stdatomic.h>]])], [
    ac_cv_c_c11="yes_c11"
  ], [
    # C11 failed, try GNU11 (common fallback for GCC/Clang)
    CFLAGS="$_VEEJAY_CFLAGS_SAVE -std=gnu11"
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <stdatomic.h>]])], [
      ac_cv_c_c11="yes_gnu11"
    ], [
      # Both failed, restore original CFLAGS and flag failure
      CFLAGS="$_VEEJAY_CFLAGS_SAVE"
      ac_cv_c_c11="no"
    ])
  ])
])

AC_MSG_RESULT([$ac_cv_c_c11])
CFLAGS="$_VEEJAY_CFLAGS_SAVE"

if test "x$ac_cv_c_c11" = xno; then
  AC_MSG_ERROR([C11 compiler support is mandatory for this project. Please use GCC >= 4.9 or Clang >= 3.8.])
elif test "x$ac_cv_c_c11" = xyes_c11; then
  CFLAGS="$CFLAGS -std=c11"
  AC_DEFINE([HAVE_C11_ATOMICS], [1], [Define to 1 if C11 atomics are available.])
  AC_MSG_NOTICE([Using -std=c11])
elif test "x$ac_cv_c_c11" = xyes_gnu11; then
  CFLAGS="$CFLAGS -std=gnu11"
  AC_DEFINE([HAVE_C11_ATOMICS], [1], [Define to 1 if C11 atomics are available.])
  AC_MSG_NOTICE([Using -std=gnu11])
fi

AC_MSG_CHECKING([for usable GNU built-in atomics])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdatomic.h> // Keep for compatibility, or remove if conflict persists
]], [[
  long test_val = 0;
  __atomic_store_n(&test_val, 1, __ATOMIC_RELEASE);
]])], [
  AC_MSG_RESULT([yes])
  AC_DEFINE([HAVE_GNU_ATOMICS], [1], [Define to 1 if GNU built-in atomics are available.])
], [
  AC_MSG_ERROR([GNU built-in atomics are mandatory.])
])

AC_MSG_CHECKING([whether 64-bit GNU atomics link without libatomic])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
static volatile unsigned long long vj_atomic_test_value;
]], [[
  return (int) __atomic_fetch_add(&vj_atomic_test_value, 1, __ATOMIC_SEQ_CST);
]])], [
  AC_MSG_RESULT([yes])
  ATOMIC_LIBS=""
], [
  AC_MSG_RESULT([no])
  vj_save_LIBS="$LIBS"
  LIBS="$LIBS -latomic"
  AC_MSG_CHECKING([whether 64-bit GNU atomics link with libatomic])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
static volatile unsigned long long vj_atomic_test_value;
]], [[
  return (int) __atomic_fetch_add(&vj_atomic_test_value, 1, __ATOMIC_SEQ_CST);
]])], [
    AC_MSG_RESULT([yes])
    ATOMIC_LIBS="-latomic"
  ], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([64-bit GNU atomics are mandatory, but neither the compiler nor libatomic provides them.])
  ])
  LIBS="$vj_save_LIBS"
])
AC_SUBST([ATOMIC_LIBS])

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

AC_SEARCH_LIBS([clock_gettime], [rt],
	       [AC_DEFINE(HAVE_POSIX_TIMERS,[1],[Set to 1 if clock_gettime and POSIX timers are available])])


AC_SUBST(LIBGETOPT_LIB)
AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])


have_v4l2=false
have_linux=false
have_darwin=false
case $host in
  *-*-linux*)
	AC_CHECK_HEADER(linux/version.h,
		[have_linux=true
	         AC_DEFINE(HAVE_LINUX,1,[Linux platform])
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

if test "x${GCC}" != "xyes" ; then
  enable_compile_warnings=no
fi

VEEJAY_CFLAGS=""
VEEJAY_CXXFLAGS=""
warnCFLAGS=""
warnCXXFLAGS=""


enable_debug=${enable_debug:-no}
enable_compile_warnings=${enable_compile_warnings:-no}
enable_warnings_as_errors=${enable_warnings_as_errors:-no}

AC_MSG_CHECKING([whether we are using GCC])
GCC=no
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
                  [GCC=yes], [GCC=no])
AC_MSG_RESULT([$GCC])


AC_MSG_CHECKING([what warning flags to pass to the C compiler])
if test "x$enable_compile_warnings" = "xyes"; then
    if test "x$GCC" = "xyes"; then
        case "$CFLAGS" in
            *-Wall*) ;;
            *) warnCFLAGS="-Wall -Wunused" ;;
        esac

        warnCFLAGS="$warnCFLAGS -Wundef -Wshadow -Wbad-function-cast -Wunreachable-code -Wmissing-declarations -Wpointer-arith -Wcast-align -Wwrite-strings"
        warnCXXFLAGS="$warnCXXFLAGS -Wpointer-arith -Wcast-align -Wwrite-strings"

        if test "x$enable_warnings_as_errors" = "xyes"; then
            warnCFLAGS="$warnCFLAGS -Werror"
            warnCXXFLAGS="$warnCXXFLAGS -Werror"
        fi
    fi
fi
AC_MSG_RESULT([done])

AC_MSG_CHECKING([whether to enable debug build])
if test "x$enable_debug" = "xyes"; then
    AC_MSG_RESULT([yes])
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -g"
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align"
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -Wstrict-prototypes -Wmissing-prototypes"
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -Wformat=2 -Wundef -Wwrite-strings"

    AC_DEFINE([VALIDATE_VIMS], [1], [Enable VIMS buffer validation in debug builds])
else
    AC_MSG_RESULT([no])
fi


if test "x$enable_debug" != "xyes" ; then
   VEEJAY_CFLAGS="$VEEJAY_CFLAGS -fno-stack-protector"
fi

if test "x$enable_sanitizer" = "xyes"; then
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -fsanitize=address -fno-omit-frame-pointer"
fi

if test "x$enable_threadsanitizer" = "xyes"; then
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS -fsanitize=thread"
fi


# Optional extra user-provided CFLAGS
if test "x$with_extra_cflags" != "x"; then
    VEEJAY_CFLAGS="$VEEJAY_CFLAGS $with_extra_cflags"
fi

have_asm_mmx=false
have_asm_sse=false
have_asm_sse2=false
have_asm_sse4_1=false
have_asm_sse4_2=false
have_asm_mmx2=false
have_asm_3dnow=false
have_asm_avx=false
have_asm_avx2=false
have_cmov=false
have_x86cpu=false
have_x86_64cpu=false
have_altivec=false
have_mips=false
have_ppccpu=false
have_ps2=false
have_arm=false
have_armv7a=false
have_aarch64=false
have_esp32=false


AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
        AC_DEFINE(HAVE_X86CPU,1,[Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

	AC_MSG_RESULT([x86])
    ;;

  x86_64*|k8-*)
        AC_DEFINE(HAVE_X86_CPU,1,[Compiling for x86-64 architecture CPU])
        AC_DEFINE(ARCH_X86_64,1,[Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true

	AC_MSG_RESULT([x86_64])
    ;;

    powerpc|powerpc64|powerpc64le)
        AC_DEFINE(HAVE_PPCCPU,1,[Compiling for PowerPC CPU])
	AC_DEFINE(ARCH_PPC,1,[Compiling for PowerPC])
	have_ppccpu=true

	case $host in
	      *-apple-darwin*)
                PROGRAM_NOPIC="-mdynamic-no-pic"
	        ;;
     		 *)
   		PROGRAM_NOPIC=""
      		;;
    	esac

	AC_MSG_RESULT([powerpc])
    ;;

    mips|mipsel)
	AC_DEFINE(ARCH_MIPS,1,[Compiling for MIPS CPU])
	AC_DEFINE(HAVE_MIPS,1,[Compiling for MIPS CPU])
	have_mips=true

        AC_COMPILE_IFELSE(
            [AC_LANG_PROGRAM([[]], [[#include <linux/ps2/dev.h>
                                      #include <linux/ps2/gs.h>]])],
		[have_ps2=true],
            [have_ps2=false]
        )

	AC_MSG_RESULT([mips])

        AC_MSG_CHECKING([if compiling for playstation2])
        if test x$have_ps2 = xtrue; then
            AC_DEFINE(HAVE_PS2,1,[Compile for playstation2])
		AC_MSG_RESULT(yes)
	else
		AC_MSG_RESULT(no)
	fi
	;;

    aarch64*|arm64*)
      AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
      have_arm=true
      have_aarch64=true

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

      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
  #if !defined(__ARM_ARCH) || __ARM_ARCH < 7
  #error Compiler target is older than ARMv7
  #endif
      ]], [[]])], [
        AC_DEFINE(HAVE_ARMV7A,1,[Compiling for ARMv7-A CPU])
        have_armv7a=true
      ])

      AC_MSG_RESULT([arm])
    ;;

    xtensa*|riscv32*)
      case $host_vendor in
        esp|esp32*)
          AC_DEFINE(HAVE_ESP32,1,[Compiling for an Espressif ESP32 target])
          have_esp32=true
          AC_MSG_RESULT([esp32 ($host_cpu)])
        ;;
        *)
          AC_MSG_ERROR([$host is not a recognized Espressif ESP32 target])
        ;;
      esac
    ;;

    *)
        AC_MSG_ERROR([unknown architecture])
    ;;
esac

CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES"

dnl ----------------------------------------------------------
dnl Sub-architecture / tuning
dnl ----------------------------------------------------------
AC_MSG_CHECKING([sub-architecture settings])
ARCHFLAGS=""
case $arch_target in
  generic)
    AC_MSG_RESULT([portable compiler defaults])
  ;;
  auto|native)
    saved_CFLAGS="$CFLAGS"
    if test x$have_ppccpu = xtrue || test x$have_arm = xtrue; then
      requested_arch_flag="-mcpu=native"
    else
      requested_arch_flag="-march=native"
    fi
    CFLAGS="$CFLAGS $requested_arch_flag $VEEJAY_CFLAGS"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
      [ARCHFLAGS="$requested_arch_flag"],
      [AC_MSG_ERROR([the compiler does not support $requested_arch_flag])])
    CFLAGS="$saved_CFLAGS"
    AC_MSG_RESULT([$ARCHFLAGS])
  ;;
  *)
    if test x$have_ppccpu = xtrue || test x$have_arm = xtrue; then
      requested_arch_flag="-mcpu=$arch_target"
    else
      requested_arch_flag="-march=$arch_target"
    fi
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $requested_arch_flag $VEEJAY_CFLAGS"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
      [ARCHFLAGS="$requested_arch_flag"],
      [AC_MSG_ERROR([the compiler does not support $requested_arch_flag])])
    CFLAGS="$saved_CFLAGS"
    AC_MSG_RESULT([$ARCHFLAGS])
  ;;
esac

if test x$have_x86cpu = xtrue && test "x$arch_target" != "xgeneric"; then
  saved_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $ARCHFLAGS"

  AC_CACHE_CHECK([whether the selected target supports MMX], [ac_cv_flag_mmx], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __MMX__
#error MMX is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_mmx=yes], [ac_cv_flag_mmx=no])
  ])
  if test x$ac_cv_flag_mmx = xyes; then
    AC_DEFINE(HAVE_ASM_MMX,1,[Compiling in MMX support])
    AC_DEFINE(HAVE_MMX,1,[Compiling in MMX support])
    have_asm_mmx=true
  fi

  AC_CACHE_CHECK([whether the selected target supports SSE], [ac_cv_flag_sse], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __SSE__
#error SSE is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_sse=yes], [ac_cv_flag_sse=no])
  ])
  if test x$ac_cv_flag_sse = xyes; then
    AC_DEFINE(HAVE_ASM_SSE,1,[Compiling in SSE support])
    AC_DEFINE(HAVE_SSE,1,[Compiling in SSE support])
    have_asm_sse=true
  fi

  AC_CACHE_CHECK([whether the selected target supports SSE2], [ac_cv_flag_sse2], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __SSE2__
#error SSE2 is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_sse2=yes], [ac_cv_flag_sse2=no])
  ])
  if test x$ac_cv_flag_sse2 = xyes; then
    AC_DEFINE(HAVE_ASM_SSE2,1,[Compiling in SSE2 support])
    AC_DEFINE(HAVE_SSE2,1,[Compiling in SSE2 support])
    have_asm_sse2=true
  fi

  AC_CACHE_CHECK([whether the selected target supports SSE4.1], [ac_cv_flag_sse4_1], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __SSE4_1__
#error SSE4.1 is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_sse4_1=yes], [ac_cv_flag_sse4_1=no])
  ])
  if test x$ac_cv_flag_sse4_1 = xyes; then
    AC_DEFINE(HAVE_ASM_SSE4_1,1,[Compiling in SSE4.1 support])
    AC_DEFINE(HAVE_SSE4_1,1,[Compiling in SSE4.1 support])
    have_asm_sse4_1=true
  fi

  AC_CACHE_CHECK([whether the selected target supports SSE4.2], [ac_cv_flag_sse4_2], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __SSE4_2__
#error SSE4.2 is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_sse4_2=yes], [ac_cv_flag_sse4_2=no])
  ])
  if test x$ac_cv_flag_sse4_2 = xyes; then
    AC_DEFINE(HAVE_ASM_SSE4_2,1,[Compiling in SSE4.2 support])
    AC_DEFINE(HAVE_SSE4_2,1,[Compiling in SSE4.2 support])
    have_asm_sse4_2=true
  fi

  AC_CACHE_CHECK([whether the selected target supports 3DNow!], [ac_cv_flag_3dnow], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#ifndef __3dNOW__
#error 3DNow! is not enabled for the selected target
#endif
    ]], [[]])], [ac_cv_flag_3dnow=yes], [ac_cv_flag_3dnow=no])
  ])
  if test x$ac_cv_flag_3dnow = xyes; then
    AC_DEFINE(HAVE_ASM_3DNOW,1,[Compiling in 3DNow! support])
    have_asm_3dnow=true
  fi

  AC_CACHE_CHECK([whether the assembler supports CMOV], [ac_cv_flag_cmov], [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
      int source = 1;
      int result = 0;
      __asm__ __volatile__("test %1, %1; cmovnz %1, %0"
                 : "+r" (result) : "r" (source) : "cc");
    ]])], [ac_cv_flag_cmov=yes], [ac_cv_flag_cmov=no])
  ])
  if test x$ac_cv_flag_cmov = xyes; then
    AC_DEFINE(HAVE_CMOV,1,[Compiling in CMOV support])
    have_cmov=true
  fi

  AC_CACHE_CHECK([whether the selected target supports MMX2/MMXEXT], [ac_cv_flag_mmx2], [
    if test x$ac_cv_flag_sse = xyes; then
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
                char buffer[64];
                __asm__ __volatile__(
                    "prefetchnta (%0)\n\t"
                    "movntq %%mm0, (%0)\n\t"
                    "sfence"
                    :: "r" (buffer) : "memory");
            ]])], [ac_cv_flag_mmx2=yes], [ac_cv_flag_mmx2=no])
        else
            ac_cv_flag_mmx2=no
        fi
    ])
        if test x$ac_cv_flag_mmx2 = xyes; then
          AC_DEFINE(HAVE_ASM_MMX2,1,[Compiling in MMX2 support])
          AC_DEFINE(HAVE_ASM_MMXEXT,1,[Compiling in MMXEXT support])
          have_asm_mmx2=true
        fi

        AC_CACHE_CHECK([whether the selected target supports AVX], [ac_cv_flag_avx], [
          AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #ifndef __AVX__
      #error AVX is not enabled for the selected target
      #endif
          ]], [[]])], [ac_cv_flag_avx=yes], [ac_cv_flag_avx=no])
    ])
        if test x$ac_cv_flag_avx = xyes; then
          AC_DEFINE(HAVE_ASM_AVX,1,[Compiling in AVX support])
          have_asm_avx=true
    fi

        AC_CACHE_CHECK([whether the selected target supports AVX2], [ac_cv_flag_avx2], [
          AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #ifndef __AVX2__
      #error AVX2 is not enabled for the selected target
      #endif
          ]], [[]])], [ac_cv_flag_avx2=yes], [ac_cv_flag_avx2=no])
    ])
        if test x$ac_cv_flag_avx2 = xyes; then
          AC_DEFINE(HAVE_ASM_AVX2,1,[Compiling in AVX2 support])
          have_asm_avx2=true
    fi

        CFLAGS="$saved_CFLAGS"
fi

if test x$have_x86_64cpu = xtrue && test "x$arch_target" = "xgeneric"; then
  AC_DEFINE(HAVE_ASM_SSE,1,[Compiling in baseline x86-64 SSE support])
  AC_DEFINE(HAVE_SSE,1,[Compiling in baseline x86-64 SSE support])
  AC_DEFINE(HAVE_ASM_SSE2,1,[Compiling in baseline x86-64 SSE2 support])
  AC_DEFINE(HAVE_SSE2,1,[Compiling in baseline x86-64 SSE2 support])
  AC_DEFINE(HAVE_CMOV,1,[Compiling for baseline x86-64 CMOV support])
    ac_cv_flag_mmx=no
    ac_cv_flag_sse=yes
    ac_cv_flag_sse2=yes
    ac_cv_flag_sse4_1=no
    ac_cv_flag_sse4_2=no
    ac_cv_flag_3dnow=no
    ac_cv_flag_mmx2=no
    ac_cv_flag_avx=no
    ac_cv_flag_avx2=no
    ac_cv_flag_cmov=yes
  have_asm_sse=true
  have_asm_sse2=true
  have_cmov=true
fi

  if test x$have_x86cpu = xtrue; then
    AC_CACHE_CHECK([whether the compiler supports isolated AVX2 functions],
      [ac_cv_target_avx2], [
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <immintrin.h>
  __attribute__((target("avx2")))
  static int avx2_target_test(const int *input)
  {
    __m256i value = _mm256_loadu_si256((const __m256i *) input);
    return _mm256_extract_epi32(value, 0);
  }
      ]], [[
        int input[8] = { 0 };
        return avx2_target_test(input);
      ]])], [ac_cv_target_avx2=yes], [ac_cv_target_avx2=no])
    ])
    if test x$ac_cv_target_avx2 = xyes; then
      AC_DEFINE(HAVE_TARGET_AVX2,1,
        [Compiler supports isolated functions targeting AVX2])
    fi
  fi

  if test x$have_arm = xtrue; then
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $ARCHFLAGS $VEEJAY_CFLAGS"

    if test x$have_armv7a = xtrue; then
      ac_cv_flag_asimd=no
      AC_CACHE_CHECK([whether the selected target supports ARM NEON],
        [ac_cv_flag_neon], [
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
  #include <arm_neon.h>
        ]], [[
          uint8x16_t value = vdupq_n_u8(0);
          return vgetq_lane_u8(value, 0);
        ]])], [ac_cv_flag_neon=yes], [ac_cv_flag_neon=no])
        ])
      if test x$ac_cv_flag_neon = xyes; then
        AC_DEFINE(HAVE_ARM_NEON,1,
          [Selected compiler target supports ARM NEON])
        fi
    fi

    if test x$have_aarch64 = xtrue; then
      ac_cv_flag_neon=no
      AC_CACHE_CHECK([whether the selected target supports AArch64 ASIMD],
        [ac_cv_flag_asimd], [
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
  #include <arm_neon.h>
        ]], [[
          uint8x16_t value = vdupq_n_u8(0);
          return vgetq_lane_u8(value, 0);
        ]])], [ac_cv_flag_asimd=yes], [ac_cv_flag_asimd=no])
      ])
      if test x$ac_cv_flag_asimd = xyes; then
        AC_DEFINE(HAVE_ARM_ASIMD,1,
          [Selected compiler target supports AArch64 ASIMD])
      else
        AC_MSG_ERROR([AArch64 compiler does not provide baseline ASIMD support])
      fi
    fi

    CFLAGS="$saved_CFLAGS"
fi

if test x$have_ppccpu = xtrue && test "x$arch_target" != "xgeneric"; then

    AC_MSG_CHECKING([compiler support for AltiVec])
    cat > conftest.c <<EOF
#ifdef HAVE_ALTIVEC_H
#include <altivec.h>
#endif
int main() {
    union { vector signed int v;
            signed int i;
          } vi;
    vi.v = vec_splat_s32(1);
    return vi.i;
}
EOF
    ALTIVEC_CFLAGS=""
    if $CC -o conftest conftest.c >/dev/null 2>&1; then
      have_altivec=true
    elif $CC $CFLAGS -faltivec -o conftest conftest.c >/dev/null 2>&1; then
      have_altivec=true
      ALTIVEC_CFLAGS="-faltivec"
    elif $CC $CFLAGS -fvec -o conftest conftest.c >/dev/null 2>&1; then
      have_altivec=true
      ALTIVEC_CFLAGS="-fvec"
    elif $CC $CFLAGS -DHAVE_ALTIVEC_H=1 -maltivec -mabi=altivec -o conftest conftest.c >/dev/null 2>&1; then
      have_altivec=true
      ALTIVEC_CFLAGS="-DHAVE_ALTIVEC_H=1 -maltivec -mabi=altivec"
    fi
    rm -f conftest*;
 
    if test x$have_altivec = xtrue; then
        ALTIVEC_CFLAGS="-O3 ${ALTIVEC_CFLAGS}"
        AC_DEFINE(HAVE_ALTIVEC,1,[Inline PPC Altivec primitives available])
      AC_DEFINE(HAVE_ASM_ALTIVEC,1,[Compiling in AltiVec support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS $ALTIVEC_CFLAGS"
        AC_SUBST(ALTIVEC_CFLAGS)
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
    fi

fi


VEEJAY_CFLAGS="$VEEJAY_CFLAGS $ARCHFLAGS $OPENMP_CFLAGS $OPENMP_PRAGMA_CFLAGS $warnCFLAGS"
VEEJAY_CXXFLAGS="$VEEJAY_CFLAGS $warnCXXFLAGS"

AC_MSG_NOTICE([Final VEEJAY_CFLAGS: $VEEJAY_CFLAGS])
AC_MSG_NOTICE([Final VEEJAY_CXXFLAGS: $VEEJAY_CXXFLAGS])


AC_SUBST(VEEJAY_CFLAGS)
AC_SUBST(PROGRAM_NOPIC)

PKG_PROG_PKG_CONFIG
if test x$cross_compiling = xyes &&
   test -z "$PKG_CONFIG_LIBDIR" &&
   test -z "$PKG_CONFIG_SYSROOT_DIR"; then
  AC_MSG_ERROR([cross builds require PKG_CONFIG_LIBDIR or PKG_CONFIG_SYSROOT_DIR pointing to target package metadata])
fi

glib_modules="glib-2.0 >= 2.4"
PKG_CHECK_MODULES(GLIB, [$glib_modules])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

AC_SUBST(LIBQRENCODE_LIBS)
AC_SUBST(LIBQRENCODE_CFLAGS)

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED,,
                    [Defined if building against uninstalled FFmpeg source])

PKG_CHECK_MODULES(LIBAVUTIL, [libavutil >= 58],have_avutil=true,have_avutil=false)
PKG_CHECK_MODULES(LIBAVCODEC,[libavcodec >= 60],have_avcodec=true,have_avcodec=false)
PKG_CHECK_MODULES(LIBAVFORMAT,[libavformat >= 60],have_avformat=true,have_avformat=false)
PKG_CHECK_MODULES(LIBSWSCALE,[libswscale >= 7],have_swscale=true,have_swscale=false)
PKG_CHECK_MODULES(LIBSWRESAMPLE,[libswresample >= 4],have_swresample=true,have_swresample=false)

if test x$have_swscale = xfalse;
then
  AC_MSG_ERROR([FFmpeg 6 or newer is required (libswscale >= 7).])
fi
if test x$have_avutil = xfalse;
then
  AC_MSG_ERROR([FFmpeg 6 or newer is required (libavutil >= 58).])
fi
if test x$have_avformat = xfalse;
then
  AC_MSG_ERROR([FFmpeg 6 or newer is required (libavformat >= 60).])
fi
if test x$have_avcodec = xfalse;
then
  AC_MSG_ERROR([FFmpeg 6 or newer is required (libavcodec >= 60).])
fi
if test x$have_swresample = xfalse;
then
  AC_MSG_ERROR([FFmpeg 6 or newer is required (libswresample >= 4).])
fi

FFMPEG_CFLAGS="${LIBAVFORMAT_CFLAGS} ${LIBAVCODEC_CFLAGS} ${LIBAVUTIL_CFLAGS} ${LIBSWSCALE_CFLAGS} ${LIBSWRESAMPLE_CFLAGS}"
FFMPEG_LIBS="${LIBAVFORMAT_LIBS} ${LIBAVCODEC_LIBS} ${LIBAVUTIL_LIBS} ${LIBSWSCALE_LIBS} ${LIBSWRESAMPLE_LIBS}"
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

have_libcurl=false
have_mediamtx_executable=false
have_mediamtx=false
LIBCURL_CFLAGS=""
LIBCURL_LIBS=""

if test "x$with_mediamtx" != "xno"; then
  PKG_CHECK_MODULES([LIBCURL], [libcurl >= 7.68],
                    [have_libcurl=true], [have_libcurl=false])

  if test -n "$MEDIAMTX"; then
    AC_MSG_CHECKING([whether $MEDIAMTX is an executable MediaMTX binary])
    if test -x "$MEDIAMTX"; then
      have_mediamtx_executable=true
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
  elif test "x$with_mediamtx" != "xauto" && test "x$with_mediamtx" != "xyes"; then
    AC_MSG_CHECKING([whether $with_mediamtx is an executable MediaMTX binary])
    if test -x "$with_mediamtx"; then
      MEDIAMTX="$with_mediamtx"
      have_mediamtx_executable=true
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
  else
    AC_PATH_PROG([MEDIAMTX], [mediamtx], [])
    if test -n "$MEDIAMTX"; then
      have_mediamtx_executable=true
    fi
  fi

  if test "x$have_libcurl" = "xtrue" && test "x$have_mediamtx_executable" = "xtrue"; then
    have_mediamtx=true
    AC_DEFINE([HAVE_MEDIAMTX], [1],
              [Compile optional MediaMTX gateway integration])
  elif test "x$with_mediamtx" != "xauto"; then
    AC_MSG_ERROR([MediaMTX integration requested, but libcurl >= 7.68 or the MediaMTX executable is unavailable])
  else
    AC_MSG_NOTICE([MediaMTX executable or libcurl development files not found; gateway integration will not be built])
  fi
fi

AC_SUBST([LIBCURL_CFLAGS])
AC_SUBST([LIBCURL_LIBS])
AC_SUBST([MEDIAMTX])

AC_SUBST(LIBM_LIBS)

dnl ********************************************************************
dnl Look for X
AC_PATH_XTRA
AC_CHECK_HEADER(X11/Xlib.h, HAVE_X="yes", HAVE_X="no")

if test "x$HAVE_X" = "xno"
then
        AC_MSG_NOTICE([Cannot find X11 development files.])
else
	X_LIBS="$X_LIBS $X_PRE_LIBS $X_EXTRA_LIBS"
	X_LIBS="$X_LIBS -lX11"
	AC_SUBST(X_CFLAGS)
	AC_SUBST(X_LIBS)
fi
AC_SUBST(HAVE_X)
AM_CONDITIONAL(HAVE_X, test "x$HAVE_X" = "xyes")

have_xdpms=false
AC_CHECK_HEADERS([X11/extensions/dpms.h], [
    AC_CHECK_LIB([Xext], [DPMSQueryExtension], [
        AC_DEFINE([HAVE_XDPMS], [1], [Define if you have the XDPMS extension])
        have_xdpms=true
        X_EXTRA_LIBS="$X_EXTRA_LIBS -lXext"
    ], [], [$X_LIBS -lX11])
], [], [#include <X11/Xlib.h>])

dnl the check doesnt work on ubuntu, so just fake it.
RT_LIBS="-lrt"
AC_CHECK_LIB( rt, clock_gettime, [
	AC_DEFINE( HAVE_CLOCK_GETTIME, 1, [clock_gettime] )
	RT_LIBS="-lrt" ])
AC_SUBST(RT_LIBS)
dnl Check for Xinerama
use_xinerama=no
AC_CHECK_LIB( Xinerama, XineramaQueryExtension,
	[ AC_CHECK_HEADER(X11/extensions/Xinerama.h,
	   [
		AC_DEFINE( HAVE_XINERAMA,1,[Use Xinerama] )
		XINERAMA_LIBS="-lXext -lX11 -lXinerama"
		AC_SUBST(XINERAMA_LIBS)
        	use_xinerama=yes,
	   ],
	   use_xinerama=no,
	   [#include <X11/Xlib.h>])],
	   use_xinerama=no, -lXext)
AC_MSG_CHECKING(for Xinerama support on XFree86)
AC_MSG_RESULT($use_xinerama)	

AC_MSG_CHECKING(for video4linux II support)
AC_MSG_RESULT( $with_v4l2 )

if test x$with_v4l2 != xno ; then
  if test x$have_linux != xfalse ; then

	  AC_CHECK_HEADER(linux/videodev2.h,
                  [have_v4l2=true
	  if test x$with_v4l2 != xno; then
                   AC_DEFINE(HAVE_V4L2, 1,
		             [Building for Linux - using the video4linux2 API])
				   with_v4l=no
			fi
			],
                  [AC_MSG_NOTICE([videodev2.h not found - please install the linux kernel headers or try configure with --enable-v4l1])])
          fi
fi

dnl *********************************************************************
dnl Check for the pthread lib
dnl
AC_SUBST(PTHREAD_LIBS)
have_pthread=false
AC_CHECK_LIB(pthread, pthread_create,
  [ PTHREAD_LIBS="-lpthread" 
    AC_DEFINE(HAVE_LIBPTHREAD,1,[Compiling with pthread library])
    have_pthread=true ],,)

if test x$have_pthread != xtrue ; then
	AC_MSG_NOTICE([Cannot find pthread library])
fi

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,1,[Compile with dlopen support])
 have_dl_dlopen=true
fi


dnl *********************************************************************
dnl Optional nvJPEG/CUDA MJPEG encoder/decoder.
have_nvjpeg=false
have_nvjpeg_kernel=false
NVJPEG_CFLAGS=""
NVJPEG_LIBS=""
NVJPEG_KERNEL_LIBS=""
NVJPEG_NVCCFLAGS=""

if test "x$with_nvjpeg" != "xno"; then
  nvjpeg_api=no
  nvjpeg_include_dir=""
  nvjpeg_cuda_root=""
  nvjpeg_headers_seen=no
  nvjpeg_versioned_roots=""

  case "$host_cpu" in
    x86_64|amd64) nvjpeg_cuda_target=x86_64-linux ;;
    aarch64|arm64) nvjpeg_cuda_target=sbsa-linux ;;
    ppc64le) nvjpeg_cuda_target=ppc64le-linux ;;
    *) nvjpeg_cuda_target=$host_cpu-linux ;;
  esac

  if test "x$with_nvjpeg" != "xauto" && test "x$with_nvjpeg" != "xyes"; then
    nvjpeg_search_roots="$with_nvjpeg"
  else
    for nvjpeg_versioned_root in /usr/local/cuda-*; do
      if test -d "$nvjpeg_versioned_root"; then
        nvjpeg_versioned_roots="$nvjpeg_versioned_root $nvjpeg_versioned_roots"
      fi
    done
    nvjpeg_search_roots="$CUDA_HOME $CUDA_PATH /usr/local/cuda $nvjpeg_versioned_roots /opt/cuda /usr"
  fi

  AC_MSG_CHECKING([for compatible CUDA/nvJPEG encode headers])
  vj_nvjpeg_save_CPPFLAGS="$CPPFLAGS"
  
  dnl nvjpeg.h includes <cstddef> which is C++ only, so we must test with the C++ compiler
  AC_LANG_PUSH([C++])
  
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <cstddef>
#include <cuda_runtime_api.h>
#include <nvjpeg.h>
  ]], [[
    nvjpegHandle_t handle = (nvjpegHandle_t)0;
    nvjpegEncoderState_t enc_state = (nvjpegEncoderState_t)0;
    nvjpegEncoderParams_t enc_params = (nvjpegEncoderParams_t)0;
    nvjpegImage_t image;
    nvjpegChromaSubsampling_t css = NVJPEG_CSS_422;
    cudaStream_t stream = (cudaStream_t)0;
    (void)handle; (void)enc_state; (void)enc_params; (void)image; (void)css;
    (void)stream;
    (void)nvjpegCreateSimple;
    (void)nvjpegEncoderStateCreate;
    (void)nvjpegEncoderParamsCreate;
    (void)nvjpegEncoderParamsSetQuality;
    (void)nvjpegEncodeYUV;
    (void)nvjpegEncodeRetrieveBitstream;
    (void)cudaGetDevice;
    (void)cudaSetDevice;
    (void)cudaStreamCreateWithFlags;
    (void)cudaMalloc;
    (void)cudaMemcpyAsync;
  ]])], [
    nvjpeg_api=yes
    nvjpeg_include_dir="compiler default search path"
  ], [nvjpeg_api=no])

  if test "x$nvjpeg_api" != "xyes"; then
    for nvjpeg_cuda_root_try in $nvjpeg_search_roots; do
      test -d "$nvjpeg_cuda_root_try" || continue
      for nvjpeg_try_dir in \
        "$nvjpeg_cuda_root_try" \
        "$nvjpeg_cuda_root_try/include" \
        "$nvjpeg_cuda_root_try/targets/$nvjpeg_cuda_target/include" \
        "$nvjpeg_cuda_root_try/targets/$host_cpu-linux/include"; do
        if test -f "$nvjpeg_try_dir/nvjpeg.h" && \
           test -f "$nvjpeg_try_dir/cuda_runtime_api.h"; then
          nvjpeg_headers_seen=yes
          CPPFLAGS="$vj_nvjpeg_save_CPPFLAGS -I$nvjpeg_try_dir"
          AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <cstddef>
#include <cuda_runtime_api.h>
#include <nvjpeg.h>
          ]], [[
            nvjpegHandle_t handle = (nvjpegHandle_t)0;
            nvjpegEncoderState_t enc_state = (nvjpegEncoderState_t)0;
            nvjpegEncoderParams_t enc_params = (nvjpegEncoderParams_t)0;
            nvjpegImage_t image;
            nvjpegChromaSubsampling_t css = NVJPEG_CSS_422;
            cudaStream_t stream = (cudaStream_t)0;
            (void)handle; (void)enc_state; (void)enc_params; (void)image; (void)css;
            (void)stream;
            (void)nvjpegCreateSimple;
            (void)nvjpegEncoderStateCreate;
            (void)nvjpegEncoderParamsCreate;
            (void)nvjpegEncoderParamsSetQuality;
            (void)nvjpegEncodeYUV;
            (void)nvjpegEncodeRetrieveBitstream;
            (void)cudaGetDevice;
            (void)cudaSetDevice;
            (void)cudaStreamCreateWithFlags;
            (void)cudaMalloc;
            (void)cudaMemcpyAsync;
          ]])], [nvjpeg_api=yes], [nvjpeg_api=no])
          if test "x$nvjpeg_api" = "xyes"; then
            nvjpeg_cuda_root="$nvjpeg_cuda_root_try"
            nvjpeg_include_dir="$nvjpeg_try_dir"
            NVJPEG_CFLAGS="-I$nvjpeg_include_dir"
            break 2
          fi
        fi
      done
    done
  fi
  
  AC_LANG_POP([C++])
  CPPFLAGS="$vj_nvjpeg_save_CPPFLAGS"

  if test "x$nvjpeg_api" = "xyes"; then
    AC_MSG_RESULT([$nvjpeg_include_dir])
  elif test "x$nvjpeg_headers_seen" = "xyes"; then
    AC_MSG_RESULT([headers found but encode API compile failed (CUDA toolkit might be too old or missing C++ stdlib)])
  else
    AC_MSG_RESULT([not found])
  fi

  if test "x$nvjpeg_api" = "xyes"; then
    nvjpeg_cuda_ldflags=""
    nvjpeg_cuda_lib_dir=""
    for nvjpeg_cuda_root_try in $nvjpeg_search_roots; do
      for nvjpeg_lib_dir in \
        "$nvjpeg_cuda_root_try/lib64" \
        "$nvjpeg_cuda_root_try/lib" \
        "$nvjpeg_cuda_root_try/lib/$host_cpu-linux-gnu" \
        "$nvjpeg_cuda_root_try/targets/$nvjpeg_cuda_target/lib" \
        "$nvjpeg_cuda_root_try/targets/$host_cpu-linux/lib"; do
        if test -f "$nvjpeg_lib_dir/libnvjpeg.so" || \
           test -f "$nvjpeg_lib_dir/libnvjpeg.a"; then
          nvjpeg_cuda_lib_dir="$nvjpeg_lib_dir"
          nvjpeg_cuda_ldflags="-L$nvjpeg_lib_dir"
          break 2
        fi
      done
    done

    if test -z "$nvjpeg_cuda_lib_dir"; then
      for nvjpeg_lib_dir in \
        "/usr/lib/$host" \
        "/usr/lib/$host_cpu-linux-gnu" \
        "/usr/lib/x86_64-linux-gnu"; do
        if test -f "$nvjpeg_lib_dir/libnvjpeg.so" || \
           test -f "$nvjpeg_lib_dir/libnvjpeg.a"; then
          nvjpeg_cuda_lib_dir="$nvjpeg_lib_dir"
          nvjpeg_cuda_ldflags="-L$nvjpeg_lib_dir"
          break
        fi
      done
    fi

    if test -n "$nvjpeg_cuda_lib_dir"; then
      vj_nvjpeg_save_LDFLAGS="$LDFLAGS"
      vj_nvjpeg_save_LIBS="$LIBS"
      LDFLAGS="$LDFLAGS $nvjpeg_cuda_ldflags"
      
      AC_CHECK_LIB([nvjpeg], [nvjpegCreateSimple],
        [AC_CHECK_LIB([cudart], [cudaGetDevice],
           [have_nvjpeg=true], [have_nvjpeg=false])],
        [have_nvjpeg=false])
        
      LDFLAGS="$vj_nvjpeg_save_LDFLAGS"
      LIBS="$vj_nvjpeg_save_LIBS"

      if test "x$have_nvjpeg" = "xtrue"; then
        NVJPEG_LIBS="$nvjpeg_cuda_ldflags -lnvjpeg -lcudart -lstdc++"
        AC_DEFINE([HAVE_NVJPEG], [1],
          [Compile nvJPEG MJPEG encode/decode support])
      fi
    else
      AC_MSG_NOTICE([libnvjpeg.so not found in standard CUDA library directories])
    fi
  fi

  if test "x$have_nvjpeg" = "xtrue"; then
    nvjpeg_nvcc_path="$PATH"
    for nvjpeg_cuda_root_try in $nvjpeg_search_roots; do
      if test -d "$nvjpeg_cuda_root_try/bin"; then
        nvjpeg_nvcc_path="$nvjpeg_cuda_root_try/bin:$nvjpeg_nvcc_path"
      fi
    done
    AC_PATH_PROG([NVCC], [nvcc], [], [$nvjpeg_nvcc_path])

    if test -n "$NVCC"; then
      nvjpeg_cuda_ldflags=""
      nvjpeg_cuda_lib_dir=""
      for nvjpeg_cuda_root_try in $nvjpeg_search_roots; do
        for nvjpeg_lib_dir in \
          "$nvjpeg_cuda_root_try/lib64" \
          "$nvjpeg_cuda_root_try/lib" \
          "$nvjpeg_cuda_root_try/lib/$host_cpu-linux-gnu" \
          "$nvjpeg_cuda_root_try/targets/$nvjpeg_cuda_target/lib" \
          "$nvjpeg_cuda_root_try/targets/$host_cpu-linux/lib"; do
          if test -f "$nvjpeg_lib_dir/libcudart.so" || \
             test -f "$nvjpeg_lib_dir/libcudart.a"; then
            nvjpeg_cuda_lib_dir="$nvjpeg_lib_dir"
            nvjpeg_cuda_ldflags="-L$nvjpeg_lib_dir"
            break 2
          fi
        done
      done

      if test -z "$nvjpeg_cuda_lib_dir"; then
        for nvjpeg_lib_dir in \
          "/usr/lib/$host" \
          "/usr/lib/$host_cpu-linux-gnu" \
          "/usr/lib/x86_64-linux-gnu"; do
          if test -f "$nvjpeg_lib_dir/libcudart.so" || \
             test -f "$nvjpeg_lib_dir/libcudart.a"; then
            nvjpeg_cuda_lib_dir="$nvjpeg_lib_dir"
            nvjpeg_cuda_ldflags="-L$nvjpeg_lib_dir"
            break
          fi
        done
      fi

      vj_nvjpeg_save_LDFLAGS="$LDFLAGS"
      vj_nvjpeg_save_LIBS="$LIBS"
      LDFLAGS="$LDFLAGS $nvjpeg_cuda_ldflags"
      AC_CHECK_LIB([cudart], [cudaGetDevice],
        [AC_CHECK_LIB([stdc++], [__cxa_guard_acquire],
           [have_nvjpeg_kernel=true], [have_nvjpeg_kernel=false])],
        [have_nvjpeg_kernel=false])
      LDFLAGS="$vj_nvjpeg_save_LDFLAGS"
      LIBS="$vj_nvjpeg_save_LIBS"

      if test "x$have_nvjpeg_kernel" = "xtrue"; then
        NVJPEG_KERNEL_LIBS="$nvjpeg_cuda_ldflags -lcudart -lstdc++"
        NVJPEG_NVCCFLAGS="-std=c++11"
        AC_DEFINE([HAVE_NVJPEG_CUDA_KERNEL], [1],
          [Compile CUDA 4:2:2 to 4:4:4 chroma upsampling kernel])
      else
        AC_MSG_NOTICE([CUDA runtime or C++ ABI library not linkable; checked toolkit and Ubuntu multiarch paths; nvJPEG remains available for native 4:2:2 only])
      fi
    else
      AC_MSG_NOTICE([nvcc not found; nvJPEG remains available for native 4:2:2 only])
    fi
  fi
  
  if test "x$have_nvjpeg" != "xtrue" && test "x$with_nvjpeg" != "xauto" && test "x$with_nvjpeg" != "xno"; then
    AC_MSG_ERROR([nvJPEG support requested, but headers/libraries are unavailable; see config.log])
  elif test "x$have_nvjpeg" != "xtrue"; then
    AC_MSG_NOTICE([Compatible CUDA/nvJPEG libraries not found; checked toolkit and Ubuntu multiarch paths])
  fi
fi
AC_SUBST([NVJPEG_CFLAGS])
AC_SUBST([NVJPEG_LIBS])
AC_SUBST([NVJPEG_KERNEL_LIBS])
AC_SUBST([NVJPEG_NVCCFLAGS])
AC_SUBST([NVCC])


AC_ARG_WITH([ndi],
  [AS_HELP_STRING([--with-ndi@<:@=DIR@:>@],
                  [enable optional NDI network video/audio support; look for the SDK headers below DIR])],
  [with_ndi="$withval"], [with_ndi=auto])

have_ndi=false
NDI_CFLAGS=""
NDI_LIBS=""
if test "x$with_ndi" != "xno"; then
  ndi_header=no
  ndi_api=no
  ndi_header_path=""
  ndi_include_dir=""
  if test "x$with_ndi" != "xauto" && test "x$with_ndi" != "xyes"; then
    AC_MSG_CHECKING([for Processing.NDI.Lib.h below $with_ndi])
    for ndi_include_dir in "$with_ndi" "$with_ndi/include" "$with_ndi/include/ndi"; do
      if test -f "$ndi_include_dir/Processing.NDI.Lib.h"; then
        ndi_header_path="$ndi_include_dir/Processing.NDI.Lib.h"
        ndi_header=yes
        break
      fi
    done
    if test "x$ndi_header" = "xyes"; then
      AC_MSG_RESULT([$ndi_header_path])
      NDI_CFLAGS="-I\"$ndi_include_dir\""
    else
      AC_MSG_RESULT([not found])
    fi
  else
    AC_CHECK_HEADER([Processing.NDI.Lib.h], [ndi_header=yes], [ndi_header=no])
  fi
  if test "x$ndi_header" = "xyes"; then
    if test -n "$ndi_header_path"; then
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include "$ndi_header_path"]], [[
        const NDIlib_v5 *api = 0;
        (void)api->find_create_v2;
        (void)api->recv_create_v3;
        (void)api->recv_capture_v3;
        (void)api->recv_free_audio_v3;
        (void)api->send_send_video_async_v2;
        (void)api->util_send_send_audio_interleaved_16s;
      ]])], [ndi_api=yes], [ndi_api=no])
    else
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <Processing.NDI.Lib.h>]], [[
        const NDIlib_v5 *api = 0;
        (void)api->find_create_v2;
        (void)api->recv_create_v3;
        (void)api->recv_capture_v3;
        (void)api->recv_free_audio_v3;
        (void)api->send_send_video_async_v2;
        (void)api->util_send_send_audio_interleaved_16s;
      ]])], [ndi_api=yes], [ndi_api=no])
    fi
  fi
  if test "x$ndi_header" = "xyes" && test "x$ndi_api" = "xyes" && test "x$have_dl_dlopen" = "xtrue"; then
    have_ndi=true
    NDI_LIBS="-ldl"
    AC_DEFINE([HAVE_NDI], [1], [Compile optional NDI network video/audio support])
  elif test "x$with_ndi" != "xauto"; then
    AC_MSG_ERROR([NDI support requested, but a compatible Processing.NDI.Lib.h or dlopen support is unavailable])
  else
    AC_MSG_NOTICE([Compatible NDI SDK headers not found; NDI support will not be built])
  fi
fi
AC_SUBST([NDI_CFLAGS])
AC_SUBST([NDI_LIBS])

dnl
dnl Check if we want to link with libjpeg (used for making a screenshot)
have_jpeg=false
if test x$with_jpeg != xno ; then

	dnl *********************************************************************
	dnl Check for the special mmx accelerated jpeg library
	dnl At the end of this mess, JPEG_LIBS will contain the libraries and 
	dnl flags needed to link with the jpeg library and JPEG_CFLAGS will 
	dnl have the needed flags to compile against said library
	AC_SUBST(JPEG_LIBS)
	AC_SUBST(JPEG_CFLAGS)
	have_jpeg=false
	if test x$with_jpegmmx = xyes ; then
	  AC_MSG_ERROR([
	*** A directory must be specified for --with-jpeg-mmx option.])
	fi
	if test x$with_jpegmmx = x ; then
	  dnl Special case for building .deb's
	  if test -d ../jpeg-mmx ; then
	    with_jpegmmx=`pwd`/../jpeg-mmx
	  else
	    with_jpegmmx=/usr/local/src/jpeg-mmx
	  fi
	fi

	dnl
	dnl Look for the installed/specified copy
	dnl

	OLD_CFLAGS="$CFLAGS"
	OLD_LIBS="$LIBS"
	LIBS="$LIBS -L$with_jpegmmx"
	CFLAGS="$CFLAGS -I$with_jpegmmx"
	AC_CHECK_LIB(jpeg-mmx, jpeg_start_compress,
	   [ JPEG_LIBS="$LIBS -ljpeg-mmx"
	     JPEG_CFLAGS="-I$with_jpegmmx"
	       have_jpeg=true ],,)
	  LIBS="$OLD_LIBS"
	  CFLAGS="$OLD_CFLAGS"

	dnl 
	dnl Look for _a_ jpeg lib that will work.
	dnl
	if test x$have_jpeg = xfalse ; then
	  AC_CHECK_HEADER(jpeglib.h,,
	    AC_MSG_WARN([jpeglib.h not found - please install the libjpeg headers]))
	  AC_CHECK_LIB(jpeg, jpeg_start_compress,
	    [ JPEG_LIBS="-ljpeg"
	      JPEG_CFLAGS=""
	      have_jpeg=true ],,)
	fi

	if test x$have_jpeg = xtrue ; then
		AC_DEFINE(HAVE_JPEG,1,[Define is JPEG libraries are available])
	fi
fi
have_pixbuf=false
if test x$with_pixbuf != xno ; then
  PKG_CHECK_MODULES(PIXBUF, [gdk-pixbuf-2.0],
                    [
 		     AC_SUBST(PIXBUF_CFLAGS)
		     AC_SUBST(PIXBUF_LIBS)
		     have_pixbuf=true
                     AC_DEFINE(USE_GDK_PIXBUF, 1, [use gdk image load / save])],
                    [have_pixbuf=false])
fi


if test x$with_pixbuf != xno && test x$have_pixbuf != xtrue ; then
	AC_MSG_ERROR([Cannot find the gdk pixbuf library.])
fi

dnl *********************************************************************
dnl Check for libdv
dnl   (creates LIBDV_CFLAGS, LIBDV_LIBS; defines HAVE_LIBDV)
dnl *********************************************************************
have_libdv=false
if test x$with_libdv != xno ; then
  PKG_CHECK_MODULES(LIBDV, [libdv >= 0.102],
                    [have_libdv=true
                     AC_DEFINE(SUPPORT_READ_DV2, 1, [libdv is present])],
                    [have_libdv=false])
fi

dnl *********************************************************************
dnl Check for libquicktime
dnl *********************************************************************
have_libquicktime=false
if test x$with_libquicktime != xno ; then
  PKG_CHECK_MODULES(LIBQUICKTIME, [libquicktime >= 1.0.0],
                    [have_libquicktime=true
                     AC_DEFINE(HAVE_LIBQUICKTIME, 1, [libquicktime >= 1.0.0 present])],
                    [have_libquicktime=false])
fi
AM_CONDITIONAL(HAVE_LIBQUICKTIME, test x$have_libquicktime = xtrue)



dnl ********************************************************************
dnl Check to see if __progname is provided by the system
dnl ********************************************************************
AC_CACHE_CHECK([for __progname],
               [mjt_cv_extern___progname],
               [AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>]],
                                                [[extern char *__progname;
                                                  puts(__progname);]])],
                               [mjt_cv_extern___progname=yes],
			       [mjt_cv_extern___progname=no])])
if test x$mjt_cv_extern___progname = xyes ; then
  AC_DEFINE(HAVE___PROGNAME, 1, [Is __progname defined by system?])
fi

dnl ********************************************************************
dnl Check for the SDL library (for software playback)
dnl   (defines SDL_CFLAGS and SDL_LIBS)
dnl ********************************************************************
dnl have_sdl=false
dnl if test x$with_sdl != xno ; then
dnl AM_PATH_SDL(1.2.3,[
dnl            have_sdl=true
dnl            AC_DEFINE(HAVE_SDL,, [SDL library present])],,)
dnl fi

have_sdl=false
if test x$with_sdl != xno ; then
PKG_CHECK_MODULES( SDL2, sdl2 >= 2.0.0, have_sdl=true,have_sdl=false)
if test x$have_sdl = xfalse ; then
 AC_MSG_ERROR([Could not find SDL 2.0 library!])
fi
AC_DEFINE(HAVE_SDL,1,[SDL2 library is installed])
fi

dnl *********************************************************************
dnl Check for Liblo
dnl *********************************************************************
have_liblo=false
PKG_CHECK_MODULES( LIBLO, liblo >= 0.26,have_liblo=true,have_liblo=false)
if test x$have_liblo = xfalse ; then
 AC_MSG_WARN([Could not find optional library liblo])
else
 have_liblo=true
 AC_SUBST(HAVE_LIBLO)
 AC_DEFINE(HAVE_LIBLO,1,[define if liblo])
fi

dnl *********************************************************************
dnl Check for the Jack Audio connection kit
dnl   AC_DEFINE(HAVE_JACK,,[Compiling with support for Jack])
dnl

have_jack=false
if test x$with_jack != xno ; then
  PKG_CHECK_MODULES(JACK, jack >= 0.1, have_jack=true, have_jack=false)
  if test x$have_jack = xtrue ; then
  	AC_SUBST(JACK_LIBS)
   	AC_SUBST(JACK_CFLAGS)
    AC_SEARCH_LIBS(jack_port_get_latency_range,jack,
              [AC_DEFINE(HAVE_JACK_PORT_GET_LATENCY_RANGE, 1, [jack_port_get_latency_range present in libjack])],
              [])
	AC_DEFINE(HAVE_JACK,1,[Compiling in Jack])
	AC_SEARCH_LIBS(jack_client_open,jack,
			  [AC_DEFINE(HAVE_JACK2,1,[newer jack versions])],[])
	fi
fi

dnl ********************************************************************
dnl Check for the libxml2 library (for saving/loading project files)
dnl
have_xml2=false
if test x$with_xml2 != xno; then
	PKG_CHECK_MODULES( XML2, [libxml-2.0 >= 2.5.4],
	[
		have_xml2=true
		AC_DEFINE(HAVE_XML2,1,[with XML])
	],
	[ have_xml2=false ])
fi

if test x$have_xml2 != xtrue ; then
  AC_MSG_ERROR([libxml2 is required for project-file support])
fi

have_freetype=false
have_fontconfig=false

PKG_CHECK_MODULES([FREETYPE], [freetype2 >= 2.0], [
    AC_DEFINE([HAVE_FREETYPE], [1], [Compiling with FreeType2])
    AC_SUBST([FREETYPE_CFLAGS])
    AC_SUBST([FREETYPE_LIBS])
    have_freetype=true
], [
    AC_MSG_WARN([FreeType2 not found; some features may be disabled])
])

PKG_CHECK_MODULES([FONTCONFIG], [fontconfig >= 2.0], [
    AC_DEFINE([HAVE_FONTCONFIG], [1], [Compiling with Fontconfig])
    AC_SUBST([FONTCONFIG_CFLAGS])
    AC_SUBST([FONTCONFIG_LIBS])
    have_fontconfig=true
], [
    AC_MSG_WARN([Fontconfig not found; some features may be disabled])
])


AC_MSG_CHECKING(if libunwind is installed)
have_unwind=false

PKG_CHECK_MODULES( LIBUNWIND, libunwind, [have_unwind=true], [
	OLDLIBS="$LIBS"
	LIBS="$LIBS -lunwind"
	AC_CACHE_CHECK([for libunwind support], [ac_cv_libunwind],
	 [AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#define UNW_LOCAL_ONLY
#include <libunwind.h>
]],[[
unw_context_t ctx;
unw_getcontext( &ctx );
]])],[ac_cv_libunwind=yes],[ac_cv_libunwind=no])])
	LIBS="$OLDLIBS"

	if test "$ac_cv_libunwind" = yes; then
		LIBUNWIND_CFLAGS=
		LIBUNWIND_LIBS=-lunwind
		have_unwind=true
	fi
])

if test x$have_unwind = xtrue ; then
   AC_SUBST(LIBUNWIND_LIBS)
   AC_SUBST(LIBUNWIND_CFLAGS)
   AC_DEFINE(HAVE_LIBUNWIND,1,[Define if you have libunwind installed])
fi

have_veejaycore=false
PKG_CHECK_MODULES( VEEJAYCORE, [veejaycore >= 1.6.0 ],
                  [ AC_SUBST(VEEJAYCORE_CFLAGS)
                    AC_SUBST(VEEJAYCORE_LIBS)
                    have_veejaycore=true
                    AC_DEFINE(HAVE_VEEJAYCORE,1,[Veejay 1.6.0 installed])
                    ],
                    [have_veejaycore=false])

if test x$have_veejaycore != xtrue ; then
    AC_MSG_ERROR([Cannot find libveejaycore 1.6.0 or later])
fi

dnl **********************************************************************
dnl All the conditional stuff for the Makefiles
AM_CONDITIONAL(HAVE_SDL, test x$have_sdl = xtrue)
AM_CONDITIONAL(HAVE_XML2, test x$have_xml2 = xtrue)
AM_CONDITIONAL(HAVE_JACK, test x$have_jack = xtrue)
AM_CONDITIONAL(HAVE_FREETYPE, test x$have_freetype = xtrue)
AM_CONDITIONAL(HAVE_V4L2, test x$have_v4l2 = xtrue)
AM_CONDITIONAL(SUPPORT_READ_DV2, test x$have_libdv = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_NVJPEG, test x$have_nvjpeg = xtrue)
AM_CONDITIONAL(HAVE_NVJPEG_CUDA_KERNEL, test x$have_nvjpeg_kernel = xtrue)
AM_CONDITIONAL(HAVE_NDI, test x$have_ndi = xtrue)
AM_CONDITIONAL(HAVE_MEDIAMTX, test x$have_mediamtx = xtrue)
AM_CONDITIONAL(HAVE_ALTIVEC, test x$have_altivec = xtrue)
AM_CONDITIONAL(HAVE_JPEG,test x$have_jpeg = xtrue)
AM_CONDITIONAL(HAVE_LIBLO,test x$have_liblo = xtrue)
AM_CONDITIONAL(HAVE_XDPMS, test x$have_xdpms = xtrue)

dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

veejay_cflags_without_optimization=""
for flag in $CFLAGS; do
  case $flag in
    -O*|-g*) ;;
    *) veejay_cflags_without_optimization="$veejay_cflags_without_optimization $flag" ;;
  esac
done
veejay_cxxflags_without_optimization=""
for flag in $CXXFLAGS; do
  case $flag in
    -O*|-g*) ;;
    *) veejay_cxxflags_without_optimization="$veejay_cxxflags_without_optimization $flag" ;;
  esac
done
CFLAGS="$ARCHFLAGS$veejay_cflags_without_optimization -O3"
CXXFLAGS="$ARCHFLAGS$veejay_cxxflags_without_optimization -O3"

dnl	output some info on what veejay was build
BUILD_PLATFORM=${build}
BUILD_CPU=${build_cpu}
BUILD_VENDOR=${build_vendor}
BUILD_OS=${build_os}
AC_SUBST(BUILD_PLATFORM)
AC_SUBST(BUILD_CPU)
AC_SUBST(BUILD_VENDOR)
AC_SUBST(BUILD_OS)

if test ! -r $srcdir/buildinfo.sh; then
	AC_MSG_ERROR([buildinfo.sh script not found.])
fi

chmod +x $srcdir/buildinfo.sh

$srcdir/buildinfo.sh

dnl **********************************************************************
dnl Output a Makefile or two and the lib/header descriptor script
dnl

AC_CONFIG_FILES([Makefile])

AC_CONFIG_FILES([
thirdparty/Makefile
thirdparty/bio2jack/Makefile
thirdparty/libOSC/Makefile
libvjxml/Makefile
libvje/Makefile
libplugger/Makefile
libsample/Makefile
libsubsample/Makefile
libel/Makefile
libstream/Makefile
libsamplerec/Makefile
libveejay/Makefile
veejay/Makefile
man/Makefile
livido-plugins/Makefile
share/Makefile
veejay.pc
])

AC_DEFINE_UNQUOTED([VERSION], ["$VERSION"], [Veejay version number])

AC_MSG_CHECKING([for git commit hash])
GIT_HASH=`git rev-parse --short HEAD 2>/dev/null || echo "unknown"`
AC_MSG_RESULT([$GIT_HASH])
AH_TEMPLATE([GIT_HASH_VEEJAY], [git commit hash libveejay])
AC_DEFINE_UNQUOTED([GIT_HASH_VEEJAY], ["$GIT_HASH"], [git commit hash libveejay])



AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay ${VERSION} build configuration :])
AC_MSG_NOTICE([])


AC_MSG_NOTICE([ Compiler flags: $VEEJAY_CFLAGS $CFLAGS])
AC_MSG_NOTICE([   architecture: $arch_target])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Architecture: ${host_cpu} ])
AC_MSG_NOTICE([])
if test "$have_x86cpu" = "true" ; then
AC_MSG_NOTICE([   x86 compile target (global ISA):])
AC_MSG_NOTICE([    MMX                 : ${ac_cv_flag_mmx}])
AC_MSG_NOTICE([    MMX2                : ${ac_cv_flag_mmx2}])
AC_MSG_NOTICE([    SSE                 : ${ac_cv_flag_sse}])
AC_MSG_NOTICE([    SSE2                : ${ac_cv_flag_sse2}])
AC_MSG_NOTICE([    SSE4.1              : ${ac_cv_flag_sse4_1}])
AC_MSG_NOTICE([    SSE4.2              : ${ac_cv_flag_sse4_2}])
AC_MSG_NOTICE([    3DNow!              : ${ac_cv_flag_3dnow}])
AC_MSG_NOTICE([    CMOV                : ${ac_cv_flag_cmov}])
AC_MSG_NOTICE([    AVX                 : ${ac_cv_flag_avx}])
AC_MSG_NOTICE([    AVX2                : ${ac_cv_flag_avx2}])
AC_MSG_NOTICE([   Isolated runtime-dispatch implementations:])
AC_MSG_NOTICE([    AVX2                : ${ac_cv_target_avx2}])
AC_MSG_NOTICE([   Runtime CPU eligibility: detected by libavutil at process startup])
fi

if test "$have_ppccpu" = "true" ; then
AC_MSG_NOTICE([   ppc])
AC_MSG_NOTICE([    AltiVec enabled     : ${have_altivec}])
fi

if test "$have_arm" = "true" ; then
AC_MSG_NOTICE([   arm])
AC_MSG_NOTICE([    ASIMD    enabled     : ${ac_cv_flag_asimd}])
fi
if test "$have_armv7a"  = "true"; then
AC_MSG_NOTICE([   arm7a])
AC_MSG_NOTICE([    NEON    enabled     : ${ac_cv_flag_neon}])
fi

if test "$have_mips" = "true" ; then
AC_MSG_NOTICE([   mips])
AC_MSG_NOTICE([    PS/2 enabled        : ${have_ps2}])
fi
if test "$have_esp32" = "true" ; then
AC_MSG_NOTICE([   ESP32 target         : ${host}])
fi
AC_MSG_NOTICE([])
if test "$have_linux" = "true" ; then
AC_MSG_NOTICE([   Platform: Linux])
fi

if test "$have_darwin" = "true" ; then
AC_MSG_NOTICE([   Platform: Darwin])
AC_MSG_NOTICE([      Good luck! You can be the first! ])
fi
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Required dependencies:])
AC_MSG_NOTICE([  - Veejay Core                   : ${have_veejaycore}])
AC_MSG_NOTICE([  - POSIX Threads (pthread)       : ${have_pthread}])
AC_MSG_NOTICE([  - FFmpeg/AV AVFormat            : ${have_avformat} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVCodec             : ${have_avcodec} ])
AC_MSG_NOTICE([  - FFmpeg/AV Swscaler            : ${have_swscale} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVUtil              : ${have_avutil} ])
AC_MSG_NOTICE([  - FFmpeg/AV SwResample          : ${have_swresample} ])
AC_MSG_NOTICE([  - SDL support                   : ${have_sdl}])
AC_MSG_NOTICE([  - Freetype support              : ${have_freetype}])
AC_MSG_NOTICE([  - Fontconfig                    : ${have_fontconfig}])
AC_MSG_NOTICE([  - XML c library for Gnome       : ${have_xml2}])
AC_MSG_NOTICE([  - JPEG support                  : ${have_jpeg} ])
AC_MSG_NOTICE([  - GDK Pixbuf support            : ${have_pixbuf}])
AC_MSG_NOTICE([ Optional dependencies])
AC_MSG_NOTICE([  - libDV (digital video) support : ${have_libdv} ])
AC_MSG_NOTICE([  - QuickTime support             : ${have_libquicktime} ])
AC_MSG_NOTICE([  - Jack Audio Connection Kit     : ${have_jack}])
AC_MSG_NOTICE([  - Liblo OSC client              : ${have_liblo}])
AC_MSG_NOTICE([  - V4L2 (default)                : ${have_v4l2}])
AC_MSG_NOTICE([  - libunwind                     : ${have_unwind}])
AC_MSG_NOTICE([  - nvJPEG MJPEG decode           : ${have_nvjpeg}])
AC_MSG_NOTICE([  - nvJPEG GPU 4:4:4 output        : ${have_nvjpeg_kernel}])
AC_MSG_NOTICE([  - NDI network video/audio       : ${have_ndi}])
AC_MSG_NOTICE([  - MediaMTX gateway               : ${have_mediamtx}])
AC_MSG_NOTICE([    libcurl                        : ${have_libcurl}])
AC_MSG_NOTICE([    MediaMTX executable            : ${MEDIAMTX:-not found}])