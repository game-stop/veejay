CFLAGS=-Wall -g -fPIC -DSILENT -ftree-vectorize -O3 -march=native
CCFLAGS=Wall -g -fPIC -DSILENT -ftree-vectorize -O3 -march=native
LDFLAGS=-g -Wall -lstdc++ -lgmic -shared

OBJS=lvdgmic.o lvdgmicglue.o 
CARTOON_SO=gmic_cartoon.so
CRACKS_SO=gmic_cracks.so
BOKEY_SO=gmic_bokeh.so
PENCILBW_SO=gmic_pencilbw.so
ARRAYFADE_SO=gmic_arrayfade.so
PENCILPORTRAIT_SO=gmic_pencilportrait.so
RODILIUS_SO=gmic_rodilius.so
WATER_SO=gmic_water.so
WAVE_SO=gmic_wave.so
WIND_SO=gmic_wind.so
LIGHTPATCH_SO=gmic_lightpatch.so
NOISEHURL_SO=gmic_noisehurl.so

all: $(CARTOON_SO) $(BOKEY_SO) $(PENCILBW_SO) $(ARRAYFADE_SO) $(RODILIUS_SO) $(WATER_SO) $(WAVE_SO) $(WIND_SO) $(CRACKS_SO) \
	$(LIGHTPATCH_SO) $(NOISEHURL_SO)

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

$(WAVE_SO): $(OBJS) gmic_wave.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(WIND_SO): $(OBJS) gmic_wind.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(CRACKS_SO): $(OBJS) gmic_cracks.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(LIGHTPATCH_SO): $(OBJS) gmic_lightpatch.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

$(NOISEHURL_SO): $(OBJS) gmic_noisehurl.o
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(CC) $(CFLAGS) -shared $^ -lgmic -o $@

clean:
	rm -f *.o
	rm -f $(CARTOON_SO) $(BOKEY_SO) $(PENCLIBW_SO) $(ARRAYFADE_SO) $(RODILIUS_SO) $(WATER_SO) $(WAVE_SO) $(WIND_SO) $(CRACKS_SO) \
		$(LIGHTPATCH_SO) $(NOISEHURL_SO)
