# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS = $(OP_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg

AM_CPPFLAGS += $(FFMPEG_CFLAGS)

RESAMPLE_LIB_FILE = libresample.la
noinst_LTLIBRARIES = $(RESAMPLE_LIB_FILE)
libresample_la_SOURCES = audioconvert.c resample.c resample2.c
EXTRA_DIST= 
