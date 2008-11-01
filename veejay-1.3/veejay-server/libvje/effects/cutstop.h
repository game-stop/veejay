#ifndef CUTSTOP_H
#define CUTSTOP_H
#include <libvje/vje.h>
void cutstop_free() ;

vj_effect *cutstop_init(int width , int height);

int	cutstop_malloc(int width, int height);

void cutstop_apply( VJFrame *frame,
	int width, int height, int treshold,
	int freq, int cutmode, int holdmode);

#endif
