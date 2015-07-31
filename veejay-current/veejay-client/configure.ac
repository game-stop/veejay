dnl Process this file with autoconf to produce a configure script.
dnl AC_INIT
AC_INIT([reloaded],[1.5.57],[nwelburg@gmail.com])
AC_PREREQ(2.57)
AC_CONFIG_SRCDIR([src/gveejay.c])

RELOADED_MAJOR_VERSION=1
RELOADED_MINOR_VERSION=5
RELOADED_MICRO_VERSION=57
RELOADED_VERSION=$VEEJAY_MAJOR_VERSION.$VEEJAY_MINOR_VERSION.$VEEJAY_MICRO_VERSION
RELOADED_CODENAME="Reloaded - build $RELOADED_MAJOR_VERSION $RELOADED_MINOR_VERSION $RELOADED_MICRO_VERSION"
AC_CONFIG_HEADERS([config.h])

AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.7])
AM_MAINTAINER_MODE

AC_DEFINE(HAVE_CONFIG_H,1,[Include config.h])
# libtool versioning
LT_RELEASE=$RELOADED_MAJOR_VERSION.$RELOADED_MINOR_VERSION.$RELOADED_MICRO_VERSION
LT_CURRENT=$RELOADED_MICRO_VERSION
LT_REVISION=$RELOADED_MICRO_VERSION
LT_AGE=$RELOADED_MICRO_VERSION
AC_SUBST(LT_RELEASE)
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AC_CONFIG_MACRO_DIR([m4])

dnl kill CFLAGS
CFLAGS=""
AC_ARG_ENABLE(strict,
		AC_HELP_STRING([--enable-strict],
			     [Compile in paranoia assertion checking]))
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
AC_ARG_WITH(pixbuf,
            AC_HELP_STRING([--without-pixbuf], [Do not use GDK pixbuf]),
            [], [with_pixbuf=yes])

user_specified_target=
AC_ARG_WITH(arch-target, AS_HELP_STRING( [--with-arch-target=CPU_TYPE],
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

dnl AX_PREFIXED_DEFINE([RELOADED], [HAVE_STDINT_H])
dnl AX_PREFIXED_DEFINE([RELOADED], [HAVE_INTTYPES_H])

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



AC_SUBST(LIBGETOPT_LIB)

AC_TYPE_SIGNAL

AC_MSG_NOTICE([The host is $host  $host_cpu  $host_vendor  $host_os .])

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


AC_SUBST(LIBM_LIBS)

dnl *********************************************************************
dnl Check for the pthread lib
dnl
AC_SUBST(PTHREAD_LIBS)
have_pthread=false
AC_CHECK_LIB(pthread, pthread_create,
  [ PTHREAD_LIBS="-lpthread" 
    AC_DEFINE(HAVE_LIBPTHREAD,1,[Compiling with pthread library])
    have_pthread=true ],,)

AC_CHECK_FUNC(pthread_attr_getstacksize,
	          [
				AC_DEFINE(HAVE_PTHREADSTACKSIZE,,[Using pthread stack size])
			  ] )


have_alsa=false
AC_CHECK_LIB( asound, snd_seq_open,
	[
		AC_DEFINE( HAVE_ALSA,1, [Whether or not we have alsa])
		ALSA_LIBS="-lasound"
		AC_SUBST(ALSA_LIBS)
		have_alsa=true ],,)

AC_CHECK_HEADER(alsa/asoundlib.h,,[AC_MSG_ERROR([alsa/asoundlib.h not found - please install the alsa header])] )

have_dl_dlopen=false
AC_CHECK_LIB(dl,dlopen)
if test "$ac_cv_lib_dl_dlopen" = "yes"; then
 AC_DEFINE(HAVE_DL_DLOPEN,1,[Compile with dlopen support])
 have_dl_dlopen=true
fi

dnl Check for Veejay
have_veejay=false
PKG_CHECK_MODULES( VEEJAY, [veejay >= 1.5.50 ],
	[	
		AC_SUBST(VEEJAY_CFLAGS)
		AC_SUBST(VEEJAY_LIBS)
		have_veejay=true
		AC_DEFINE(HAVE_VEEJAY,1, [Veejay 1.5.50 installed])]
		,
		[have_veejay=false])

if test x$have_veejay != xtrue ; then
   AC_MSG_ERROR([Cannot find veejay 1.5.50 or later])
fi

have_pixbuf=false
if test x$with_pixbuf != xno ; then
  PKG_CHECK_MODULES(PIXBUF, [gtk+-2.0 >= 2.8 gdk-pixbuf-2.0],
                    [
 		     AC_SUBST(PIXBUF_CFLAGS)
		     AC_SUBST(PIXBUF_LIBS)
		     have_pixbuf=true
                     AC_DEFINE(USE_GDK_PIXBUF, 1, [use gdk image load / save])],
                    [have_pixbuf=false])
fi

have_sdl=false
if test x$with_sdl != xno ; then
 PKG_CHECK_MODULES(SDL, [sdl >= 1.2],
	[
	 AC_SUBST(SDL_CFLAGS)
	 AC_SUBST(SDL_LIBS)
	 have_sdl=true
	 AC_DEFINE(HAVE_SDL,1,[use sdl keybindings])],
	[have_sdl=false])
fi

if test x$have_sdl != xtrue ; then
 AC_MSG_WARN([Cannot find SDL, reloaded will not be able to map keybindings])
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

dnl Look for X
AC_PATH_XTRA
AM_CONDITIONAL(HAVE_X, test x$have_x = xyes)

debugCFLAGS=""

AC_MSG_CHECKING(whether to compile in profiling information)
if test "x$enable_profiling" = "xyes" ; then
 debugCFLAGS="$debugCFLAGS -fprofile-arcs -ftest-coverage"
 DEBUGLIBS="-lgcov"
 AC_SUBST(DEBUGLIBS)
 AC_MSG_RESULT([yes])
else
 AC_MSG_RESULT([no])
fi

AC_MSG_CHECKING(whether to compile in debug symbols)
if test "x$enable_debug" = "xyes" ; then
 debugCFLAGS="$debugCFLAGS -g"
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
YUVCFLAGS="-fif-conversion"

AC_MSG_CHECKING([Architecture])
case $host_cpu in
	i[[3-7]]86)
	AC_DEFINE(HAVE_X86CPU,1, [Compiling for x86 architecture CPU])
	AC_DEFINE(ARCH_X86,1,[Compiling for x86 architecture]) 
	have_x86cpu=true

	PROGRAM_NOPIC="-fno-PIC"

	if test "x$enable_debug" != "xyes" ; then
		OP_CFLAGS="-O3"
 	fi

	AC_SUBST(OP_CFLAGS)
	AC_SUBST(YUVCFLAGS)

   	dnl If the host is pentium, align on double word boundary
    	case $host_cpu in
		i686)
			CFLAGS="$CFLAGS"
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
	CFLAGS="$CFLAGS -m64 -fPIC -DPIC"
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

CFLAGS="$CFLAGS -DFILE_OFFSET_BITS=64 -D_LARGE_FILES"
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
		AC_DEFINE(HAVE_ASM_3DNOW,1,[Compiling in 3Dnow])
		have_asm_3dnow=true
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
		AC_DEFINE(HAVE_ASM_MMX2,,[Compiling in MMX2])
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
		fi

		if test x$av_cv_flag_sse2 = xyes; then
			CFLAGS="$CFLAGS -msse2 -mfpmath=sse"
		fi
	fi	
fi

AC_MSG_RESULT($CFLAGS)

PKG_CHECK_MODULES(LIBAVUTIL,[libavutil >= 49.7], have_avutil=true,have_avutil=false)
if test x$have_avutil = xfalse;
then
       AC_MSG_ERROR([libavutil not found.])
fi

PKG_CHECK_MODULES( LIBX11, x11, have_x11=true,have_x11=false)
if test x$have_x11 = xfalse;
then
	AC_MSG_ERROR([libx11 not found.])
fi

FFMPEG_CFLAGS="${LIBAVUTIL_CFLAGS}"
AC_SUBST(FFMPEG_CFLAGS)
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
AM_CONDITIONAL(HAVE_DL_DLOPEN, test x$have_dl_dlopen = xtrue)
dnl *********************************************************************
dnl Check for what warnings we want gcc to use and adjust the CFLAGS
dnl as needed. This only works for GCC.
dnl We do this last as it can screw up library detection etc...

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

reloaded_datadir="${datarootdir}/reloaded"
AC_SUBST(reloaded_datadir)
sh gveejay-paths.sh > gveejay-paths.h

glib_modules="glib-2.0 >= 2.4"
PKG_CHECK_MODULES(GLIB, [$glib_modules])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

gtk_modules="gtk+-2.0 >= 2.8"
PKG_CHECK_MODULES(GTK, [$gtk_modules])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

libglade_modules="libglade-2.0 >= 2.0.1"
PKG_CHECK_MODULES(GLADE, [$libglade_modules])
AC_SUBST(GLADE_CFLAGS)
AC_SUBST(GLADE_LIBS)


dnl **********************************************************************
dnl Output a Makefile or two and the lib/header descriptor script
dnl
AC_CONFIG_FILES([
Makefile
src/Makefile
gveejay-paths.sh
])
AC_OUTPUT

AC_DEFINE(VERSION, ${VERSION})

AC_MSG_NOTICE([])
AC_MSG_NOTICE([ GVeejayReloaded ${VERSION} build configuration :])
AC_MSG_NOTICE([])
AC_MSG_NOTICE([ Compiler flags: $CFLAGS])
AC_MSG_NOTICE([                 $YUVCFLAGS])
AC_MSG_NOTICE([			$OP_CFLAGS])
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
AC_MSG_NOTICE([  - Veejay 1.5.50                 : ${have_veejay}])
AC_MSG_NOTICE([  - POSIX Threads (pthread)       : ${have_pthread}])
AC_MSG_NOTICE([  - GDK Pixbuf support            : ${have_pixbuf}])
AC_MSG_NOTICE([  - Alsa support (midi sequencer) : ${have_alsa}])
AC_MSG_NOTICE([])
