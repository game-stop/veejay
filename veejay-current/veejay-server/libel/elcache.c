/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/*
	Cache frames from file to memory
 */
#include <config.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libel/elcache.h>


//FIXME: move to core and use as cache for samples and v4l2 devices 

typedef struct
{
	int	size;
	int	fmt;
	long	num;
	void    *buffer;
} cache_slot_t;

typedef struct
{
	cache_slot_t	**cache;
	int		len;
	long		*index;
	long total_mem_used;
} cache_t;


static void free_single_slot(cache_t *v, int slot_index) {
    if (v->cache[slot_index]) {
        v->total_mem_used -= v->cache[slot_index]->size;
        if (v->cache[slot_index]->buffer) {
            free(v->cache[slot_index]->buffer);
        }
        free(v->cache[slot_index]);
        v->cache[slot_index] = NULL;
    }
    v->index[slot_index] = -1;
}

static int cache_free_slot(cache_t *v) {
    for (int i = 0; i < v->len; i++) {
        if (v->index[i] == -1) return i;
    }
    return -1;
}

static int cache_find_slot(cache_t *v, long frame_num) {
    int k = 0;
    long max_score = -1;
    for (int i = 0; i < v->len; i++) {
        long diff = v->index[i] - frame_num;

        long score = (diff < 0) ? labs(diff) * 2 : labs(diff); 
        
        if (score > max_score) {
            max_score = score;
            k = i;
        }
    }
    return k;
}

static int cache_locate_slot(cache_t *v, long frame_num) {
    for (int i = 0; i < v->len; i++) {
        if (v->index[i] == frame_num) return i;
    }
    return -1;
}

static void cache_claim_slot(cache_t *v, int slot, uint8_t *linbuf, long frame_num, int buf_len, int format) {

    cache_slot_t *new_data = (cache_slot_t*) vj_malloc(sizeof(cache_slot_t));
    if (!new_data) return;

    new_data->buffer = vj_malloc(buf_len);
    if (!new_data->buffer) {
        free(new_data);
        return;
    }

    free_single_slot(v, slot);

    new_data->size = buf_len;
    new_data->num = frame_num;
    new_data->fmt = format;
    veejay_memcpy(new_data->buffer, linbuf, buf_len);

    v->index[slot] = frame_num;
    v->cache[slot] = new_data;
    v->total_mem_used += buf_len;
}
void *init_cache(unsigned int n_slots) {
    if (n_slots == 0) return NULL;

    cache_t *v = (cache_t*) vj_calloc(sizeof(cache_t));
    if (!v) return NULL;

    v->len = n_slots;
    v->cache = (cache_slot_t**) vj_calloc(v->len * sizeof(cache_slot_t*));
    v->index = (long*) vj_malloc(sizeof(long) * v->len);

    if (!v->cache || !v->index) {
        if (v->cache) free(v->cache);
        if (v->index) free(v->index);
        free(v);
        return NULL;
    }

    for (int n = 0; n < v->len; n++) {
        v->index[n] = -1;
        v->cache[n] = NULL;
    }
    v->total_mem_used = 0;
    return (void*) v;
}

void reset_cache(void *cache) {
    if (!cache) return;
    cache_t *v = (cache_t*) cache;
    for (int i = 0; i < v->len; i++) {
        free_single_slot(v, i);
    }
}

long cache_avail_mb(void *cache) { //FIXME callers
    if (!cache) return 0;
    cache_t *v = (cache_t*) cache;
    return v->total_mem_used / (1024 * 1024);
}

void free_cache(void *cache) {
    if (!cache) return;
    cache_t *v = (cache_t*) cache;
    
    reset_cache(v);

    if (v->cache) free(v->cache);
    if (v->index) free(v->index);
    free(v);
}

void cache_frame(void *cache, uint8_t *linbuf, int buflen, long frame_num, int format) { // take VJFrame* and another function to take buf
    if (!cache || buflen <= 0 || !linbuf) return;
    cache_t *v = (cache_t*) cache;

    int slot_num = cache_free_slot(v);
    if (slot_num == -1) {
        slot_num = cache_find_slot(v, frame_num);
    }

    cache_claim_slot(v, slot_num, linbuf, frame_num, buflen, format);
}

uint8_t *get_cached_frame(void *cache, long frame_num, int *buf_len, int *format) {
    if (!cache) return NULL;
    cache_t *v = (cache_t*) cache;
    int slot = cache_locate_slot(v, frame_num);
    
    if (slot == -1 || v->cache[slot] == NULL) return NULL;

    cache_slot_t *data = v->cache[slot];
    if (buf_len) *buf_len = data->size;
    if (format) *format = data->fmt;
    return (uint8_t*) data->buffer;
}