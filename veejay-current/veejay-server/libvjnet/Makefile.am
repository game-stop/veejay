# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in
INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/libyuv -I$(top_srcdir)/liblzo -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg 
VJNET_LIB_FILE = libvjnet.la
EXTRA_DIST= vj-client.h bswap.h cmd.h mcastreceiver.h mcastsender.h packet.h vj-server.h
pkginclude_HEADERS=vj-client.h
noinst_LTLIBRARIES = $(VJNET_LIB_FILE)
libvjnet_la_SOURCES =	packet.c mcastsender.c mcastreceiver.c cmd.c vj-server.c vj-client.c
