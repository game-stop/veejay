# Makefile for veejay
AM_CFLAGS = $(OP_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/libel

AM_CPPFLAGS += $(XML2_CFLAGS) $(FFMPEG_CFLAGS)

SAMPLEREC_LIB_FILE = libsamplerec.la
noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)
libsamplerec_la_SOURCES =	samplerecord.c 
EXTRA_DIST = samplerecord.h
