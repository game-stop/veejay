# Makefile for veejay

AM_CFLAGS = $(OP_CFLAGS)

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty 

AM_CPPFLAGS += $(FFMPEG_CFLAGS) $(PIXBUF_CFLAGS) $(LIBQUICKTIME_CFLAGS)
 
VJEL_LIB_FILE = libel.la
noinst_LTLIBRARIES = $(VJEL_LIB_FILE)
libel_la_SOURCES = vj-mmap.c elcache.c avilib.c lav_io.c vj-dv.c rawdv.c pixbuf.c vj-avcodec.c vj-el.c

