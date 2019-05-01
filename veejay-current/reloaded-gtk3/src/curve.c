/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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

#include <config.h>
#include <glib.h>
#include <veejay/vj-msg.h>
#include <veejay/vjmem.h>
#include <src/vj-api.h>
#include <stdlib.h>
#include "curve.h"

void	get_points_from_curve( GtkWidget *curve, int len, float *vec )
{
  gtk3_curve_get_vector( curve, len, vec );
}

void	reset_curve( GtkWidget *curve )
{
  gtk_widget_set_sensitive( curve, TRUE );
  gtk3_curve_reset( curve );
  gtk3_curve_set_range( curve , 0.0, 1.0, 0.0, 1.0 );
}

void	set_points_in_curve( Gtk3CurveType type, GtkWidget *curve)
{
  gtk3_curve_set_curve_type( curve , type );
}


int	set_points_in_curve_ext( GtkWidget *curve, unsigned char *blob, int id, int fx_entry, int *lo, int *hi, int *curve_type, int *status)
{
	int parameter_id = 0;
	int start = 0, end =0,type=0;
	int entry  = 0;
	int n = sscanf( (char*) blob, "key%2d%2d%8d%8d%2d%2d", &entry, &parameter_id, &start, &end,&type,status );
	int len = end - start;
	int i;
	int min = 0, max = 0;

	if(n != 6 || len <= 0 )	
	{
		return -1;
	}

	_effect_get_minmax(id, &min, &max, parameter_id );

	unsigned int k = 0;
	unsigned char *in = blob + 27;
	float	*vec = (float*) vj_calloc(sizeof(float) * len );
	int diff = max - min;
	for(i = start ; i < end; i ++ )
	{
		unsigned char *ptr = in + (k * 4);
		int value = 
		  ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );

		// val = ((Input - InputLow) / (InputHigh - InputLow)) * (OutputHigh - OutputLow) + OutputLow;
		// with OutputLow==0 and OutputHigh==1 in gtkcurve range
		float val = ((float)(value - min) / (diff));

		vec[k] = val;
		k++;
	}
	
  gtk3_curve_set_vector( curve , len, vec );

	switch( type ) {
		case 1: *curve_type = GTK3_CURVE_TYPE_SPLINE; break;
		case 2: *curve_type = GTK3_CURVE_TYPE_FREE; break;
		default: *curve_type = GTK3_CURVE_TYPE_LINEAR; break;
	}

  gtk3_curve_set_curve_type( curve, *curve_type );

	*lo = start;
	*hi = end;

	free(vec);

	return parameter_id;
}
