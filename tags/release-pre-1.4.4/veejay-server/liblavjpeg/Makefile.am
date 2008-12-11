# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/libel -I$(top_srcdir)/mjpegtools $(JPEG_CFLAGS)
LAVJPEG_LIB_FILE = liblavjpeg.la
noinst_LTLIBRARIES = $(LAVJPEG_LIB_FILE)
liblavjpeg_la_SOURCES = jpegutils.c
