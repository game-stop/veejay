/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg <nwelburg at gmail.com>
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

#ifndef VIEWPORTXML
#define VIEWPORTXML
typedef struct
{
	int	saved_w;
	int	saved_h;
	int	reverse;
	int	grid_resolution;
	int	grid_color;
	int	frontback;
	int	x0,y0,w0,h0;
	float x1;
	float x2;
	float x3;
	float x4;
	float y1;
	float y2;
	float y3;
	float y4;
	float	scale;
	int	composite_mode;
	int	colormode;
	int	grid_mode;
	int	marker_size;
	int	initial_active;
} viewport_config_t;

void 	viewport_save_xml(xmlNodePtr parent,void *vv);
void	*viewport_load_xml(xmlDocPtr doc, xmlNodePtr cur, void *vv );
extern void	composite_add_to_config( void *compiz, void *vc, int which_vp );
extern void	*composite_load_config( void *compiz, void *vc, int *result );

void	*composite_get_config(void *compiz, int which_vp );
#endif
