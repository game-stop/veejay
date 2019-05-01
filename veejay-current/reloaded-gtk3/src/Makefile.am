## Process this file with automake to produce Makefile.in
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src \
              $(VEEJAY_CFLAGS) $(GTK_CFLAGS) \
              $(FFMPEG_CFLAGS) \
              -DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
              -DRELOADED_DATADIR=\""$(reloaded_datadir)"\"

RELOADED_BIN=reloaded
reloaded_SOURCES = cellrendererspin.c gtktimeselection.c vj-midi.c curve.c utils.c tracksources.c gveejay.c keyboard.c sequence.c multitrack.c vj-api.c mjpeg_logging.c mpegconsts.c mpegtimecode.c yuv4mpeg.c yuv4mpeg_ratio.c gtk3curve.c ${reloaded_headers}

bin_PROGRAMS = $(RELOADED_BIN)

reloaded_LDFLAGS =  $(VEEJAY_LIBS) $(GTK_LIBS) \
$(ALSA_LIBS) $(LIBM_LIBS) $(DEBUGLIBS) -export-dynamic -lgmodule-2.0 -lgthread-2.0
