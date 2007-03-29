#!/bin/bash


# install scripts in gveejay directory, symlink to user's dir

reloaded=`which gveejayreloaded`
srcdir=`pwd`
dstdir=""
lndir=`echo $HOME/.veejay/theme`

if test -x $reloaded; then 
	dstdir=`$reloaded -q`
else
	echo "gveejayreloaded not found"
	exit 1
fi

themedir=$dstdir/theme

for rcfile in $srcdir/*.rc; do
	tmp=`basename $rcfile`
	themename=`echo $tmp|cut -d '.' -f1`

	if test -d $themedir/$themename ; then
		if rm -rf $themedir/$themename; then
			echo "Uninstalled $theme from $themedir"
		else
			echo "Unable to remove $themedir/$themename"
		fi
	fi
	if test -f $lndir/$themename ; then
		if rm -rf $lndir/$themename; then
			echo "Removed $lndir/$themename"
		else
			echo "Unable to remove $lndir/$themename"
		fi
	fi
done
