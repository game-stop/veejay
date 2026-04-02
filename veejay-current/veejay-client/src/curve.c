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

//ISSUE : change sample bound start end nothing happen?
//ISSUE : memory issue allocating GTK3curve / priv->curve_data.d_point width devrait etre  largeur du clip!???
//ISSUE : ZIGZag not well shaped
//ISSUE : are we force to init gtk3curve / set curve vector with full vector witdh or what?
//NEED TO reset before setting new curve ?


#include <config.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <src/vj-api.h>
#include <stdlib.h>
#include "curve.h"

static int curve_is_empty = 1;

int     is_curve_empty(void) {
    return curve_is_empty;
}

void	get_points_from_curve( GtkWidget *curve, int len, float *vec )
{
  gtk3_curve_get_vector( curve, len, vec );
}

void	reset_curve( GtkWidget *curve )
{
  gtk_widget_set_sensitive( curve, TRUE );
  if (!curve_is_empty)
  {
    gtk3_curve_reset( curve );
  }
  curve_is_empty = 0;
}

void	set_points_in_curve( Gtk3CurveType type, GtkWidget *curve)
{
  gtk3_curve_set_curve_type( curve , type );

  curve_is_empty = 0;
}


void   set_initial_curve( GtkWidget *curve, int fx_id, int parameter_id, int start, int end, int value )
{
    int min=0, max=0;
	_effect_get_minmax(fx_id, &min, &max, parameter_id );
    int len = end - start;
	int i,k=0;
    float	*vec = (float*) vj_calloc(sizeof(float) * len ); // FIXME less values len/step?

	int diff = max - min;
	for(i = start ; i < end; i ++ ) //FIXME less values ? i+=step
	{
		//~ float val = ((float)(value - min) / (diff)); # BYPASS [0-1] NORMALISATION
		//~ vec[k] = val;  # BYPASS [0-1] NORMALISATION
		vec[k] = value;
		k++;
	}

    gtk3_curve_set_range( curve,  (gfloat) start, (gfloat) end, (gfloat) min, (gfloat) max );
    gtk3_curve_set_grid_resolution(curve, 16); // default grid resolution
    gtk3_curve_set_vector( curve , len, vec );
    gtk3_curve_set_curve_type( curve, GTK3_CURVE_TYPE_LINEAR );


    free(vec);

    curve_is_empty = 0;
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
    if(vec == NULL) {
        return -1;
    }

	int diff = max - min;
	for(i = start ; i < end; i ++ )
	{
		unsigned char *ptr = in + (k * 4);
		int value =
		  ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );

		// val = ((Input - InputLow) / (InputHigh - InputLow)) * (OutputHigh - OutputLow) + OutputLow;
		// with OutputLow==0 and OutputHigh==1 in gtkcurve range

		//~ float val = ((float)(value - min) / (diff)); // # BYPASS [0-1] NORMALISATION
		//~ vec[k] = val;
		vec[k] = (float)value;
		k++;
	}
    gtk3_curve_reset( curve );
    gtk3_curve_set_range( curve, (gfloat) start, (gfloat) end, (gfloat) min, (gfloat) max );
    gtk3_curve_set_grid_resolution( curve, 16 ); // default grid resolution

    gtk3_curve_set_vector( curve , len, vec );

	switch( type ) {
		case 1: *curve_type = GTK3_CURVE_TYPE_SPLINE; break;
		case 2: *curve_type = GTK3_CURVE_TYPE_FREE; break;
		default: *curve_type = GTK3_CURVE_TYPE_LINEAR; break;
	}

    gtk3_curve_set_curve_type( curve, *curve_type );

	*lo = start; //FIXME , why affected ?
	*hi = end;

	free(vec);

    curve_is_empty = 0;

	return parameter_id;
}

void curve_set_position( GtkWidget *curve, double pos)
{
    gtk3_curve_set_position( curve, pos);
}

void curve_set_predifined_animation( GtkWidget *curve, int fx_id, int parameter_id,
                                      int start, int end, int animation, int amplitude, int steps)
{
    int min=0, max=0;
    _effect_get_minmax(fx_id, &min, &max, parameter_id );
    int veclen = -1;
    int i,k;
    float j, rx, ry, dx, dy, min_x, delta_x, complement;

    int diff = max - min;

    if (end - start <= 1) return; // FIXME (guard again div0)
    if (steps < 1) steps = 1; //(guard again div0)
    amplitude = 100; //FIXME missing ui ?
    complement = 100 - amplitude;

    switch(animation)
    {
        case FX_ANIM_SHAPE_ZAGZIG:
        case FX_ANIM_SHAPE_ZIGZAG:
            //~ veclen = steps; //only needed point in vector, need to fix gtk3curve?
            veclen = end - start;
            dy = (diff) / (float)(veclen - 1);
            dy = dy * ((float)(steps<<1)); //
            delta_x = ((end - start)/(float)steps);
            break;

        default:
            veclen = end - start;
            dy = (diff) / (float)(veclen - 1);
            break;
    }

    float   *vec = (float*) vj_calloc(sizeof(float) * veclen );

    switch(animation)
    {
        case FX_ANIM_SHAPE_UP:
            for(i = start, k = 0, ry = min; i <= end; i ++ , ry+=dy)
            {
                vec[k] = ry;
                k++;
            }

        break;
        case FX_ANIM_SHAPE_DOWN:
            for(i = start, k = 0, ry = max; i <= end; i ++ , ry-=dy)
            {
                vec[k] = ry;
                k++;
            }
        break;
        //~ case FX_ANIM_SHAPE_MONTAIN:
            //~ for(i = start, k = 0, ry = min; i < end/2; i ++ , ry+=2*dy)
            //~ {
                //~ vec[k] = ry;
                //~ vec[end-k] = ry;
                //~ k++;
            //~ }
            //~ if (k != end-k) //fill last points (in the middle) if end is odd
            //~ {
                //~ vec[k] = max;
                //~ vec[k+1] = max;
            //~ }
        //~ break;
        //~ case FX_ANIM_SHAPE_VALLEY:
            //~ for(i = start, k = 0, ry = max; i < end/2; i ++ , ry-=2*dy)
            //~ {
                //~ vec[k] = ry;
                //~ vec[end-k] = ry;
                //~ k++;
            //~ }
            //~ if (k != end-k) //fill last points (in the middle)
            //~ {
                //~ vec[k] = min;
                //~ vec[k+1] = min;
            //~ }
        //~ break;
        case FX_ANIM_SHAPE_ZIGZAG: //NAIVE Implement. Could be nested loop to fill all redondant values once
            for(i = start, k = 0, ry = min; i < end; i++, ry+=dy)
            {
                vec[k] = ry;
                if (dy > 0)
                {
                    if ( (ry + dy) > max)
                    {
                        ry = max+dy;
                        dy = -dy;
                    }
                }
                else
                {
                    if ( (ry + dy) < min)
                    {
                        ry = min+dy;
                        dy = -dy;
                    }
                }

                k++;
            }

        break;
        case FX_ANIM_SHAPE_ZAGZIG:
            dy = -dy;
            for(i = start, k = 0, ry = max; i < end; i++, ry+=dy)
            {
                vec[k] = ry;
                if (dy < 0)
                {
                    if ( ( ry - dy) < min)
                    {
                        ry = min-dy;
                        dy = -dy;
                    }
                }
                else
                {
                    if ( ( ry - dy) > max)
                    {
                        ry = max-dy;
                        dy = -dy;
                    }
                }

                k++;
            }

        break;
        default: break;
    }

    int curve_type = GTK3_CURVE_TYPE_FREE;
    //~ curve type is force to free ( in callback.c - update_curve_shape()) until gtk3curvewidget point limit is fixed (issue # )
    if( is_button_toggled("curve_typespline")) {
        curve_type = GTK3_CURVE_TYPE_SPLINE;
    } else if ( is_button_toggled("curve_typefreehand")) {
        curve_type = GTK3_CURVE_TYPE_FREE;
    } else if (is_button_toggled("curve_typelinear")) {
        curve_type = GTK3_CURVE_TYPE_LINEAR;
    }

    gtk3_curve_set_vector( curve , veclen, vec );
    gtk3_curve_set_curve_type( curve, curve_type );

    curve_is_empty = 0;
    free(vec);
}
