# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg 

YUV_LIB_OPTS = -export-dynamic 

YUV_LIB_FILE = libyuv.la

noinst_LTLIBRARIES = $(YUV_LIB_FILE)

libyuv_la_SOURCES =	subsample.c yuvconv.c


libyuv_la_LDFLAGS = $(YUV_LIB_OPTS) \
			-L$(top_builddir)/libvjmem -lvjmem \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-DDYNAMIC -O3 -Wall 
