## Process this file with automake to produce Makefile.in

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/gveejay-reloaded \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvevo \
		-I$(top_srcdir)/libvjnet \
		-I$(top_srcdir)/libhash \
		$(GLIB_CFLAGS) $(GLADE_CFLAGS) $(GTKCAIRO_CFLAGS) $(GTK_CFLAGS) \
		-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
		-DGVEEJAY_DATADIR=\""$(gveejay_datadir)"\" 

LAUNCHERLIBS = $(top_builddir)/libvjmem/libvjmem.la \
		$(top_builddir)/libvjmsg/libvjmsg.la \
		$(top_builddir)/libvjnet/libvjnet.la \
		$(top_builddir)/libhash/libhash.la \
		$(top_builddir)/libvevo/libvevo.la

vlaunch_SOURCES = vlaunch.c cb.c 
vlaunch_LDADD = $(LAUNCHERLIBS) @LIBGETOPT_LIB@
vlaunch_LDFLAGS = $(MJPEGTOOLS_LIBS) $(GTK_LIBS) $(GLIB_LIBS) \
		  $(GLADE_LIBS) $(GTKCAIRO_LIBS) \
		  -lgthread-2.0 -export-dynamic 

VEEJAYLAUNCHER_BIN=vlaunch

bin_PROGRAMS = $(VEEJAYLAUNCHER_BIN)
