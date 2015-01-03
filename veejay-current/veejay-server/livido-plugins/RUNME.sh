#!/bin/bash
#FIXME: replace me with real build tool
OUTDIR=$1

MY_CC=gcc
MY_CFLAGS="-g -fPIC -DIS_LIVIDO_PLUGIN"

MY_INCLUDES="-I `pwd`"


for i in lvd*.c ; do

	bname=`echo \`basename $i\` | cut -d '.' -f1`
	modulename=$bname.so
	
	echo $MY_CC $MY_CFLAGS $MY_INCLUDES `pkg-config --libs libswscale` `pkg-config --cflags libswscale`  -shared -o $OUTDIR/$modulename
	$MY_CC $MY_CFLAGS $MY_INCLUDES -shared $i -o $OUTDIR/$modulename || exit 1

done;
