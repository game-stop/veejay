CC = gcc
LINKER = ld
CFLAGS = -I. -I../include -Wall -g $(shell vevo-config --cflags)
all:
	# vevo - veejay video objects
	#
	# plugins :
	#          bathroom
	#          opacity
	#
	# compile: make plugins
	# run:
	#  export LD_LIBRARY_PATH=`pwd`
	#  host <plugin.so>

plugins:	opacity.so bathroom.so 

opacity.so:    opacity.o 

bathroom.so:	bathroom.o

clean:
	rm -rf *.o *.so 

# generic make rules

%: %.c
	$(CC) $(CFLAGS) -o $@ $< -ldl 

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 
%.so: %.o
	$(LINKER) $(shell vevo-config --libs) -E -z now -shared $^ -o $@

