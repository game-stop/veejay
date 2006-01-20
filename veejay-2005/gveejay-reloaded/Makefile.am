## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/utils -I$(top_srcdir)/gveejay-reloaded -I$(top_srcdir)/libcom $(GTK_CFLAGS) \
		$(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

GVEEJAY_BIN=gveejayreloaded
bin_PROGRAMS = $(GVEEJAY_BIN) 
gveejayreloaded_SOURCES = widgets/gtkcolorsel.c widgets/gtkknob.c widgets/cellrendererspin.c widgets/gtktimeselection.c curve.c gveejay.c keyboard.c vj-api.c ${gveejay_headers}
gveejayreloaded_LDADD = $(top_builddir)/libvjmem/libvjmem.la $(top_builddir)/libvjmsg/libvjmsg.la $(top_builddir)/libvjnet/libvjnet.la $(top_builddir)/utils/libmjpegutils.a 
gveejayreloaded_LDFLAGS = $(GTK_LIBS) $(GLIB_LIBS) $(GLADE_LIBS) $(GTKCAIRO_LIBS) -export-dynamic 

