# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libhash \
			${MJPEGTOOLS_CFLAGS} ${FFMPEG_CFLAGS} ${XML2_CFLAGS}
AM_CFLAGS=$(OP_CFLAGS)
SAMPLE_LIB_FILE = libsample.la
noinst_LTLIBRARIES = $(SAMPLE_LIB_FILE)
libsample_la_SOURCES = sampleadm.c 

