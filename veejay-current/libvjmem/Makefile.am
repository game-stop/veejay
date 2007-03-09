# Makefile for memory managment / platform specific optimizations
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_builddir)/libvjmsg
AM_CFLAGS=$(OP_CFLAGS)
VJMEM_LIB_FILE = libvjmem.la
noinst_LTLIBRARIES = $(VJMEM_LIB_FILE)
libvjmem_la_SOURCES =	memcpy.c vj-x86.c 

