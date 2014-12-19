#!/bin/bash

# veejay build script 

SCRIPT=`whereis script|cut -d ':' -f1`
RUNSCRIPT="-a -c"
FILE="veejay.build.log"
if [ -n "$SCRIPT" ]; then
	echo "Enabling typescript for logging";
fi

if [ -f autogen.sh ]; then
	echo "Bootstrapping ..."
	$SCRIPT $RUNSCRIPT./autogen.sh $FILE > /dev/null
fi

PKGCONFIG=`echo $PKG_CONFIG_PATH`
if [ ! -n "$PKGCONFIG" ]; then
	echo "Guess that your .pc files are in /usr/lib/pkgconfig and /usr/local/pkgconfig"
	export PKG_CONFIG_PATH=/usr/lib/pkgconfig:/usr/local/lib/pkgconfig
fi

if [ -f configure ]; then
	echo "Configuring ..."
	$SCRIPT $RUNSCRIPT"./configure --enable-debug" $FILE > /dev/null
	RET=$?
	if [ $RET = 0 ]; then
		tail -n38 $FILE
		echo "Building veejay, please wait."
		$SCRIPT $RUNSCRIPT make $FILE > /dev/null
		RET=$?
		if [ $RET = 0 ]; then
			echo "Veejay build completed. You can continue with 'make install'"
			echo ""
			echo "Open another Terminal and type 'veejay myvideo.avi"
			echo "Open another Terminal and type 'reloaded'"
			exit
		else
			echo "Build error in Make, please send $FILE to the Veejay Group"
			echo "http://groups.google.com/group/veejay-discussion/topics"
			exit
		fi
	else
		echo "Please run ./configure manually to see which dependencies are missing."
		echo "On ubuntu/debian systems you will most likely need to install the *-dev packages"
		echo "According to http://www.veejayhq.net/2009/01/veejay-howto-compile/ you could use:"
		echo "$ sudo apt-get install build-essential autogen autotools-dev autoconf automake1.8 libtool libsdl1.2-dev \
libjack0.100.0-dev libquicktime-dev libxml2-dev libglade2-dev libgtk2.0-dev \
libavcodec-dev libjpeg62-dev libavformat-dev libswscale-dev libdv-dev xorg-dev libasound-dev \
libsamplerate-dev
"
		exit
	fi
fi
