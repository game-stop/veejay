#ifndef RAWDV_H
#define RAWDV_H
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
#include <config.h>
#include <stdint.h>
#include <sys/types.h>
#include <libdv/dv.h>
#include <libel/vj-mmap.h>
typedef struct
{
	int	fd;
	char	*filename;
	long	num_frames;
	int	width;
	int	height;
	float	fps;
	int	chunk_size;
	long	audio_rate;
	int	audio_chans;
	int	audio_qbytes;
	dv_decoder_t *decoder;
	int16_t	*audio_buffers[4];
	off_t	offset;
	uint8_t *buf;
	int	size;
	int	fmt;
	mmap_region_t *mmap_region;
} dv_t;

int	rawdv_sampling(dv_t *dv);
int	rawdv_close(dv_t *dv);
dv_t	*rawdv_open_input_file(const char *filename, int mmap_size);
int	rawdv_set_position(dv_t *dv, long nframe);
int	rawdv_read_frame(dv_t *dv, uint8_t *buf );
int	rawdv_read_audio_frame(dv_t *dv, uint8_t *buf);
int	rawdv_video_frames(dv_t *dv);
int	rawdv_width(dv_t *dv);
int	rawdv_height(dv_t *dv);
double	rawdv_fps(dv_t *dv);
int	rawdv_compressor(dv_t *dv);
char 	*rawdv_video_compressor(dv_t *dv);
int	rawdv_audio_channels(dv_t *dv);
int	rawdv_audio_bits(dv_t *dv);
int	rawdv_audio_format(dv_t *dv);
int	rawdv_audio_rate(dv_t *dv);
int	rawdv_audio_bps(dv_t *dv);
int	rawdv_frame_size(dv_t *dv);
int	rawdv_interlacing(dv_t *dv);

#endif
