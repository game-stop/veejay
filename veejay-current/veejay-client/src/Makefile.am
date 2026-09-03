## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = callback.c

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

reloaded_headers = \
    callback.h \
    cellrendererspin.h \
    common.h \
    curve.h \
    format_codes.h \
    gtk3curve.h \
    gtkeditlistview.h \
    gtkmediaview.h \
    gtkmidicontrol.h \
    gtkmultitrackedit.h \
    gtkpreviewcache.h \
    gtksamplebankview.h \
    gtksequencebankview.h \
    gtkshapeselector.h \
    gtktimeselection.h \
    gtkvimshistoryview.h \
    gtkvimspatternview.h \
    gtkvimsview.h \
    keyboard.h \
    mjpeg_logging.h \
    mjpeg_types.h \
    mpegconsts.h \
    mpegtimecode.h \
    multitrack.h \
    sequence.h \
    tracksources.h \
    utils-gtk.h \
    utils.h \
    videodev_mjpeg.h \
    vj-api.h \
    vj-midi-engine.h \
    vj-midi-map.h \
    vmidi.h \
    yuv4mpeg.h \
    yuv4mpeg_intern.h

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
    gtkmultitrackedit.c \
    gtkshapeselector.c \
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
    gtkeditlistview.c \
    gtkmediaview.c \
    gtkpreviewcache.c \
    utils-gtk.c \
    vj-midi-engine.c \
    vj-midi-map.c \
    gtkmidicontrol.c \
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
