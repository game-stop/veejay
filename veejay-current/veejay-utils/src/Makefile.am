MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) \
              -I$(includedir) \
			  $(VEEJAYCORE_CFLAGS) $(GLIB_CFLAGS) $(LIBAVUTIL_CFLAGS)

sayVIMS_SOURCES = sayVIMS.c
sayVIMS_LDFLAGS = $(VEEJAYCORE_LIBS) $(VEEJAY_LIBS) $(LAV_ALL_LIB_OPTS)
sayVIMS_LDADD = $(VEEJAYCORE_LIBS) $(VEEJAY_LIBS) $(LIBAVUTIL_LIBS) @LIBGETOPT_LIB@

YUV2RAWDV_BIN = sayVIMS

bin_PROGRAMS = \
	$(YUV2RAWDV_BIN)
