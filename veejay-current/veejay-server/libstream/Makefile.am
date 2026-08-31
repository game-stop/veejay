# Makefile for veejay
AM_CFLAGS=$(VEEJAY_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty

AM_CPPFLAGS += $(XML2_CFLAGS) $(FFMPEG_CFLAGS) $(PTHREAD_CFLAGS) $(NDI_CFLAGS) 

STREAM_LIB_FILE = libstream.la
noinst_LTLIBRARIES = $(STREAM_LIB_FILE)
libstream_la_SOURCES =	vj-yuv4mpeg.c vj-net.c vj-avformat.c vj-dv1394.c vj-vloopback.c vj-cali.c vj-tag.c v4l2utils.c vj-ndi.c 
EXTRA_DIST = *.h
