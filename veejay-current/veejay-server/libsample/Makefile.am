# Makefile for veejay
AM_CFLAGS = $(OP_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty 

AM_CPPFLAGS += $(FFMPEG_CFLAGS) $(XML2_CFLAGS) 

SAMPLE_LIB_FILE = libsample.la
noinst_LTLIBRARIES = $(SAMPLE_LIB_FILE)
libsample_la_SOURCES = sampleadm.c 
EXTRA_DIST= sampleadm.h
