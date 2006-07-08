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

//void *vj_el_init_with_args(char **filenames, int n, int flags, int deinter, int force, char norm, int fmt);

void	*vj_el_open_video_file( const char *filename );

int	vj_el_cache_size();

void	vj_el_prepare(void); // reset cache

void	vj_el_init(); 

void	vj_el_init_chunk(int n);

void	vj_el_free(void *el);

int	vj_el_get_audio_frame_at(void *el, uint32_t nframe, uint8_t *dst, int speed );

int	vj_el_append_video_file(void *el, char *filename);

int	vj_el_write_void( char *filename, long start, long end, void *el );

int	vj_el_get_video_frame(void *el, long nframe, void *frame);

int	vj_el_get_audio_frame(void *el, uint32_t nframe, uint8_t *dst);

int	vj_el_get_file_fourcc(void *el, int num, char *buf);

void	vj_el_print(void *el);

void	vj_el_frame_cache(int n);

void	vj_el_show_formats(void);

void	vj_el_ref(void *el, int num);

void	vj_el_unref(void *el, int num);

void *vj_el_dummy(int flags, int deinterlace, int chroma, char norm, int width, int height, float fps, int fmt);

int	vj_el_get_file_entry( void *el,long *start_pos, long *end_pos, long entry );

void *vj_el_clone(void *el);

void *vj_el_soft_clone(void *el);

void	vj_el_set_itu601( void *edl , int status );

void		vj_el_set_itu601_preference( int status );

int		vj_el_framelist_clone( void *src, void *dst);

char *vj_el_write_line_ascii( void *el, int *bytes_written );

void		vj_el_deinit();

char		vj_el_get_norm( void *edl );
float		vj_el_get_fps(void *edl);
int		vj_el_get_width( void *edl );
int		vj_el_get_height( void *edl );
int		vj_el_get_inter( void *edl );
int		vj_el_get_audio_rate( void *edl );
int		vj_el_get_audio_bps( void *edl );
int		vj_el_get_audio_chans( void *edl );

int	vj_el_match( void *sv, void *edl);

#endif
