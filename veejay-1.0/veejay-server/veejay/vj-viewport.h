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
#ifndef VIEWPORT_H
#define VIEWPORT_H

/* Viewport component for FX */
#define	VP_QUADZOOM 1
void		viewport_process_dynamic( void *data, uint8_t *in[3], uint8_t *out[3] );
void 		*viewport_fx_init(	int type, int wid, int hei, int x, int y, int zoom );


/* The viewport */
int        viewport_active( void *data );
int	viewport_render_ssm(void *vdata );
void	viewport_render( void *data, uint8_t *in[3], uint8_t *out[3], int width, int height,int uv_len );
void	viewport_external_mouse( void *data, uint8_t *in[3],int sx, int sy, int button, int frontback, int w, int h  );
char	*viewport_get_help(void *data);
void	viewport_clone_parameters( void *src , void *dst );
void 	*viewport_init(int w, int h, const char *dir, int *enable, int *frontback, int mode);
int	viewport_active( void *data );
void			viewport_destroy( void *data );
void	vewport_draw_interface_color( void *vdata, uint8_t *img[3] );
void	viewport_produce_full_img_yuyv( void *vdata, uint8_t *img[3], uint8_t *out_img );
void      viewport_draw_interface_color( void *vdata, uint8_t *img[3] );
void	viewport_set_marker( void *vdata, int status );
#endif
