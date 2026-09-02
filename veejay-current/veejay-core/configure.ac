dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejaycore],[1.6.0],[http://www.veejayhq.net])
AC_PREREQ([2.69])

USER_CFLAGS="$CFLAGS"
dnl Build modes below own optimization and debug flags; preserve only caller input here.
CFLAGS="${CFLAGS-}"
CXXFLAGS="${CXXFLAGS-}"

AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_SRCDIR([veejaycore/core.c])

VEEJAYCORE_MAJOR_VERSION=1
VEEJAYCORE_MINOR_VERSION=6
VEEJAYCORE_MICRO_VERSION=0
VEEJAYCORE_VERSION=$VEEJAYCORE_MAJOR_VERSION.$VEEJAYCORE_MINOR_VERSION.$VEEJAYCORE_MICRO_VERSION
VEEJAYCORE_CODENAME="Veejay Core - build $VEEJAY_MINOR_VERSION $VEEJAY_MICRO_VERSION"
AC_CONFIG_HEADERS([config.h])
dnl AX_CONFIG_PREFIXED_HEADER([veejay-config.h], [VEEJAY], [config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE

AC_DEFINE(VEEJAYCORE,1,[Building Veejay Core])
AC_DEFINE(HAVE_CONFIG_H,1,[Include config.h])
# libtool versioning
LT_RELEASE=$VEEJAYCORE_MAJOR_VERSION.$VEEJAYCORE_MINOR_VERSION
LT_CURRENT=$VEEJAYCORE_MAJOR_VERSION
LT_REVISION=$VEEJAYCORE_MINOR_VERSION
LT_AGE=$VEEJAYCORE_MAJOR_VERSION
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)
dnl **********************************************************************
dnl Options

#AC_CONFIG_MACRO_DIR([m4])

dnl enable auto vectorization for libvjmem, libvje and libyuv

AC_ARG_ENABLE([strict],
		AS_HELP_STRING([--enable-strict],[Compile in paranoia assertion checking]))

AC_ARG_ENABLE([sanitizer],
        AS_HELP_STRING([--enable-sanitizer],[Enable AddressSanitizer]))

AC_ARG_ENABLE([portleak],
	      	AS_HELP_STRING([--enable-portleak],[Compile in vevo port validation (requires --enable-strict)]))

AC_ARG_ENABLE([debug],
		AS_HELP_STRING([--enable-debug],[Compile in debugging information]))
AC_ARG_ENABLE([threadsanitizer],
	      	AS_HELP_STRING([--enable-threadsanitizer],[Compile with -fsanitize=thread]))
AC_ARG_ENABLE([profile],
		AS_HELP_STRING([--enable-profiling],[Compile in profiling information]))
AC_ARG_ENABLE([strict],
		AS_HELP_STRING([--enable-strict],[Compile in strict checking]))
AC_ARG_ENABLE([compile-warnings],     
              AS_HELP_STRING([--enable-compile-warnings],[Turn on compiler warnings.]))
AC_ARG_ENABLE([warnings_as_errors],   
              AS_HELP_STRING([--enable-warnings_as_errors],[Compiler warnings are errors.]))
AC_ARG_WITH([extra-cflags],
            AS_HELP_STRING([--with-extra-cflags=flags],[Options to be added to CFLAGS (optional)]))
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

AC_CHECK_HEADERS([fenv.h stdint.h inttypes.h sys/types.h alloca.h sys/time.h sys/times.h])

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


AC_TYPE_OFF_T
AC_CHECK_TYPE(ptrdiff_t, long)
AC_TYPE_SIZE_T

AC_SEARCH_LIBS([clock_gettime], [rt],
	       [AC_DEFINE(HAVE_POSIX_TIMERS,[1],[Set to 1 if clock_gettime and POSIX timers are available])])


AC_SUBST(LIBGETOPT_LIB)

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

dnl e_xdpms=no
dnl ACCHECK_HEADER(X11/extensions/dpms.h,
dnl	AC_DEFINE( HAVE_XDPMS, 1, [If we have xpdms] ) have_xdpms=yes,,
dnl	[])

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
    AC_CACHE_CHECK([whether the compiler supports isolated SSE4.1 functions],
        [ac_cv_target_sse4_1], [
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <smmintrin.h>
__attribute__((target("sse4.1")))
static int sse4_1_target_test(int input)
{
    __m128i value = _mm_set1_epi32(input);
    return _mm_extract_epi32(value, 0);
}
        ]], [[return sse4_1_target_test(0);]])],
            [ac_cv_target_sse4_1=yes], [ac_cv_target_sse4_1=no])
    ])
    if test x$ac_cv_target_sse4_1 = xyes; then
        AC_DEFINE(HAVE_TARGET_SSE4_1,1,
            [Compiler supports isolated functions targeting SSE4.1])
    fi

    AC_CACHE_CHECK([whether the compiler supports isolated SSE4.2 functions],
        [ac_cv_target_sse4_2], [
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <nmmintrin.h>
__attribute__((target("sse4.2")))
static unsigned int sse4_2_target_test(unsigned int input)
{
    return _mm_crc32_u32(0, input);
}
        ]], [[return sse4_2_target_test(0);]])],
            [ac_cv_target_sse4_2=yes], [ac_cv_target_sse4_2=no])
    ])
    if test x$ac_cv_target_sse4_2 = xyes; then
        AC_DEFINE(HAVE_TARGET_SSE4_2,1,
            [Compiler supports isolated functions targeting SSE4.2])
    fi

    AC_CACHE_CHECK([whether the compiler supports isolated AVX functions],
        [ac_cv_target_avx], [
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <immintrin.h>
__attribute__((target("avx")))
static int avx_target_test(const int *input)
{
    __m256i value = _mm256_loadu_si256((const __m256i *) input);
    return _mm256_extract_epi32(value, 0);
}
        ]], [[
            int input[8] = { 0 };
            return avx_target_test(input);
        ]])], [ac_cv_target_avx=yes], [ac_cv_target_avx=no])
    ])
    if test x$ac_cv_target_avx = xyes; then
        AC_DEFINE(HAVE_TARGET_AVX,1,
            [Compiler supports isolated functions targeting AVX])
    fi

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
AC_SUBST(LIBPNG_LIBS)
AC_SUBST(LIBPNG_CFLAGS)

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED,,
                    [Defined if building against uninstalled FFmpeg source])

PKG_CHECK_MODULES(LIBAVUTIL, [libavutil >= 58],have_avutil=true,have_avutil=false)
PKG_CHECK_MODULES(LIBAVCODEC,[libavcodec >= 60],have_avcodec=true,have_avcodec=false)
PKG_CHECK_MODULES(LIBAVFORMAT,[libavformat >= 60],have_avformat=true,have_avformat=false)
PKG_CHECK_MODULES(LIBSWSCALE,[libswscale >= 7],have_swscale=true,have_swscale=false)

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

FFMPEG_CFLAGS="${LIBAVFORMAT_CFLAGS} ${LIBAVCODEC_CFLAGS} ${LIBAVUTIL_CFLAGS} ${LIBSWSCALE_CFLAGS}"
FFMPEG_LIBS="${LIBAVFORMAT_LIBS} ${LIBAVCODEC_LIBS} ${LIBAVUTIL_LIBS} ${LIBSWSCALE_LIBS}"
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)

AC_SUBST(LIBM_LIBS)

dnl the check doesnt work on ubuntu, so just fake it.
RT_LIBS="-lrt"
AC_CHECK_LIB( rt, clock_gettime, [
	AC_DEFINE( HAVE_CLOCK_GETTIME, 1, [clock_gettime] )
	RT_LIBS="-lrt" ])
AC_SUBST(RT_LIBS)

AC_CHECK_LIB([pthread], [pthread_create], [PTHREAD_LIBS="-lpthread"], [], [])
AC_SUBST([PTHREAD_LIBS])

if test "x$PTHREAD_LIBS" != "x"; then
  AC_DEFINE([HAVE_LIBPTHREAD], [1], [Define to 1 if you have the pthread library (-lpthread).])
  have_pthread=true
else
  have_pthread=false
fi

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,1,[Compile with dlopen support])
 have_dl_dlopen=true
fi

dnl ********************************************************************
dnl Check to see if __progname is provided by the system
dnl ********************************************************************
AC_CACHE_CHECK([for __progname],
               [mjt_cv_extern___progname],
               [AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
                                                [[extern char *__progname;
                                                  puts(__progname);]])],
                               [mjt_cv_extern___progname=yes],
			       [mjt_cv_extern___progname=no])])
if test x$mjt_cv_extern___progname = xyes ; then
  AC_DEFINE(HAVE___PROGNAME, 1, [Is __progname defined by system?])
fi

dnl **********************************************************************
dnl All the conditional stuff for the Makefiles
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_ARMV7A, test x$have_armv7a = xtrue)

dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

veejaycore_cflags_without_optimization=""
for flag in $CFLAGS; do
    case $flag in
        -O*|-g*) ;;
        *) veejaycore_cflags_without_optimization="$veejaycore_cflags_without_optimization $flag" ;;
    esac
done
veejaycore_cxxflags_without_optimization=""
for flag in $CXXFLAGS; do
    case $flag in
        -O*|-g*) ;;
        *) veejaycore_cxxflags_without_optimization="$veejaycore_cxxflags_without_optimization $flag" ;;
    esac
done
CFLAGS="$veejaycore_cflags_without_optimization -O3"
CXXFLAGS="$veejaycore_cxxflags_without_optimization -O3"

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
thirdparty/mjpegtools/Makefile
thirdparty/fastarm/Makefile
thirdparty/libhash/Makefile
thirdparty/liblzo/Makefile
libvjmsg/Makefile
libvevo/Makefile
libvjmem/Makefile
libvjnet/Makefile
libyuv/Makefile
veejaycore/Makefile
veejaycore.pc
])


AC_DEFINE_UNQUOTED([VERSION], ["$VERSION"], [Veejay version number])

AC_MSG_CHECKING([for git commit hash])
GIT_HASH=`git rev-parse --short HEAD 2>/dev/null || echo "unknown"`
AC_MSG_RESULT([$GIT_HASH])
AH_TEMPLATE([GIT_HASH_VEEJAYCORE], [git commit hash libveejaycore])
AC_DEFINE_UNQUOTED([GIT_HASH_VEEJAYCORE], ["$GIT_HASH"], [git commit hash libveejaycore])


AC_OUTPUT

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay-Core ${VERSION} build configuration :])
AC_MSG_NOTICE([])


AC_MSG_NOTICE([ Compiler flags: $VEEJAY_CFLAGS])
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
AC_MSG_NOTICE([    SSE4.1              : ${ac_cv_target_sse4_1}])
AC_MSG_NOTICE([    SSE4.2              : ${ac_cv_target_sse4_2}])
AC_MSG_NOTICE([    AVX                 : ${ac_cv_target_avx}])
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
AC_MSG_NOTICE([  - POSIX Threads (pthread)       : ${have_pthread}])
AC_MSG_NOTICE([  - FFmpeg/AV AVFormat            : ${have_avformat} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVCodec             : ${have_avcodec} ])
AC_MSG_NOTICE([  - FFmpeg/AV Swscaler            : ${have_swscale} ])
AC_MSG_NOTICE([  - FFmpeg/AV AVUtil              : ${have_avutil} ])

