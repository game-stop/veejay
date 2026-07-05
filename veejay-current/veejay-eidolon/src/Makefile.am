MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(includedir) \
	$(VEEJAYCORE_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(LIBAVUTIL_CFLAGS)

bin_PROGRAMS = eidolon

eidolon_SOURCES = eidolon_life.c
eidolon_LDADD = \
	$(VEEJAYCORE_LIBS) \
	$(VEEJAY_LIBS) \
	$(LIBAVUTIL_LIBS) \
	@LIBGETOPT_LIB@ \
	-lm
