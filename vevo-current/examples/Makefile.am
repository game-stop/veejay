INCLUDES = 	-I$(top_srcdir)/include \
			-I$(top_srcdir) -I$(includedir) \
			-DG_LOG_DOMAIN=\"libvido\" -DLIBVIDO_VERSION=\"$(VERSION)\" 

LIBVEVO = $(top_builddir)/src/libvevo.la

# *********************************************************************
# The tools themselves
EXAMPLES_BIN = hosttest 

bin_PROGRAMS = $(EXAMPLES_BIN)  

hosttest_SOURCES = testhost.c 
hosttest_LDADD = $(LIBVEVO) -ldl

