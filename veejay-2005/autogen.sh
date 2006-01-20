#!/bin/sh
# Run this to generate all the initial makefiles, etc.

if [ ! -d "ffmpeg" ]
then
	sh ./ffmpeg-configure.in
fi

autoreconf -v -f -i
