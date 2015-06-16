/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef VJ_MLT_EL_H
#define VJ_MLT_EL_H
#include <config.h>
#include <libel/lav_io.h>
#include <libvje/vje.h>
#include <veejay/vims.h>
#define N_EL_FRAME(x)  ( (x)&0xfffffffffffffLLU  )
#define N_EL_FILE(x) (uint32_t)  ( ((x)>>52)&0xfffU ) 
/* ((file)&0xfff<<52) */
#define EL_ENTRY(file,frame) ( ((file)<<52) | ((frame)& 0xfffffffffffffLLU) )

#define VIDEO_MODE_PAL		0
#define VIDEO_MODE_NTSC		1
#define VIDEO_MODE_SECAM	2
#define VIDEO_MODE_AUTO		3

typedef struct 
{
	int 	has_video;
	int	is_empty;
	int	video_width;  
	int	video_height;
	int	video_inter;
	float	video_fps;
	int	video_sar_width;	
	int 	video_sar_height;	
	char 	video_norm;	

	int	has_audio; 
	long	audio_rate;
	int	audio_chans;
	int	audio_bits;
	int	audio_bps;

	long 	video_frames; 
	long	num_video_files;
	uint64_t 	total_frames;
	long	max_frame_size;
	int	MJPG_chroma;

	char		*(video_file_list[MAX_EDIT_LIST_FILES]);
	lav_file_t	*(lav_fd[MAX_EDIT_LIST_FILES]);
	int		pixfmt[MAX_EDIT_LIST_FILES];
	void		*ctx[MAX_EDIT_LIST_FILES];
	void		*decoders[MAX_EDIT_LIST_FILES];
	long 		num_frames[MAX_EDIT_LIST_FILES];
	long		max_frame_sizes[MAX_EDIT_LIST_FILES];
	uint64_t 	*frame_list;

	int 		last_afile;
	long 		last_apos;
	int		auto_deinter;
	
	int		pixel_format;	
	void		*cache;

	int		is_clone;
	void		*scaler;
} editlist;  

int     test_video_frame(editlist *el, int n, lav_file_t *lav,int out_pix_fmt);

void	vj_el_scan_video_file( char *filename,  int *dw, int *dh, float *dfps, long *arate );

editlist *vj_el_init_with_args(char **filenames, int n, int flags, int deinter, int force, char norm, int fmt, int w, int h);

int	vj_el_cache_size();

void	vj_el_prepare(void); // reset cache

void	vj_el_init(int out, int sj, int dw, int dh, float fps); 

void	vj_el_init_chunk(int n);

int	vj_el_is_dv(editlist *el);

void	vj_el_set_mmap_size( long size );

void	vj_el_free(editlist *el);

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int speed );

int	vj_el_append_video_file(editlist *el, char *filename);

int	vj_el_write_editlist( char *filename, long start, long end, editlist *el );

int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[4]);

void    vj_el_break_cache( editlist *el );

void    vj_el_setup_cache( editlist *el );

int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst);

int	vj_el_get_file_fourcc(editlist *el, int num, char *buf);

void	vj_el_print(editlist *el);

int     vj_el_init_420_frame(editlist *el, VJFrame *frame);
int     vj_el_init_422_frame(editlist *el, VJFrame *frame);

void	vj_el_frame_cache(int n);

void	vj_el_show_formats(void);

editlist *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt);

int	vj_el_get_file_entry( editlist *el,long *start_pos, long *end_pos, long entry );

editlist *vj_el_clone(editlist *el);

editlist *vj_el_soft_clone(editlist *el);

int		vj_el_framelist_clone( editlist *src, editlist *dst);

char *vj_el_write_line_ascii( editlist *el, int *bytes_written );

void		vj_el_deinit();

void	vj_el_clear_cache( editlist *el );

int     get_ffmpeg_pixfmt( int pf );

int open_video_file(char *filename, editlist * el, int preserve_pathname, int deinter, int force, char override_norm, int w, int h, int fmt);

void	vj_el_set_caching(int status);

int	vj_el_bogus_length( editlist *el, long nframe );

int	vj_el_set_bogus_length( editlist *el, long nframe, int len );

int	vj_el_pixfmt_to_veejay(int pix_fmt );

int	vj_el_get_usec_per_frame( float video_fps );

float	vj_el_get_default_framerate( int norm );

char	vj_el_get_default_norm( float fps );

long      vj_el_get_mem_size();

int	vj_el_auto_detect_scenes( editlist *el, uint8_t *tmp[4], int w, int h, int dl_threshold );

#endif
