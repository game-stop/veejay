# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg \
			${XML_CPPFLAGS} ${XML_CFLAGS} 

SAMPLE_LIB_FILE = libsample.la

SAMPLE_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(SAMPLE_LIB_FILE)

libsample_la_SOURCES =	hash.c sampleadm.c 


libsample_la_LDFLAGS = $(SAMPLE_ALL_LIB_OPTS) \
			-L$(top_builddir)/libvjmem -lvjmem \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			${XML_LIBS} \
			-DDYNAMIC -O3 -Wall -rdynamic 

