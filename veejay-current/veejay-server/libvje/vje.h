/*
 * Copyright (C) 2002-2015 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJE_H
#define VJE_H

#define TRANSITION_RUNNING 1
#define TRANSITION_COMPLETED 2
#define TRANSITION_NONE 0

#define FX_LIMIT	1024

#define PARAM_WIDTH	    (1<<0x2)
#define PARAM_HEIGHT	(1<<0x3)
#define PARAM_FADER  	(1<<0x1)

typedef struct
{
	int type;
	int tmp[10];
	int ref;
} vjp_kf;   

typedef struct {
	char **description;
	int limit;
} vj_value_hint_t;

#define FLAG_ALPHA_NONE (1 << 0) /* no alpha */
#define FLAG_ALPHA_SRC_A (1 << 1) /* alpha-in A */
#define FLAG_ALPHA_SRC_B (1 << 2) /* alpha-in B */
#define FLAG_ALPHA_OUT (1 << 3) /* writes alpha */
#define FLAG_ALPHA_OPTIONAL (1<<4) /* parameter driven */
#define FLAG_ALPHA_IN_OPERATOR (1<<5) /* logical operator */
#define FLAG_ALPHA_IN_BLEND (1<<6) /* blend operator */

typedef int (*is_transition_ready_func)(void *ptr,int width, int height);

typedef struct vj_effect_t {
	char *description;			
	int num_params;			
	char **param_description;
	vj_value_hint_t **hints;
	int *defaults;			
	int *limits[2];		
	int extra_frame;		
	int sub_format;		
	int has_user;		
	int static_bg;
	int has_help;
	int rgb_conv;
	int n_out;
	int instance;
	int parallel;
	int rgba_only;
	int motion;
	int alpha;
	int global;
	int is_gen;
	int is_plugin;
	int (*is_transition_ready_func)(void *ptr, int w, int h); //FIXME pass in private data
    int (*prepare)(void *fx_instance, VJFrame *frame);
    int (*apply)(void *fx_instance, VJFrame *frame, int *parameter_values);
    int (*apply2)(void *fx_instance, VJFrame *frame, VJFrame *frame2, int *parameter_values);
    void (*instantiate)(VJFrame *frame);
    int (*destroy)(void *fx_instance);
} vj_effect;

#endif
