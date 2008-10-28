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

void *composite_init( int pw, int ph, int iw, int ih, const char *hd, int sample_mode, int zoom_type, int pixf );
void composite_destroy( void *c );
void    composite_event( void *compiz, uint8_t *in[3], int mouse_x, int mouse_y, int mouse_button, int w, int h );
void	composite_process_prepare(void *compiz, uint8_t *img_dat[3], VJFrame *input, int use_vp , int focus, int mode );
void    composite_process( void *compiz, uint8_t *in[3], VJFrame *input, int vp_active, int focus);
void	composite_blit( void *compiz,uint8_t *yuyv );
int	composite_blitX( void *compiz, uint8_t *img[3] , uint8_t *out_img[3], int uvlen, int isFull);
int	composite_get_colormode(void *compiz);
void	composite_set_colormode( void *compiz, int mode );
void	composite_blitXfinish(void *compiz, uint8_t *out_img[3] );
void	*composite_get_draw_buffer( void *compiz );
void	composite_get_blit_buffer( void *compiz, uint8_t *buf[3] );
void	*composite_get_vp( void *data );

void	composite_blit2( void *compiz, uint8_t *yuyv );
#endif
