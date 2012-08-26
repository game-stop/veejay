lividoplugin_LTLIBRARIES = \
			   lvd_shmin.la \
			   lvd_shmout.la \
			   lvd_solid.la 

lvd_shmin_la_SOURCES = lvd_shmin.c
lvd_shmout_la_SOURCES= lvd_shmout.c
lvd_solid_la_SOURCES = lvd_solid.c

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
	for file in $$list; do '
	 sofile=`basename $$file .la`.so; \
	 rm -f $(DESTDIR)/$(lividoplugindir)/$$sofile; \
	done
