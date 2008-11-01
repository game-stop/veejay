CFLAGS=  -O2 
LIB=libOSC.a

LIBOBJS= ${LIB}(OSC-client.o) ${LIB}(OSC-timetag.o)

all:	${LIBOBJS}

.c.a:
	${CC} -c ${CFLAGS} $<
	${AR} ${ARFLAGS} $@ $*.o
	rm -f $*.o

test_OSC: test_OSC.o ${LIB}
	cc -o test_OSC test_OSC.o ${LIB}

test_OSC_timeTag: test_OSC_timeTag.o OSC-timetag.o
	cc -o test_OSC_timeTag test_OSC_timeTag.o  OSC-timetag.o


clean:
	rm -f ${LIB} *.o
