# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir)\
		-I$(top_srcdir)/vjmem\
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/libel \
		${XML_CFLAGS} ${XML_CPPFLAGS}\
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavcodec \
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavformat \
		-I$(top_srcdir)/utils

SAMPLEREC_LIB_FILE = libsamplerec.la

SAMPLEREC_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(SAMPLEREC_LIB_FILE)

libsamplerec_la_SOURCES =	samplerecord.c 


libsamplerec_la_LDFLAGS = $(SAMPLEREC_ALL_LIB_OPTS) \
			-L$(top_builddir)/libvjmem -lvjmem \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libel -lel \
			${XML_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

