/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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
#include <veejay/vj-global.h>
#include <libel/vj-avformat.h>
#include <libel/lav_io.h>
#include <libvje/vje.h>

#define N_EL_FRAME(x)  ( (x)&0xfffffffffffffLLU  )
#define N_EL_FILE(x) (int)  ( ((x)>>52)&0xfff ) 
/* ((file)&0xfff<<52) */
#define EL_ENTRY(file,frame) ( ((file)<<52) | ((frame)& 0xfffffffffffffLLU) )

//#define	FMT_420 0
//#define FMT_422 1
//#define MAX_EDITLIST_FILES 4096


typedef struct 
{
	int has_video;
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
	int	play_rate;

	long 	video_frames; 

	long	num_video_files;

	long	max_frame_size;
	int	MJPG_chroma;

	char		*(video_file_list[MAX_EDIT_LIST_FILES]);
	lav_file_t	*(lav_fd[MAX_EDIT_LIST_FILES]);
	long 		num_frames[MAX_EDIT_LIST_FILES];
	uint64_t 	*frame_list;

	int 		last_afile;
	long 		last_apos;
	int		auto_deinter;
	
	int		pixel_format;
	
} editlist;  


editlist *vj_el_init_with_args(char **filenames, int n, int flags, int deinter, int force, char norm);

editlist *vj_el_probe_from_file( char *filename );

void	vj_el_free(editlist *el);

int	vj_el_get_audio_frame_at(editlist *el, uint32_t nframe, uint8_t *dst, int speed );

int	vj_el_append_video_file(editlist *el, char *filename);

int	vj_el_write_editlist( char *filename, long start, long end, editlist *el );

int	vj_el_get_video_frame(editlist *el, long nframe, uint8_t *dst[3], int pix_fmt);

int	vj_el_get_audio_frame(editlist *el, uint32_t nframe, uint8_t *dst);

int	vj_el_get_file_fourcc(editlist *el, int num, char *buf);

void	vj_el_print(editlist *el);

int     vj_el_init_420_frame(editlist *el, VJFrame *frame);
int     vj_el_init_422_frame(editlist *el, VJFrame *frame);

void	vj_el_frame_cache(int n);

void	vj_el_show_formats(void);

editlist *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps);

#endif
