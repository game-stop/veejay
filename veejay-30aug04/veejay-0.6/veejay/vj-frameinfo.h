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

#ifndef VJ_FRAMEINFO
#define VJ_FRAMEINFO

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t *histogram_yuv[3];
    unsigned int width;
    unsigned int height;
    unsigned int len;
    unsigned int x;
    unsigned int y;
} vj_frameinfo;

typedef struct {
    uint8_t *Y[255];
    uint8_t *Cb[255];
    uint8_t *Cr[255];
} vj_lookup_table;


int vj_frameinfo_set_dot(vj_frameinfo * fi, int x, int y);
void vj_frameinfo_what_is(vj_frameinfo * fi, uint8_t * yuv_frame[3],
			  int *r, int *g, int *b);
int vj_frameinfo_most_occuring(vj_frameinfo * fi, int *r, int *g, int *b);
int vj_frameinfo_less_occuring(vj_frameinfo * frameinfo, int *r, int *g,
			       int *b);
int vj_frameinfo_make_histogram(vj_frameinfo * fi, uint8_t * yuv_frame[3]);
int vj_frameinfo_clean(vj_frameinfo * fi);
void vj_frameinfo_init(vj_frameinfo * frameinfo, int width, int height);
int vj_frameinfo_reset(vj_frameinfo * fi);

vj_lookup_table *vj_lookup_new();
void vj_lookup_table_process(vj_lookup_table * vlt, uint8_t * yuv1[3],
			     uint8_t * yuv2[3], double opa, double opb,
			     int width, int height);

#endif
