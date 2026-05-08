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

void curve_set_predifined_shape(GtkWidget *curve, int fx_id, int parameter_id,
                                int start, int end, int shape,
                                int bound_min, int bound_max,
                                int steps, gboolean reverse)
{
    int param_min = 0;
    int param_max = 0;

    _effect_get_minmax(fx_id, &param_min, &param_max, parameter_id);

    int i, k;
    float ry = 0.0f;
    float dy = 0.0f;

    if(end - start <= 1)
        return;

    if(steps < 1)
        steps = 1;

    if(bound_min > bound_max) {
        int tmp = bound_min;
        bound_min = bound_max;
        bound_max = tmp;
    }

    int param_diff = param_max - param_min;

    int shape_min = ((param_diff / 100.0f) * bound_min) + param_min;
    int shape_max = ((param_diff / 100.0f) * bound_max) + param_min;
    int shape_diff = shape_max - shape_min;

    int veclen1 = end - start;

    if(veclen1 <= 0)
        return;

    float *vec = (float*) vj_calloc(sizeof(float) * veclen1);
    if(vec == NULL)
        return;

    int veclen = veclen1 - 1;

    if(shape_diff == 0) {
        for(i = 0; i < veclen1; i++)
            vec[i] = shape_min;

        gtk3_curve_reset(curve);

        gtk3_curve_set_range(curve,
                             (gfloat) start,
                             (gfloat) end,
                             (gfloat) param_min,
                             (gfloat) param_max);

        gtk3_curve_set_grid_resolution(curve, 16);
        gtk3_curve_set_vector(curve, veclen1, vec);
        gtk3_curve_set_curve_type(curve, GTK3_CURVE_TYPE_LINEAR);
        gtk_widget_queue_draw(curve);

        curve_is_empty = 0;

        free(vec);
        return;
    }

    switch(shape)
    {
        case FX_ANIM_SHAPE_ZIGZAG:
            dy = shape_diff / (float)(veclen1 - 1);
            dy = dy * ((float)(steps << 1));
            break;

        case FX_ANIM_SHAPE_SINE:
        case FX_ANIM_SHAPE_COSINE:
        case FX_ANIM_SHAPE_SAWTOOTH:
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
        case FX_ANIM_SHAPE_PULSE:
        case FX_ANIM_SHAPE_DAMPED_SINE:
        case FX_ANIM_SHAPE_SMOOTH_NOISE:
            break;

        default:
            dy = shape_diff / (float)(veclen1 - 1);
            break;
    }

    switch(shape)
    {
        case FX_ANIM_SHAPE_ZIGZAG:
            for(i = start, k = 0, ry = shape_min; i < end; i++, ry += dy, k++)
            {
                vec[k] = ry;

                if(dy > 0.0f) {
                    if((ry + dy) > shape_max) {
                        ry = shape_max + dy;
                        dy = -dy;
                    }
                } else {
                    if((ry + dy) < shape_min) {
                        ry = shape_min + dy;
                        dy = -dy;
                    }
                }
            }
            break;

        case FX_ANIM_SHAPE_SINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                vec[k] = midpoint + radius * sinf(2.0f * M_PI * frequency * progress);
            }
        }
        break;

        case FX_ANIM_SHAPE_COSINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                vec[k] = midpoint + radius * cosf(2.0f * M_PI * frequency * progress);
            }
        }
        break;

        case FX_ANIM_SHAPE_SAWTOOTH:
        {
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                vec[k] = shape_min + shape_diff * t;
            }
        }
        break;

        case FX_ANIM_SHAPE_SQUARE:
        {
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = progress * frequency;
                int phase = (int)(t * 2.0f);
                vec[k] = (phase % 2 == 0) ? shape_max : shape_min;
            }
        }
        break;

        case FX_ANIM_SHAPE_BOUNCE:
        {
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float b = fabsf(1.0f - 2.0f * t);
                float bounce = 1.0f - (b * b);
                vec[k] = shape_min + shape_diff * bounce;
            }
        }
        break;

        case FX_ANIM_SHAPE_NOISE:
            for(i = start, k = 0; i < end; i++, k++) {
                float random_factor = (float) rand() / (float) RAND_MAX;
                vec[k] = shape_min + (shape_diff * random_factor);
            }
            break;

        case FX_ANIM_SHAPE_SMOOTHSTEP:
        {
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float local_progress = fmodf(progress * frequency, 1.0f);
                float smooth_factor = local_progress * local_progress *
                                      (3.0f - 2.0f * local_progress);
                vec[k] = shape_min + (shape_diff * smooth_factor);
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;

            for(i = start, k = 0; i < end; i++, k++) {
                float step = ((float) rand() / RAND_MAX) - 0.5f;
                value += step * step_scale;

                if(value < shape_min) value = shape_min + (shape_min - value);
                if(value > shape_max) value = shape_max - (value - shape_max);

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_INERTIA:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float velocity = 0.0f;
            float accel_scale = shape_diff * 0.02f;
            float damping = 0.90f;

            for(i = start, k = 0; i < end; i++, k++) {
                float accel = (((float) rand() / RAND_MAX) - 0.5f) * accel_scale;

                velocity += accel;
                velocity *= damping;
                value += velocity;

                if(value < shape_min) {
                    value = shape_min;
                    velocity *= -0.5f;
                }

                if(value > shape_max) {
                    value = shape_max;
                    velocity *= -0.5f;
                }

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_MEAN:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float mean = value;
            float step_scale = shape_diff * 0.04f;
            float pull = 0.05f;

            for(i = start, k = 0; i < end; i++, k++) {
                float noise = (((float) rand() / RAND_MAX) - 0.5f) * step_scale;
                value += noise + (mean - value) * pull;

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;
            int levels = 8;

            for(i = start, k = 0; i < end; i++, k++) {
                float step = (((float) rand() / RAND_MAX) - 0.5f) * step_scale;
                value += step;

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                float norm = (value - shape_min) / (float) shape_diff;
                norm = floorf(norm * levels) / (float)(levels - 1);

                value = shape_min + norm * shape_diff;

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_BURST:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float small_step = shape_diff * 0.01f;
            float big_step = shape_diff * 0.8f;
            int desired_bursts = steps > 0 ? steps : 3;
            float burst_prob = (float) desired_bursts / (float) veclen;

            for(i = start, k = 0; i < end; i++, k++) {
                float r = (float) rand() / RAND_MAX;
                float step;

                if(r < burst_prob)
                    step = (((float) rand() / RAND_MAX) - 0.5f) * big_step;
                else
                    step = (((float) rand() / RAND_MAX) - 0.5f) * small_step;

                value += step;

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_SMOOTH:
        {
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;
            float smooth = 0.85f;

            for(i = start, k = 0; i < end; i++, k++) {
                float step = (((float) rand() / RAND_MAX) - 0.5f) * step_scale;
                float target = value + step;

                value = value * smooth + target * (1.0f - smooth);

                if(value < shape_min) value = shape_min;
                if(value > shape_max) value = shape_max;

                vec[k] = value;
            }
        }
        break;

        case FX_ANIM_SHAPE_GAUSSIAN:
        {
            float frequency = (float) steps;
            float sigma = 0.25f;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float x = (t - 0.5f) / sigma;
                float g = expf(-0.5f * x * x);
                float g0 = expf(-0.5f * (0.5f / sigma) * (0.5f / sigma));
                float normalized = (g - g0) / (1.0f - g0);

                vec[k] = shape_min + shape_diff * normalized;
            }
        }
        break;

        case FX_ANIM_SHAPE_EXPONENTIAL:
        {
            float frequency = (float) steps;
            float denom = expf(4.0f) - 1.0f;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float expv = (expf(4.0f * t) - 1.0f) / denom;
                vec[k] = shape_min + shape_diff * expv;
            }
        }
        break;

        case FX_ANIM_SHAPE_EASE_IN:
            for(i = start, k = 0; i < end; i++, k++) {
                float t = (float)(i - start) / (float) veclen;
                float v = t * t;
                vec[k] = shape_min + shape_diff * v;
            }
            break;

        case FX_ANIM_SHAPE_EASE_OUT:
            for(i = start, k = 0; i < end; i++, k++) {
                float t = (float)(i - start) / (float) veclen;
                float v = 1.0f - (1.0f - t) * (1.0f - t);
                vec[k] = shape_min + shape_diff * v;
            }
            break;

        case FX_ANIM_SHAPE_PULSE:
        {
            float frequency = (float) steps;
            float duty = 0.2f;

            for(i = start, k = 0; i < end; i++, k++) {
                float t = fmodf(((float)(i - start) / (float) veclen) * frequency, 1.0f);
                vec[k] = (t < duty) ? shape_max : shape_min;
            }
        }
        break;

        case FX_ANIM_SHAPE_DAMPED_SINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;
            float frequency = (float) steps;
            float damping = 3.0f;

            for(i = start, k = 0; i < end; i++, k++) {
                float t = (float)(i - start) / (float) veclen;
                float env = expf(-damping * t);
                float v = sinf(2.0f * M_PI * frequency * t);
                vec[k] = midpoint + radius * v * env;
            }
        }
        break;

        case FX_ANIM_SHAPE_SMOOTH_NOISE:
        {
            float last = (shape_min + shape_max) * 0.5f;

            for(i = start, k = 0; i < end; i++, k++) {
                float rnd = (float) rand() / (float) RAND_MAX;
                float target = shape_min + shape_diff * rnd;
                last = last * 0.85f + target * 0.15f;
                vec[k] = last;
            }
        }
        break;

        case FX_ANIM_SHAPE_STEPS:
        {
            int levels = steps > 1 ? steps : 4;

            for(i = start, k = 0; i < end; i++, k++) {
                float t = (float)(i - start) / (float) veclen;
                float q = floorf(t * levels) / (float)(levels - 1);
                vec[k] = shape_min + shape_diff * q;
            }
        }
        break;

        case FX_ANIM_SHAPE_RAMP_DROP:
        {
            float frequency = (float) steps;
            float denom = expf(4.0f) - 1.0f;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float v = (expf(4.0f * t) - 1.0f) / denom;
                vec[k] = shape_min + shape_diff * v;
            }
        }
        break;

        case FX_ANIM_SHAPE_BURST_ENVELOPE:
        {
            float frequency = (float) steps;

            for(i = start, k = 0; i < end; i++, k++) {
                float progress = (float)(i - start) / (float) veclen;
                float t = fmodf(progress * frequency, 1.0f);
                float v;

                if(t < 0.1f) {
                    v = t / 0.1f;
                } else {
                    float d = (t - 0.1f) / 0.9f;
                    v = expf(-5.0f * d);
                }

                vec[k] = shape_min + shape_diff * v;
            }
        }
        break;

        default:
            for(i = start, k = 0, ry = shape_min; i < end; i++, ry += dy, k++) {
                vec[k] = ry;

                if(vec[k] < shape_min) vec[k] = shape_min;
                if(vec[k] > shape_max) vec[k] = shape_max;
            }
            break;
    }

    if(reverse) {
        for(i = start, k = 0; i < end; i++, k++) {
            vec[k] = shape_max - vec[k] + shape_min;
        }
    }

    int curve_type = GTK3_CURVE_TYPE_FREE;

    if(is_button_toggled("curve_typespline")) {
        curve_type = GTK3_CURVE_TYPE_SPLINE;
    } else if(is_button_toggled("curve_typefreehand")) {
        curve_type = GTK3_CURVE_TYPE_FREE;
    } else if(is_button_toggled("curve_typelinear")) {
        curve_type = GTK3_CURVE_TYPE_LINEAR;
    }

    gtk3_curve_reset(curve);

    gtk3_curve_set_range(curve,
                         (gfloat) start,
                         (gfloat) end,
                         (gfloat) param_min,
                         (gfloat) param_max);

    gtk3_curve_set_grid_resolution(curve, 16);

    gtk3_curve_set_vector(curve, veclen1, vec);
    gtk3_curve_set_curve_type(curve, curve_type);

    gtk_widget_queue_draw(curve);

    curve_is_empty = 0;

    free(vec);
}