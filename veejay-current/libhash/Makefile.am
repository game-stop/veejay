# Makefile for veejay

INCLUDES = 	-I$(top_srcdir) -I$(includedir) 

HASH_LIB_FILE = libhash.la

HASH_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

noinst_LTLIBRARIES = $(HASH_LIB_FILE)

libhash_la_SOURCES =	hash.c 


libhash_la_LDFLAGS = $(HASH_ALL_LIB_OPTS) 
			-DDYNAMIC -O3 -Wall -rdynamic 

