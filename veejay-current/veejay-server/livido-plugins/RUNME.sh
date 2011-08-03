#!/bin/bash

OUTDIR=$1

MY_CC=gcc
MY_CFLAGS="-g -DIS_LIVIDO_PLUGIN -DSTRICT_CHECKING"
MY_INCLUDES="-I `pwd`"


for i in lvd*.c ; do

	bname=`echo \`basename $i\` | cut -d '.' -f1`
	modulename=$bname.so
	
	echo $MY_CC $MY_CFLAGS $MY_INCLUDES -shared -o $OUTDIR/$modulename
	$MY_CC $MY_CFLAGS $MY_INCLUDES -shared $i -o $OUTDIR/$modulename || exit 1

done;
