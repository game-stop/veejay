# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) ${JPEG_CFLAGS} -I$(top_srcdir)/libel $(MJPEGTOOLS_CFLAGS) $(JPEG_CFLAGS)
LAVJPEG_LIB_FILE = liblavjpeg.la
noinst_LTLIBRARIES = $(LAVJPEG_LIB_FILE)
liblavjpeg_la_SOURCES = jpegutils.c
