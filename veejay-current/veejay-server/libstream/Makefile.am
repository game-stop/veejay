# Makefile for veejay
AM_CFLAGS=$(OP_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty

AM_CPPFLAGS += $(XML2_CFLAGS) $(FFMPEG_CFLAGS) $(PTHREAD_CFLAGS) 

STREAM_LIB_FILE = libstream.la
noinst_LTLIBRARIES = $(STREAM_LIB_FILE)
libstream_la_SOURCES =	vj-yuv4mpeg.c vj-net.c vj-avformat.c vj-dv1394.c vj-vloopback.c vj-tag.c v4l2utils.c 
EXTRA_DIST = dv1394.h v4lutils.h v4l2utils.h vj-dv1394.h vj-tag.h vj-vloopback.h v4lvideo.h vj-net.h vj-yuv4mpeg.h
