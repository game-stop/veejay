dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejay],[1.5.57],[http://www.veejayhq.net])
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([veejay/veejay.c])

VEEJAY_MAJOR_VERSION=1
VEEJAY_MINOR_VERSION=5
VEEJAY_MICRO_VERSION=57
VEEJAY_VERSION=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION.$VEEJAY_MICRO_VERSION
VEEJAY_CODENAME="Veejay Classic - build $VEEJAY_MINOR_VERSION $VEEJAY_MICRO_VERSION"
AC_CONFIG_HEADERS([config.h])
dnl AX_CONFIG_PREFIXED_HEADER([veejay-config.h], [VEEJAY], [config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.7])
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

AC_CONFIG_MACRO_DIR([m4])


dnl kill CFLAGS
CFLAGS=""
dnl enable auto vectorization for libvjmem, libvje and libyuv
OPTCFLAGS="" 

AC_ARG_ENABLE(strict,
		AC_HELP_STRING([--enable-strict],
			     [Compile in paranoia assertion checking]))
AC_ARG_ENABLE(portleak,
	      	AC_HELP_STRING([--enable-portleak],
			      [Compile in vevo port validation (requires --enable-strict)]))

AC_ARG_ENABLE(debug,
		AC_HELP_STRING([--enable-debug],
			     [Compile in debugging information]))
AC_ARG_ENABLE(profile,
		AC_HELP_STRING([--enable-profiling],
			     [Compile in profiling information]))
AC_ARG_ENABLE(compile-warnings,     
              AC_HELP_STRING([--enable-compile-warnings],
                             [Turn on compiler warnings.]))
AC_ARG_ENABLE(warnings_as_errors,   
              AC_HELP_STRING([--enable-warnings_as_errors],
                             [Compiler warnings are errors.]))
AC_ARG_ENABLE(simd-accel,
              AC_HELP_STRING([--enable-simd-accel],
                             [use SIMD multimedia instructions such as MMX, AltiVec, etc.if possible]))

AC_ARG_WITH(extra-cflags,
            AC_HELP_STRING([--with-extra-cflags=flags],
                           [Options to be added to CFLAGS (optional)]))
AC_ARG_WITH(libdv,
            AC_HELP_STRING([--without-libdv], [Do not use libdv.]),
            [], [with_libdv=yes])

AC_ARG_WITH(pixbuf,
            AC_HELP_STRING([--without-pixbuf], [Do not use GDK pixbuf]),
            [], [with_pixbuf=yes])

AC_ARG_WITH(playthread,
            AC_HELP_STRING([--without-playthread], [Do not use playback thread]),
            [], [with_playthread=yes])
	    
AC_ARG_WITH(directfb,
	     AC_HELP_STRING([--without-directfb], [Do not use DirectFB.]),
	 	[], [with_directfb=yes])
AC_ARG_WITH(sdl,
	     AC_HELP_STRING([--without-sdl], [Do not use SDL.]),
	        [], [with_sdl=yes])
AC_ARG_WITH(jack,
		AC_HELP_STRING([--without-jack], [Do not use jack.]),
		[], [with_jack=yes])

AC_ARG_ENABLE(v4l1,
		AC_HELP_STRING([--enable-v4l1],
			     [Compile with video4linux instead of video4linux 2]))

AC_ARG_WITH(xml2,
		AC_HELP_STRING([--without-xml2], [Do not use the XML library for Gnome]),
		[], [with_xml2=yes])

AC_ARG_WITH(jpeg,
		AC_HELP_STRING([--without-jpeg], [Do not JPEG support]),
		[],	[with_libjpeg=yes])

AC_ARG_WITH(jpeg-mmx,
            AC_HELP_STRING([--with-jpegmmx=PFX],
                           [Prefix where jpeg-mmx is installed (optional).]))

AC_ARG_WITH(v4l2, AC_HELP_STRING([--without-v4l2],[Do not use Video4Linux]),
		[],	[with_v4l2=yes])


user_specified_target="generic"
AC_ARG_WITH(arch-target, AS_HELP_STRING( [--with-arch-target=CPU_TYPE],
					 [Set a specific sub-architecture (defaults to native)]),
		[ user_specified_target="$withval" ] )


AC_GNU_SOURCE

dnl Initialize libtool
AC_PROG_LIBTOOL

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
dnl AM_PROG_CC_STDC
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
dnl AX_PREFIXED_DEFINE([VEEJAY], [HAVE_STDINT_H])
dnl AX_PREFIXED_DEFINE([VEEJAY], [HAVE_INTTYPES_H])

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

dnl *********************************************************************
dnl Test whether we're linux or not. If so, define HAVE_V4L and build the
dnl video4linux/hardware dependent tools. Else, undefine HAVE_V4L
have_v4l=false
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

AC_MSG_CHECKING(whether to compile in debugging information)
debugCFLAGS=""
if test "x$enable_debug" = "xyes" ; then
 debugCFLAGS="-g"
 CFLAGS="$CFLAGS $debugCFLAGS"
 OPTCFLAGS="$OPTCFLAGS $debugCFLAGS"
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
AC_MSG_CHECKING(whether to compile in vevo port tracking)
if test "x$enable_portleak" = "xyes"; then
 debugCFLAGS="$debugCFLAGS -DVEVO_TRACKPORTS"
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi

if test "x$with_v4l2" != "xno" ; then
	with_v4l=no
fi


if test "x$enable_v4l1" = "xyes" ; then
	 with_v4l2=no
	 with_v4l=yes
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

OP_CFLAGS=""
SUBSAMPLE_CFLAGS=""
AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

	if test "x$enable_debug" != "xyes" ; then
		OP_CFLAGS="-O2"
		OPTCFLAGS="-O3 -ftree-vectorize"
	fi

	AC_SUBST(OP_CFLAGS)
	SUBSAMPLE_CFLAGS=""
	CFLAGS="$CFLAGS $OP_CFLAGS"
    ;;
  x86_64*|k8-*)
    	AC_DEFINE(HAVE_X86_CPU,1, [Compiling for x86-64 architecture CPU])
    	AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    	have_x86cpu=true
    	have_x86_64cpu=true
	if test "x$enable_debug" != "xyes" ; then
		OP_CFLAGS="-O2"
		OPTCFLAGS="-O3 -m64 -ftree-vectorize -fPIC -DPIC"
 	fi

	AC_SUBST(OP_CFLAGS)
	SUBSAMPLE_CFLAGS="-m64 -fPIC -DPIC"
	CFLAGS="$CFLAGS -m64 -fPIC -DPIC $OP_CFLAGS"
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
    ;;
   mips | mipsel)
	AC_DEFINE(ARCH_MIPS,1,[Compiling for MIPS CPU])
	AC_DEFINE(HAVE_MIPS,1,[Compiling for MIPS CPU])
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

CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES"


dnl This flag is used for PROGRAMS not SHARED LIBRARIES.  PIC code is required
dnl for shared libraries but is slower than non-pic code.  Compute bound 
dnl programs such as yuvdenoise and y4mspatialfilter can use this flag by
dnl simply adding programname_CFLAGS=@PROGRAM_NOPIC@ to the Makefile.am

AC_SUBST(PROGRAM_NOPIC)

if test x$have_x86cpu = xtrue
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


	dnl check for sse2
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
			ac_cv_flag_cmov=yes
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
			ac_cv_flag_mmx2=yes
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
			ac_cv_flag_avx=yes
		fi
	])

	if test $ac_cv_flag_avx = yes; then
		AC_DEFINE(HAVE_ASM_AVX,1,[Compiling in AVX])
		have_asm_avx=true
	fi

dnl	ac_cv_flag_avx=yes
dnl	AC_MSG_CHECKING([for AVX assembler])
dnl	AC_COMPILE_IFELSE(
dnl		[AC_LANG_PROGRAM([[]], 
dnl			[[asm("vmovaps %ymm1,%ymm0");]])],
dnl				[AC_DEFINE(HAVE_ASM_AVX,1,[define if compiler supports avx inline assembler])
dnl					AC_MSG_RESULT(yes)
dnl					ac_cv_flag_avx=no
dnl				],[AC_MSG_RESULT(no)])

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

if test ! -r $srcdir/cpuinfo.sh; then
       AC_MSG_ERROR([cpuinfo.sh script not found - do a cvs update])
fi



if test x$have_x86cpu = xtrue -o x$have_ppccpu = xtrue; then
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
       		ARCHFLAGS=`$srcdir/cpuinfo.sh $user_specified_target`
		AC_MSG_RESULT($ARCHFLAGS)
  	fi
fi


if test x$have_x86cpu = xtrue;
then
	if test "x$enable_debug" != "xyes" ;
	then
		if test x$ac_cv_flag_sse = xyes; then
			CFLAGS="$CFLAGS -msse -mfpmath=sse"
			OPTCFLAGS="$OPTCFLAGS -msse -mfpmath=sse"
	
		fi

		if test x$ac_cv_flag_sse2 = xyes; then
			CFLAGS="$CFLAGS -msse2 -mfpmath=sse"
			OPTCFLAGS="$OPTCFLAGS -msse2 -mfpmath=sse"
		fi
	fi	
fi



AC_MSG_RESULT($CFLAGS)

have_mjpegtools=false
AC_SUBST(MJPEGTOOLS_CFLAGS)
AC_SUBST(MJPGETOOLS_LIBS)

PKG_CHECK_MODULES(LIBPNG, [libpng], have_libpng=true,have_libpng=false)
PKG_CHECK_MODULES(LIBQRENCODE,[libqrencode >= 3.4.4], have_qrencode=true,have_qrencode=false)
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

FFMPEG_CFLAGS="${LIBAVFORMAT_CFLAGS} ${LIBAVCODEC_CFLAGS} ${LIBAVUTIL_CFLAGS} ${LIBSWSCALE_CFLAGS}"
FFMPEG_LIBS="${LIBAVFORMAT_LIBS} ${LIBAVCODEC_LIBS} ${LIBAVUTIL_LIBS} ${LIBSWSCALE_LIBS}"
AC_SUBST(FFMPEG_CFLAGS)
AC_SUBST(FFMPEG_LIBS)


FREEFRAME_CFLAGS="-freg-struct-return -Wno-unknown-pragmas -DLINUX"
AC_SUBST(FREEFRAME_CFLAGS)


AC_SUBST(LIBM_LIBS)

dnl ********************************************************************
dnl Look for X
AC_PATH_XTRA
AC_CHECK_HEADER(X11/Xlib.h, HAVE_X="yes", HAVE_X="no")

if test "x$HAVE_X" = "xno"
then
	AC_MSG_ERROR([Cannot find X11 development files.])
else
	X_LIBS="$X_LIBS $X_PRE_LIBS $X_EXTRA_LIBS"
	X_LIBS="$X_LIBS -lX11"
	AC_SUBST(X_CFLAGS)
	AC_SUBST(X_LIBS)
fi
AC_SUBST(HAVE_X)
AM_CONDITIONAL(HAVE_X, test "x$HAVE_X" = "xyes")


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
AC_MSG_CHECKING( for Xinerama support on XFree86 )
AC_MSG_RESULT($use_xinerama)	


AC_MSG_RESULT( $with_v4l )

AC_MSG_RESULT( $with_v4l2 )

if test x$with_v4l2 != xno ; then
  if test x$have_linux != xfalse ; then

	  AC_CHECK_HEADER(linux/videodev2.h,
                  [have_v4l2=true
	  if test x$with_v4l2 != xno; then
                   AC_DEFINE(HAVE_V4L2, 1,
		             [Building for Linux - using the video4linux2 API])
				   have_v4l=false
				   with_v4l=no
			fi
			],
                  [AC_MSG_ERROR([videodev2.h not found - please install the linux kernel headers or try configure with --enable-v4l1])])
          fi
fi

if test x$with_v4l != xno ; then
  if test x$have_linux != xfalse ; then

	  AC_CHECK_HEADER(linux/videodev.h,
                  [have_v4l=true
	  if test x$with_v4l != xno; then
                   AC_DEFINE(HAVE_V4L, 1,
		             [Building for Linux - using the video4linux API])
			fi
			],
                  [AC_MSG_ERROR([videodev.h not found - please install the linux kernel headers])])
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
	AC_MSG_ERROR([Cannot find pthread library])
fi

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,1,[Compile with dlopen support])
 have_dl_dlopen=true
fi


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
  PKG_CHECK_MODULES(PIXBUF, [gtk+-2.0 >= 2.0 gdk-pixbuf-2.0],
                    [
 		     AC_SUBST(PIXBUF_CFLAGS)
		     AC_SUBST(PIXBUF_LIBS)
		     have_pixbuf=true
                     AC_DEFINE(USE_GDK_PIXBUF, 1, [use gdk image load / save])],
                    [have_pixbuf=false])
fi


if test x$have_pixbuf != xtrue ; then
	AC_MSG_ERROR([Cannot find the gtk-2.0-dev / gdk pixbuf library.])
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

have_libquicktime=false
if test x$with_libquicktime != xno ; then
  PKG_CHECK_MODULES(LIBQUICKTIME, [libquicktime >= 1.0.0],
                    [have_libquicktime=true
                     AC_DEFINE(HAVE_LIBQUICKTIME, 1, [libquicktime >= 1.0.0 prese
nt])],
                    [have_libquicktime=false])
fi
AM_CONDITIONAL(HAVE_LIBQUICKTIME, test x$have_libquicktime = xtrue)



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
PKG_CHECK_MODULES( SDL, sdl >= 1.2.4, have_sdl=true,have_sdl=false)
if test x$have_sdl = xfalse ; then
 AC_MSG_ERROR([Could not find SDL 1.2.4 library!])
fi
AC_DEFINE(HAVE_SDL,1,[SDL library is installed])
fi

have_sdl_ttf=false
if test x$with_sdl != xno ; then
	PKG_CHECK_MODULES( SDL_TTF, SDL_ttf <= 2.0.11, have_sdl_ttf=true,have_sdl_ttf=false)
	if test x$have_sdl_ttf = xtrue ; then
		AC_DEFINE(HAVE_SDL_TTF,1,[SDL_TTF library is installed])
		AC_SUBST(SDL_TTF_CFLAGS)
		AC_SUBST(SDL_TTF_LIBS)
	fi
fi


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
dnl Check for the DirectFB library (for Matrox G550 Tvout)
dnl
have_directfb=false
if test x$with_directfb != xno ; then
PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.17, have_directfb=true,
	have_directfb=false)

if test x$have_directfb = xtrue ; then
AC_SUBST(DIRECTFB_LIBS)
AC_SUBST(DIRECTFB_CFLAGS)
AC_DEFINE(HAVE_DIRECTFB,1,[Compiling with DirectFB])
fi
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
	
dnl AM_PATH_XML2(2.5.4, [ 
dnl	have_xml2=true
dnl	AC_DEFINE(HAVE_XML2,,[Compiling with XML2])],,)
dnl
dnl	if test x$have_xml2 = xfalse ; then
dnl		AC_MSG_WARN([cannot find The XML C library for gnome])
dnl	fi
fi

if test x$have_xml2 != xtrue ; then
	AC_MSG_ERROR([Cannot find the The XML C library for gnome])
fi


have_freetype=false
have_freetype2=false
dnl PKG_CHECK_MODULES( FREETYPE2, [freetype2], [
dnl		AC_SUBST(FREETYPE2_CFLAGS)
dnl		AC_SUBST(FREETYPE2_LIBS)
dnl		AC_DEFINE(HAVE_FREETYPE,,[compiling with freetype])
dnl		have_freetype2=true],
dnl		[have_freetype2=false] )

if test x$have_freetype2 != xtrue; then
	AC_CHECK_PROG(FREETYPE_CONFIG, freetype-config,yes,no)
	if test $FREETYPE_CONFIG = yes; then
		FT_CFLAGS="`freetype-config --cflags`" 
		FT_WORD="`freetype-config --libs`"
		FT_LIBS=""
		for word in $FT_WORD; do	
			beginning=`echo $word |cut -c -2` 
			if test ".$beginning" = ".-L"; then
				FT_LDFLAGS="$FT_LDFLAGS $word"
			else
				FT_LIBS="$FT_LIBS $word"
			fi
		done
		AC_DEFINE(HAVE_FREETYPE,1,[compiling with freetype])
		FTPATHTOKENS=${FT_CFLAGS##*/}
		AC_DEFINE_UNQUOTED(FT_FTSNAMES_H,"$FTPATHTOKENS/ftsnames.h",[location of ftsnames.h (freetype)])
		AC_DEFINE_UNQUOTED(FT_TTNAMEID_H,"$FTPATHTOKENS/ttnameid.h",[location of ttnameid.h (freetype)])
		AC_SUBST(FT_LIBS)
		AC_SUBST(FT_LDFLAGS)
		AC_SUBST(FT_CFLAGS)
		have_freetype=true
		AC_MSG_NOTICE([Compiling with FreeType])
	else
		AC_MSG_ERROR([Cannot find the freetype-config program])
	fi
fi



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

dnl **********************************************************************
dnl All the conditional stuff for the Makefiles
AM_CONDITIONAL(HAVE_ASM_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_X86CPU, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(HAVE_PPCCPU, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(ARCH_PPC, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(ARCH_X86, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(HAVE_ALTIVEC, test x$have_altivec = xtrue)
AM_CONDITIONAL(HAVE_ASM_MMX2, test x$have_asm_mmx2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_NASM, test x$have_asm_nasm = xtrue)
AM_CONDITIONAL(HAVE_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_SDL, test x$have_sdl = xtrue)
AM_CONDITIONAL(HAVE_XML2, test x$have_xml2 = xtrue)
AM_CONDITIONAL(HAVE_JACK, test x$have_jack = xtrue)
AM_CONDITIONAL(HAVE_DIRECTFB, test x$have_directfb = xtrue)
AM_CONDITIONAL(HAVE_FREETYPE, test x$have_freetype = xtrue)
AM_CONDITIONAL(HAVE_V4L, test x$have_v4l = xtrue)
AM_CONDITIONAL(HAVE_V4L2, test x$have_V4l2 = xtrue)
AM_CONDITIONAL(SUPPORT_READ_DV2, test x$have_libdv = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_JPEG,test x$have_jpeg = xtrue)
AM_CONDITIONAL(HAVE_LIBLO,test x$have_liblo = xtrue)
AM_CONDITIONAL(HAVE_FREETYPE2, test x$have_freetype2 = xtrue)
AM_CONDITIONAL(HAVE_MJPEGTOOLS, test x$have_mjpegtools = xtrue )
dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

CFLAGS="$ARCHFLAGS $CFLAGS"
CXXFLAGS="$ARCHFLAGS $CXXFLAGS"
SUBSAMPLE_CFLAGS="$ARCHFLAGS $SUBSAMPLE_CFLAGS"
OPTCFLAGS="$ARCHFLAGS $OPTCFLAGS"

AC_SUBST(SUBSAMPLE_CFLAGS)
AC_SUBST(OPTCFLAGS)

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
    warnCFLAGS="$warnCFLAGS -Wmissing-prototypes -Wundef -Wshadow -Wbad-function-cast -Wconversion -Wpadded -Wunreachable-code -Wmissing-declarations -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual -Wall"
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
 	CFLAGS="$CFLAGS $warnCFLAGS"
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

if test "$have_mjpegtools" = "false" ; then
	AC_CONFIG_FILES([mjpegtools/Makefile])
fi

AC_CONFIG_FILES([
aclib/Makefile
libOSC/Makefile
libhash/Makefile
libvjmsg/Makefile
libvevo/Makefile
liblzo/Makefile
libvjmem/Makefile
libvje/Makefile
libplugger/Makefile
libsample/Makefile
libvjnet/Makefile
libqrwrap/Makefile
libsubsample/Makefile
libyuv/Makefile
libel/Makefile
libstream/Makefile
libsamplerec/Makefile
bio2jack/Makefile
veejay/Makefile
man/Makefile
livido-plugins/Makefile
veejay.pc
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([   FX library flags: $OPTCFLAGS])
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
AC_MSG_NOTICE([  - FFmpeg AVFormat               : ${have_avformat} ])
AC_MSG_NOTICE([  - FFmpeg AVCodec                : ${have_avcodec} ])
AC_MSG_NOTICE([  - FFmpeg Swscaler               : ${have_swscale} ])
AC_MSG_NOTICE([  - FFmpeg avutil		     : ${have_avutil} ])
AC_MSG_NOTICE([  - SDL support                   : ${have_sdl}])
AC_MSG_NOTICE([  - SDL on-screen-logging         : ${have_sdl_ttf}])

if test "$have_freetype" = "true"; then
	AC_MSG_NOTICE([  - Freetype support              : ${have_freetype}])
fi

if test "$have_freetype2" = "true"; then
	AC_MSG_NOTICE([  - FreeType 2 support            : ${have_freetype2}])
fi

AC_MSG_NOTICE([  - XML c library for Gnome       : ${have_xml2}])
AC_MSG_NOTICE([  - JPEG support                  : ${have_jpeg} ])
AC_MSG_NOTICE([  - GDK Pixbuf support            : ${have_pixbuf}])

if test "$have_mjpegtools" = "true"; then
	AC_MSG_NOTICE([  - Mjpegtools support            : ${have_mjpegtools}])
fi

AC_MSG_NOTICE([ Optional dependencies])
AC_MSG_NOTICE([  - DirectFB support              : ${have_directfb}])
AC_MSG_NOTICE([  - libDV (digital video) support : ${have_libdv} ])
AC_MSG_NOTICE([  - QuickTime support             : ${have_libquicktime} ])
AC_MSG_NOTICE([  - Jack Audio Connection Kit     : ${have_jack}])
AC_MSG_NOTICE([  - Liblo OSC client              : ${have_liblo}])
AC_MSG_NOTICE([  - V4L2 (default)                : ${have_v4l2}])
AC_MSG_NOTICE([  - V4L1 (obsolete)               : ${have_v4l}])
AC_MSG_NOTICE([  - libunwind                     : ${have_unwind}])
AC_MSG_NOTICE([  - QR code support (libqrencode) : ${have_qrencode}])

cat NEWS
