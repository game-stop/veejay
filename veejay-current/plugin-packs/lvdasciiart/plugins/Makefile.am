lividoplugin_LTLIBRARIES = lvd_aa.la  

lvd_aa_la_SOURCES = lvd_aa.c

AM_CPPFLAGS=$(FFMPEG_CFLAGS) $(FREETYPE2_CFLAGS) -I$(top_srcdir)/plugins
AM_CFLAGS=-I@top_srcdir@/include $(OPTCFLAGS)
AM_LDFLAGS=-module -avoid-version -lm -export-dynamic -lstdc++ -DDYNAMIC -rdynamic $(AA_LIBS) $(FREETYPE2_LIBS) $(FFMPEG_LIBS)
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

