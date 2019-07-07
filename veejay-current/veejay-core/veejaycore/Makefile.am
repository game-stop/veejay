# Makefile for veejay core

AM_CFLAGS = $(OP_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -DG_LOG_DOMAIN=\"veejaycore\" -DVEEJAYCORE_VERSION=\"$(VERSION)\" 

AM_CPPFLAGS += -I$(top_srcdir)/thirdparty/mjpegtools

AM_CPPFLAGS += -I /usr/X11R6/include \
		-I$(top_srcdir)/thirdparty \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjlzo \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/libvevo \
		$(LIBAVFORMAT_CFLAGS) $(LIBAVCODEC_CFLAGS) $(LIBAVUTIL_CFLAGS) $(LIBSWSCALE_CFLAGS) $(GLIB_CFLAGS) \
		$(PTHREAD_CFLAGS) 

pkginclude_HEADERS = core.h vj-task.h defs.h avhelper.h avcommon.h vims.h

VEEJAYCORE_LIB_FILE = libveejaycore.la

lib_LTLIBRARIES = $(VEEJAYCORE_LIB_FILE) 

libveejaycore_la_SOURCES = core.c vj-task.c avhelper.c 

libveejaycore_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		       -release $(LT_RELEASE) -no-undefined -export-dynamic $(DEBUGLIBS)


libveejaycore_la_LIBADD = \
			$(top_builddir)/thirdparty/libhash/libhash.la \
			$(top_builddir)/thirdparty/liblzo/libvjlzo.la \
			$(top_builddir)/libvjmsg/libvjmsg.la \
			$(top_builddir)/libvjmem/libvjmem.la \
			$(top_builddir)/libvjnet/libvjnet.la \
			$(top_builddir)/libyuv/libyuv.la \
			$(top_builddir)/thirdparty/mjpegtools/libmjpegutils.la \
			$(top_builddir)/libvevo/libvevo.la

if HAVE_ARM
libveejaycore_la_LIBADD+=$(top_builddir)/thirdparty/fastarm/libfastarm.la
endif

libveejaycore_la_LIBADD += $(LIBAVFORMAT_LIBS) $(LIBAVCODEC_LIBS) $(LIBAVUTIL_LIBS) $(LIBSWSCALE_LIBS) 

libveejaycore_la_LDFLAGS +=	$(PTHREAD_LIBS)
			$(GLIB_LIBS) \
		 	$(LIBM_LIBS) $(RT_LIBS) \
			-DDYNAMIC -O3 -Wall -rdynamic -Wl,-no-undefined 

