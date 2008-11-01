# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(YUVCFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libpostproc 
YUV_LIB_FILE = libyuv.la
noinst_LTLIBRARIES = $(YUV_LIB_FILE)
libyuv_la_SOURCES =	subsample.c yuvconv.c
