#ifndef YUVCONF_H
#define YUVCONF_H

/*
 * subsample.h:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *                2004 Niels Elburg <nelburg@looze.net>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <libvje/vje.h>
typedef enum subsample_mode {
    SSM_UNKNOWN = 0,
    SSM_420_JPEG_TR = 1,
    SSM_420_JPEG_BOX = 2,
    SSM_420_MPEG2 = 3,
    SSM_422_444 = 4, 
    SSM_420_422 = 5, 
    SSM_COUNT = 6,
} subsample_mode_t;

extern const char *ssm_id[SSM_COUNT];
extern const char *ssm_description[SSM_COUNT];

void subsample_init(int buf_len);

void chroma_subsample(subsample_mode_t mode, uint8_t * ycbcr[],
		      int width, int height);

void chroma_supersample(subsample_mode_t mode, uint8_t * ycbcr[],
			int width, int height);

// yuv 4:2:2 packed to yuv 4:2:0 planar 
void yuy2toyv12( uint8_t *y, uint8_t *u, uint8_t *v,  uint8_t *in, int w, int h);
// yuv 4:2:2 packet to yuv 4:2:2 planar
void yuy2toyv16( uint8_t *y, uint8_t *u, uint8_t *v, uint8_t *in, int w, int h);
// yuv 4:2:2 planar to yuv 4:2:2 packed
void yuv422p_to_yuv422( uint8_t *yuv422[3], uint8_t *dst, int w, int h );

// yuv 4:2:2 planar to yuv 4:2:0 planar
void yuv420p_to_yuv422( uint8_t *yuv420[3], uint8_t *dst, int w, int h );

// yuv 4:2:2 planar to YUYV
void yuv422_to_yuyv( uint8_t *yuv422[3], uint8_t *dst, int w, int h );

// scene detection
int luminance_mean(uint8_t * frame[], int w, int h);

/* software scaler from ffmpeg project: */

typedef struct
{
	float lumaGBlur;
	float chromaGBlur;
	float lumaSarpen;
	float chromaSharpen;
	float chromaHShift;
	float chromaVShift;
	int	verbose;
	int	flags;
	int	use_filter;
} sws_template;

void*	yuv_init_swscaler(VJFrame *src, VJFrame *dst, sws_template *templ, int cpu_flags);

void	yuv_convert_and_scale( void *sws, VJFrame *src, VJFrame *dst );

int	yuv_sws_get_cpu_flags(void);

void	yuv_free_swscaler(void *sws);

#endif
