lib_LTLIBRARIES = libgtkcairo.la
include_HEADERS = gtkcairo.h
 
libgtkcairo_la_SOURCES = \
	gtkcairo.c \
	gtkcairo.h \
	gdkcairo.h \
	gdkcairo.c

libgtkcairo_la_LDFLAGS = -version-info @VERSION_INFO@

INCLUDES = $(GTKCAIRO_CFLAGS)

libgtkcairo_la_LIBADD = $(GTKCAIRO_LIBS)

