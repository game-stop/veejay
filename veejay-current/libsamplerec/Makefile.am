# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir)\
		-I$(top_srcdir)/vjmem\
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libel \
		$(MJPEGTOOLS_CFLAGS) ${XML_CFLAGS} ${XML_CPPFLAGS} ${FFMPEG_CFLAGS}

SAMPLEREC_LIB_FILE = libsamplerec.la
noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)
libsamplerec_la_SOURCES =	samplerecord.c 
