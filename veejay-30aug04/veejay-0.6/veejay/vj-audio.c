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
/* simple routines for audio */
#include "vj-audio.h"
#include "vj-lib.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
/*
#include <ladspa.h>
*/
#define MLIMIT(var, low, high) \
if((var) < (low)) { var = (low); } \
if((var) > (high)) { var = (high); }

int nb_plugin = 0;

/* mixing functions taken from lavfilter, a command line utility for making video transitions for the mjpegtools.
   see the mjpeg-users archives for more information 
   (drop this)

void vj_audio_mix_16_bit(EditList * el, uint8_t * buf1, uint8_t * buf2,
			 int len, int f2, int g2)
{
    int i;
    long f, g, v1, v2;
    int f1 = (65535 - f2);
    int g1 = (65535 - g2);
    int size = len * el->audio_chans *(el->audio_bits / 8);
    //int size = len * el->audio_bps;
    for (i = 0; i < size;) {
	f = f2 * i / size + f1 * (size - i) / size;
	g = g2 * i / size + g1 * (size - i) / size;
	//if(i < 1) fprintf(stderr, "i%d = f %ld  g %ld\n",i,f,g);
	MLIMIT(f, 0, 65535);
	MLIMIT(g, 0, 65535);

	v1 = buf1[i] + 256 * buf1[i + 1];
	v2 = buf2[i] + 256 * buf2[i + 1];

	if (v1 >= 0x8000L) {
	    v1 -= 0x10000L;
	}
	if (v2 >= 0x8000L) {
	    v2 -= 0x10000L;
	}

	MLIMIT(v1, -32767, 32768)
	    MLIMIT(v2, -32767, 32768)

	    v1 *= g;
	v2 *= f;

	v1 += v2;

	v1 /= 65536;
	v2 /= 65536;

	if (v1 < 0) {
	    v1 = 0x10000L + v1;
	}

	buf1[i] = v1 & 255;
	buf1[i + 1] = (v1 >> 8) & 255;

	i += 2;
    }
}



void vj_audio_mix_8_bit(EditList * el, uint8_t * buf1, uint8_t * buf2,
			int len, int f1, int f2, int g1, int g2)
{
    int i;
    long f, g;
    long v1, v2;

    int size = len * el->audio_chans * el->audio_bits / 8;

    for (i = 0; i < size;) {
	f = f2 * i / size + f1 * (size - i) / size;
	g = g2 * i / size + g1 * (size - i) / size;

	MLIMIT(f, 0, 65535)
	    MLIMIT(g, 0, 65535)

	    v1 = buf1[i];
	v2 = buf2[i];

	v1 -= 0x80;
	v2 -= 0x80;

	v1 *= g;
	v2 *= f;

	v1 += v2;
	v1 /= 65535;
	MLIMIT(v1, -128, 127)
	    MLIMIT(v2, -128, 127)

	    v1 += 0x80;
	buf1[i] = v1;
	i += 1;
    }
}
*/
