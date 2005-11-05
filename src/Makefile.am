INCLUDES = 	-I$(top_srcdir)/include -I$(includedir) 

VEVO_LIB_FILE = libvevo.la

VEVO_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

lib_LTLIBRARIES = $(VEVO_LIB_FILE)

libvevo_la_SOURCES = hash.c pool.c vevo.c

libvevo_la_LDFLAGS = $(VEVO_ALL_LIB_OPTS) 

