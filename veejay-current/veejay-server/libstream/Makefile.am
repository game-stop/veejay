# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libvjnet/ \
		-I$(top_srcdir)/libhash 

if HAVE_MJPEGTOOLS
INCLUDES +=
else
INCLUDES += -I$(top_srcdir)/mjpegtools 
endif

INCLUDES += $(XML2_CFLAGS) $(FFMPEG_CFLAGS) $(PTHREAD_CFLAGS) $(MJPEGTOOLS_CFLAGS)

STREAM_LIB_FILE = libstream.la
noinst_LTLIBRARIES = $(STREAM_LIB_FILE)
libstream_la_SOURCES =	frequencies.c v4lutils.c v4lvideo.c vj-yuv4mpeg.c vj-net.c vj-dv1394.c vj-vloopback.c vj-tag.c v4l2utils.c 
pkginclude_HEADERS = vj-net.h 
EXTRA_DIST = dv1394.h v4lutils.h v4l2utils.h vj-dv1394.h vj-tag.h vj-vloopback.h \
                     frequencies.h v4lvideo.h vj-net.h vj-yuv4mpeg.h
