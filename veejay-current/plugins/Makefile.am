
INCLUDES = -I$(top_srcdir) -I$(includedir) ${FT_CFLAGS} -g
			
ALL_PLUGIN_OPTS = -export-dynamic

DUMMY_PLUGIN = libvj_dummy_plugin.la

TTF_PLUGIN = libvj_drawtext_plugin.la

lib_LTLIBRARIES = $(DUMMY_PLUGIN) $(TTF_PLUGIN) 
libvj_dummy_plugin_la_CFLAGS = -fPIC
libvj_dummy_plugin_la_SOURCES = dummy.c support.c
libvj_dummy_plugin_la_LDFLAGS = $(ALL_PLUGIN_OPTS)

libvj_drawtext_plugin_la_CFLAGS = -fPIC
libvj_drawtext_plugin_la_SOURCES = drawtext.c support.c
libvj_drawtext_plugin_la_LDFLAGS = $(ALL_PLUGIN_OPTS) ${FT_LIBS}

