# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) ${JPEG_CFLAGS} -I$(top_srcdir)/utils -I$(top_srcdir)/libel

LAVJPEG_LIB_OPTS = -export-dynamic 

LAVJPEG_LIB_FILE = liblavjpeg.la

noinst_LTLIBRARIES = $(LAVJPEG_LIB_FILE)

liblavjpeg_la_SOURCES = jpegutils.c

liblavjpeg_la_LDFLAGS = $(LAVJPEG_LIB_OPTS) $(JPEG_LIBS) \
			-DDYNAMIC -O3 -Wall 
