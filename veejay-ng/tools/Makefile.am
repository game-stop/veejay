MAINTAINERCLEANFILES = Makefile.in
	
INCLUDES = 	-I$(top_srcdir) \
		-I$(includedir) \
		-I$(top_scrdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvjnet \
		$(MJPEGTOOLS_CFLAGS) $(GLIB_CFLAGS) 

VJLIBS = $(top_builddir)/libvjnet/libvjnet.la \
	 $(top_builddir)/libvjmsg/libvjmsg.la \
	 $(top_builddir)/libvjmem/libvjmem.la

sayVIMS_SOURCES = sayVIMS.c
sayVIMS_LDFLAGS = $(LAV_ALL_LIB_OPTS)
sayVIMS_LDADD = $(VJLIBS) @LIBGETOPT_LIB@

YUV2RAWDV_BIN = sayVIMS

bin_PROGRAMS = \
	$(YUV2RAWDV_BIN)

	
