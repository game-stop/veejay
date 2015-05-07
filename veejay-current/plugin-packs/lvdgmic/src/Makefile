CFLAGS=-Wall -g -fPIC
CCFLAGS=Wall -g -fPIC
LDFLAGS=-g -Wall -lstdc++ -lgmic -shared

OBJS=lvdgmic.o lvdgmicglue.o 
CARTOON_SO=gmic_cartoon.so
BOKEY_SO=gmic_bokeh.so
PENCILBW_SO=gmic_pencilbw.so
ARRAYFADE_SO=gmic_arrayfade.so
PENCILPORTRAIT_SO=gmic_pencilportrait.so
RODILIUS_SO=gmic_rodilius.so
WATER_SO=gmic_water.so

all: $(CARTOON_SO) $(BOKEY_SO) $(PENCILBW_SO) $(ARRAYFADE_SO) $(RODILIUS_SO) $(WATER_SO)
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

$(ARRAYFADE_SO): $(OBJS) gmic_arrayfade.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(RODILIUS_SO): $(OBJS) gmic_rodilius.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(WATER_SO): $(OBJS) gmic_water.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@


clean:
	rm -f *.o
	rm -f $(CARTOON_SO) $(BOKEY_SO) $(PENCLIBW_SO) $(ARRAYFADE_SO) $(RODILIUS_SO) $(WATER_SO)
