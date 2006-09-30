# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libvjnet/ \
		-I$(top_srcdir)/libhash \
		${XML_CFLAGS} ${XML_CPPFLAGS} \
		$(MJPEGTOOLS_CFLAGS) ${FFMPEG_CFLAGS} ${PTHREAD_CFLAGS}
STREAM_LIB_FILE = libstream.la
noinst_LTLIBRARIES = $(STREAM_LIB_FILE)
libstream_la_SOURCES =	v4lutils.c vj-v4lvideo.c vj-yuv4mpeg.c vj-shm.c vj-net.c vj-dv1394.c vj-vloopback.c vj-tag.c 
