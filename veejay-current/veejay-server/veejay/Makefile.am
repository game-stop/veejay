# Makefile for veejay

AM_CFLAGS = $(OP_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -DG_LOG_DOMAIN=\"veejay\" -DVEEJAY_VERSION=\"$(VERSION)\" 

AM_CPPFLAGS += -I /usr/X11R6/include \
		-I$(top_srcdir)/thirdparty \
		-I$(top_srcdir)/libvje \
		-I$(top_srcdir)/libplugger \
		-I$(top_srcdir)/libel \
		-I$(top_srcdir)/libstream \
		-I$(top_srcdir)/libsubsample \
		-I$(top_srcdir)/libsamplerec \
		-I$(top_srcdir)/libqrwrap \
		-I$(top_srcdir)/libvjxml \
		$(FFMPEG_CFLAGS) $(LIBQUICKTIME_CFLAGS) $(FT_CFLAGS) $(FREETYPE2_CFLAGS) $(GLIB_CFLAGS) \
		$(AVIPLAY_CFLAGS) $(XML2_CFLAGS) $(DV_FLAGS) $(X_CFLAGS) $(LIBLO_CFLAGS) \
		$(DIRECTFB_CFLAGS) $(SDL2_CFLAGS) $(JPEG_CFLAGS) $(JACK_CFLAGS) \
		$(LIBUNWIND_CFLAGS) $(PTHREAD_CFLAGS) $(LIBQRENCODE_CFLAGS) $(VEEJAYCORE_CFLAGS) 

VEEJAY_LIB_FILE = libveejay.la

lib_LTLIBRARIES = $(VEEJAY_LIB_FILE) 

libveejay_la_SOURCES =  jpegutils.c vj-misc.c \
			vj-osc.c vjkf.c vj-macro.c vj-event.c vj-eventman.c vj-perform.c \
			x11misc.c vj-shm.c vj-sdl.c vj-dfb.c vj-viewport.c vj-composite.c \
			vj-font.c vj-pjack.c vj-share.c vj-split.c liblavplayvj.c 

libveejay_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
		       -release $(LT_RELEASE) -no-undefined -export-dynamic $(DEBUGLIBS)


libveejay_la_LIBADD = \
			$(top_builddir)/thirdparty/bio2jack/libbio2jack4vj.la \
			$(top_builddir)/thirdparty/libOSC/libOSC.la \
			$(top_builddir)/thirdparty/libresample/libresample.la \
			$(top_builddir)/libvje/libvje.la \
			$(top_builddir)/libplugger/libvjplug.la \
			$(top_builddir)/libsample/libsample.la \
			$(top_builddir)/libel/libel.la \
			$(top_builddir)/libstream/libstream.la \
			$(top_builddir)/libsubsample/libsubsample.la \
			$(top_builddir)/libsamplerec/libsamplerec.la \
			$(top_builddir)/libqrwrap/libqrwrap.la \
			$(top_builddir)/libvjxml/libvjxml.la 

libveejay_la_LDFLAGS +=	$(SDL2_LIBS) $(DIRECTFB_LIBS) $(X_LIBS) $(PTHREAD_LIBS) $(FT_LDFLAGS) $(FT_LIBS) \
			$(XML2_LIBS) $(JPEG_LIBS) $(LIBLO_LIBS) $(LIBUNWIND_LIBS) $(GLIB_LIBS) \
		 	$(FREETYPE2_LIBS) $(FFMPEG_LIBS) $(XINERAMA_LIBS) $(LIBPNG_LIBS) \
			$(LIBDV_LIBS) $(LIBM_LIBS) $(PIXBUF_LIBS) $(JACK_LIBS) $(LIBQUICKTIME_LIBS) $(RT_LIBS) $(LIBQRENCODE_LIBS) \
			$(VEEJAYCORE_LIBS) \
			-DDYNAMIC -O3 -Wall -rdynamic -Wl,-no-undefined 

# *********************************************************************
# The tools themselves
VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)

veejay_headers = vims.h jpegutils.h vevo.h vj-composite.h vj-jack.h \
                 vj-OSC.h vj-viewport.h lav_common.h vims.h vj-dfb.h \
                 vjkf.h vj-perform.h vj-viewport-xml.h libveejay.h \
                 vj-audio.h vj-event.h vj-lib.h vj-plug.h x11misc.h \
                 veejay.h vj-bjack.h vj-font.h vj-misc.h vj-sdl.h vj-split.h

veejay_SOURCES	= veejay.c ${veejay_headers} 
veejay_LDADD 	= libveejay.la @LIBGETOPT_LIB@
veejay_LDFLAGS  = $(VEEJAYCORE_LIBS)

