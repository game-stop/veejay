CC = gcc
LINKER = ld
CFLAGS = -I. -I../include  -I ../ -Wall -g $(shell vevo-config --cflags) 
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

plugins:	fade_plugin.so 


fade.so:	fade_plugin.o

clean:
	rm -rf *.o *.so 

# generic make rules

%: %.c
	$(CC) $(CFLAGS) -o $@ $< -ldl 

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 
%.so: %.o
	$(LINKER) -E -z now -shared $^ -o $@


