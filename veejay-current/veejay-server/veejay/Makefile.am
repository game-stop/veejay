AM_CFLAGS = $(VEEJAY_CFLAGS)
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              $(FFMPEG_CFLAGS) \
              $(XML2_CFLAGS) \
              -DG_LOG_DOMAIN=\"veejay\" \
              -DVEEJAY_VERSION=\"$(VERSION)\"

VEEJAY_BIN = veejay

bin_PROGRAMS = $(VEEJAY_BIN)

veejay_SOURCES = veejay.c

veejay_LDADD = \
    $(top_builddir)/libveejay/libveejay.la \
    @LIBGETOPT_LIB@

veejay_LDFLAGS = $(VEEJAYCORE_LIBS)