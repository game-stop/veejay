MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir)/libplugger -I$(includedir) -I$(top_srcdir)/libhash -I$(top_srcdir)/libvevo ${FFMPEG_CFLAGS} 


AM_CFLAGS = $(CFLAGS) $(FREEFRAME_CFLAGS)

LIBVJPLUG_LIB_FILE = libvjplug.la
noinst_LTLIBRARIES = $(LIBVJPLUG_LIB_FILE)

libvjplug_la_SOURCES = utility.c freeframe-loader.c frei0r-loader.c livido-loader.c plugload.c

