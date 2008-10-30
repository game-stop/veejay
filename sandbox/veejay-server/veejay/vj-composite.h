/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2007 Niels Elburg <nelburg@looze.net>
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

int	composite_processX(  void *compiz, uint8_t *tmp_data[3], VJFrame *input );

int	composite_process(void *compiz, VJFrame *output, VJFrame *input, int which_vp );

void	composite_blit( void *compiz,uint8_t *in[3], uint8_t *yuyv, int which_vp );

void	composite_event( void *compiz, uint8_t *in[3], int mouse_x, int mouse_y, int mouse_button, int w_x, int w_y );

void	composite_destroy( void *compiz );

void	*composite_init( int pw, int ph, int iw, int ih, const char *homedir, int sample_mode, int zoom_type, int pf );

void	*composite_get_vp( void *data );



#endif
