dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([veejay],[0.8.0],[veejay-users@lissts.sourceforge.net])
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([veejay/veejay.c])

VEEJAY_MAJOR_VERSION=0
VEEJAY_MINOR_VERSION=8
VEEJAY_MICRO_VERSION=0
VEEJAY_VERSION=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION.$VEEJAY_MICRO_VERSION

AC_CONFIG_HEADERS([config.h])
dnl AX_CONFIG_PREFIXED_HEADER([veejay-config.h], [VEEJAY], [config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([1.7])
AM_MAINTAINER_MODE


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


dnl kill CFLAGS
CFLAGS=""

AC_ARG_ENABLE(debug,
		AC_HELP_STRING([--enable-debug],
			     [Compile in debugging information]))

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
AC_ARG_WITH(directfb,
	     AC_HELP_STRING([--without-directfb], [Do not use DirectFB.]),
	 	[], [with_directfb=yes])
AC_ARG_WITH(sdl,
	     AC_HELP_STRING([--without-sdl], [Do not use SDL.]),
	        [], [with_sdl=yes])
AC_ARG_WITH(jack,
		AC_HELP_STRING([--without-jack], [Do not use jack.]),
		[], [with_jack=yes])
AC_ARG_WITH(v4l,
		AC_HELP_STRING([--without-v4l], [Do not use video4linux]),	
		[], [with_v4l=yes])
AC_ARG_WITH(xml2,
		AC_HELP_STRING([--without-xml2], [Do not use the XML library for Gnome]),
		[], [with_xml2=yes])
AC_ARG_WITH(freetype2,
		AC_HELP_STRING([--without-freetype2], [Do not use the freetype library]),
		[], [with_freetype2=yes])
AC_ARG_WITH(jpeg,
		AC_HELP_STRING([--without-jpeg], [Do not JPEG support]),
		[],	[with_libjpeg=yes])

AC_ARG_WITH(jpeg-mmx,
            AC_HELP_STRING([--with-jpegmmx=PFX],
                           [Prefix where jpeg-mmx is installed (optional).]))

AC_ARG_WITH(gui,
		AC_HELP_STRING([--without-gui], [Do not build gveejay]),
		[],	[with_gui=yes])

AC_GNU_SOURCE

dnl Initialize libtool
AC_PROG_LIBTOOL

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_STDC
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
AC_SUBST(LIBGETOPT_LIB)

AC_TYPE_SIGNAL

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])


dnl *********************************************************************
dnl Test whether we're linux or not. If so, define HAVE_V4L and build the
dnl video4linux/hardware dependent tools. Else, undefine HAVE_V4L
have_v4l=false
have_linux=false
have_darwin=false
with_gui=no
case $host in
  *-*-linux*)
	AC_CHECK_HEADER(linux/version.h,
		[have_linux=true
		 with_gui=yes
	         AC_DEFINE(HAVE_LINUX,,[Linux platform])
		],
		[AC_MSG_ERROR([version.h not found - please install the linux kernel headers])
		])
		LIBM_LIBS="-lm"
	;;
   *-apple-darwin*)
		AC_DEFINE(HAVE_DARWIN,,[MAC OS X Darin])
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

AC_SUBST(LIBM_LIBS)

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
    AC_DEFINE(HAVE_LIBPTHREAD,,[Compiling with pthread library])
    have_pthread=true ],,)

AC_CHECK_FUNC(pthread_attr_getstacksize,
	          [
				AC_DEFINE(HAVE_PTHREADSTACKSIZE,,[Using pthread stack size])
			  ] )

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,,[Compile with dlopen support])
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
	    AC_MSG_ERROR([jpeglib.h not found - please install the libjpeg headers]))
	  AC_CHECK_LIB(jpeg, jpeg_start_compress,
	    [ JPEG_LIBS="-ljpeg"
	      JPEG_CFLAGS=""
	      have_jpeg=true ],,)
	fi

	if test x$have_jpeg = xtrue ; then
		AC_DEFINE(HAVE_JPEG,,[Define is JPEG libraries are available])
	fi
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
dnl Check for DGA (for v4l-conf)
dnl ********************************************************************
AC_SUBST(V4LCONF_LIBS)
AC_CHECK_LIB(Xxf86dga, XF86DGAQueryExtension,
	V4LCONF_LIBS="$X_LIBS -lXxf86dga" 
	AC_DEFINE(HAVE_LIBXXF86DGA,1, [libxf86dga is present]),,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)

dnl ********************************************************************
dnl Check for the SDL library (for software playback)
dnl   (defines SDL_CFLAGS and SDL_LIBS)
dnl ********************************************************************
have_sdl=false
if test x$with_sdl != xno ; then
AM_PATH_SDL(1.2.3,[
            have_sdl=true
            AC_DEFINE(HAVE_SDL,, [SDL library present])],,)
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
AC_DEFINE(HAVE_DIRECTFB,,[Compiling with DirectFB])
fi
fi

dnl *********************************************************************
dnl Check for the Jack Audio connection kit
dnl

have_jack=false
if test x$with_jack != xno ; then
  PKG_CHECK_MODULES(JACK, jack >= 0.98.1 , have_jack=true, have_jack=false)
  if test x$have_jack = xtrue ; then
    AC_SUBST(JACK_LIBS)
    AC_SUBST(JACK_CFLAGS)
    AC_DEFINE(HAVE_JACK,,[Compiling with support for Jack])
  else
    AC_MSG_WARN([Jack >= 0.981.1 is required, get it at http://jackit.sourceforge.net]);
  fi
fi

dnl ********************************************************************
dnl Check for the libxml2 library (for saving/loading project files)
dnl
have_xml2=false
if test x$with_xml2 != xno; then
AM_PATH_XML2(2.5.4, [ 
	have_xml2=true
	AC_DEFINE(HAVE_XML2,,[Compiling with XML2])],,)

	if test x$have_xml2 = xfalse ; then
		AC_MSG_WARN([cannot find The XML C library for gnome])
	fi
fi

have_freetype=false
if test x$with_freetype2 != xno; then
AC_CHECK_PROG(FREETYPE_CONFIG, freetype-config,yes,no)
if test $FREETYPE_CONFIG = yes; then
  FT_CFLAGS="`freetype-config --cflags`"
  FT_WORD="`freetype-config --libs`"
  FT_LDFLAGS=""
  FT_LIBS=""
  for word in $FT_WORD ; do
	beginning=`echo $word | cut -c -2`
	if test ".$beginning" = ".-L"; then
		FT_LDFLAGS="$FT_LDFLAGS $word"  
	else
		FT_LIBS="$FT_LIBS $word"
	fi
  done
  AC_DEFINE(HAVE_FREETYPE,,[Compiling with FreeType])
  AC_SUBST(FT_LIBS)
  AC_SUBST(FT_LDFLAGS)
  AC_SUBST(FT_CFLAGS)
  have_freetype=true
fi  
fi

AC_MSG_CHECKING(whether to compile in debugging information)
debugCFLAGS=""
if test "x$enable_debug" = "xyes" ; then
 debugCFLAGS="-g"
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

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_MSG_RESULT([x86])
    AC_DEFINE(HAVE_X86CPU,, [Compiling for x86 architecture CPU])
    AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
    have_x86cpu=true

    dnl Programs without PIC!
    PROGRAM_NOPIC="-fno-PIC"

    if test "x$enable_debug" != "xyes" ; then
	dnl optimize if not debugging
	CFLAGS="-fno-common -O2 -fomit-frame-pointer"
    fi
 
    dnl If the host is pentium, align on double word boundary
    case $host_cpu in
	i686)
		CFLAGS="$CFLAGS -malign-double -mfpmath=sse"
		;;
	*)
		;;
	esac 
	
    ;;
  x86_64*)
	AC_MSG_RESULT([x86-64])
    AC_DEFINE(HAVE_X86_CPU,, [Compiling for x86-64 architecture CPU])
    AC_DEFINE(ARCH_X86_64,1, [Compiling for x86-64 architecture CPU])
    have_x86cpu=true
    have_x86_64cpu=true
    PROGRAM_NOPIC="-fno-PIC"
    ;;
  powerpc | powerpc64)
	AC_MSG_RESULT([powerpc])
    AC_DEFINE(HAVE_PPCCPU,, [Compiling for PowerPC CPU])
    AC_DEFINE(ARCH_PPC,,[Compiling for PowerPC])
    have_ppccpu=true
    case $host in
      *-apple-darwin*)
       PROGRAM_NOPIC="-mdynamic-no-pic" # dynamic-no-pic is an Apple gcc option
dnl	PROGRAM_NOPIC="-bundle -flat_namespace -undefined suppress"
dnl     CFLAGS="$CFLAGS -no-cpp-precomp"
       ;;
      *)
       PROGRAM_NOPIC=""
       ;;
    esac
    ;;
   mips | mipsel)
	AC_MSG_RESULT([mips])
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
		AC_DEFINE(HAVE_PS2,,[Compile for playstation2])
	else
		AC_MSG_RESULT(no)
	fi

	;;
  *)
    AC_MSG_RESULT([unknown])
    ;;
esac

dnl This flag is used for PROGRAMS not SHARED LIBRARIES.  PIC code is required
dnl for shared libraries but is slower than non-pic code.  Compute bound 
dnl programs such as yuvdenoise and y4mspatialfilter can use this flag by
dnl simply adding programname_CFLAGS=@PROGRAM_NOPIC@ to the Makefile.am

AC_SUBST(PROGRAM_NOPIC)

if test "$enable_simd_accel" != "false" -a "$enable_simd_accel" != "no"
then
  if test x$have_x86cpu = xtrue
  then

	AC_PATH_PROGS(AS, nasm nasmw)
	if test x$AS = x -o x$AS = x'"$AS"'; then
		AC_MSG_NOTICE([NASM not found]) 
    	else
    		AC_DEFINE(HAVE_ASM_NASM,,[Compiling with nasm])
	 	have_asm_nasm=true
     		ASFLAGS="-f elf"
     		AC_SUBST(ASFLAGS)
    	fi

	dnl check for mmx
	AC_CACHE_CHECK(for MMX on processor(s), ac_cv_flag_mxx, [
		if grep "^flags.* mmx" /proc/cpuinfo > /dev/null; then
			ac_cv_flag_mmx=yes
		else
			ac_cv_flag_mmx=no
		fi
	])

	if test $ac_cv_flag_mmx = yes; then
       		AC_DEFINE(HAVE_ASM_MMX,,[Compiling in MMX support])
    	  	AC_DEFINE(HAVE_MMX,,[Compiling in MMX support in ffmpeg])
       		have_asm_mmx=true
	fi

	dnl check for sse
	AC_CACHE_CHECK(for SSE on processor(s), ac_cv_flag_sse, [
		if grep "^flags.* sse" /proc/cpuinfo > /dev/null; then
 			ac_cv_flag_sse=yes
		else
			ac_cv_flag_sse=no
		fi
	])

	if test $ac_cv_flag_sse = yes; then
		AC_DEFINE(HAVE_ASM_SSE,,[Compiling in SSE support])
		AC_DEFINE(HAVE_SSE,,[Compiling in SSE support])
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
		AC_DEFINE(HAVE_ASM_SSE2,,[Compiling in SSE2 support])
		AC_DEFINE(HAVE_SSE2,,[Compiling in SSE2 support])
		have_asm_sse2=true
	fi

	dnl check for 3dnow
	AC_CACHE_CHECK(for 3DNOW on processor(s), ac_cv_flag_3dnow, [
		if grep "^flags.* 3dnow" /proc/cpuinfo > /dev/null; then
			ac_cv_flag_3dnow=yes
		else
			ac_cv_flag_3dnow=no
		fi
	])

	if test $ac_cv_flag_3dnow = yes; then
		AC_DEFINE(HAVE_ASM_3DNOW,,[Compiling in 3Dnow])
		have_asm_3dnow=true
	fi

	dnl check for CMOV
	AC_CACHE_CHECK(for CMOV on processor(s), ac_cv_flag_cmov, [
		if grep "^flags.* cmov" /proc/cpuinfo > /dev/null; then
			ac_cv_flag_cmov=yes
		else
			ac_cv_flag_cmov=no
		fi
	])

	if test $ac_cv_flag_cmov = yes; then
		AC_DEFINE(HAVE_CMOV,,[Compiling in CMOV])
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
		AC_DEFINE(HAVE_ASM_MMX2,,[Compiling in MMX2])
		AC_DEFINE(HAVE_MMX2,,[Compiling in MMX2])
		have_asm_mmx2=true
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
  
    if test x$have_altivec = xtrue
    then
      # add -O3 flag
      ALTIVEC_CFLAGS="-O3 ${ALTIVEC_CFLAGS}"
      AC_MSG_RESULT(yes)
      AC_DEFINE(HAVE_ALTIVEC,,
				[Inline PPC Altivec primitives available])
      AC_SUBST(ALTIVEC_CFLAGS)
    else
      AC_MSG_RESULT(no)
    fi
  fi
fi

if test ! -r $srcdir/cpuinfo.sh; then
       AC_MSG_ERROR([cpuinfo.sh script not found - do a cvs update])
fi


AC_MSG_CHECKING(sub-architecture settings)
chmod +x $srcdir/cpuinfo.sh

if test x$have_x86cpu = xtrue; then
       ARCHFLAGS=`$srcdir/cpuinfo.sh x86`
else
  if test x$have_ppccpu = xtrue; then
       ARCHFLAGS=`$srcdir/cpuinfo.sh ppc`
  else
       ARCHFLAGS=`$srcdir/cpuinfo.sh mips`
  fi
fi
AC_MSG_RESULT([$ARCHFLAGS])

dnl **********************************************************************
dnl All the conditional stuff for the Makefiles

AM_CONDITIONAL(HAVE_ASM_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_X86CPU, test x$have_x86cpu = xtrue)
AM_CONDITIONAL(HAVE_PPCCPU, test x$have_ppccpu = xtrue)
AM_CONDITIONAL(HAVE_ALTIVEC, test x$have_altivec = xtrue)
AM_CONDITIONAL(HAVE_ASM_MMX2, test x$have_asm_mmx2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_ASM_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_ASM_NASM, test x$have_asm_nasm = xtrue)
AM_CONDITIONAL(HAVE_MMX, test x$have_asm_mmx = xtrue)
AM_CONDITIONAL(HAVE_SSE, test x$have_asm_sse = xtrue)
AM_CONDITIONAL(HAVE_SSE2, test x$have_asm_sse2 = xtrue)
AM_CONDITIONAL(HAVE_MMX2, test x$have_asm_mmx2 = xtrue)
AM_CONDITIONAL(HAVE_SDL, test x$have_sdl = xtrue)
AM_CONDITIONAL(HAVE_XML2, test x$have_xml2 = xtrue)
AM_CONDITIONAL(HAVE_JACK, test x$have_jack = xtrue)
AM_CONDITIONAL(HAVE_DIRECTFB, test x$have_directfb = xtrue)
AM_CONDITIONAL(HAVE_FREETYPE, test x$have_freetype = xtrue)
AM_CONDITIONAL(HAVE_V4L, test x$have_v4l = xtrue)
AM_CONDITIONAL(SUPPORT_READ_DV2, test x$have_libdv = xtrue)
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
AM_CONDITIONAL(HAVE_JPEG,test x$have_jpeg = xtrue)
dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

dnl maddog:  check out AC-COMPILE-WARNINGS ???????

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
     *) warnCFLAGS="-Wall -Wunused" ;;
    esac
  if test "x$enable_compile_warnings" = "xyes" ; then
    warnCFLAGS="$warnCFLAGS -Wmissing-prototypes -Wmissing-declarations -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual"
    warnCXXFLAGS="$warnCXXFLAGS -Wmissing-prototypes  -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual"
  fi
  if test "x$enable_warnings_as_errors" = "xyes" ; then
    warnCFLAGS="$warnCFLAGS -Werror"
    warnCXXFLAGS="$warnCXXFLAGS -Werror"
  fi
 fi
fi
AC_MSG_RESULT($warnCFLAGS)
AC_MSG_RESULT($warnCXXFLAGS)

if test "x$cflags_set" != "xyes" ; then
  if test "x$enable_debug" = "xyes" ; then
  	CFLAGS="-g -Wall-Wunused"
  else
 	CFLAGS="$CFLAGS $warnCFLAGS $debugCFLAGS"
  fi
  CXXFLAGS="$CXXFLAGS $warnCXXFLAGS"
  cflags_set=yes
fi


if test x"$with_extra_cflags" != "x"; then
   CFLAGS="$CFLAGS $with_extra_cflags"
   CXXFLAGS="$CXXFLAGS $with_extra_cflags"
fi

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED,,
                    [Defined if building against uninstalled FFmpeg source])

if test x$have_ps2 = xtrue ; then
 ac_configure_args="$ac_configure_args --disable-v4l --disable-audio-oss --disable-vhook --disable-ffmpeg --disable-ffserver --disable-zlib --disable-dv1394 --disable-mpeg-audio-hp --disable-network --disable-audio-oss --disable-audiobeos"
else
ac_configure_args="$ac_configure_args --disable-ffserver --disable-ffplay --disable-v4l --disable-audio-oss --disable-vhook --disable-dv1394 --disable-mpeg-audio-hp --disable-network --disable-audio-oss --disable-audio-beos"
fi

dnl  bah autoreconf wants to play with independent package ffmpeg 
dnl  (needs configure.ac). so, instead of AC_CONFIG_SUBDIRS(ffmpeg/ffmeg)
dnl  do it manually

if test x$with_gui != xno ; then

	gveejay_datadir="${datadir}/veejay"
	AC_SUBST(gveejay_datadir)
	sh gveejay-paths.sh > gveejay-paths.h


	glib_modules="glib-2.0 >= 2.4"
	PKG_CHECK_MODULES(GLIB, [$glib_modules])
	AC_SUBST(GLIB_CFLAGS)
	AC_SUBST(GLIB_LIBS)


	gtk_modules="gtk+-2.0 >= 2.4"
	PKG_CHECK_MODULES(GTK, [$gtk_modules])
	AC_SUBST(GTK_CFLAGS)
	AC_SUBST(GTK_LIBS)


	libglade_modules="libglade-2.0 >= 2.2.0"
	PKG_CHECK_MODULES(GLADE, [$libglade_modules])
	AC_SUBST(GLADE_CFLAGS)
	AC_SUBST(GLADE_LIBS)

fi
 
dnl  AC_CONFIG_SUBDIRS(ffmpeg/ffmpeg)

(
	cd ffmpeg/ffmpeg
	./configure.gnu $ac_configure_args

)


AC_DEFINE(VEEJAY,,[Building veejay])

dnl **********************************************************************
dnl Output a Makefile or two and the lib/header descriptor script
dnl
AC_CONFIG_FILES([
Makefile
ffmpeg/Makefile
libOSC/Makefile
libpostproc/Makefile
libhash/Makefile
libvjmsg/Makefile
libvjmem/Makefile
libvje/Makefile
libsample/Makefile
libvjnet/Makefile
libyuv/Makefile
libel/Makefile
libstream/Makefile
liblavjpeg/Makefile
libsamplerec/Makefile
utils/Makefile
bio2jack/Makefile
plugins/Makefile
veejay/Makefile
gveejay/Makefile
man/Makefile
tools/Makefile
gveejay-paths.sh
veejay-config
veejay.pc
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Veejay ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Architecture: ${host_cpu} ])
AC_MSG_NOTICE([])
if test "$have_x86cpu" = "true" ; then
AC_MSG_NOTICE([   x86  ])
AC_MSG_NOTICE([    MMX     enabled     : ${have_asm_mmx}])
AC_MSG_NOTICE([    MMX2    enabled     : ${have_asm_mmx2}])
AC_MSG_NOTICE([    SSE     enabled     : ${have_asm_sse}])
AC_MSG_NOTICE([    SSE2    enabled     : ${have_asm_sse2}])
AC_MSG_NOTICE([    3DNOW   enabled     : ${have_asm_3dnow}])
AC_MSG_NOTICE([    CMOV    enabled     : ${have_cmov}])
AC_MSG_NOTICE([    Nasm    enabled     : ${have_asm_nasm}])
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
AC_MSG_NOTICE([      Veejay will not compile ])
fi
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Required dependencies:])
AC_MSG_NOTICE([  - POSIX Threads (pthread)       : ${have_pthread}])

AC_MSG_NOTICE([ Optional dependencies])

AC_MSG_NOTICE([  - video4linux                   : ${have_v4l}])
AC_MSG_NOTICE([  - SDL support                   : ${have_sdl}])
if test x$have_sdl != xyes ; then
AC_MSG_NOTICE([      There will be NO video window ])
fi
AC_MSG_NOTICE([  - DirectFB support              : ${have_directfb}])
AC_MSG_NOTICE([  - AVI MJPEG playback/recording  : true (always)])
AC_MSG_NOTICE([  - libDV (digital video) support : ${have_libdv} ])
AC_MSG_NOTICE([  - JPEG support                  : ${have_jpeg} ])
AC_MSG_NOTICE([  - jack                          : ${have_jack}])
AC_MSG_NOTICE([  - xml c library for gnome       : ${have_xml2}])
AC_MSG_NOTICE([  - freetype support              : ${have_freetype}])
AC_MSG_NOTICE([])

if test x$with_gui != xno ; then
	AC_MSG_NOTICE([   - GVeejay UI])
	AC_MSG_NOTICE([     share : ${gveejay_datadir}])
fi

if test "$GCC" = yes ; then
	AC_MSG_NOTICE([])
	ac_gcc_version=`echo __GNUC__ | $CC -E - | grep -v ^\#`
	ac_gcc_minor_version=`echo __GNUC_MINOR__ | $CC -E - | grep -v ^\#`

	dnl wierd things happen with 3.4 
	dnl the compile flag --without-pic somehow affects the compile error
	dnl when gcc fails to find sufficient free registers in the postproc
	dnl library when compiling inline MMX asm. 
	dnl Also, depending on your luck, the -O2 (optimization) might be  
	dnl able to "optimize" so there are sufficient free registers 
	dnl compiler bug ??? who knows. Apperently its not in gcc 4.0 
	if test "$ac_gcc_version" = 3 -a "$ac_gcc_minor_version" = 4 ; then
		if test x$with_pic != xno ; then
			AC_MSG_NOTICE([*** You are using gcc >= 3.4 !!])
			AC_MSG_NOTICE([*** Please re-rerun configure with --without-pic])
		fi
	fi
fi


