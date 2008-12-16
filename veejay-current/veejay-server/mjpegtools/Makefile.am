MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = -I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/mjpegtools  $(PTHREAD_CFLAGS)
MJPEGUTILS_LIB_FILE = libmjpegutils.la
noinst_LTLIBRARIES = $(MJPEGUTILS_LIB_FILE)
libmjpegutils_la_SOURCES = mjpeg_logging.c mpegconsts.c mpegtimecode.c yuv4mpeg.c yuv4mpeg_ratio.c 
