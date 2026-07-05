#!/bin/sh
# Run this to generate all the initial makefiles, etc.

# if you use autoconf 2.64 or earlier,
# you may have to create the m4 directory yourself
# 

if [ ! -d m4 ]; then	
	mkdir m4
fi

autoreconf -v -fi -I m4
