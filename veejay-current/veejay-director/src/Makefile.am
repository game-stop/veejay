# BEGIN VEEJAY DIRECTOR
MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS = \
    -I$(top_srcdir) \
    -I$(top_builddir) \
    -I$(top_srcdir)/src \
    $(VEEJAYCORE_CFLAGS) \
    $(GTK_CFLAGS) \
    $(NDI_CFLAGS) \
    -DGNOMELOCALEDIR=\"$(datadir)/locale\" \
    -DRELOADED_DATADIR=\"$(reloaded_datadir)\"

bin_PROGRAMS = veejay-director

veejay_director_SOURCES = \
    veejay-director.c \
    director-compat.h \
    director-client.c \
    director-client.h \
    director-wire.c \
    director-wire.h \
    director-ndi.c \
    director-ndi.h \
    director-model.c \
    director-model.h \
    gtkviewportmesh.c \
    gtkviewportmesh.h \
    gtkvjlogview.c \
    gtkvjlogview.h \
    director-presets.c \
    director-presets.h \
    org.veejay.Director.desktop

veejay_director_LDFLAGS = \
    -export-dynamic

veejay_director_LDADD = \
    $(GTK_LIBS) \
    $(LIBM_LIBS) \
    $(VEEJAYCORE_LIBS) \
    -lgthread-2.0 \
    $(NDI_LIBS)

director_desktopdir = $(datadir)/applications
director_desktop_DATA = org.veejay.Director.desktop
# END VEEJAY DIRECTOR
