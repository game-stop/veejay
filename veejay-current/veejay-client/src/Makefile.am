## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src $(VEEJAY_CFLAGS ) $(GTK_CFLAGS) \
		$(GLIB_CFLAGS) $(GLADE_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

GVEEJAY_BIN=reloaded
reloaded_SOURCES = widgets/gtkcolorsel.c widgets/gtkknob.c widgets/cellrendererspin.c widgets/gtktimeselection.c vj-midi.c curve.c utils.c tracksources.c gveejay.c keyboard.c sequence.c multitrack.c vj-api.c mjpeg_logging.c mpegconsts.c mpegtimecode.c yuv4mpeg.c yuv4mpeg_ratio.c ${gveejay_headers}


bin_PROGRAMS = $(GVEEJAY_BIN) 

reloaded_LDFLAGS =  $(VEEJAY_LIBS) $(GTK_LIBS) $(GLIB_LIBS)\
$(GLADE_LIBS) $(FFMPEG_AVCODEC_LIBS) $(FFMPEG_SWSCALER_LIBS) $(ALSA_LIBS) -lgthread-2.0 -export-dynamic 

