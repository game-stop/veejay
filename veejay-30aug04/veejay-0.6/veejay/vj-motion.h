
#ifndef VJ_MOTION_H
#define VJ_MOTION_H

#include <config.h>
#include <stdint.h>

typedef struct {
	int xsize;
	int ysize;
	int x;
	int y;
	int diff_t;
	int width;
	int height;
	uint8_t *motion;
	int _first;
	int *history;
	int hist_size;
	int index;
	int sensitivity;
	int in_use;
	int levels[10];
	int action[10];
	int parameters[100];
} vj_window;

int motion_del(vj_window *window);

int motion_get_avg(vj_window *window);

int motion_detected(vj_window *window);

void motion_insert_live_frame(vj_window *window, uint8_t **dst, int transparency); 

int motion_live_detect(vj_window *window);

int motion_live_copy(vj_window *window);

void motion_get_address(uint8_t **);

#endif
