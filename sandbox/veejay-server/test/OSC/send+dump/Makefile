LIBOSCDIR = ../libOSC
LIBOSC = ${LIBOSCDIR}/libOSC.a
CFLAGS=  -O2 -I$(LIBOSCDIR) -Wall -g
DEFS= -Dunix

DUMPOBJS=dumpOSC.o


both: sendOSC dumpOSC mcastOSC

mcastOSC: mcastOSC.o mcastsocket.o ${LIBOSC}
	${CC} ${CFLAGS} ${DEFS} -o mcastOSC mcastOSC.o mcastsocket.o ${LIBOSC}

sendOSC: sendOSC.o htmsocket.o ${LIBOSC}
	${CC} ${CFLAGS} ${DEFS} -o sendOSC sendOSC.o htmsocket.o  ${LIBOSC}

dumpOSC: ${DUMPOBJS}
	${CC} ${CFLAGS} ${DEFS} -o $@ ${DUMPOBJS}
	
dumpUDP: dumpUDP.o
	${CC} ${CFLAGS} ${DEFS} -o dumpUDP dumpUDP.o

${LIBOSC}:
	echo "You need to go to " ${LIBOSCDIR} " and do a make."

clean:
	rm -f sendOSC dumpOSC *.o


