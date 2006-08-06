/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
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
#ifndef FRAME_T_DEF
#define FRAME_T_DEF
typedef struct VJFrame_t 
{
        uint8_t *data[4];
        int     uv_len;
        int     len;
        int     uv_width;
        int     uv_height;
        int     shift_v;
        int     shift_h;
        int     format;
	int	pixfmt;
        int     width;
        int     height;
        int     sampling;
	double	timecode;
	double	fps;
} VJFrame;

typedef struct AFrame_t
{
	long	rate;
	uint8_t *data;
	int	bits;
	int	bps;
	int	samples;
	int	num_chans;
} AFrame;

#define	FMT_420	0
#define	FMT_422	1	
#define FMT_444	2
#define FMT_411 3

#define	NO_AUDIO 0
#define AUDIO_PLAY 1

#define	VEEJAY_STATE_PLAYING 1
#define VEEJAY_STATE_STOP 0


#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3

#define	PREVIEW_50		1
#define PREVIEW_25		2
#define PREVIEW_125		3

#define PREVIEW_NONE		0
#define PREVIEW_GREYSCALE	1
#define PREVIEW_COLOR		2

#define	ENCODER_MJPEG		0
#define ENCODER_YUV420		1	
#define ENCODER_YUV422		2
#define ENCODER_MPEG4		3	
#define ENCODER_DIVX		4
#define ENCODER_DVVIDEO		5

#endif
