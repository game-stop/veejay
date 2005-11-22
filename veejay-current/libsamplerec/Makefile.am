# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir)\
		-I$(top_srcdir)/vjmem\
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libel \
		${XML_CFLAGS} ${XML_CPPFLAGS} ${FFMPEG_CFLAGS}\
		-I$(top_srcdir)/utils

SAMPLEREC_LIB_FILE = libsamplerec.la
noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)
libsamplerec_la_SOURCES =	samplerecord.c 
