# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg 

VJNET_LIB_OPTS = -export-dynamic 

VJNET_LIB_FILE = libvjnet.la

noinst_LTLIBRARIES = $(VJNET_LIB_FILE)

libvjnet_la_SOURCES =	packet.c mcastsender.c mcastreceiver.c cmd.c vj-server.c vj-client.c


libvjnet_la_LDFLAGS = $(VJNET_LIB_OPTS) \
			-L$(top_builddir)/libvjmem -lvjmem \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-DDYNAMIC -O3 -Wall 

SERVER_BIN = dummy

check_PROGRAMS = $(SERVER_BIN)

dummy_SOURCES = dummy.c 
dummy_LDADD = libvjnet.la \
		$(top_builddir)/libvjmsg/libvjmsg.la \
		$(top_builddir)/libvjmem/libvjmem.la 
