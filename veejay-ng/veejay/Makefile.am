# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libplugger \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libel \
		-I$(top_srcdir)/libstream \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/liblavjpeg \
		-I$(top_srcdir)/libsamplerec \
		-I$(top_srcdir)/libvevo \
		$(FFMPEG_CFLAGS)\
		${XML_CFLAGS} ${DV_FLAGS} ${X_CFLAGS} ${XML_CPPFLAGS} \
		${PTHREAD_CFLAGS} ${MJPEGTOOLS_CFLAGS} ${GL_CFLAGS} ${SDL_CFLAGS} ${LIBLO_CFLAGS} 

VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

VEEJAY_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

libveejay_la_SOURCES =  vj-misc.c \
			vj-pjack.c performer.c \
			gl.c vj-sdl.c \
			oscsend.c \
			oscservit.c \
			oscevents.c \
			liblavplayvj.c

libveejay_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS )

libveejay_la_LIBADD = -L$(top_builddir)/libhash / -lhash \
			-L$(top_builddir)/bio2jack/ -lbio2jack \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem/ -lvjmem \
			-L$(top_builddir)/libplugger -lvjplug \
			-L$(top_builddir)/libvjnet -lvjnet \
			-L$(top_builddir)/libel -lel \
			-L$(top_builddir)/libyuv -lyuv \
			-L$(top_builddir)/libvevo -lvevo \
			-L$(top_builddir)/libvjaudio -lvjaudio \
			-L$(top_builddir)/vevosample -lvevosample 

#libveejay_la_LDFLAGS +=	${FFMPEG_LIBS} \
#			${SDL_LIBS} ${DIRECTFB_LIBS} ${PTHREAD_LIBS} \
#			${XML_LIBS} ${JPEG_LIBS} ${JACK_LIBS} \
#			${LIBDV_LIBS} ${LIBM_LIBS} ${PIXBUF_LIBS} ${MJPEGTOOLS_LIBS}\
#			-DDYNAMIC -O3 -Wall -rdynamic 
#


# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)  

veejay_SOURCES	= veejay.c  
veejay_LDADD 	= libveejay.la @LIBGETOPT_LIB@ ${FFMPEG_AVCODEC_LIBS} ${FFMPEG_AVFORMAT_LIBS} \
			${PTHREAD_LIBS} ${XML_LIBS} ${LIBDV_LIBS} ${GL_LIBS} \
			${MJPEGTOOLS_LIBS} ${UNICAP_LIBS} ${LIBM_LIBS}  ${SDL_LIBS} ${LIBLO_LIBS}
