MAINTAINERCLEANFILES=Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/libgoom
GOOM_LIB_FILE = libgoom.la
noinst_LTLIBRARIES = $(GOOM_LIB_FILE)
libgoom_la_SOURCES = goomsl_yacc.y goomsl_lex.l goomsl.c goomsl_hash.c goomsl_heap.c \
	goom_tools.c \
	config_param.c convolve_fx.c filters.c \
	flying_stars_fx.c gfontlib.c gfontrle.c \
	goom_core.c graphic.c ifs.c lines.c \
	mathtools.c sound_tester.c surf3d.c \
	tentacle3d.c plugin_info.c \
	v3d.c drawmethods.c \
	cpu_info.c


