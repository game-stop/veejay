## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src $(VEEJAY_CFLAGS ) $(GTK_CFLAGS) \
		$(MJPEGTOOLS_CFLAGS) $(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

GVEEJAY_BIN=reloaded 
bin_PROGRAMS = $(GVEEJAY_BIN)
reloaded_SOURCES = widgets/gtkcolorsel.c widgets/gtkknob.c widgets/cellrendererspin.c widgets/gtktimeselection.c vj-midi.c curve.c utils.c tracksources.c gveejay.c keyboard.c sequence.c multitrack.c vj-api.c ${gveejay_headers}

reloaded_LDFLAGS = $(VEEJAY_LIBS) $(MJPEGTOOLS_LIBS) $(GTK_LIBS) $(GLIB_LIBS)\
$(GLADE_LIBS) $(GTKCAIRO_LIBS) $(FFMPEG_AVCODEC_LIBS) $(FFMPEG_SWSCALER_LIBS) $(ALSA_LIBS) -lgthread-2.0 -export-dynamic 

