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
	extra=$themename.tar.bz2
	mkdir -p $themedir/$themename 2>/dev/null
	if test -f $extra ; then
		cd $themedir/$themename/
		if tar -jxvf $srcdir/$extra >/dev/null ; then
			echo "   Extracted $extra in `pwd`"
		fi	
		echo "$themedir/$themename"
		if cp $rcfile $themedir/$themename/; then
			ln -s $themedir/$themename $lndir/$themename 2>/dev/null
			ln -s $lndir/$themename/$tmp $themedir/$themename/gveejay.rc 2>/dev/null
		fi
		cd $srcdir		
	else
		mkdir $lndir/$themename 2>/dev/null
		if cp $rcfile $themedir/$themename/; then
			ln -s $themedir/$themename/$tmp $lndir/$themename/gveejay.rc 2>/dev/null
			echo "Installed $themename to $themedir"
		fi

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
