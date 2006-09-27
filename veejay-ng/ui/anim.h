/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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

#ifndef ANIM_H
#define ANIM_H

void	*anim_new( void *sender, char *osc_path, char *types );
void	anim_destroy( void *danim );
void	 anim_change_curve( void *danim, int type );
void	 anim_set_range( void *danim, double min_x, double max_x, 
			 double min_y, double max_y );
void	 anim_clear( void *danim );
void	 anim_update( void *danim );	
void	anim_bang( void *danim, double position );
char	*anim_get_path( void *danim);
GtkWidget	*anim_get( void *danim );

#endif 
