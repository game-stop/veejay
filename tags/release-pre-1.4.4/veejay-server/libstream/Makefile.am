# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libvjnet/ \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/mjpegtools \
		$(XML2_CFLAGS) \
		$(FFMPEG_CFLAGS) $(PTHREAD_CFLAGS) $(UNICAP_CFLAGS)
STREAM_LIB_FILE = libstream.la
noinst_LTLIBRARIES = $(STREAM_LIB_FILE)
libstream_la_SOURCES =	frequencies.c v4lutils.c v4lvideo.c vj-unicap.c vj-yuv4mpeg.c vj-net.c vj-dv1394.c vj-vloopback.c vj-tag.c 
