MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS = $(OP_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir)/libvevo -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_srcdir)/libvjmem \
              -I$(top_srcdir)/libvjmsg

AM_CPPFLAGS += $(GLIB_CFLAGS)

VEVO_LIB_FILE = libvevo.la
pkginclude_HEADERS = libvevo.h lowlevel.h pool.h vevo.h
noinst_LTLIBRARIES = $(VEVO_LIB_FILE)

libvevo_la_SOURCES = pool.c vevo.c  
