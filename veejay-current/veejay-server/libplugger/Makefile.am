MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir)/libplugger -I$(includedir) -I$(top_srcdir)/libhash -I$(top_srcdir)/libvevo ${FFMPEG_CFLAGS} 


LIBVJPLUG_LIB_FILE = libvjplug.la
noinst_LTLIBRARIES = $(LIBVJPLUG_LIB_FILE)

libvjplug_la_SOURCES = defaults.c utility.c weed-utils.c freeframe-loader.c frei0r-loader.c livido-loader.c plugload.c

