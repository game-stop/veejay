/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */
#ifndef COMPOSITEHVJ
#define COMPOSITEHVJ

void	*composite_get_draw_buffer( void *compiz );

void	composite_set_colormode( void *compiz, int mode );

int	composite_get_colormode(void *compiz);

int	composite_get_original_frame(void *compiz, uint8_t *current_in[4], uint8_t *out[4], int which_vp, int row_start, int row_end );

int	composite_get_top(void *compiz, uint8_t *current_in[4], uint8_t *out[4], int mode );

int	composite_processX(  void *compiz, void *back1,uint8_t *tmp_data[4], VJFrame *input );

int	composite_process(void *compiz, VJFrame *output, VJFrame *input, int which_vp, int pixfmt );

void	composite_blit_ycbcr( void *compiz,uint8_t *in[4], int which_vp, void *gl );


void	composite_blit_yuyv( void *compiz,uint8_t *in[4], uint8_t *yuyv, int which_vp );

int	composite_event( void *compiz, uint8_t *in[4], int mouse_x, int mouse_y, int mouse_button, int w_x, int w_y );

void	composite_destroy( void *compiz );

void	*composite_init( int pw, int ph, int iw, int ih, const char *homedir, int sample_mode, int zoom_type, int pf, int *vp1_enabled );

void	composite_set_backing( void *compiz, void *vp );

void	*composite_clone( void *compiz );

void	*composite_get_vp( void *data );
void	composite_set_ui(void *compiz, int status );
int	composite_get_ui(void *compiz );
//@ load config after loading to activate viewport setup
//@ add to config before saving
void	*composite_load_config( void *compiz, void *vc, int *result );
void	composite_add_to_config( void *compiz, void *vc, int which_vp );
int	composite_get_status(void *compiz );
void	composite_set_status(void *compiz, int mode);
#endif
