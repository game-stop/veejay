#!/bin/sh

if test x"$CC" = x; then
   CC=gcc
fi

if test $# -ne 1; then
   echo "Please give the arch (ppc or x86) as an argument!" 1>&2
   exit 1
fi

if test `uname -s` = Darwin; then
   IsDarwin=yes
else
   IsDarwin=no
fi

target=$1

cc_version=`$CC -dumpversion`
_cc_major=`echo $cc_version | cut -d'.' -f1`
_cc_minor=`echo $cc_version | cut -d'.' -f2`

if test $_cc_major -ge 4; then
  _opt_mcpu="-mtune"
else
  _opt_mcpu="-mcpu"
fi

do_cc()
{
	$CC -o conftest conftest.c $@ >/dev/null 2>&1
}

extcheck()
{
cat > conftest.c <<EOF
#include <signal.h>
void catch() { exit(1); }
int main(void){
  signal(SIGILL, catch);
  __asm__ __volatile__ ("$1":::"memory");
  exit(0);
}
EOF

do_cc
if test -x ./conftest; then
     ./conftest
     if test $? -ne 0; then
        return 1
     fi
     return 0
else
     return 1
fi
}

do_x86()
{

CFLAGS=-O
if test $IsDarwin = yes; then
   CFLAGS="$CFLAGS -fno-pic -Wl,-read_only_relocs -Wl,suppress"
fi

if test -r /proc/cpuinfo; then
	_cpuinfo="cat /proc/cpuinfo"
else
	$CC $CFLAGS -o cpuinfo utils/cpuinfo.c
	_cpuinfo="./cpuinfo"
fi

# Cpu determination logic adapted from the MPlayer configure script.

pname=`$_cpuinfo | grep 'model name' | cut -d ':' -f 2 | head -n 1`
pvendor=`$_cpuinfo | grep 'vendor_id' | cut -d':' -f2 | cut -d' ' -f2 | head -n 1`
pfamily=`$_cpuinfo | grep 'cpu family' | cut -d':' -f2 | cut -d' ' -f2 | head -n 1`
pmodel=`$_cpuinfo | grep -v 'model name' | grep 'model' | cut -d':' -f2 | cut -d' ' -f2 | head -n 1`
pstep=`$_cpuinfo | grep 'stepping' | cut -d':' -f2 | cut -d' ' -f2 | head -n 1`
pparam=`$_cpuinfo | grep 'features' | cut -d':' -f2 | head -n 1`

if test -z "$pparam" ; then
 pparam=`$_cpuinfo | grep 'flags' | cut -d ':' -f 2 | head -n 1`
fi

_mmx=no
_3dnow=no
_3dnowex=no
_mmx2=no
_sse=no
_sse2=no
_mtrr=no

for i in $pparam ; do
 case "$i" in
  3dnow)        _3dnow=yes               ;;
  3dnowext)     _3dnow=yes  _3dnowex=yes ;;
  mmx)          _mmx=yes                 ;;
  mmxext)       _mmx2=yes                ;;
  mtrr|k6_mtrr|cyrix_arr)   _mtrr=yes    ;;
  xmm|sse|kni)  _sse=yes    _mmx2=yes    ;;
  sse2)         _sse2=yes                ;;
 esac
done

case "$pvendor" in
	AuthenticAMD)
		case "$pfamily" in
			3)proc=i386
			  ;;
			4) proc=i486
			  ;;
			5) iproc=586
			# models are: K5/SSA5 K5 K5 K5 ? ? K6 K6 K6-2 K6-3
			# K6 model 13 are the K6-2+ and K6-III+
			   if test "$pmodel" -eq 9 -o "$pmodel" -eq 13; then
				proc=k6-3
			   elif test "$pmodel" -ge 8; then
				proc=k6-2
			   elif test "$pmodel" -ge 6; then
				proc=k6
			   else
				proc=i586
			   fi
			   ;;
			6) iproc=686
			   if test "$pmodel" -ge 7; then
				proc=athlon-4
			   elif test "$pmodel" -ge 6; then
			       if test "$_sse" = yes && test "$pstep" -ge 2; then
				   proc=athlon-xp
			       else
				   proc=athlon-4
			       fi
			   elif test "$pmodel" -ge 4; then
				proc=athlon-tbird
			   else
				proc=athlon
			   fi
			   ;;
			15) 
			    # Despite what the gcc into says 'athlon64' is not accepted as 
			    # synonym for 'k8'
			   proc=k8
			   ;;
			16)
			   proc=barcelona
			   ;;
			*) proc=athlon-xp
			   ;;
		esac
		;;
	GenuineIntel)
		case "$pfamily" in
			3) proc=i386
			   ;;
			4) proc=i486
			   ;;
			5) iproc=586
			   if test "$pmodel" -eq 4 || test "$pmodel" -eq 8; then
				proc=pentium-mmx # 4 is desktop, 8 is mobile
			   else
				proc=i586
			   fi
			   ;;
			6) iproc=686
                           if test "$pmodel" -ge 23; then
                                proc=core2
                           elif test "$pmodel" -ge 15; then
                                proc=nocona
                           elif test "$pmodel" -ge 13; then
                                proc=pentium-m
			   elif test "$pmodel" -ge 7; then
				proc=pentium3
			   elif test "$pmodel" -ge 3; then
				proc=pentium2
			   else
				proc=i686
			   fi
                           ;;
			15) proc=pentium4
			   ;;
			*) proc=pentium4
			   ;;
		esac
		;;
	unknown)
		case "$pfamily" in
			3) proc=i386
			   ;;
			4) proc=i486
			   ;;
			*) proc=i586
			   ;;
  		esac
		;;
	*)
	   proc=i586
	   ;;
esac

# check that gcc supports our CPU, if not, fall back to earlier ones

cat > conftest.c << EOF
int main(void) { return 0; }
EOF
if  test "$proc" = "athlon64" ; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=athlon-xp
fi

if test "$proc" = "athlon-xp" || test "$proc" = "athlon-4" || test "$proc" = "athlon-tbird"; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=athlon
fi

if test "$proc" = "k6-3" || test "$proc" = "k6-2"; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=k6
fi

if test "$proc" = "k6"; then
    do_cc -march=$proc $_opt_mcpu=$proc
    if test $? -ne 0; then
        if do_cc -march=i586 $_opt_mcpu=i686; then
          proc=i586-i686
        else 
          proc=i586
	fi
    fi
fi

# Seems some variants of gcc accept 'core2' instead of 'nocona'.
if test "$proc" = "core2"; then
        do_cc  -march=$proc $_opt_mcpu=$proc || proc=nocona
fi

if test "$proc" = "pentium4" || test "$proc" = "pentium3" || test "$proc" = "pentium2" || test "$proc" = "athlon"; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=i686
fi
if test "$proc" = "i686" || test "$proc" = "pentium-mmx"; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=i586
fi
if test "$proc" = "i586" ; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=i486
fi
if test "$proc" = "i486" ; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=i386
fi
if test "$proc" = "i386" ; then
	do_cc -march=$proc $_opt_mcpu=$proc || proc=error
fi
if test "$proc" = "error" ; then
	echo "Your $CC does not even support \"i386\" for '-march' and $_opt_mcpu."
	_mcpu=""
	_march=""
elif test "$proc" = "i586-i686"; then
	_march="-march=i586"
	_mcpu="$_opt_mcpu=i686"
else
      _march="-march=$proc"
      _mcpu="$_opt_mcpu=$proc"
fi

if test $_cc_major -ge 3; then
   extcheck "xorps %%xmm0, %%xmm0" || _gcc3_ext="$_gcc3_ext -mno-sse"
   extcheck "xorpd %%xmm0, %%xmm0" || _gcc3_ext="$_gcc3_ext -mno-sse2"

   if test x"$_gcc3_ext" != "x"; then
    # if we had to disable sse/sse2 because the active kernel does not
    # support this instruction set extension, we also have to tell
    # gcc3 to not generate sse/sse2 instructions for normal C code
    cat > conftest.c << EOF
int main(void) { return 0; }
EOF
    do_cc $_march $_gcc3_ext && _march="$_march $_gcc3_ext"
   fi
fi

echo $_march $_mcpu
rm -f conftest.c conftest cpuinfo
return 0
}

do_ppc()
{
# Linux on a PPC has /proc/info
# Darwin (OS/X) has the hostinfo command
# If neither of those we have no idea what to do - so do nothing.
if test -r /proc/cpuinfo; then
	proc=`grep cpu /proc/cpuinfo | cut -d':' -f2 | cut -d',' -f1 | cut -b 2- | head -n 1`
elif test $IsDarwin = yes; then
	proc=`hostinfo | grep "Processor type" | cut -f3 -d' ' | sed 's/ppc//'`
else
	return 0
fi

case "$proc" in
	601) _march="$_opt_mcpu=601" _mcpu='-mtune=601'
	     ;;
	603) _march="$_opt_mcpu=603" _mcpu='-mtune=603'
	     ;;
	603e|603ev) _march="$_opt_mcpu=603e" _mcpu='-mtune=603e'
	     ;;
	604|604e|604r|604ev) _march="$_opt_mcpu=604" _mcpu='-mtune=604'
	     ;;
	740|740/750|745/755) _march="$_opt_mcpu=740" _mcpu='-mtune=740'
	     ;;
	750|750CX) _march="$_opt_mcpu=750" _mcpu='-mtune=750'
	     ;;
	*) ;;
esac

# gcc 3.1(.1) and up supports 7400 and 7450
if test "$_cc_major" -ge "3" && test "$_cc_minor" -ge "1" || test "$_cc_major" -ge "4"; then
	case "$proc" in
		7400*|7410*) _march="$_opt_mcpu=7400" _mcpu='-mtune=7400' ;;
		7450*|7455*) _march="$_opt_mcpu=7450" _mcpu='-mtune=7450' ;;
		*) ;;
	esac
fi

# gcc 3.2 and up supports 970
if test "$_cc_major" -ge "3" && test "$_cc_minor" -ge "3" || test "$_cc_major" -ge "4"; then
	case "$proc" in
	     970*) if test $IsDarwin = yes; then
		      _march="$_opt_mcpu=G5 -mpowerpc64 -mpowerpc-gpopt -falign-loops=16 -force_cpusubtype_ALL" _mcpu='-mtune=G5'
		   else
		      _march="$_opt_mcpu=970" _mcpu='-mtune=970'
		   fi
		   ;;
		*) ;;
	esac
fi

echo $_march $_mcpu
return 0
}

#
# The script that runs the various functions above
#

if test $target = x86; then
	do_x86
elif test $target = ppc; then
        do_ppc
fi
