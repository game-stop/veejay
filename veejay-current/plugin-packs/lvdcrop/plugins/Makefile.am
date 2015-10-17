lividoplugin_LTLIBRARIES = lvd_crop.la 

lvd_crop_la_SOURCES = lvd_crop.c

INCLUDES=$(FFMPEG_CFLAGS) -I$(top_srcdir)/liblvdgmic
AM_CFLAGS=-I@top_srcdir@/include $(OPTCFLAGS) -DSILENT
AM_LDFLAGS=-module -avoid-version -lm -export-dynamic -lgmic -lstdc++ -DDYNAMIC -rdynamic $(FFMPEG_LIBS)
AM_LIBTOOLFLAGS = --tag=disable-static

lividoplugindir = @libdir@/livido-plugins
liblvdgmicdir= @libdir@/livido-plugins

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

