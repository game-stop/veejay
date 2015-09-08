# Makefile for veejay

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg \
              -I$(top_srcdir)/libel
if HAVE_MJPEGTOOLS
AM_CPPFLAGS +=
else
AM_CPPFLAGS += -I$(top_srcdir)/thirdparty/mjpegtools
endif

AM_CPPFLAGS += $(XML2_CFLAGS) $(FFMPEG_CFLAGS) $(MJPEGTOOLS_CFLAGS)

SAMPLEREC_LIB_FILE = libsamplerec.la
noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)
libsamplerec_la_SOURCES =	samplerecord.c 
EXTRA_DIST = samplerecord.h
