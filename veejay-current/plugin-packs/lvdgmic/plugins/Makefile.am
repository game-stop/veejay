lividoplugin_LTLIBRARIES = gmic_cartoon.la \
			   gmic_cracks.la \
			   gmic_bokeh.la \
			   gmic_pencilbw.la \
			   gmic_arrayfade.la \
			   gmic_rodilius.la \
			   gmic_water.la \
			   gmic_wave.la \
			   gmic_wind.la \
			   gmic_lightpatch.la \
			   gmic_noisehurl.la \
			   gmic_crystal.la \
			   gmic_lava.la \
			   gmic_marble.la \
			   gmic_shockwaves.la \
			   gmic_spread.la \
			   gmic_texturizepaper.la \
			   gmic_vignette.la \
			   gmic_abstraction.la \
			   gmic_whirl.la \
			   gmic_posteredges.la \
			   gmic_posterhope.la \
			   gmic_kuwahara.la \
			   gmic_pendrawing.la \
			   gmic_oldscool8bits.la \
			   gmic_sponge.la \
			   gmic_tetris.la

gmic_cartoon_la_SOURCES = gmic_cartoon.c
gmic_cartoon_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_cracks_la_SOURCES = gmic_cracks.c
gmic_cracks_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_bokeh_la_SOURCES = gmic_bokeh.c
gmic_bokeh_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_pencilbw_la_SOURCES = gmic_pencilbw.c
gmic_pencilbw_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_arrayfade_la_SOURCES = gmic_arrayfade.c
gmic_arrayfade_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_rodilius_la_SOURCES = gmic_rodilius.c
gmic_rodilius_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_water_la_SOURCES = gmic_water.c
gmic_water_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_wave_la_SOURCES = gmic_wave.c
gmic_wave_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_wind_la_SOURCES = gmic_wind.c
gmic_wind_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_lightpatch_la_SOURCES = gmic_lightpatch.c
gmic_lightpatch_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_noisehurl_la_SOURCES = gmic_noisehurl.c
gmic_noisehurl_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_crystal_la_SOURCES = gmic_crystal.c
gmic_crystal_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_lava_la_SOURCES = gmic_lava.c
gmic_lava_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_marble_la_SOURCES = gmic_marble.c
gmic_marble_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_shockwaves_la_SOURCES = gmic_shockwaves.c
gmic_shockwaves_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_spread_la_SOURCES = gmic_spread.c
gmic_spread_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_texturizepaper_la_SOURCES = gmic_texturizepaper.c
gmic_texturizepaper_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_vignette_la_SOURCES = gmic_vignette.c
gmic_vignette_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_abstraction_la_SOURCES = gmic_abstraction.c
gmic_abstraction_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

#gmic_dices_la_SOURCES = gmic_dices.c
#gmic_dices_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_whirl_la_SOURCES = gmic_whirl.c
gmic_whirl_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_posteredges_la_SOURCES = gmic_posteredges.c
gmic_posteredges_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_posterhope_la_SOURCES = gmic_posterhope.c
gmic_posterhope_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_kuwahara_la_SOURCES = gmic_kuwahara.c
gmic_kuwahara_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_pendrawing_la_SOURCES = gmic_pendrawing.c
gmic_pendrawing_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_oldscool8bits_la_SOURCES = gmic_oldscool8bits.c
gmic_oldscool8bits_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_sponge_la_SOURCES = gmic_sponge.c
gmic_sponge_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic

gmic_tetris_la_SOURCES = gmic_tetris.c
gmic_tetris_la_LIBADD = -L$(top_builddir)/liblvdgmic -llvdgmic


INCLUDES=$(FFMPEG_CFLAGS) -I$(top_srcdir)/liblvdgmic
AM_CFLAGS=-I@top_srcdir@/include $(OPTCFLAGS) -DSILENT
AM_LDFLAGS=-module -avoid-version -lm -export-dynamic -lgmic -lstdc++ -DDYNAMIC -rdynamic
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

