#!/bin/sh
# Run this to generate all the initial makefiles, etc.

set -e

ARGV0=$0

LIBTOOLIZE=${LIBTOOLIZE-libtoolize}
LIBTOOLIZE_FLAGS="--copy --force"
ACLOCAL=${ACLOCAL-aclocal}
AUTOHEADER=${AUTOHEADER-autoheader}
AUTOMAKE=${AUTOMAKE-automake}
AUTOMAKE_FLAGS="--add-missing"
AUTOCONF=${AUTOCONF-autoconf}


if test -z "$ACLOCAL_FLAGS"; then
    acdir=`$ACLOCAL --print-ac-dir`
    if [ ! -f $acdir/pkg.m4 ]; then
	echo "$ARGV0: Error: Could not find pkg-config macros."
	echo "        (Looked in $acdir/pkg.m4)"
	echo "        If pkg.m4 is available in /another/directory, please set"
	echo "        ACLOCAL_FLAGS=\"-I /another/directory\""
	echo "        Otherwise, please install pkg-config."
	echo ""
	echo "pkg-config is available from:"
	echo "http://www.freedesktop.org/software/pkgconfig/"
	exit 1
    fi
fi

if test -z "$*"; then
  echo "$ARGV0:	Note: \`./configure' will be run with no arguments."
  echo "		If you wish to pass any to it, please specify them on the"
  echo "		\`$0' command line."
  echo
fi

do_cmd() {
    echo "$ARGV0: running \`$@'"
    $@
}

do_cmd $LIBTOOLIZE ${LIBTOOLIZE_FLAGS}

do_cmd $ACLOCAL ${ACLOCAL_FLAGS}

do_cmd $AUTOHEADER

do_cmd $AUTOMAKE --add-missing

do_cmd $AUTOCONF

do_cmd ./configure --enable-maintainer-mode ${1+"$@"} && echo "Now type \`make' to compile" || exit 1
