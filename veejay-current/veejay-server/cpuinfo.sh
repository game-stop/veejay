#!/bin/sh

# simple script that detects march setting for this computer
# only for gcc

if test x"$CC" = x; then
	CC=gcc
fi

target=$1

cc_version=`$CC -dumpversion`
_cc_major=`echo $cc_version | cut -d'.' -f1`
_cc_minor=`echo $cc_version | cut -d'.' -f2`

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


cat > conftest.c << EOF
int main(void) { return 0; }
EOF

arch=`$CC -march=native -Q --help=target|grep -- '-march='|cut -f3`
do_cc -march=$arch
if test $? -ne 0; then
	# gcc failed, lets try -dumpmachine and test specifically for arm
	# since we know that 'gcc -march=native -Q --help=target` fails on gcc 4.6.3
	machine=`$CC -dumpmachine`
	cpu=`echo $machine |cut -d '-' -f1`
	case $cpu in
		arm)
			arch=`cat /proc/cpuinfo|grep 'model name'|head -n1|cut -d ':' -f2 |cut -d ' ' -f2 | tr '[:upper:]' '[:lower:]'`	
		;;
	esac

fi

rm -rf veejay.arch
echo "-march=$arch" > veejay.arch

