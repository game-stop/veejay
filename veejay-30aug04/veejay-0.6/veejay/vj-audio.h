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
#ifndef VJ_AUDIO_H
#define VJ_AUDIO_H
#include <stdint.h>

#include <math.h>
#include <dlfcn.h>
//#include "utils.h"
#include <unistd.h>
#include <stdio.h>
/*
#include <ladspa.h>
*/
#include "editlist.h"

/*
typedef struct _vj_ladspa_tab {
	const char filename[1024];
	const char label[255];
} vj_ladspa_tab;

typedef struct _vj_ladspa_instance {
	LADSPA_Handle *handle;
	LADSPA_Descriptor *descriptor;
	LADSPA_Data *cpv;
} vj_ladspa_instance;


vj_ladspa_tab available_plugins[100];
*/
void vj_audio_mix_8_bit(EditList * el, uint8_t * buf1, uint8_t * buf2,
			int len, int f1, int f2, int g1, int g2);

void vj_audio_mix_16_bit(EditList * el, uint8_t * buf1, uint8_t * buf2,
			 int len, int f1, int f2);
/*
void vj_ladspa_search(const char *filename, void *handle,
	LADSPA_Descriptor_Function func);

void vj_ladspa_print();

vj_ladspa_instance *vj_ladspa_init_plugin(int plugin);

void vj_ladspa_plugin_run(vj_ladspa_instance *plugin, unsigned long size);

int vj_ladspa_connect_port(vj_ladspa_instance *plugin, LADSPA_Data *in1, LADSPA_Data *in2,
		LADSPA_Data *out1, LADSPA_Data *out2) ;

int vj_ladspa_usable(vj_ladspa_instance *plugin) ;

void vj_audio_uint8_t_float(uint8_t *audio, float *out, int size) ;


int vj_ladspa_count_output_ports(vj_ladspa_instance *plugin) ;

int vj_ladspa_count_input_port(vj_ladspa_instance *plugin) ;

int vj_ladspa_connect_output_port(vj_ladspa_instance *plugin, LADSPA_Data *out) ;


int vj_ladspa_connect_input_port(vj_ladspa_instance *plugin, LADSPA_Data *in) ;

void vj_audio_uint8_t_float_split(uint8_t *audio, float *left, float *right, unsigned long size);

void vj_audio_float_uint8_t_merge(uint8_t *audio, float *left, float *right, long size) ;
*/
#endif
