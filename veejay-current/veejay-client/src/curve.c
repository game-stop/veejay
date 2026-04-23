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
#include <gtk/gtk.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <src/vj-api.h>
#include <stdlib.h>
#include "curve.h"
#include <math.h>

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

    if( len <= 0 ) return;

    float	*vec = (float*) vj_calloc(sizeof(float) * len );
    if(vec == NULL ) return;

	int diff = max - min;
	for(i = start ; i < end; i ++ ) // initial curve, stepsize is widget internal
	{
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

int	set_points_in_curve_ext( GtkWidget *curve, unsigned char *blob, int id, int fx_entry,int *curve_type, int *status)
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
		int value = ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );
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

	free(vec);

    curve_is_empty = 0;

	return parameter_id;
}

void curve_set_position( GtkWidget *curve, double pos)
{
    gtk3_curve_set_position( curve, pos);
}
void curve_set_predifined_animation( GtkWidget *curve, int fx_id, int parameter_id,
                                      int start, int end, int animation, int amplitude, int steps, gboolean reverse)
{
    int min=0, max=0;
    _effect_get_minmax(fx_id, &min, &max, parameter_id );
    int veclen1 = -1;
    int i,k;
    float j, rx, ry, dx, dy, min_x, delta_x;

    int diff = max - min;
    // only FX parameter value is normalized

    // veclen is the same as sample length (we dont have a envelope window .. yet to support this on very large clips)
    // every frame index position has a "parameter keyframe"

    // use start/end position in fx anim to define a region

    if (end - start <= 1) return;
    if (steps < 1) steps = 1;


    // TODO : break down this switch case and refactor
    //        feature: enable layering to combine shapes ( for example, ramp drop + random walk smooth or burst envelope + noise)
    switch(animation)
    {
        case FX_ANIM_SHAPE_ZAGZIG:
        case FX_ANIM_SHAPE_ZIGZAG:
            veclen1 = end - start;
            dy = (diff) / (float)(veclen1 - 1);
            dy = dy * ((float)(steps<<1));
            delta_x = ((end - start)/(float)steps);
        break;
        case FX_ANIM_SHAPE_SINE:
        case FX_ANIM_SHAPE_COSINE:
        case FX_ANIM_SHAPE_TRIANGLE:
        case FX_ANIM_SHAPE_SAWTOOTH:
        case FX_ANIM_SHAPE_REVERSE_SAWTOOTH:
        case FX_ANIM_SHAPE_SQUARE:
        case FX_ANIM_SHAPE_BOUNCE:
        case FX_ANIM_SHAPE_NOISE:
        case FX_ANIM_SHAPE_SMOOTHSTEP:
        case FX_ANIM_SHAPE_RANDOMWALK:
        case FX_ANIM_SHAPE_RANDOMWALK_INERTIA:
        case FX_ANIM_SHAPE_RANDOMWALK_MEAN:
        case FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED:
        case FX_ANIM_SHAPE_RANDOMWALK_BURST:
        case FX_ANIM_SHAPE_RANDOMWALK_SMOOTH:
        case FX_ANIM_SHAPE_GAUSSIAN:
        case FX_ANIM_SHAPE_EXPONENTIAL:
        case FX_ANIM_SHAPE_EASE_IN:
        case FX_ANIM_SHAPE_EASE_OUT:
        case FX_ANIM_SHAPE_STEPS:
        case FX_ANIM_SHAPE_BURST_ENVELOPE:
        case FX_ANIM_SHAPE_RAMP_DROP:
            veclen1 = end - start;
            break;
        default:
            veclen1 = end - start;
            dy = (diff) / (float)(veclen1 - 1);
            break;
    }

    if (veclen1 <= 0) return;

    float   *vec = (float*) vj_calloc(sizeof(float) * veclen1 );
    if (vec == NULL) return;

    int veclen = veclen1 - 1;

    switch(animation)
    {
        case FX_ANIM_SHAPE_ZIGZAG:
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
                    if ( ( ry + dy) < min)
                    {
                        ry = min-dy;
                        dy = -dy;
                    }
                }
                else
                {
                    if ( ( ry + dy) > max)
                    {
                        ry = max-dy;
                        dy = -dy;
                    }
                }
                k++;
            }
        break;
        case FX_ANIM_SHAPE_SINE:
        {
            float midpoint = (max + min) / 2.0f;
            float radius = (max - min) / 2.0f;
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                vec[k] = midpoint + radius * sinf(2.0f * M_PI * frequency * progress);
            }
        }
        break;
        case FX_ANIM_SHAPE_COSINE:
        {
            float midpoint = (max + min) / 2.0f;
            float radius = (max - min) / 2.0f;
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                vec[k] = midpoint + radius * cosf(2.0f * M_PI * frequency * progress);
            }
        }
        break;
        case FX_ANIM_SHAPE_TRIANGLE:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float tri = 1.0f - fabsf(2.0f * t - 1.0f);
                vec[k] = min + diff * tri;
            }
        }
        break;
        case FX_ANIM_SHAPE_SAWTOOTH:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                vec[k] = min + diff * t;
            }
        }
        break;
        case FX_ANIM_SHAPE_REVERSE_SAWTOOTH:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                vec[k] = max - diff * t;
            }
        }
        break;
        case FX_ANIM_SHAPE_SQUARE:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = progress * frequency;
                int phase = (int)(t * 2.0f);
                vec[k] = (phase % 2 == 0) ? max : min;
            }
        }
        break;
        case FX_ANIM_SHAPE_BOUNCE:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float b = fabsf(1.0f - 2.0f * t);
                float bounce = 1.0f - (b * b);
                vec[k] = min + diff * bounce;
            }
        }
        break;
        case FX_ANIM_SHAPE_NOISE:
        {
            for(i = start, k = 0; i < end; i++, k++)
            {
                float random_factor = (float)rand() / (float)RAND_MAX;
                vec[k] = min + (diff * random_factor);
            }
        }
        break;
        case FX_ANIM_SHAPE_SMOOTHSTEP:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float local_progress = fmodf(progress * frequency, 1.0f);
                float smooth_factor = local_progress * local_progress * (3.0f - 2.0f * local_progress);
                vec[k] = min + (diff * smooth_factor);
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK:
        {
            float value = (min + max) * 0.5f;
            float step_scale = diff * 0.05f;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float step = ((float)rand() / RAND_MAX) - 0.5f;
                value += step * step_scale;
                if (value < min) value = min + (min - value);
                if (value > max) value = max - (value - max);
                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK_INERTIA:
        {
            float value = (min + max) * 0.5f;
            float velocity = 0.0f;

            float accel_scale = diff * 0.02f;
            float damping = 0.90f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float accel = (((float)rand() / RAND_MAX) - 0.5f) * accel_scale;

                velocity += accel;
                velocity *= damping;

                value += velocity;

                if (value < min) {
                    value = min;
                    velocity *= -0.5f;
                }
                if (value > max) {
                    value = max;
                    velocity *= -0.5f;
                }

                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK_MEAN:
        {
            float value = (min + max) * 0.5f;
            float mean = value;

            float step_scale = diff * 0.04f;
            float pull = 0.05f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float noise = (((float)rand() / RAND_MAX) - 0.5f) * step_scale;
                value += noise + (mean - value) * pull;

                if (value < min) value = min;
                if (value > max) value = max;

                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED:
        {
            float value = (min + max) * 0.5f;
            float step_scale = diff * 0.05f;
            int levels = 8;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float step = (((float)rand() / RAND_MAX) - 0.5f) * step_scale;
                value += step;

                // quantize
                float norm = (value - min) / diff;
                norm = floorf(norm * levels) / (levels - 1);

                value = min + norm * diff;

                if (value < min) value = min;
                if (value > max) value = max;

                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK_BURST:
        {
            float value = (min + max) * 0.5f;
            float small_step = diff * 0.01f;
            float big_step   = diff * 0.8f;

            int desired_bursts = steps > 0 ? steps : 3;
            float burst_prob = (float)desired_bursts / (float)veclen;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float r = (float)rand() / RAND_MAX;

                float step;
                if (r < burst_prob)
                {
                    // symmetric burst (up + down)
                    step = (((float)rand() / RAND_MAX) - 0.5f) * big_step;
                }
                else
                {
                    step = (((float)rand() / RAND_MAX) - 0.5f) * small_step;
                }

                value += step;

                if (value < min) value = min;
                if (value > max) value = max;

                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_RANDOMWALK_SMOOTH:
        {
            float value = (min + max) * 0.5f;
            float step_scale = diff * 0.05f;
            float smooth = 0.85f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float step = (((float)rand() / RAND_MAX) - 0.5f) * step_scale;
                float target = value + step;

                value = value * smooth + target * (1.0f - smooth);

                if (value < min) value = min;
                if (value > max) value = max;

                vec[k] = value;
            }
        }
        break;
        case FX_ANIM_SHAPE_GAUSSIAN:
        {
            float frequency = (float)steps;
            float sigma = 0.25f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float x = (t - 0.5f) / sigma;
                float g = expf(-0.5f * x * x);
                float g0 = expf(-0.5f * (0.5f / sigma) * (0.5f / sigma));
                float normalized = (g - g0) / (1.0f - g0);
                vec[k] = min + diff * normalized;
            }
        }
        break;
        case FX_ANIM_SHAPE_EXPONENTIAL:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float expv = (expf(4.0f * t) - 1.0f) / (expf(4.0f) - 1.0f);
                vec[k] = min + diff * expv;
            }
        }
        break;
        case FX_ANIM_SHAPE_EASE_IN:
        {
            for(i = start, k = 0; i < end; i++, k++)
            {
                float t = (float)(i - start) / (float)veclen;
                float v = t * t; // quadratic ease-in
                vec[k] = min + diff * v;
            }
        }
        break;

        case FX_ANIM_SHAPE_EASE_OUT:
        {
            for(i = start, k = 0; i < end; i++, k++)
            {
                float t = (float)(i - start) / (float)veclen;
                float v = 1.0f - (1.0f - t) * (1.0f - t);
                vec[k] = min + diff * v;
            }
        }
        break;
        case FX_ANIM_SHAPE_PULSE:
        {
            float frequency = (float)steps;
            float duty = 0.2f; // 20% TODO: make it a parameter!

            for(i = start, k = 0; i < end; i++, k++)
            {
                float t = fmodf(((float)(i - start) / veclen) * frequency, 1.0f);
                vec[k] = (t < duty) ? max : min;
            }
        }
        break;
        case FX_ANIM_SHAPE_DAMPED_SINE:
        {
            float midpoint = (max + min) * 0.5f;
            float radius = (max - min) * 0.5f;
            float frequency = (float)steps;
            float damping = 3.0f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float t = (float)(i - start) / (float)veclen;
                float env = expf(-damping * t);
                float v = sinf(2.0f * M_PI * frequency * t);
                vec[k] = midpoint + radius * v * env;
            }
        }
        break;
        case FX_ANIM_SHAPE_SMOOTH_NOISE:
        {
            float last = (min + max) * 0.5f;

            for(i = start, k = 0; i < end; i++, k++)
            {
                float rnd = ((float)rand() / RAND_MAX);
                float target = min + diff * rnd;
                last = last * 0.85f + target * 0.15f; // low-pass filter
                vec[k] = last;
            }
        }
        break;
        case FX_ANIM_SHAPE_STEPS:
            {
                int levels = steps > 1 ? steps : 4;

                for(i = start, k = 0; i < end; i++, k++)
                {
                    float t = (float)(i - start) / (float)veclen;
                    float q = floorf(t * levels) / (levels - 1);
                    vec[k] = min + diff * q;
                }
            }
        break;
        case FX_ANIM_SHAPE_RAMP_DROP:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float v = (expf(4.0f * t) - 1.0f) / (expf(4.0f) - 1.0f);
                vec[k] = min + diff * v;
            }
        }
        break;
        case FX_ANIM_SHAPE_BURST_ENVELOPE:
        {
            float frequency = (float)steps;
            for(i = start, k = 0; i < end; i++, k++)
            {
                float progress = (float)(i - start) / (float)veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float v;
                if (t < 0.1f)
                {
                    // fast attack
                    v = t / 0.1f;
                }
                else
                {
                    // exponential decay
                    float d = (t - 0.1f) / 0.9f;
                    v = expf(-5.0f * d);
                }
                vec[k] = min + diff * v;
            }
        }
        break;
        default: break;
    }

    int curve_type = GTK3_CURVE_TYPE_FREE;

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
