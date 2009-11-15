# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(YUVCFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/aclib  -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libpostproc 
YUV_LIB_FILE = libyuv.la
pkginclude_HEADERS = mmx.h yuvconv.h 
noinst_LTLIBRARIES = $(YUV_LIB_FILE)
libyuv_la_SOURCES =	subsample.c yuvconv.c
