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

# make sure dir exists
mkdir -p $lndir 2>/dev/null

# find all rc files in current dir
for rcfile in $srcdir/*.rc; do
	tmp=`basename $rcfile`
	themename=`echo $tmp|cut -d '.' -f1`
	mkdir $themedir/$themename 2>/dev/null
	mkdir $lndir/$themename 2> /dev/null
	if cp $rcfile $themedir/$themename/; then
		ln -s $themedir/$themename/$tmp $lndir/$themename/gveejay.rc 2>/dev/null
		echo "Installed $themename to $themedir"
	fi
done

if test -f $lndir/theme.config ; then
	content=`cat $lndir/theme.config`
	echo "Current theme is $content"
else
	echo "Default" > $lndir/theme.config
	echo "Using default theme"
fi
echo "You can now (re)start gveejayreloaded"
