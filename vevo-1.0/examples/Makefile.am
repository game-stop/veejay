INCLUDES = 	-I$(top_srcdir)/include \
			-I$(top_srcdir) -I$(includedir) \
			-DG_LOG_DOMAIN=\"libvido\" -DLIBVIDO_VERSION=\"$(VERSION)\" 

LIBVEVO = $(top_builddir)/src/libvevo.la

# *********************************************************************
# The tools themselves
EXAMPLES_BIN = host 

bin_PROGRAMS = $(EXAMPLES_BIN)  

host_SOURCES = host.c 
host_LDADD = $(LIBVEVO) -ldl

