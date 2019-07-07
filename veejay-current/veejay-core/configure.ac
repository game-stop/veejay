dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejaycore],[1.5.65],[http://www.veejayhq.net])
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([veejaycore/core.c])

VEEJAYCORE_MAJOR_VERSION=1
VEEJAYCORE_MINOR_VERSION=5
VEEJAYCORE_MICRO_VERSION=65
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

AC_CONFIG_MACRO_DIR([m4])


dnl kill CFLAGS
USER_CFLAGS="$CFLAGS"
CFLAGS=""
dnl enable auto vectorization for libvjmem, libvje and libyuv

AC_ARG_ENABLE([strict],
		AC_HELP_STRING([--enable-strict],
			     [Compile in paranoia assertion checking]))

AC_ARG_ENABLE([sanitizer],
        AC_HELP_STRING([--enable-sanitizer],
                 [Enable AddressSanitizer]))

AC_ARG_ENABLE([portleak],
	      	AC_HELP_STRING([--enable-portleak],
			      [Compile in vevo port validation (requires --enable-strict)]))

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
AC_ARG_WITH([extra-cflags],
            AC_HELP_STRING([--with-extra-cflags=flags],
                           [Options to be added to CFLAGS (optional)]))
arch_target="auto"
AC_ARG_WITH(arch-target, AS_HELP_STRING( [--with-arch-target=generic, auto or user defined],
					[Build a generic binary, auto-detect current cpu type or user defined -mtune/-march setting)]),
					[ arch_target="$withval"])

AC_GNU_SOURCE

dnl Initialize libtool
AC_PROG_LIBTOOL

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
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

AC_SEARCH_LIBS([clock_gettime], [rt],
	       [AC_DEFINE(HAVE_POSIX_TIMERS,[1],[Set to 1 if clock_gettime and POSIX timers are available])])


AC_SUBST(LIBGETOPT_LIB)

AC_TYPE_SIGNAL

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

dnl e_xdpms=no
dnl ACCHECK_HEADER(X11/extensions/dpms.h,
dnl	AC_DEFINE( HAVE_XDPMS, 1, [If we have xpdms] ) have_xdpms=yes,,
dnl	[])

have_linux=false
have_darwin=false
case "${host_os}" in
  linux*)
	AC_CHECK_HEADER(linux/version.h,
		[have_linux=true
	         AC_DEFINE(HAVE_LINUX,1,[Linux platform])
		],
		[AC_MSG_ERROR([version.h not found - please install the linux kernel headers])
		])
		LIBM_LIBS="-lm"
	;;
   darwin*)
		AC_DEFINE(HAVE_DARWIN,1,[MAC OS X Darwin])
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

AC_MSG_CHECKING(whether to compile in debugging information)
debugCFLAGS=""
if test "x$enable_debug" = "xyes" ; then
 debugCFLAGS="-g"
 CFLAGS="$CFLAGS $debugCFLAGS"
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
if test "x$enable_sanitizer" = "xyes" ; then
 debugCFLAGS="$debugCFLAGS -fsanitize=address -fno-omit-frame-pointer"
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi
AC_MSG_CHECKING(whether to compile in vevo port tracking)
if test "x$enable_portleak" = "xyes"; then
 debugCFLAGS="$debugCFLAGS -DVEVO_TRACKPORTS"
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi

dnl ********************************************************************
dnl Test for MMX support if an IA32 platform.  If on a PPC then look for
dnl Altivec support.  For the IA32 platform see if gcc understands inline 
dnl MMX instructions.
dnl
dnl TODO: host = non intel, target = intel <blah> do the checks also?
dnl       Remember we are checking that the toolchain can generate the
dnl       code, not whether the host can execute the code, thats done
dnl       at run time with the exception of the SSE code.


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

OP_CFLAGS=""
VJE_CFLAGS=""
SUBSAMPLE_CFLAGS=""

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

	if test "x$enable_debug" != "xyes" ; then
		SUBSAMPLE_CFLAGS=""
		VJE_CFLAGS="-O3 -ftree-vectorize -ffast-math"
	fi

	AC_MSG_RESULT([x86])
    ;;
  x86_64*|k8-*)
    	AC_DEFINE(HAVE_X86_CPU,1, [Compiling for x86-64 architecture CPU])
    	AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true
	if test "x$enable_debug" != "xyes" ; then
		VJE_CFLAGS="-O3 -ffast-math -ftree-vectorize -m64 -fPIC -DPIC"
		SUBSAMPLE_CFLAGS="-m64 -fPIC -DPIC"
	fi
	CFLAGS="$CFLAGS -m64 -fPIC -DPIC"

	AC_MSG_RESULT([x86_64])
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
	;;

  *)

dnl If you get here, you can change AC_MSG_ERROR to AC_MSG_RESULT
dnl and uncomment setting the CFLAGS below to have it compile anyway on your system
	AC_MSG_ERROR([unknown])

dnl	OP_CFLAGS="$USER_CFLAGS"
dnl	SUBSAMPLE_CFLAGS="$USER_CFLAGS"
dnl	VJE_CFLAGS="$USER_CFLAGS"
dnl	CFLAGS="$USER_CFLAGS"

    ;;
esac

CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES"

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
       		ARCHFLAGS="-mtune=$arch_target"
	fi
		AC_MSG_RESULT($ARCHFLAGS)
fi

dnl ARM architecture detect NEON and set CFLAGS
if test x$have_arm = xtrue
then
	AC_CACHE_CHECK(for NEON on processor(s), ac_cv_flag_neon, [
		if grep "^Features.* neon" /proc/cpuinfo > /dev/null; then
			ac_cv_flag_neon=yes
		else
			ac_cv_flag_neon=no
		fi
	])
	AC_MSG_RESULT($ac_cv_flag_neon)	

	if test $ac_cv_flag_neon = yes ; then
		AC_DEFINE(HAVE_ARM_NEON,1,[Compiling in NEON support])
		USER_CFLAGS="-mfpu=neon -ftree-vectorize -mvectorize-with-neon-quad $USER_CFLAGS"
		FASTARM_CFLAGS="$ARCHFLAGS -Wa,-march=armv7-a -mthumb -Wa,-mthumb -Wa,-mimplicit-it=always -mthumb-interwork -DCONFIG_THUMB"

	else
		USER_CFLAGS="-march=native -ftree-vectorize $USER_CFLAGS"
		FASTARM_CFLAGS="$ARCHFLAGS -Wa, -mthumb -Wa,-mthumb -Wa,-mimplicit-it=always -mthumb-interwork -DCONFIG_THUMB"
	fi
	
	if test "x$enable_debug" != "xyes" ; then
		USER_CFLAGS="$USER_CFLAGS -O3 -fno-stack-protector"
	else
		USER_CFLAGS="$USER_CFLAGS -g -Wall"
	fi

	OP_CFLAGS="$USER_CFLAGS"
	SUBSAMPLE_CFLAGS="$USER_CFLAGS"
	VJE_CFLAGS="$USER_CFLAGS"
	CFLAGS="$USER_CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES"

	AC_SUBST(FASTARM_CFLAGS)
fi

dnl This flag is used for PROGRAMS not SHARED LIBRARIES.  PIC code is required
dnl for shared libraries but is slower than non-pic code.  Compute bound 
dnl programs such as yuvdenoise and y4mspatialfilter can use this flag by
dnl simply adding programname_CFLAGS=@PROGRAM_NOPIC@ to the Makefile.am

AC_SUBST(PROGRAM_NOPIC)

if test x$have_x86cpu = xtrue
then
    if test x$have_darwin = xtrue
    then
        dnl Modern OSx, has MMX, SSE, SSE2 and SSE3
        dnl This is not really the right way to do it, but should work for most systems.
        ac_cv_flag_mmx=yes
        ac_cv_flag_sse=yes
        ac_cv_flag_sse2=yes
        ac_cv_flag_cmov=yes

        AC_DEFINE(HAVE_ASM_MMX,1,[Compiling in MMX support])
		AC_DEFINE(HAVE_MMX,1,[Compiling in MMX support])
       	have_asm_mmx=true

		AC_DEFINE(HAVE_ASM_SSE,1,[Compiling in SSE support])
		AC_DEFINE(HAVE_SSE,1,[Compiling in SSE support])
		have_asm_sse=true
        
        AC_DEFINE(HAVE_ASM_SSE2,1,[Compiling in SSE2 support])
		AC_DEFINE(HAVE_SSE2,1,[Compiling in SSE2 support])
		have_asm_sse2=true
        
        AC_DEFINE(HAVE_CMOV,1,[Compiling in CMOV])
	    have_cmov=true
    fi

    if test x$have_linux = xtrue
    then
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
       		have_asm_mmx=true
	    fi

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
		    have_asm_sse=true
	    fi

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
		    have_asm_sse2=true
	    fi

	    AC_CACHE_CHECK(for 3DNOW on processor(s), ac_cv_flag_3dnow, [
	    	if grep "^flags.* 3dnow" /proc/cpuinfo > /dev/null; then
	    		ac_cv_flag_3dnow=yes
	    	else
	    		ac_cv_flag_3dnow=no
	    	fi
	    ])

	    if test $ac_cv_flag_3dnow = yes; then
	    	AC_MSG_CHECKING([if your CPU understands 3DNOW femms])
	    	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]],
				[[__asm__ __volatile__("femms":::"memory");]])],
		    	[have_asm_3dnow=true],
		    	[have_asm_3dnow=false])
		    if test $have_asm_3dnow = true; then
		    	AC_MSG_RESULT(yes)
	    		AC_DEFINE(HAVE_ASM_3DNOW,1,[Compiling in 3Dnow])
    			have_asm_3dnow=true
    		else
    			AC_MSG_RESULT(no)
    		fi
    	fi

    	AC_CACHE_CHECK(for CMOV on processor(s), ac_cv_flag_cmov, [
    		if grep "^flags.* cmov" /proc/cpuinfo > /dev/null; then
    			ac_cv_flag_cmov=yes
    		else
    			ac_cv_flag_cmov=no
    		fi
    	])

        if test $ac_cv_flag_cmov = yes; then
	    	AC_DEFINE(HAVE_CMOV,1,[Compiling in CMOV])
	    	have_cmov=true
	    fi


	    dnl check for MMX2
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
	    	have_asm_mmx2=true
	    fi   

	    dnl check for AVX
	    AC_CACHE_CHECK(for AVX on processor(s), ac_cv_flag_avx, [
	    	if grep "^flags.* avx" /proc/cpuinfo > /dev/null; then
	     		ac_cv_flag_avx=yes
		    else
	    		ac_cv_flag_avx=no
	    	fi
	    ])

	    if test $ac_cv_flag_avx = yes; then
	    	AC_DEFINE(HAVE_ASM_AVX,1,[Compiling in AVX])
	    	have_asm_avx=true
	    fi
    
    fi
fi

if test x$have_ppccpu = xtrue
then
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
      # Mac OS X style
      have_altivec=true
      ALTIVEC_CFLAGS="-faltivec"
    elif $CC $CFLAGS -fvec -o conftest conftest.c >/dev/null 2>&1; then
      # Motorola style
      have_altivec=true
      ALTIVEC_CFLAGS="-fvec"
    elif $CC $CFLAGS -DHAVE_ALTIVEC_H=1 -maltivec -mabi=altivec -o conftest conftest.c >/dev/null 2>&1; then
      # gcc 3.1 style
      have_altivec=true
      ALTIVEC_CFLAGS="-DHAVE_ALTIVEC_H=1 -maltivec -mabi=altivec"
    fi
    rm -f conftest*;
 
    AC_MSG_CHECKING([if your system supports altivec]) 
    if test x$have_altivec = xtrue
    then
      # add -O3 flag
      ALTIVEC_CFLAGS="-O3 ${ALTIVEC_CFLAGS}"
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_ALTIVEC,1,
				[Inline PPC Altivec primitives available])
      AC_SUBST(ALTIVEC_CFLAGS)
    else
      AC_MSG_RESULT(no)
    fi
fi

PKG_CHECK_MODULES(LIBPNG, [libpng], have_libpng=true,have_libpng=false)
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

AC_SUBST(LIBM_LIBS)

dnl the check doesnt work on ubuntu, so just fake it.
RT_LIBS="-lrt"
AC_CHECK_LIB( rt, clock_gettime, [
	AC_DEFINE( HAVE_CLOCK_GETTIME, 1, [clock_gettime] )
	RT_LIBS="-lrt" ])
AC_SUBST(RT_LIBS)

dnl Check for the pthread lib
dnl
AC_SUBST(PTHREAD_LIBS)
have_pthread=false
AC_CHECK_LIB(pthread, pthread_create,
  [ PTHREAD_LIBS="-lpthread" 
    AC_DEFINE(HAVE_LIBPTHREAD,1,[Compiling with pthread library])
    have_pthread=true ],,)

if test x$have_pthread != xtrue ; then
	AC_MSG_ERROR([Cannot find pthread library])
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
AM_CONDITIONAL(HAVE_ASM_NASM, test x$have_asm_nasm = xtrue)
AM_CONDITIONAL(HAVE_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_ARM, test x$have_arm = xtrue )

dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

CFLAGS="$ARCHFLAGS $CFLAGS"
CXXFLAGS="$ARCHFLAGS $CXXFLAGS"
OP_CFLAGS="$VJE_CFLAGS"

AC_SUBST(OP_CFLAGS)

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
	VJE_CFLAGS="$debugCFLAGS $warnCFLAGS $VJE_CFLAGS"
	OP_CFLAGS="$debugCFLAGS $warnCFLAGS $OP_CFLAGS"
	SUBSAMPLE_CFLAGS="$debugCFLAGS $warnCFLAGS $SUBSAMPLE_CFLAGS"
  else
 	CFLAGS="$CFLAGS $warnCFLAGS"
	VJE_CFLAGS="$debugCFLAGS $VJE_CFLAGS"
	OP_CFLAGS="$debugCFLAGS $OP_CFLAGS"
	SUBSAMPLE_CFLAGS="$debugCFLAGS $SUBSAMPLE_CFLAGS"
  fi
  CXXFLAGS="$CXXFLAGS $warnCXXFLAGS"
  cflags_set=yes
fi

if test "x$enable_debug" != "xyes" ; then
   CFLAGS="$CFLAGS -fno-stack-protector"
fi

if test x"$with_extra_cflags" != "x"; then
   CFLAGS="$CFLAGS $with_extra_cflags"
   CXXFLAGS="$CXXFLAGS $with_extra_cflags"
fi

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
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay-Core ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
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
AC_MSG_NOTICE([    3DNOW   enabled     : ${ac_cv_flag_3dnow}])
AC_MSG_NOTICE([    CMOV    enabled     : ${ac_cv_flag_cmov}])
AC_MSG_NOTICE([    AVX     enabled     : ${ac_cv_flag_avx}])
fi

if test "$have_ppccpu" = "true" ; then
AC_MSG_NOTICE([   ppc])
AC_MSG_NOTICE([    AltiVec enabled     : ${have_altivec}])
fi

if test "$have_arm" = "true" ; then
AC_MSG_NOTICE([   arm])
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

cat NEWS
