INCLUDES = 	-I$(top_srcdir)/include \
			-I$(top_srcdir) -I$(includedir) \
			-DG_LOG_DOMAIN=\"libvido\" -DLIBVIDO_VERSION=\"$(VERSION)\" 

LIBVEVO = $(top_builddir)/src/libvevo.la

# *********************************************************************
# The tools themselves
EXAMPLES_BIN = vevotest 

bin_PROGRAMS = $(EXAMPLES_BIN)  

vevotest_SOURCES = vevotest.c 
vevotest_LDADD = $(LIBVEVO) -ldl

