
#ifndef VJ_COMMON_H
#define VJ_COMMON_H
#include <stdarg.h>
#include <stdint.h>
#include "vj-lib.h"

#define VEEJAY_FILE_LIMIT (1048576*1000*2)


void veejay_strrep(char *s, char delim, char tok);

int vj_perform_screenshot2(veejay_t * info, uint8_t ** src);

void veejay_msg(int type, const char format[], ...) GNUC_PRINTF(2,3);

void mymemset_generic(void *s, char c, size_t count);

float vj_get_relative_time();

void veejay_set_debug_level(int level);
void veejay_set_colors(int level);
void veejay_silent();
int veejay_is_silent();

int vj_perform_take_bg(veejay_t *info, uint8_t **src);

void *vj_malloc(unsigned int size);

void find_best_memcpy();



#endif
