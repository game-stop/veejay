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
#ifndef CLIPREC_H
#define CLIPREC_H
#include <stdint.h>
#include "vj-lib.h"
int clip_record_init(int len);
int clip_init_encoder(int clip_id, char *filename, int format, editlist *el,long nframes);
int clip_record_frame(int s1, uint8_t *buffer[3], uint8_t *abuff, int audio_size);
int clip_get_encoder_format(int s1);
int clip_stop_encoder(int s1) ;
int clip_get_encoded_frames(int s1);
int clip_get_encoded_file(int s1, char *dst);
int clip_encoder_active(int s1);
void clip_reset_encoder(int s1);
#endif
