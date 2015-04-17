#ifndef YUVCONF_H
#define YUVCONF_H
/*  Veejay -  A visual instrument and realtime video sampler
 *            Copyright (C)    2004 Niels Elburg <nwelburg@gmail.com>
 *
 *  YUV library for veejay.
 *
 *  Mjpegtools, (C) The Mjpegtools Development Team (http://mjpeg.sourceforge.net)
 *  	      Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
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
int get_pixfmt_from_chroma(int chroma);
int get_chroma_from_pixfmt(int pixfmt);
int vj_to_pixfmt(int fmt);
int pixfmt_to_vj(int pixfmt);
int pixfmt_is_full_range(int pixfmt);
int vj_is_full_range(int fmt);

// yuv 4:2:2 packed to yuv 4:2:0 planar 
void vj_yuy2toyv12( uint8_t *y, uint8_t *u, uint8_t *v,  uint8_t *in, int w, int h);
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

void yuv_plane_sizes( VJFrame *src, int *p1, int *p2, int *p3, int *p4 );

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

void	yuv_init_lib(int sws_extra_flags, int auto_jpeg_ccir, int scaler_type);
void*	yuv_init_cached_swscaler(void *cache,VJFrame *src, VJFrame *dst, sws_template *tmpl, int cpu_flags);
void*	yuv_init_swscaler(VJFrame *src, VJFrame *dst, sws_template *templ, int cpu_flags);
void	yuv_convert_and_scale_packed( void *sws, VJFrame *src, VJFrame *dst );

void	yuv_convert_and_scale( void *sws, VJFrame *src, VJFrame *dst );

void	yuv_convert_and_scale_rgb( void *sws, VJFrame *src, VJFrame *dst );

void	yuv_convert_and_scale_gray_rgb(void *sws,VJFrame *src, VJFrame *dst);
void	yuv_convert_and_scale_from_rgb(void *sws , VJFrame *src, VJFrame *dst);
void	yuv_convert_and_scale_grey(void *sws , VJFrame *src, VJFrame *dst);

int	yuv_sws_get_cpu_flags(void);

void	yuv_free_swscaler(void *sws);

void  	yuv_crop(VJFrame *src, VJFrame *dst, VJRectangle *rect );

VJFrame	*yuv_allocate_crop_image( VJFrame *src, VJRectangle *rect );

void	yuv_deinterlace(
		uint8_t *data[3],
		const int width,
		const int height,
		int out_pix_fmt,
		int shift,
		uint8_t *Y,uint8_t *U, uint8_t *V );


void	yuv_init_lib();

void	yuv_free_lib();

void	yuv_convert_ac( VJFrame *src, VJFrame *dst, int a, int b );

//void	yuv_convert_any( VJFrame *src, VJFrame *dst, int a, int b );


void	yuv_convert_any_ac_packed( VJFrame *src, uint8_t *dst, int src_fmt, int dst_fmt );

void	yuv_convert_any3( void *scaler, VJFrame *src,int strides[], VJFrame *dst, int a, int b );


VJFrame *yuv_rgb_template( uint8_t *rgb_buffer, int w, int h, int fmt );

VJFrame *yuv_yuv_template( uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int fmt );

const char	*yuv_get_scaler_name(int id);

void	yuv_convert_any_ac( VJFrame *src, VJFrame *dst, int src_fmt, int dst_fmt );

void    *yuv_fx_context_create( VJFrame *src, VJFrame *dst, int src_fmt, int dst_fmt );

void    yuv_fx_context_process( void *ctx, VJFrame *src, VJFrame *dst );

void    yuv_fx_context_destroy( void *ctx );


void	yuv420to422planar( uint8_t *src[3], uint8_t *dst[3], int w, int h );
void	yuv422to420planar( uint8_t *src[3], uint8_t *dst[3], int w, int h );

void	yuv_scale_pixels_from_yuv( uint8_t *src[3], uint8_t *dst[3], int len );

void	yuv_scale_pixels_from_y( uint8_t *plane, int len );
void	yuv_scale_pixels_from_uv( uint8_t *plane, int len );
void	yuv_scale_pixels_from_ycbcr( uint8_t *plane, float min, float max, int len );
int 	yuv_use_auto_ccir_jpeg();

void	yuy2_scale_pixels_from_yuv( uint8_t *plane, int len );
void	yuy2_scale_pixels_from_ycbcr( uint8_t *plane, int len );
void	yuv_scale_pixels_from_ycbcr2( uint8_t *plane[3], uint8_t *dst[3], int len );
void yuv444_yvu444_1plane(
		uint8_t *data[3],
		const int width,
		const int height,
		uint8_t *dst_buffer);

int	yuv_which_scaler();
#endif
