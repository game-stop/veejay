# Makefile for memory managment / platform specific optimizations

INCLUDES = 	-I$(top_srcdir) -I$(includedir)  -I$(top_builddir)/libvjmsg



VJMEM_LIB_FILE = libvjmem.la

VJMEM_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(VJMEM_LIB_FILE)

libvjmem_la_SOURCES =	memcpy.c vj-x86.c 

libvjmem_la_LDFLAGS = $(VJMEM_ALL_LIB_OPTS) \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-DDYNAMIC -O3 -Wall -rdynamic 

