INCLUDES = -I$(top_srcdir)/libOSC -I$(includedir)
OSC_LIB_FILE = libOSC.la 
noinst_LTLIBRARIES = $(OSC_LIB_FILE)
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

EXTRA_DIST= libosc.h OSC-address-space.h OSC-dispatch.h \
		     OSC-pattern-match.h OSC-string-help.h NetworkReturnAddress.h \
		     OSC-callbacklist.h OSC-drop.h OSC-priority-queue.h \
		     OSC-struct.h NetworkUDP.h OSC-common.h \
		     OSC-internal-messages.h OSC-receive.h \
		     OSC-timetag.h

