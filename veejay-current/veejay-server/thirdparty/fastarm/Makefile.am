# Makefile for veejay
MAINTAINERCLEANFILES = Makefile.in
AM_CFLAGS = $(FASTARM_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(includedir) \
              -I$(top_srcdir)/thirdparty $(FASTARM_CFLAGS) 

FASTARM_LIB_FILE = libfastarm.la
noinst_LTLIBRARIES = $(FASTARM_LIB_FILE)
libfastarm_la_SOURCES = new_arm.S
EXTRA_DIST= 
