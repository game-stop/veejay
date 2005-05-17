## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/utils -I$(top_srcdir)/gveejay -I$(top_srcdir)/libcom $(GTK_CFLAGS) \
		$(GLIB_CFLAGS) $(GLADE_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

GVEEJAY_BIN=gveejay
bin_PROGRAMS = $(GVEEJAY_BIN) 
gveejay_SOURCES = widgets/cellrendererspin.c gveejay.c keyboard.c vj-api.c ${gveejay_headers}
gveejay_LDADD = $(top_builddir)/libvjmem/libvjmem.la $(top_builddir)/libvjmsg/libvjmsg.la $(top_builddir)/libvjnet/libvjnet.la $(top_builddir)/utils/libmjpegutils.a 
gveejay_LDFLAGS = $(GTK_LIBS) $(GLIB_LIBS) $(GLADE_LIBS) -export-dynamic

