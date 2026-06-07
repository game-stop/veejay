#ifndef PRESSUREWAVE_H
#define PRESSUREWAVE_H

#include <libvje/vje.h>

vj_effect *pressurewave_init(int w, int h);
void *pressurewave_malloc(int w, int h);
void pressurewave_free(void *ptr);
void pressurewave_apply(void *ptr, VJFrame *frame, int *args);

#endif
