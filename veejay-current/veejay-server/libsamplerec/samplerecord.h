/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
#ifndef SAMPLEREC_H
#define SAMPLEREC_H
#include <stdint.h>
#include <libel/vj-el.h>
int sample_record_init(int len);
int sample_init_encoder(int sample_id, char *filename, int format, VJFrame *frame,editlist *el, long nframes);
int sample_record_frame(int s1, uint8_t *buffer[4], uint8_t *abuff, int audio_size, int pixel_format);
int sample_get_encoder_format(int s1);
int sample_stop_encoder(int s1) ;
int sample_get_encoded_frames(int s1);
int sample_get_encoded_file(int s1, char *dst);
int sample_encoder_active(int s1);
void sample_reset_encoder(int s1);
long sample_get_frames_left(int s1);
int sample_reset_autosplit(int s1);
long sample_get_total_frames( int s1 );
int sample_get_num_encoded_files(int sample_id);
int sample_get_sequenced_file(int sample_id, char *descr, int num, char *ext);
int sample_try_filename(int sample_id, char *filename, int format);
int sample_continue_record( int s1 );
void	sample_record_free();

#endif
