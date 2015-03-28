# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" 

if HAVE_MJPEGTOOLS
INCLUDES +=
else
INCLUDES += -I$(top_srcdir)/mjpegtools 
endif

INCLUDES += -I /usr/X11R6/include -I$(top_srcdir)/libOSC \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libplugger \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libel \
		-I$(top_srcdir)/libstream \
		-I$(top_srcdir)/libsubsample \
		-I$(top_scrdir)/libyuv \
		-I$(top_srcdir)/libsamplerec \
		-I$(top_srcdir)/libvevo \
		-I$(top_srcdir)/liblzo \
		-I$(top_srcdir)/libqrwrap \
		-I$(top_srcdir)/bio2jack \
		$(FFMPEG_CFLAGS) $(LIBQUICKTIME_CFLAGS) $(FT_CFLAGS) $(MJPEGTOOLS_CFLAGS) \
		$(AVIPLAY_CFLAGS) $(XML2_CFLAGS) $(GTK_CFLAGS) $(DV_FLAGS) $(X_CFLAGS) $(LIBLO_CFLAGS) \
		$(DIRECTFB_CFLAGS) $(SDL_CFLAGS) $(SDL_TTF_CFLAGS) $(JPEG_CFLAGS) $(JACK_CFLAGS) $(LIBUNWIND_CFLAGS) $(PTHREAD_CFLAGS) $(LIBQRENCODE_CFLAGS)

pkginclude_HEADERS = vims.h

VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

libveejay_la_SOURCES =  jpegutils.c vj-misc.c vj-task.c \
			vj-osc.c vjkf.c vj-event.c vj-eventman.c vj-perform.c \
			x11misc.c vj-shm.c vj-sdl.c vj-sdl-font.c vj-dfb.c vj-viewport.c vj-composite.c \
			vj-font.c vj-pjack.c vj-share.c liblavplayvj.c 

libveejay_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		       -release $(LT_RELEASE) -no-undefined -export-dynamic $(DEBUGLIBS)


libveejay_la_LIBADD = 	-L$(top_builddir)/libOSC  -lOSC \
			-L$(top_builddir)/libhash  -lhash \
			-L$(top_builddir)/liblzo  -lvjlzo \
			-L$(top_builddir)/bio2jack -lbio2jack4vj \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem -lvjmem \
			-L$(top_builddir)/libvje -lvje \
			-L$(top_builddir)/libplugger -lvjplug \
			-L$(top_builddir)/libsample -lsample \
			-L$(top_builddir)/libvjnet -lvjnet \
			-L$(top_builddir)/libel -lel \
			-L$(top_builddir)/libstream -lstream \
			-L$(top_builddir)/aclib -lac \
			-L$(top_builddir)/libsubsample -lsubsample \
			-L$(top_builddir)/libyuv -lyuv \
			-L$(top_builddir)/libsamplerec -lsamplerec \
			-L$(top_builddir)/libqrwrap -lqrwrap \
			-L$(top_builddir)/libvevo -lvevo 

if !HAVE_MJPEGTOOLS
libveejay_la_LIBADD+=-L$(top_builddir)/mjpegtools -lmjpegutils
endif

libveejay_la_LDFLAGS +=	$(SDL_LIBS) $(SDL_TTF_LIBS) $(DIRECTFB_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(FT_LDFLAGS) $(FT_LIBS) \
			$(XML2_LIBS) $(JPEG_LIBS) $(LIBLO_LIBS) $(LIBUNWIND_LIBS) \
		 	$(FFMPEG_LIBS) $(XINERAMA_LIBS) $(MJPEGTOOLS_LIBS) $(LIBPNG_LIBS) \
			$(LIBDV_LIBS) $(LIBM_LIBS) $(PIXBUF_LIBS) $(JACK_LIBS) $(LIBQUICKTIME_LIBS) $(RT_LIBS) $(LIBQRENCODE_LIBS) \
			-DDYNAMIC -O3 -Wall -rdynamic -Wl,-no-undefined 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

#VEVOTEST = vevotest

#bin_PROGRAMS = $(VEEJAY_BIN) $(VEVOTEST)
bin_PROGRAMS = $(VEEJAY_BIN)

veejay_headers = vims.h jpegutils.h vevo.h vj-composite.h vj-jack.h \
                 vj-OSC.h vj-viewport.h lav_common.h vims.h vj-dfb.h \
                 vjkf.h vj-perform.h vj-viewport-xml.h libveejay.h \
                 vj-audio.h vj-event.h vj-lib.h vj-plug.h x11misc.h \
                 veejay.h vj-bjack.h vj-font.h vj-misc.h vj-sdl.h vj-task.h

veejay_SOURCES	= veejay.c ${veejay_headers} 
veejay_LDADD 	= libveejay.la @LIBGETOPT_LIB@

#vevotest_SOURCES = vevotest.c ${veejay_headres}
#vevotest_LDADD   = libveejay.la @LIBGETOPT_LIB@

