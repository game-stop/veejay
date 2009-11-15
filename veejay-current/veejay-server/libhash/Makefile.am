# Makefile for veejay
pkginclude_HEADERS  = hash.h


MAINTAINERCLEANFILES=Makefile.in
AM_CFLAGS=$(OP_CFLAGS)
INCLUDES = 	-I$(top_srcdir) -I$(includedir) 
HASH_LIB_FILE = libhash.la
noinst_LTLIBRARIES = $(HASH_LIB_FILE)
libhash_la_SOURCES =	hash.c 

