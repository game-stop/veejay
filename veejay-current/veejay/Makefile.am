# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include -I$(top_srcdir)/libOSC -I$(top_srcdir)/utils \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libel \
		-I$(top_srcdir)/libstream \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/liblavjpeg \
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavcodec \
		-I$(top_srcdir)/ffmpeg/ffmpeg/libavformat \
		$(AVIPLAY_CFLAGS) ${XML_CFLAGS} ${GTK_CFLAGS} ${DV_FLAGS} ${X_CFLAGS} ${XML_CPPFLAGS} \
		$(DIRECTFB_CFLAGS) ${SDL_CFLAGS} ${JPEG_CFLAGS} ${PTHREAD_CFLAGS} ${SAMPLERATE_CFLAGS} 

VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

VEEJAY_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

libveejay_la_SOURCES =  vj-misc.c hash.c \
			vj-plugin.c vj-osc.c vj-event.c vj-pjack.c vj-perform.c \
			vj-sdl.c vj-dfb.c samplerecord.c \
			liblavplayvj.c


libveejay_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS) \
			-L$(top_builddir)/ffmpeg/ffmpeg/libavcodec -lavcodec \
			-L$(top_builddir)/ffmpeg/ffmpeg/libavformat -lavformat \
			-L$(top_builddir)/libOSC / -lOSC \
			-L$(top_builddir)/utils/ -lmjpegutils \
			-L$(top_builddir)/bio2jack/ -lbio2jack \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem/ -lvjmem \
			-L$(top_builddir)/libvje -lvje \
			-L$(top_builddir)/libsample -lsample \
			-L$(top_builddir)/libvjnet -lvjnet \
			-L$(top_builddir)/libel -lel \
			-L$(top_builddir)/libstream -lstream \
			-L$(top_builddir)/libyuv -lyuv \
			-L$(top_builddir)/liblavjpeg -llavjpeg \
			${SDL_LIBS} ${DIRECTFB_LIBS} ${PTHREAD_LIBS} ${X_LIBS} \
			${XML_LIBS} ${JPEG_LIBS} ${JACK_LIBS} \
			${DV_LIBS} ${SAMPLERATE_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)  

veejay_headers = vims.h

veejay_SOURCES	= veejay.c ${veejay_headers} 
veejay_LDADD 	= libveejay.la @LIBGETOPT_LIB@ -L$(top_builddir)/libvjnet -lvjnet -L$(top_builddir)/ffmpeg/ffmpeg/libavcodec -lavcodec
