# Makefile for veejay

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) -I$(top_srcdir)/vjmem -I$(top_srcdir)/vjmsg 
VJNET_LIB_FILE = libvjnet.la
noinst_LTLIBRARIES = $(VJNET_LIB_FILE)
libvjnet_la_SOURCES =	packet.c mcastsender.c mcastreceiver.c cmd.c vj-server.c vj-client.c
