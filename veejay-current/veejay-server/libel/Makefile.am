# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES =	-I$(top_srcdir) \
	 	-I$(includedir) \
	 	-I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg 


if HAVE_MJPEGTOOLS
INCLUDES +=
else
INCLUDES += -I$(top_srcdir)/mjpegtools
endif

INCLUDES += $(FFMPEG_CFLAGS) $(PIXBUF_CFLAGS) $(LIBQUICKTIME_CFLAGS) $(MJPEGTOOLS_CFLAGS)
 
VJEL_LIB_FILE = libel.la
noinst_LTLIBRARIES = $(VJEL_LIB_FILE)
libel_la_SOURCES = vj-mmap.c elcache.c avilib.c lav_io.c vj-dv.c rawdv.c pixbuf.c vj-avcodec.c avhelper.c vj-el.c

EXTRA_DIST = avilib.h elcache.h lav_io.h pixbuf.h \
                     rawdv.h vj-avcodec.h vj-dv.h vj-el.h vj-mmap.h
