/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#include <config.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "vj-motion.h"
#include "vj-common.h"


extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;



uint8_t *motion_live[3];

int motion_alloc(int w, int h) {
    motion_live[0] = (uint8_t*) malloc(sizeof(uint8_t) * w * h);
    if(!motion_live[0]) return -1;
    motion_live[1] = (uint8_t*) malloc(sizeof(uint8_t) * w * h);
    if(!motion_live[1]) return -1;
    motion_live[2] = (uint8_t*) malloc(sizeof(uint8_t) * w * h);
    if(!motion_live[2]) return -1;

    memset(motion_live[0], 16, (w*h));
    memset(motion_live[1], 128, (w*h));
    memset(motion_live[2], 128, (w*h));

    return 0;
}

void motion_get_address(uint8_t **a) {
	a[0] = motion_live[0];
	a[1] = motion_live[1];
	a[2] = motion_live[2];
} 

void motion_free() {
	if(motion_live[0]) free(motion_live[0]);
	if(motion_live[1]) free(motion_live[1]);
	if(motion_live[2]) free(motion_live[2]);
}

/* calculate average motion , history is length in frames */
int motion_get_avg(vj_window *window) {
	int i=0;
	int sum = 0;
	int j = 0;
	for(i=0; i < window->index; i++) {
	   if(window->history[i]>0) { sum += window->history[i]; j++; }
	}
	if(sum==0) return 0;
	return (sum/j);
}

/* delete a window */
int motion_del(vj_window *window) {

	if(window->in_use==1) {
		if(window->motion) free(window->motion);
		if(window->history) free(window->history);
		window->xsize=0;
		window->ysize=0;
		window->x =0;
		window->y =0;
		window->in_use=0;
		window->_first = 0;		
		veejay_msg(VEEJAY_MSG_INFO, "Deleted Gesture window");
	        return 1;
	}
 	return 0;
}


int motion_detected(vj_window *window) {
	return (window->history[window->index]);
}

/* peek at a portion of the frame */
int motion_live_detect(vj_window *window) 
{
   unsigned int dx,dy;
   unsigned int pixels=0;
   uint8_t *dst = window->motion; 
   unsigned int j=0,i=0;
   unsigned int min_y = window->y;
   unsigned int max_y = window->ysize + window->y;
   unsigned int max_x = window->xsize + window->x;
   for(dy=min_y; dy < max_y; dy++) { 
	i = window->x + dy * window->width;
	for(dx=window->x; dx < max_x; dx++ ) {
		if(abs(motion_live[0][i] - dst[j]) > window->diff_t) pixels++; /* difference count */
		dst[j] = motion_live[0][i]; /* copy old */
		j++; /* next pixel */	
		i++;
	}
   }
   return pixels;
}

/* copy live buffer to window (initialisation) */
int motion_live_copy(vj_window *window) {
	unsigned int dx,dy;
	unsigned int pixels=0;
	uint8_t *dst = window->motion;
	unsigned int j=0,i=0;
	unsigned int min_y = window->y;
   	unsigned int max_y = window->ysize + window->y;
   	unsigned int max_x = window->xsize + window->x;
	for(dy=min_y; dy < max_y; dy ++) {
	 i = window->x + dy * window->width;
	 for(dx=window->x; dx < max_x; dx++) {
	    dst[j] = motion_live[0][i]; /* update motion buffer */
	    j++;
	    i++;
	 }
	}
	return pixels;
}

void motion_insert_live_frame(vj_window *window, uint8_t **dst, int opacity) {
	
	unsigned int dx,dy;
	uint8_t *src = window->motion;
	unsigned int j=0,i=0;
	unsigned int min_y = window->y;
   	unsigned int max_y = window->ysize + min_y;
   	unsigned int max_x = window->xsize + window->x;
	unsigned int op0 = opacity;
	unsigned int op1 = 255 - opacity;
	
	for(dy=min_y; dy < max_y; dy ++) {
	 i = window->x + dy * window->width;
	 for(dx=window->x; dx < max_x; dx++) {
	    dst[0][i] = ((src[j] * op0) + (dst[0][i] * op1))>>8; 
	    j++;
	    i++;
	 }
	}
	

	/*
 	veejay_memcpy(dst[0], motion_live[0], (window->width * window->height));
	memset(dst[2], 128 , (window->width * window->height));		
	memset(dst[1],128,(window->width * window->height));
	*/
}

