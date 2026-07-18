## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(top_builddir) \
    -I$(top_srcdir)/src \
    $(VEEJAYCORE_CFLAGS) \
    $(GTK_CFLAGS) \
    $(FFMPEG_CFLAGS) \
    $(SDL2_CFLAGS) \
    -DGNOMELOCALEDIR=\"$(datadir)/locale\" \
    -DRELOADED_DATADIR=\"$(reloaded_datadir)\"

bin_PROGRAMS = reloaded

reloaded_CFLAGS = $(OP_CFLAGS)

reloaded_SOURCES = \
    cellrendererspin.c \
    gtktimeselection.c \
    curve.c \
    utils.c \
    tracksources.c \
    gveejay.c \
    keyboard.c \
    sequence.c \
    multitrack.c \
    vj-api.c \
    mjpeg_logging.c \
    mpegconsts.c \
    mpegtimecode.c \
    yuv4mpeg.c \
    yuv4mpeg_ratio.c \
    gtk3curve.c \
    gtksequencebankview.c \
    gtksamplebankview.c \
    gtkvimspatternview.c \
    gtkvimshistoryview.c \
    gtkvimsview.c \
    gtkvimsview.h \
    utils-gtk.c \
    vj-midi.c \
    $(reloaded_headers)

reloaded_LDFLAGS = \
    -export-dynamic

reloaded_LDADD = \
    $(VEEJAYCORE_LIBS) \
    $(GTK_LIBS) \
    $(SDL2_LIBS) \
    $(ALSA_LIBS) \
    $(LIBM_LIBS) \
    $(DEBUGLIBS) \
    -lgmodule-2.0 \
    -lgthread-2.0
