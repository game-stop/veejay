## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/gveejay-reloaded -I$(top_srcdir)/libcom $(GTK_CFLAGS) \
		$(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"
		-I$(top_srcdir)/libvjmem -I$(top_srcdir)/libvjmsg -I$(top_srcdir)/libvevo


VEEJAYLAUNCHER_BIN=vlaunch
bin_PROGRAMS = $(VEEJAYLAUNCHER_BIN)
vlaunch_SOURCES = vlaunch.c cb.c ${gveejay_headers}
vlaunch_LDADD = $(top_builddir)/libvjmem/libvjmem.la $(top_builddir)/libvjmsg/libvjmsg.la $(top_builddir)/libvjnet/libvjnet.la $(top_builddir)/libhash/libhash.la  $(top_builddir)/libvevo/libvevo.la
vlaunch_LDFLAGS = $(MJPEGTOOLS_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GLADE_LIBS) $(GTKCAIRO_LIBS) -lgthread-2.0 -export-dynamic 

