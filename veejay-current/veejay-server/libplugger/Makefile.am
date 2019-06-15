MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir)/libplugger -I$(includedir) \
              -I$(top_srcdir)/thirdparty

AM_CPPFLAGS += ${FFMPEG_CFLAGS} 

AM_CFLAGS = $(OP_CFLAGS) $(FREEFRAME_CFLAGS)

LIBVJPLUG_LIB_FILE = libvjplug.la
noinst_LTLIBRARIES = $(LIBVJPLUG_LIB_FILE)

libvjplug_la_SOURCES = defaults.c utility.c freeframe-loader.c frei0r-loader.c livido-loader.c plugload.c

