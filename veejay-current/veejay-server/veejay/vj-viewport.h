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
#ifndef VIEWPORT_H
#define VIEWPORT_H

/* Viewport component for FX */
#define	VP_QUADZOOM 1
void		viewport_process_dynamic( void *data, uint8_t *in[3], uint8_t *out[3] );
void		viewport_process_dynamic_map( void *data, uint8_t *in[3], uint8_t *out[3], uint32_t *map, int feather );
void 		*viewport_fx_init(	int type, int wid, int hei, int x, int y, int zoom, int dir );
void	viewport_update_from(void *vv, void *bb);
void *viewport_clone(void *iv, int new_w, int new_h );
void	viewport_set_ui(void *vv, int value );
/* The viewport */
int        viewport_active( void *data );
int	viewport_render_ssm(void *vdata );
void	viewport_render( void *data, uint8_t *in[3], uint8_t *out[3], int width, int height,int uv_len );
int	viewport_external_mouse( void *data, uint8_t *in[3],int sx, int sy, int button, int frontback, int w, int h  );
char	*viewport_get_help(void *data);
char *viewport_get_my_status(void *v);
void	viewport_clone_parameters( void *src , void *dst );
void 	*viewport_init(int x0, int y0, int w0, int h0, int w, int h,int iw, int ih, const char *dir, int *enable, int *frontback, int mode);
int	viewport_active( void *data );
void			viewport_destroy( void *data );
void	vewport_draw_interface_color( void *vdata, uint8_t *img[3] );
void	viewport_produce_bw_img( void *vdata, uint8_t *img[3], uint8_t *out_img[3], int Yonly);
void	viewport_produce_full_img_yuyv( void *vdata, uint8_t *img[3], uint8_t *out_img );
void      viewport_draw_interface_color( void *vdata, uint8_t *img[3] );
void	viewport_set_marker( void *vdata, int status );
void	viewport_projection_inc( void *data, int incr , int w, int h );
void	viewport_transform_coords( void *data, void *coords, int n, int blob_id, int cx, int cy ,int w, int h, int num_objects,uint8_t *plane);
void	viewport_dummy_send( void *data );
int	*viewport_event_get_projection(void *data, int scale);
int	viewport_event_set_projection(void *data, float x, float y, int num, int fb);
void		viewport_push_frame(void *data, int w, int h, uint8_t *Y, uint8_t *U, uint8_t *V );
void	viewport_reconfigure(void *vv);
int	viewport_get_mode(void *vv);
int     viewport_reconfigure_from_config(void *vv, void *vc);
void	viewport_set_composite(void *vc, int mode, int colormode);
int	viewport_get_color_mode_from_config(void *vc);
int	viewport_get_composite_mode_from_config(void *vc);
void	*viewport_get_configuration(void *vv );
int	viewport_finetune_coord(void *data, int screen_width, int screen_height,int inc_x,int inc_y);
void	viewport_set_initial_active( void *vv, int status );
int	viewport_get_initial_active( void *vv );
char 	*viewport_get_my_help(void *vv);
int	viewport_finetune_coord(void *data, int screen_width, int screen_height,int inc_x, int inc_y);
void    viewport_save_settings( void *v, int frontback );

#endif

