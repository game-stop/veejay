#!/bin/bash


# install scripts in gveejay directory, symlink to user's dir

reloaded=`which reloaded`
srcdir=`pwd`
dstdir=""
home=$1

if [ -z $reloaded ]; then
	echo "Cannot find reloaded in PATH"
	exit 1
fi

echo "Using srcdir $srcdir"

if [ -z $home ]; then
	echo "Using $HOME for current reloaded user, use $0 /path/to/home to specify another"
	home=$HOME
fi

lndir=`echo $home/.veejay/theme`

if test -x $reloaded; then 
	dstdir=`$reloaded -V|grep ^data\ directory|cut -d ":" -f2`
	if [ -z $dstdir ]; then
		echo "Wrong version of reloaded"
		exit
	fi
else
	echo "reloaded not executable"
	exit 1
fi

themedir=$dstdir/theme

# make sure dir exists
mkdir -p $lndir 2>/dev/null
if [ ! -d $lndir ]; then
	echo "Cannot create $lndir, abort"
	exit 1
fi

# find all rc files in current dir
for rcfile in $srcdir/*.rc; do
	tmp=`basename $rcfile`
	themename=`echo $tmp|cut -d '.' -f1`
	extra=$themename.tar.bz2
	mkdir -p $themedir/$themename 2>/dev/null
	if [ ! -d $themedir/$themename ]; then
		echo "Cannot create $themedir/$themename, abort"
		exit 1
	fi
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
echo "You can now (re)start reloaded"
