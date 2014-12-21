# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir)\
		-I$(top_srcdir)/vjmem\
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libel 
if HAVE_MJPEGTOOLS
INCLUDES +=
else
INCLUDES += -I$(top_srcdir)/mjpegtools
endif

INCLUDES += $(XML2_CFLAGS) $(FFMPEG_CFLAGS) $(MJPEGTOOLS_CFLAGS)

SAMPLEREC_LIB_FILE = libsamplerec.la
noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)
libsamplerec_la_SOURCES =	samplerecord.c 
EXTRA_DIST = samplerecord.h
