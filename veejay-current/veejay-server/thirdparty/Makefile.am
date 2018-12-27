MAINTAINERCLEANFILES=Makefile.in

SUBDIRS = aclib bio2jack libhash liblzo libOSC libresample

if !HAVE_MJPEGTOOLS
SUBDIRS += mjpegtools 
endif

if HAVE_ARM
SUBDIRS += fastarm
endif

