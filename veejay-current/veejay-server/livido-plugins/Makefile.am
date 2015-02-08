lividoplugin_LTLIBRARIES = lvd_solid.la \
			   lvd_colortone.la \
			   lvd_displaywall.la \
			   lvd_stroboscope.la

lvd_solid_la_SOURCES = lvd_solid.c
lvd_colortone_la_SOURCES = lvd_colortone.c
lvd_displaywall_la_SOURCES = lvd_displaywall.c
lvd_stroboscope_la_SOURCES = lvd_stroboscope.c

INCLUDES=$(FFMPEG_CFLAGS)
AM_CFLAGS=-I@top_srcdir@/include 
AM_LDFLAGS=-module -avoid-version -lm -export-dynamic
AM_LIBTOOLFLAGS = --tag=disable-static

lividoplugindir = @libdir@/livido-plugins

install-lividopluginLTLIBRARIES:$(lividoplugin_LTLIBRARIES)
	mkdir -p $(DESTDIR)/$(lividoplugindir)
	list='$(lividoplugin_LTLIBRARIES)'; \
	for file in $$list; do \
	 sofile=`basename $$file .la`.so; \
	 $(INSTALL_PROGRAM) .libs/$$sofile $(DESTDIR)/$(lividoplugindir);\
	done

uninstall-lividopluginLTLIBRARIES:
	list='$(lividoplugin_LTLIBRARIES)'; \
	for file in $$list; do \
	 sofile=`basename $$file .la`.so; \
	 rm -f $(DESTDIR)/$(lividoplugindir)/$$sofile; \
	done

