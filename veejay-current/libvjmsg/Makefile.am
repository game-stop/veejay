# Makefile for  logging/messages 

INCLUDES = 	-I$(top_srcdir) -I$(includedir) 

VJMSG_LIB_FILE = libvjmsg.la

VJMSG_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(VJMSG_LIB_FILE)

libvjmsg_la_SOURCES =	vj-common.c 

libvjmsg_la_LDFLAGS = $(VJMSG_ALL_LIB_OPTS) \
			-DDYNAMIC -O3 -Wall -rdynamic 

