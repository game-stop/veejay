## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/gveejay-reloaded -I$(top_srcdir)/liblzo $(GTK_CFLAGS) \
		$(MJPEGTOOLS_CFLAGS) $(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

GVEEJAY_BIN=gveejayreloaded 
bin_PROGRAMS = $(GVEEJAY_BIN)
gveejayreloaded_SOURCES = widgets/gtkcolorsel.c widgets/gtkknob.c widgets/cellrendererspin.c widgets/gtktimeselection.c curve.c utils.c tracksources.c gveejay.c keyboard.c sequence.c multitrack.c vj-api.c ${gveejay_headers}
gveejayreloaded_LDADD = $(top_builddir)/libvjmem/libvjmem.la $(top_builddir)/liblzo/liblzo.la $(top_builddir)/libvjmsg/libvjmsg.la $(top_builddir)/libvjnet/libvjnet.la $(top_builddir)/libvevo/libvevo.la $(top_builddir)/libhash/libhash.la 
gveejayreloaded_LDFLAGS = $(MJPEGTOOLS_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GLADE_LIBS) $(GTKCAIRO_LIBS) -lgthread-2.0 -export-dynamic 

