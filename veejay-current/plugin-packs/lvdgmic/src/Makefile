CFLAGS=-Wall -g -fPIC
CCFLAGS=Wall -g -fPIC
LDFLAGS=-g -Wall -lstdc++ -lgmic

OBJS=lvdgmic.o lvdgmicglue.o lvd_gmic.o
CARTOON_SO=gmic_cartoon.so
BOKEY_SO=gmic_bokeh.so
PENCILBW_SO=gmic_pencilbw.so

all: $(CARTOON_SO) $(BOKEY_SO) $(PENCILBW_SO)
default:all

%.o: %.cc
	$(CC) $(CFLAGS) -c $<

*.o: *.c
	$(CC) $(CFLAGS) -c $<

$(CARTOON_SO): $(OBJS) gmic_cartoon.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(BOKEY_SO): $(OBJS) gmic_bokeh.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(PENCILBW_SO): $(OBJS) gmic_pencilbw.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@


clean:
	rm -f $(OBJS)
	rm -f $(CARTOON_SO) $(BOKEY_SO) $(PENCLIBW_SO)
