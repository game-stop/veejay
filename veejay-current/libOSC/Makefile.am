DEBUG_CFLAGS = 

INCLUDES = -I$(top_srcdir)/libOSC -I$(includedir)

OSC_LIB_FILE = libOSC.la 
ALL_LIB_OPTS = -release $(LT_RELEASE) -export-dynamic

noinst_LTLIBRARIES = $(OSC_LIB_FILE)


libOSC_la_CFLAGS = $(DEBUG_CFLAGS)

libOSC_la_SOURCES = OSC-address-space.c \
		 OSC-callbacklist.c \
		 OSC-drop.c \
		 OSC-pattern-match.c \
		 OSC-priority-queue.c \
		 OSC-receive.c \
		 OSC-string-help.c \
		 OSC-common.c \
		 OSC-timetag.c \
		 NetworkReturnAddress.c 

libOSC_la_LDFLAGS = $(ALL_LIB_OPTS) -DDYNAMIC -O2 -Wall -rdynamic

