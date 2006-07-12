# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include -I$(top_srcdir)/libOSC \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/libplugger \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libel \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/libvevo \
		$(FFMPEG_AVCODEC_CFLAGS) $(FFMPEG_AVFORMAT_CFLAGS) \
		${XML_CFLAGS} ${DV_FLAGS} ${GTK_CFLAGS} ${X_CFLAGS} ${XML_CPPFLAGS} \
		${PTHREAD_CFLAGS} ${MJPEGTOOLS_CFLAGS} ${UNICAP_CFLAGS} 

VEVOSAMPLE_LIB_FILE = libvevosample.la

lib_LTLIBRARIES = $(VEVOSAMPLE_LIB_FILE)

VEVOSAMPLE_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic

libvevosample_la_SOURCES = vj-unicap.c vj-yuv4mpeg.c vevosample.c
libvevosample_la_LIBADD = \
	-L$(top_builddir)/libhash -lhash \
	-L$(top_builddir)/libvjmsg -lvjmsg \
	-L$(top_builddir)/libvjmem -lvjmem \
	-L$(top_builddir)/libel -lel \
	-L$(top_builddir)/libyuv -lyuv \
	-L$(top_builddir)/libvjnet -lvjnet \
	-L$(top_builddir)/libvevo -lvevo \
	-L$(top_builddir)/libplugger -lvjplug 
