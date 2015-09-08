# Makefile for veejay

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg -I$(top_srcdir)/libhash 
if HAVE_MJPEGTOOLS
AM_CPPFLAGS +=
else
AM_CPPFLAGS += -I$(top_srcdir)/mjpegtools
endif

AM_CPPFLAGS += $(FFMPEG_CFLAGS) $(XML2_CFLAGS) $(MJPEGTOOLS_CFLAGS)

SAMPLE_LIB_FILE = libsample.la
noinst_LTLIBRARIES = $(SAMPLE_LIB_FILE)
libsample_la_SOURCES = sampleadm.c 
EXTRA_DIST= sampleadm.h
