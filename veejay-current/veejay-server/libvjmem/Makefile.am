# Makefile for memory managment / platform specific optimizations
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty \
              -I$(top_builddir)/libvjmsg

AM_CFLAGS = $(OP_CFLAGS)

VJMEM_LIB_FILE = libvjmem.la
pkginclude_HEADERS = vjmem.h

noinst_LTLIBRARIES = $(VJMEM_LIB_FILE)
libvjmem_la_SOURCES =	memcpy.c vj-x86.c 

