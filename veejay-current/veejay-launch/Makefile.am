## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/veejay-launch -I$(top_srcdir)/liblzo $(GTK_CFLAGS) \
		$(MJPEGTOOLS_CFLAGS) $(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) -DGNOMELOCALEDIR=\""$(datadir)/locale"\" -DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\"

VEEJAYLAUNCHER_BIN=vlaunch
bin_PROGRAMS = $(VEEJAYLAUNCHER_BIN)
vlaunch_SOURCES = vlaunch.c cb.c 
vlaunch_LDADD = $(top_builddir)/libvjmem/libvjmem.la $(top_builddir)/liblzo/libminilzo.la $(top_builddir)/libvjmsg/libvjmsg.la $(top_builddir)/libvjnet/libvjnet.la $(top_builddir)/libvevo/libvevo.la $(top_builddir)/libhash/libhash.la 
vlaunch_LDFLAGS = $(MJPEGTOOLS_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(GLADE_LIBS) $(GTKCAIRO_LIBS) -lgthread-2.0 -export-dynamic 
