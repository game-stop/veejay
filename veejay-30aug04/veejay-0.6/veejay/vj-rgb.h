/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
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

#ifndef VJ_RGB_H
#define VJ_RGB_H
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "editlist.h"

#define RAW_RGB24 0
#define RAW_ANY   1

typedef struct {
    uint8_t *buf;
    uint8_t *yuv;
    int size;
    int palette;
    int fd;
    int width;
    int height;
    int line_size[3];
    int need_mem;
} vj_raw;

vj_raw *vj_raw_alloc(EditList *el);

void vj_raw_free();

int vj_raw_init(vj_raw * , int palette);

int vj_raw_stream_start_write(vj_raw *raw, char *file);

int vj_raw_stream_start_read(vj_raw *raw, char *file);

int vj_raw_stream_stop_rw(vj_raw *raw);

int vj_raw_get_frame(vj_raw *raw, uint8_t *dst[3]);

int vj_raw_put_frame(vj_raw *raw, uint8_t *src[3]);

#endif
