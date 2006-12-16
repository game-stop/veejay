# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		$(MJPEGTOOLS_CFLAGS) ${FFMPEG_CFLAGS} ${PIXBUF_CFLAGS} \
		$(LIBQUICKTIME_CFLAGS)
 
VJEL_LIB_FILE = libel.la
noinst_LTLIBRARIES = $(VJEL_LIB_FILE)
libel_la_SOURCES = vj-mmap.c elcache.c avilib.c lav_io.c vj-dv.c rawdv.c pixbuf.c vj-avcodec.c vj-avformat.c vj-el.c
