# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem \
		-I$(top_srcdir)/vjmsg \
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavcodec \
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavformat \
		-I$(top_srcdir)/utils 

VJEL_LIB_OPTS = -export-dynamic 

VJEL_LIB_FILE = libel.la

noinst_LTLIBRARIES = $(VJEL_LIB_FILE)

libel_la_SOURCES =	avilib.c lav_io.c vj-dv.c vj-avcodec.c vj-avformat.c vj-el.c


libel_la_LDFLAGS = $(VJEL_LIB_OPTS) \
		-L$(top_builddir)/ffmpeg/ffmpeg/libavcodec -lavcodec \
		-L$(top_builddir)/ffmpeg/ffmpeg/libavformat -lavformat \
		-L$(top_builddir)/libvjmem -lvjmem \
		-L$(top_builddir)/libvjmsg -lvjmsg \
		-DDYNAMIC -O3 -Wall
