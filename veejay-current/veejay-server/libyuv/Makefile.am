# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/thirdparty  -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libpostproc -I$(FFMPEG_CFLAGS) 
AM_CFLAGS = $(OPTCFLAGS)

YUV_LIB_FILE = libyuv.la
pkginclude_HEADERS=yuvconv.h
EXTRA_DIST = mmx.h yuvconv.h mmx_macros.h 
noinst_LTLIBRARIES = $(YUV_LIB_FILE)
libyuv_la_SOURCES = yuvconv.c
