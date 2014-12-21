#!/bin/sh

# simple script that tests if march=native works
# override auto detection with <cpu_type> as argument 1
# generic | native | core2 | etc
# see gcc list of cpu types for march option

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

arch=native
do_cc -march=native
if test $? -ne 0; then
	if do_cc -march=generic; then
		arch=generic
	else
		arch=
	fi
else
	arch=native
fi
	  
do_cc -march=$target
if test $? -eq 0; then
	arch=$target
fi

echo "-march=$arch"

