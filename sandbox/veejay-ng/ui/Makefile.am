# Makefile for veejay ui

MAINTAINERCLEANFILES = Makefile.in

INCLUDES = 	-I$(top_srcdir) -I$(includedir) \
		-DG_LOG_DOMAIN=\"veejayui\" -DVEEJAY_VERSION=\"$(VERSION)\" \
     		-I /usr/X11R6/include \
		-I$(top_srcdir)/libhash \
		-I$(top_srcdir)/libvjmem \
		-I$(top_srcdir)/libvjmsg \
		-I$(top_srcdir)/libvevo \
		${XML_CFLAGS} \
		${LIBLO_CFLAGS} \
	 	${GLADE_CFLAGS} \
		${GTK_CFLAGS} \
		${GLIB_CFLAGS} 

UI_LIB_FILE = libveejayui.la

lib_LTLIBRARIES = $(UI_LIB_FILE) 

UI_ALL_LIB_OPTS = \
	-version-info $(LT_AGE):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) \
	-export-dynamic 

libveejayui_la_SOURCES =  buffer.c concretebuilder.c builder.c samplebank.c anim.c anim_ui.c director.c uiosc.c 

libveejayui_la_LDFLAGS = $(UI_ALL_LIB_OPTS )
libveejayui_la_LIBADD = -L$(top_builddir)/libhash / -lhash \
			-L$(top_builddir)/libvjmsg -lvjmsg \
			-L$(top_builddir)/libvjmem/ -lvjmem \
			-L$(top_builddir)/libvevo -lvevo 

UI_BIN = veejayui

bin_PROGRAMS = $(UI_BIN)  

veejayui_SOURCES	= testui.c  
veejayui_LDADD 	= libveejayui.la @LIBGETOPT_LIB@ \
			${XML_LIBS} ${LIBLO_LIBS} ${GLADE_LIBS} ${GTK_LIBS} ${GLIB_LIBS} -lgthread-2.0
