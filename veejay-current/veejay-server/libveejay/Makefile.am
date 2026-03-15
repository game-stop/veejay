# Makefile for libveejay

AM_CFLAGS = $(VEEJAY_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -DG_LOG_DOMAIN=\"veejay\" \
              -DVEEJAY_VERSION=\"$(VERSION)\" \
              -I /usr/X11R6/include \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/libvje \
              -I$(top_srcdir)/libplugger \
              -I$(top_srcdir)/libel \
              -I$(top_srcdir)/libstream \
              -I$(top_srcdir)/libsubsample \
              -I$(top_srcdir)/libsamplerec \
              -I$(top_srcdir)/libqrwrap \
              -I$(top_srcdir)/libvjxml \
              $(FFMPEG_CFLAGS) \
              $(LIBQUICKTIME_CFLAGS) \
              $(FT_CFLAGS) \
              $(FREETYPE_CFLAGS) \
              $(FONTCONFIG_CFLAGS) \
              $(GLIB_CFLAGS) \
              $(AVIPLAY_CFLAGS) \
              $(XML2_CFLAGS) \
              $(DV_FLAGS) \
              $(X_CFLAGS) \
              $(LIBLO_CFLAGS) \
              $(DIRECTFB_CFLAGS) \
              $(SDL2_CFLAGS) \
              $(JPEG_CFLAGS) \
              $(JACK_CFLAGS) \
              $(LIBUNWIND_CFLAGS) \
              $(PTHREAD_CFLAGS) \
              $(LIBQRENCODE_CFLAGS) \
              $(VEEJAYCORE_CFLAGS)

lib_LTLIBRARIES = libveejay.la

libveejay_la_SOURCES = \
    jpegutils.c \
    vj-misc.c \
    vj-osc.c \
    vjkf.c \
    vj-macro.c \
    audioscratcher.c \
    vj-event.c \
    vj-eventman.c \
    vj-perform.c \
    x11misc.c \
    vj-shm.c \
    vj-sdl.c \
    vj-dfb.c \
    vj-viewport.c \
    vj-composite.c \
    vj-font.c \
    vj-pjack.c \
    vj-share.c \
    vj-split.c \
    liblavplayvj.c

libveejay_la_LDFLAGS = \
    -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
    -release $(LT_RELEASE) \
    -no-undefined \
    -export-dynamic \
    $(DEBUGLIBS)
libveejay_la_LDFLAGS += -rpath $(libdir)

libveejay_la_LIBADD = \
    $(top_builddir)/thirdparty/bio2jack/libbio2jack4vj.la \
    $(top_builddir)/thirdparty/libOSC/libOSC.la \
    $(top_builddir)/libvje/libvje.la \
    $(top_builddir)/libplugger/libvjplug.la \
    $(top_builddir)/libsample/libsample.la \
    $(top_builddir)/libel/libel.la \
    $(top_builddir)/libstream/libstream.la \
    $(top_builddir)/libsubsample/libsubsample.la \
    $(top_builddir)/libsamplerec/libsamplerec.la \
    $(top_builddir)/libqrwrap/libqrwrap.la \
    $(top_builddir)/libvjxml/libvjxml.la \
    $(SDL2_LIBS) \
    $(DIRECTFB_LIBS) \
    $(X_LIBS) \
    $(PTHREAD_LIBS) \
    $(FT_LDFLAGS) \
    $(FT_LIBS) \
    $(XML2_LIBS) \
    $(JPEG_LIBS) \
    $(LIBLO_LIBS) \
    $(LIBUNWIND_LIBS) \
    $(GLIB_LIBS) \
    $(FREETYPE_LIBS) \
    $(FONTCONFIG_LIBS) \
    $(FFMPEG_LIBS) \
    $(XINERAMA_LIBS) \
    $(LIBPNG_LIBS) \
    $(LIBDV_LIBS) \
    $(LIBM_LIBS) \
    $(PIXBUF_LIBS) \
    $(JACK_LIBS) \
    $(LIBQUICKTIME_LIBS) \
    $(RT_LIBS) \
    $(LIBQRENCODE_LIBS) \
    $(VEEJAYCORE_LIBS)

libveejay_la_LDFLAGS += \
    -DDYNAMIC \
    -O3 \
    -Wall \
    -rdynamic \
    -Wl,-no-undefined