/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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

#ifndef V_SPLITSCREEN_H
#define V_SPLITSCREEN_H


void *vj_split_new_from_file(char *filename, int w, int h, int fmt);
void vj_split_render( void *ptr );
void vj_split_process( void *ptr, VJFrame *src );
int vj_split_auto_configure_screen( void *ptr );
int	vj_split_add_screen( void *ptr,char *hostname, int port, int row, int col, int out_w, int out_h, int fmt );
int vj_split_configure_screen( void *ptr, int screen_id, int edge_x, int edge_y, int left, int right, int top, int bottom, int w, int h );
void vj_split_free( void *ptr );
void *vj_split_init(int r, int c );
void vj_split_set_master(int port);
VJFrame *vj_split_get_screen(void *ptr, int screen_id);
#endif
