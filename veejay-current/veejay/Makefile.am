# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir)/ffmpeg/ffmpeg/libavcodec -I$(top_srcdir)/ffmpeg/ffmpeg/libavformat \
		-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include -I$(top_srcdir)/libOSC -I$(top_srcdir)/utils \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		$(AVIPLAY_CFLAGS) ${XML_CFLAGS} ${GTK_CFLAGS} ${DV_FLAGS} ${X_CFLAGS} ${XML_CPPFLAGS} \
		$(DIRECTFB_CFLAGS) ${SDL_CFLAGS} ${JPEG_CFLAGS} ${PTHREAD_CFLAGS} ${SAMPLERATE_CFLAGS} 

VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

noinst_LTLIBRARIES = liblavjpeg.la 



VEEJAY_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

liblavjpeg_la_SOURCES = jpegutils.c 
liblavjpeg_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS) $(JPEG_LIBS)

libveejay_la_SOURCES =  vj-misc.c subsample.c hash.c avilib.c vj-shm.c lav_io.c \
			v4lutils.c vj-plugin.c vj-osc.c vj-event.c vj-pjack.c vj-perform.c \
			vj-sdl.c vj-dfb.c samplerecord.c \
			vj-yuv4mpeg.c vj-v4lvideo.c vj-dv.c vj-avcodec.c vj-tag.c vj-avformat.c vj-el.c \
			liblavplayvj.c


libveejay_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS) \
			-L$(top_builddir)/ffmpeg/ffmpeg/libavcodec/ -lavcodec \
			-L$(top_builddir)/ffmpeg/ffmpeg/libavformat/ -lavformat \
			-L$(top_builddir)/libOSC / -lOSC \
			-L$(top_builddir)/utils/ -lmjpegutils \
			-L$(top_builddir)/bio2jack/ -lbio2jack \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem/ -lvjmem \
			-L$(top_builddir)/libvje -lvje \
			-L$(top_builddir)/libsample -lsample \
			-L$(top_builddir)/libvjnet -lvjnet \
			${SDL_LIBS} ${DIRECTFB_LIBS} ${PTHREAD_LIBS} ${X_LIBS} \
			${XML_LIBS} ${JPEG_LIBS} \
			${DV_LIBS} ${SAMPLERATE_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)  

veejay_headers = vj-lib.h vj-event.h libveejay.h vj-misc.h

veejay_SOURCES = veejay.c ${veejay_headers} 
veejay_LDADD =  libveejay.la liblavjpeg.la @LIBGETOPT_LIB@ @JACK_LIBS@ 
