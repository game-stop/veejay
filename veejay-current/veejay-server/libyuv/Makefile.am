# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/aclib  -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libpostproc -I$(FFMPEG_CFLAGS) 
YUV_LIB_FILE = libyuv.la
pkginclude_HEADERS=yuvconv.h
EXTRA_DIST = mmx.h yuvconv.h mmx_macros.h 
noinst_LTLIBRARIES = $(YUV_LIB_FILE)
libyuv_la_SOURCES = yuvconv.c
