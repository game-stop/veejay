# Makefile for veejay
AM_CFLAGS = $(OP_CFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/vjmem \
              -I$(top_srcdir)/vjmsg

AM_CPPFLAGS += $(XML2_CFLAGS) 

SAMPLE_LIB_FILE = libvjxml.la
noinst_LTLIBRARIES = $(SAMPLE_LIB_FILE)
libvjxml_la_SOURCES = vj-xml.c 
EXTRA_DIST= vj-xml.h
