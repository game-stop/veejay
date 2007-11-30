# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include -I$(top_srcdir)/libOSC \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libel \
		-I$(top_srcdir)/libstream \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/liblavjpeg \
		-I$(top_srcdir)/libsamplerec \
		-I$(top_srcdir)/libvevo \
		-I$(top_srcdir)/liblzo \
		-I$(top_srcdir)/bio2jack \
		$(FFMPEG_AVCODEC_CFLAGS) $(FFMPEG_AVFORMAT_CFLAGS) $(LIBQUICKTIME_CFLAGS) ${FT_CFLAGS} \
		$(AVIPLAY_CFLAGS) ${XML2_CFLAGS} ${GTK_CFLAGS} ${DV_FLAGS} ${X_CFLAGS} ${GL_CFLAGS} \
		$(DIRECTFB_CFLAGS) ${SDL_CFLAGS} ${JPEG_CFLAGS} ${PTHREAD_CFLAGS} ${SAMPLERATE_CFLAGS} ${MJPEGTOOLS_CFLAGS}
 
pkginclude_HEADERS = vims.h 
VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

VEEJAY_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

libveejay_la_SOURCES =  vj-misc.c \
			vj-osc.c vjkf.c vj-event.c vj-eventman.c vj-pjack.c vj-perform.c \
			x11misc.c vj-sdl.c vj-dfb.c gl.c vj-viewport.c vj-composite.c \
			vj-font.c liblavplayvj.c 

libveejay_la_LDFLAGS = $(VEEJAY_ALL_LIB_OPTS ) $(DEBUGLIBS)


libveejay_la_LIBADD = -L$(top_builddir)/libOSC / -lOSC \
			-L$(top_builddir)/libhash / -lhash \
			-L$(top_builddir)/liblzo / -lminilzo \
			-L$(top_builddir)/bio2jack/ -lbio2jack4vj \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem/ -lvjmem \
			-L$(top_builddir)/libvje -lvje \
			-L$(top_builddir)/libsample -lsample \
			-L$(top_builddir)/libvjnet -lvjnet \
			-L$(top_builddir)/libel -lel \
			-L$(top_builddir)/libstream -lstream \
			-L$(top_builddir)/aclib -lac \
			-L$(top_builddir)/libyuv -lyuv \
			-L$(top_builddir)/liblavjpeg -llavjpeg \
			-L$(top_builddir)/libsamplerec -lsamplerec \
			-L$(top_builddir)/libvevo -lvevo \
			-L$(top_builddir)/libgoom -lgoom 

libveejay_la_LDFLAGS +=	${SDL_LIBS} ${DIRECTFB_LIBS} ${PTHREAD_LIBS} ${FT_LDFLAGS} ${FT_LIBS} \
			${XML2_LIBS} ${JPEG_LIBS} ${JACK_LIBS} ${UNICAP_LIBS} \
		 	${FFMPEG_AVFORMAT_LIBS} ${FFMPEG_AVCODEC_LIBS} ${FFMPEG_SWSCALER_LIBS} ${XINERAMA_LIBS} \
			${LIBDV_LIBS} ${LIBM_LIBS} ${PIXBUF_LIBS} ${GL_LIBS} ${MJPEGTOOLS_LIBS} ${LIBQUICKTIME_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)  

veejay_headers = vims.h

veejay_SOURCES	= veejay.c ${veejay_headers} 
veejay_LDADD 	= libveejay.la @LIBGETOPT_LIB@

