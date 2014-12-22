# Makefile for memory managment / platform specific optimizations
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_builddir)/libvjmsg
CFLAGS = 
AM_CFLAGS = $(OPTCFLAGS)
VJMEM_LIB_FILE = libvjmem.la
pkginclude_HEADERS = vjmem.h
noinst_LTLIBRARIES = $(VJMEM_LIB_FILE)
libvjmem_la_SOURCES =	memcpy.c vj-x86.c 

