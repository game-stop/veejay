dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejaycore],[1.6.0],[http://www.veejayhq.net])
AC_PREREQ([2.69])
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


USER_CFLAGS="$CFLAGS"
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
arch_target="auto"
AC_ARG_WITH(arch-target, AS_HELP_STRING( [--with-arch-target=generic, auto or user defined],
					[Build a generic binary, auto-detect current cpu type or user defined -mtune/-march setting)]),
					[ arch_target="$withval"])

AC_USE_SYSTEM_EXTENSIONS

dnl Initialize libtool
LT_INIT

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
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

CFLAGS=""
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

have_asm_nasm=false
have_asm_mmx=false
have_asm_sse=false
have_asm_sse2=false
have_asm_sse4_1=false
have_asm_sse4_2=false
have_asm_mmx2=false
have_asm_3dnow=false
have_asm_avx=false
have_asm_avx2=false
have_asm_avx512=false
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
        AC_DEFINE(HAVE_X86CPU,1,[Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

        if test "x$enable_debug" != "xyes"; then
            VEEJAY_CFLAGS="-O3 -ftree-vectorize -ffast-math -fopenmp"
        else
            VEEJAY_CFLAGS="-g -Wall"
        fi

	AC_MSG_RESULT([x86])
    ;;

  x86_64*|k8-*)
        AC_DEFINE(HAVE_X86_CPU,1,[Compiling for x86-64 architecture CPU])
        AC_DEFINE(ARCH_X86_64,1,[Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true

        VEEJAY_CFLAGS="-m64 "
        if test "x$enable_debug" != "xyes"; then
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ftree-vectorize -ffast-math -fopenmp"
        else
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -g -Wall"
        fi

        CFLAGS="$CFLAGS -m64 -fopenmp"

	AC_MSG_RESULT([x86_64])
    ;;

    powerpc|powerpc64)
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

    aarch64*)
     	 AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	 have_arm=true


        if test "x$enable_debug" != "xyes"; then
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ffast-math -ftree-vectorize -fopenmp"
        else
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -g -Wall -fopenmp"
        fi

        AC_MSG_RESULT([aarch64])
     ;;

    arm|armv7l)
         AC_DEFINE(HAVE_ARM,1,[Compiling for ARM CPU])
	 have_arm=true
        have_armv7a=true

        if test "x$enable_debug" != "xyes"; then
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ffast-math -ftree-vectorize -fopenmp"
        else
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -g -Wall -fopenmp"
        fi

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

if test x$host_alias != x; then
	if test x$have_x86cpu = xtrue; then
        host_mod_cpu=`echo $host_cpu | tr _ -`
		ARCHFLAGS="-march=$host_mod_cpu -mcpu=$host_mod_cpu"
		AC_MSG_RESULT($ARCHFLAGS)
	fi
else
	chmod +x $srcdir/cpuinfo.sh

	if test "$arch_target" = "auto"; then
		TMP=`$srcdir/cpuinfo.sh`
		ARCHFLAGS=`cat veejay.arch`
	else
       	ARCHFLAGS="-mtune=$arch_target"
	fi
		AC_MSG_RESULT($ARCHFLAGS)
fi

if test x$have_x86cpu = xtrue; then

    # MMX
    AC_CACHE_CHECK(for MMX on processor(s), ac_cv_flag_mmx, [
        if grep "^flags.* mmx" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_mmx=yes
		    else
            ac_cv_flag_mmx=no
		    fi
	    ])
	    if test $ac_cv_flag_mmx = yes; then
       		AC_DEFINE(HAVE_ASM_MMX,1,[Compiling in MMX support])
		    AC_DEFINE(HAVE_MMX,1,[Compiling in MMX support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -mmmx"
	    fi

    # SSE
    AC_CACHE_CHECK(for SSE on processor(s), ac_cv_flag_sse, [
        if grep "^flags.* sse" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_sse=yes
        else
            ac_cv_flag_sse=no
        fi
    ])
	    if test $ac_cv_flag_sse = yes; then
		    AC_DEFINE(HAVE_ASM_SSE,1,[Compiling in SSE support])
		    AC_DEFINE(HAVE_SSE,1,[Compiling in SSE support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -msse"
	    fi

    # SSE2
    AC_CACHE_CHECK(for SSE2 on processor(s), ac_cv_flag_sse2, [
        if grep "^flags.* sse2" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_sse2=yes
        else
            ac_cv_flag_sse2=no
        fi
    ])
	    if test $ac_cv_flag_sse2 = yes; then
		    AC_DEFINE(HAVE_ASM_SSE2,1,[Compiling in SSE2 support])
		    AC_DEFINE(HAVE_SSE2,1,[Compiling in SSE2 support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -msse2"
	    fi

    # SSE4.1
    AC_CACHE_CHECK(for SSE4_1 on processor(s), ac_cv_flag_sse4_1, [
        if grep "^flags.* sse4_1" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_sse4_1=yes
        else
            ac_cv_flag_sse4_1=no
        fi
    ])
    if test $ac_cv_flag_sse4_1 = yes; then
        AC_DEFINE(HAVE_ASM_SSE4_1,1,[Compiling in SSE4_1 support])
        AC_DEFINE(HAVE_SSE4_1,1,[Compiling in SSE4_1 support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -msse4.1"
    fi

    # SSE4.2
    AC_CACHE_CHECK(for SSE4_2 on processor(s), ac_cv_flag_sse4_2, [
        if grep "^flags.* sse4_2" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_sse4_2=yes
        else
            ac_cv_flag_sse4_2=no
        fi
    ])
	    if test $ac_cv_flag_sse4_2 = yes; then
		    AC_DEFINE(HAVE_ASM_SSE4_2,1,[Compiling in SSE4_2 support])
		    AC_DEFINE(HAVE_SSE4_2,1,[Compiling in SSE4_2 support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -msse4.2"
	    fi

    # 3DNOW
    AC_CACHE_CHECK(for 3DNOW on processor(s), ac_cv_flag_3dnow, [
        if grep "^flags.*\b3dnow\b" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_3dnow=yes
        else
            ac_cv_flag_3dnow=no
        fi
    ])
	    if test $ac_cv_flag_3dnow = yes; then
	    	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],
				[[__asm__ __volatile__("femms":::"memory");]])],
		    	[have_asm_3dnow=true],
		    	[have_asm_3dnow=false])
		    if test $have_asm_3dnow = true; then
            AC_DEFINE(HAVE_ASM_3DNOW,1,[Compiling in 3DNow])
            VEEJAY_CFLAGS="$VEEJAY_CFLAGS -m3dnow"
    		fi
    	fi

    # CMOV
    AC_CACHE_CHECK(for CMOV on processor(s), ac_cv_flag_cmov, [
        if grep "^flags.* cmov" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_cmov=yes
        else
            ac_cv_flag_cmov=no
        fi
    ])
        if test $ac_cv_flag_cmov = yes; then
	    	AC_DEFINE(HAVE_CMOV,1,[Compiling in CMOV])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -DCMOV"
	    fi

    # MMX2 / MMXEXT
    AC_CACHE_CHECK(for MMX2 on processor(s), ac_cv_flag_mmx2, [
        if grep "^flags.* mmxext" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_mmx2=yes
        else
            ac_cv_flag_mmx2=no
        fi
    ])
	    if test $ac_cv_flag_mmx2 = yes; then
	    	AC_DEFINE(HAVE_ASM_MMX2,1,[Compiling in MMX2])
	    	AC_DEFINE(HAVE_ASM_MMXEXT,1,[Compiling in MMXEXT])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -mmmxext"
	    fi   

    # AVX
    AC_CACHE_CHECK(for AVX on processor(s), ac_cv_flag_avx, [
        if grep "^flags.* avx" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_avx=yes
        else
            ac_cv_flag_avx=no
        fi
    ])
	    if test $ac_cv_flag_avx = yes; then
	    	AC_DEFINE(HAVE_ASM_AVX,1,[Compiling in AVX])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -mavx"
	    fi

    # AVX2
    AC_CACHE_CHECK(for AVX2 on processor(s), ac_cv_flag_avx2, [
        if grep "^flags.* avx2" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_avx2=yes
        else
            ac_cv_flag_avx2=no
        fi
    ])
    if test $ac_cv_flag_avx2 = yes; then
        AC_DEFINE(HAVE_ASM_AVX2,1,[Compiling in AVX2])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -mavx2"
    fi

    # AVX-512
    AC_CACHE_CHECK(for AVX-512 on processor(s), ac_cv_flag_avx512, [
        if grep "^flags.* avx512" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_avx512=yes
        else
            ac_cv_flag_avx512=no
        fi
    ])
    if test $ac_cv_flag_avx512 = yes; then
        AC_DEFINE(HAVE_ASM_AVX512,1,[Compiling in AVX-512])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -mavx512f"
    fi
fi

if test x$have_arm = xtrue; then

    # NEON support
    if test x$have_armv7a = xtrue; then
        AC_CACHE_CHECK(for NEON on processor(s), ac_cv_flag_neon, [
            if grep "^Features.* neon" /proc/cpuinfo > /dev/null; then
                ac_cv_flag_neon=yes
            else
                ac_cv_flag_neon=no
            fi
        ])

        if test $ac_cv_flag_neon = yes; then
            AC_DEFINE(HAVE_ARM_NEON,1,[Compiling in NEON support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ftree-vectorize -mfpu=neon -mvectorize-with-neon-quad -march=armv7-a -mthumb -mimplicit-it=always -mthumb-interwork -DCONFIG_THUMB"
        else
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ftree-vectorize -march=native"
        fi
    fi

    AC_CACHE_CHECK(for ASIMD on processor(s), ac_cv_flag_asimd, [
        if grep "^Features.* asimd" /proc/cpuinfo > /dev/null; then
            ac_cv_flag_asimd=yes
		    else
            ac_cv_flag_asimd=no
	    	fi
	    ])	
    
    if test $ac_cv_flag_asimd = yes; then
        AC_DEFINE(HAVE_ARM_ASIMD,1,[Compiling in ASIMD support])
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ftree-vectorize  -ffast-math"
    else
        VEEJAY_CFLAGS="$VEEJAY_CFLAGS -O3 -ftree-vectorize "
    fi
fi

if test x$have_ppccpu = xtrue; then

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


VEEJAY_CFLAGS="$VEEJAY_CFLAGS $ARCHFLAGS $warnCFLAGS"
VEEJAY_CXXFLAGS="$VEEJAY_CFLAGS $ARCHFLAGS $warnCXXFLAGS"

AC_MSG_NOTICE([Final VEEJAY_CFLAGS: $VEEJAY_CFLAGS])
AC_MSG_NOTICE([Final VEEJAY_CXXFLAGS: $VEEJAY_CXXFLAGS])


AC_SUBST(VEEJAY_CFLAGS)
AC_SUBST(PROGRAM_NOPIC)


PKG_CHECK_MODULES(LIBQRENCODE,[libqrencode >= 3.4.2], have_qrencode=true,have_qrencode=false)
if test x$have_qrencode = xfalse;
then
	AC_MSG_WARN([libqrencode is not present])
else
	if test x$have_libpng = xfalse;
	then
		AC_MSG_ERROR([libpng is not present])
	fi
	AC_DEFINE( HAVE_QRENCODE,1,[Use QREncode] )
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

PKG_CHECK_MODULES(LIBAVUTIL, [libavutil >= 49.7.0],have_avutil=true,have_avutil=false)
PKG_CHECK_MODULES(LIBAVCODEC,[libavcodec >= 51.35.2],have_avcodec=true,have_avcodec=false)
PKG_CHECK_MODULES(LIBAVFORMAT,[libavformat >= 52.14.0],have_avformat=true,have_avformat=false)
PKG_CHECK_MODULES(LIBSWSCALE,[libswscale >= 0.7.1],have_swscale=true,have_swscale=false)
PKG_CHECK_MODULES(LIBSWRESAMPLE,[libswresample >= 0.7.1],have_swresample=true,have_swresample=false)

if test x$have_swscale = xfalse;
then
	AC_MSG_ERROR([libswscale not found.])
fi
if test x$have_avutil = xfalse;
then
	AC_MSG_ERROR([libavutil not found.])
fi
if test x$have_avformat = xfalse;
then
	AC_MSG_ERROR([libavformat not found.])
fi
if test x$have_avcodec = xfalse;
then
	AC_MSG_ERROR([libavcodec >= 51.57 not found.])
fi

FFMPEG_CFLAGS="${LIBAVFORMAT_CFLAGS} ${LIBAVCODEC_CFLAGS} ${LIBAVUTIL_CFLAGS} ${LIBSWSCALE_CFLAGS} ${LIBSWRESAMPLE_CFLAGS}"
FFMPEG_LIBS="${LIBAVFORMAT_LIBS} ${LIBAVCODEC_LIBS} ${LIBAVUTIL_LIBS} ${LIBSWSCALE_LIBS} ${LIBSWRESAMPLE_LIBS}"
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
AM_CONDITIONAL(HAVE_ASM_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_X86CPU, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(HAVE_PPCCPU, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(ARCH_PPC, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(ARCH_X86, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(ARCH_X86_64, test x$have_x86_64cpu = xtrue)
AM_CONDITIONAL(HAVE_ALTIVEC, test x$have_altivec = xtrue)
AM_CONDITIONAL(HAVE_ASM_MMX2, test x$have_asm_mmx2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE4_2, test x$have_asm_sse4_2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE4_1, test x$have_asm_sse4_1 = xtrue)
AM_CONDITIONAL(HAVE_ASM_AVX, test x$have_asm_avx = xtrue)
AM_CONDITIONAL(HAVE_ASM_AVX2, test x$have_asm_avx2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_NASM, test x$have_asm_nasm = xtrue)
AM_CONDITIONAL(HAVE_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_ARM, test x$have_arm = xtrue )
AM_CONDITIONAL(HAVE_ARM_NEON, test x$ac_cv_flag_neon = xtrue )
AM_CONDITIONAL(HAVE_ARM_ASIMD, test x$ac_cv_flag_asimd = xtrue )
AM_CONDITIONAL(HAVE_ARMV7A, test x$have_armv7a = xtrue)

dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

CFLAGS="$ARCHFLAGS $CFLAGS"
CXXFLAGS="$ARCHFLAGS $CXXFLAGS"

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
AC_MSG_NOTICE([   x86  ])
AC_MSG_NOTICE([    MMX     enabled     : ${ac_cv_flag_mmx}])
AC_MSG_NOTICE([    MMX2    enabled     : ${ac_cv_flag_mmx2}])
AC_MSG_NOTICE([    SSE     enabled     : ${ac_cv_flag_sse}])
AC_MSG_NOTICE([    SSE2    enabled     : ${ac_cv_flag_sse2}])
AC_MSG_NOTICE([    SSE4_2  enabled     : ${ac_cv_flag_sse4_2}])
AC_MSG_NOTICE([    SSE4_1  enabled     : ${ac_cv_flag_sse4_1}])
AC_MSG_NOTICE([    3DNOW   enabled     : ${ac_cv_flag_3dnow}])
AC_MSG_NOTICE([    CMOV    enabled     : ${ac_cv_flag_cmov}])
AC_MSG_NOTICE([    AVX     enabled     : ${ac_cv_flag_avx}])
AC_MSG_NOTICE([    AVX2    enabled     : ${ac_cv_flag_avx2}])
AC_MSG_NOTICE([    AVX-512 enabled     : ${ac_cv_flag_avx512}])
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

