#ifndef DATAMOSH_H
#define DATAMOSH_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

vj_effect *datamosh_init(int w, int h);
void *datamosh_malloc(int w, int h);
void datamosh_free(void *ptr);
void datamosh_apply(void *ptr, VJFrame *frame, int *args);

#ifdef __cplusplus
}
#endif

#endif
