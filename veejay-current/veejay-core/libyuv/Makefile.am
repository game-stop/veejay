# Makefile for veejay
AM_CFLAGS = $(OP_CFLAGS) -O3 -fopt-info-vec-optimized -ftree-vectorize
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg \
              -I$(top_srcdir)/libpostproc

AM_CPPFLAGS += $(FFMPEG_CFLAGS)

YUV_LIB_FILE = libyuv.la
pkginclude_HEADERS=yuvconv.h mmx.h mmx_macros.h
noinst_LTLIBRARIES = $(YUV_LIB_FILE)
libyuv_la_SOURCES = yuvconv.c
