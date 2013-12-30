## Process this file with automake to produce Makefile.in
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
 INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src $(VEEJAY_CFLAGS) $(GTK_CFLAGS) \
	$(GLIB_CFLAGS) $(FFMPEG_CFLAGS) $(GLADE_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DRELOADED_DATADIR=\""$(reloaded_datadir)"\"

RELOADED_BIN=reloaded
reloaded_SOURCES = gtkcolorsel.c gtkknob.c cellrendererspin.c gtktimeselection.c vj-midi.c curve.c utils.c tracksources.c gveejay.c keyboard.c sequence.c multitrack.c vj-api.c mjpeg_logging.c mpegconsts.c mpegtimecode.c yuv4mpeg.c yuv4mpeg_ratio.c ${reloaded_headers}

bin_PROGRAMS = $(RELOADED_BIN) 
EXTRA_DIST = common.h curve.h fastintfns.h format_codes.h keyboard.h mjpeg_logging.h mjpeg_types.h mpegconsts.h mpegtimecode.h multitrack.h sequence.h tracksources.h utils.h videodev_mjpeg.h vj-api.h vmidi.h yuv4mpeg.h yuv4mpeg_intern.h cellrendererspin.h gtkcolorsel.h gtkhsv.h gtkknob.h gtktimeselection.h callback.c

reloaded_LDFLAGS =  $(VEEJAY_LIBS) $(GTK_LIBS) $(GLIB_LIBS) \
$(GLADE_LIBS) $(ALSA_LIBS) $(LIBM_LIBS) $(LIBX11_LIBS) -lgthread-2.0 -export-dynamic 

