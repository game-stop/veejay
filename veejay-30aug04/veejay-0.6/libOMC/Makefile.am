DEBUG_CFLAGS = 

INCLUDES = -I$(top_srcdir)/libOMC -I$(includedir)

OMC_LIB_FILE = libOMC.la 
ALL_LIB_OPTS = -release $(LT_RELEASE) -export-dynamic

noinst_LTLIBRARIES = $(OMC_LIB_FILE)
#libOMCincludedir = $(includedir)/libOMC
#libOMCinclude_HEADERS = OSC-common.h \
#			OSC-address-space.h \
#			OSC-drop.h \
#			OSC-pattern-match.h \
#			OSC-priority-queue.h \
#			OSC-receive.h \
#			OSC-string-help.h \
#			OSC-timetag.h \
#			NetworkReturnAddress.h \
#			NetworkUDP.h \
#			OSC-callbacklist.h \
#			OSC-dispatch.h \
#			OSC-internal-messages.h \
#			OSC-struct.h 
#

libOMC_la_CFLAGS = $(DEBUG_CFLAGS)

libOMC_la_SOURCES = OSC-address-space.c \
		 OSC-callbacklist.c \
		 OSC-drop.c \
		 OSC-pattern-match.c \
		 OSC-priority-queue.c \
		 OSC-receive.c \
		 OSC-string-help.c \
		 OSC-common.c \
		 OSC-timetag.c \
		 NetworkReturnAddress.c 

libOMC_la_LDFLAGS = $(ALL_LIB_OPTS) -DDYNAMIC -O2 -Wall -rdynamic

