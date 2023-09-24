# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS = $(LZO_EXTRA_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg

VJLZO_LIB_FILE = libvjlzo.la
noinst_LTLIBRARIES = $(VJLZO_LIB_FILE)
libvjlzo_la_SOURCES = minilzo.c lzo.c 
pkginclude_HEADERS = lzo.h
