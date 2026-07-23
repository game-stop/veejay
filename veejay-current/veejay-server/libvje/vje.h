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
#define VJ_PLUGIN 500

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

typedef enum {
    VJ_BEAT_OFF = 0,

    VJ_BEAT_TRIGGER,
    VJ_BEAT_FLOW,
    VJ_BEAT_DRIFT,
    VJ_BEAT_WARP,
    VJ_BEAT_MOTION_REACT,

    VJ_BEAT_GEOMETRY_AMPLITUDE,
    VJ_BEAT_GEOMETRY_FREQUENCY,
    VJ_BEAT_GEOMETRY_PHASE,
    VJ_BEAT_GRID_SIZE,
    VJ_BEAT_WINDOW_RADIUS,

    VJ_BEAT_SPEED,
    VJ_BEAT_SIGNED_SPEED,
    VJ_BEAT_SIGNED_CURVE,

    VJ_BEAT_MEMORY,
    VJ_BEAT_INERTIA,
    VJ_BEAT_SOURCE_MIX,

    VJ_BEAT_COLOR_AMOUNT,
    VJ_BEAT_COLOR_PHASE,
    VJ_BEAT_DETAIL,
    VJ_BEAT_GLOW,

    VJ_BEAT_SELECTOR,
    VJ_BEAT_RESET,
    VJ_BEAT_ALPHA_OR_OPACITY,

    VJ_BEAT_TRAIL_LENGTH,
    VJ_BEAT_DENSITY,
    VJ_BEAT_CONTRAST,
    VJ_BEAT_INTENSITY,
    VJ_BEAT_TURBULENCE,

    VJ_BEAT_KICK,
    VJ_BEAT_SNARE,
    VJ_BEAT_HAT,

    VJ_BEAT_LAST = VJ_BEAT_HAT

} vj_beat_param_class_t;

#define VJ_BEAT_HINT_VERSION_1 1
#define VJ_BEAT_HINT_VERSION_2 2

typedef enum {
    VJ_BEAT_SRC_NONE = 0,
    VJ_BEAT_SRC_LEVEL,
    VJ_BEAT_SRC_ENVELOPE,
    VJ_BEAT_SRC_ACTIVITY,
    VJ_BEAT_SRC_ONSET,
    VJ_BEAT_SRC_TRANSIENT,
    VJ_BEAT_SRC_FLUX,
    VJ_BEAT_SRC_LOW_ACTIVITY,
    VJ_BEAT_SRC_MID_ACTIVITY,
    VJ_BEAT_SRC_HIGH_ACTIVITY,
    VJ_BEAT_SRC_LOW_ONSET,
    VJ_BEAT_SRC_MID_ONSET,
    VJ_BEAT_SRC_HIGH_ONSET,
    VJ_BEAT_SRC_BAND_BALANCE,
    VJ_BEAT_SRC_CANDIDATE_PULSE,
    VJ_BEAT_SRC_BEAT_PULSE,
    VJ_BEAT_SRC_BEAT_GATE,
    VJ_BEAT_SRC_BEAT_TOGGLE,
    VJ_BEAT_SRC_BEAT_PHASE,
    VJ_BEAT_SRC_KICK_PULSE,
    VJ_BEAT_SRC_SNARE_PULSE,
    VJ_BEAT_SRC_HAT_PULSE,
    VJ_BEAT_SRC_SCRATCH_ACTIVITY,
    VJ_BEAT_SRC_SCRATCH_VELOCITY,
    VJ_BEAT_SRC_SCRATCH_SIGNED,
    VJ_BEAT_SRC_BPM,
    VJ_BEAT_SRC_GROOVE,
    VJ_BEAT_SRC_PHRASE,
    VJ_BEAT_SRC_CLIMAX,
    VJ_BEAT_SRC_SCRATCH_BURST,
    VJ_BEAT_SRC_LAST = VJ_BEAT_SRC_SCRATCH_BURST
} vj_beat_source_t;

typedef enum {
    VJ_BEAT_OP_NONE = 0,
    VJ_BEAT_OP_MAP_RANGE,
    VJ_BEAT_OP_OFFSET_BASE,
    VJ_BEAT_OP_SCALE_BASE,
    VJ_BEAT_OP_IMPULSE,
    VJ_BEAT_OP_GATE,
    VJ_BEAT_OP_TOGGLE,
    VJ_BEAT_OP_WRAP,
    VJ_BEAT_OP_RATE,
    VJ_BEAT_OP_SAMPLE_HOLD,
    VJ_BEAT_OP_BEAT_TIME,
    VJ_BEAT_OP_LAST = VJ_BEAT_OP_BEAT_TIME
} vj_beat_operator_t;

typedef enum {
    VJ_BEAT_POLARITY_POSITIVE = 0,
    VJ_BEAT_POLARITY_NEGATIVE,
    VJ_BEAT_POLARITY_BIPOLAR,
    VJ_BEAT_POLARITY_ALTERNATE,
    VJ_BEAT_POLARITY_SOURCE_SIGN,
    VJ_BEAT_POLARITY_LAST = VJ_BEAT_POLARITY_SOURCE_SIGN
} vj_beat_polarity_t;

typedef enum {
    VJ_BEAT_CURVE_LINEAR = 0,
    VJ_BEAT_CURVE_SMOOTHSTEP,
    VJ_BEAT_CURVE_EASE_IN,
    VJ_BEAT_CURVE_EASE_OUT,
    VJ_BEAT_CURVE_SQUARE,
    VJ_BEAT_CURVE_SQRT,
    VJ_BEAT_CURVE_LOG,
    VJ_BEAT_CURVE_PUNCH,
    VJ_BEAT_CURVE_LAST = VJ_BEAT_CURVE_PUNCH
} vj_beat_curve_t;

typedef enum {
    VJ_BEAT_COST_CHEAP = 0,
    VJ_BEAT_COST_MODERATE,
    VJ_BEAT_COST_EXPENSIVE,
    VJ_BEAT_COST_STRUCTURAL,
    VJ_BEAT_COST_LAST = VJ_BEAT_COST_STRUCTURAL
} vj_beat_cost_t;

typedef enum {
    VJ_BEAT_GROUP_NONE = 0,
    VJ_BEAT_GROUP_ASCENDING,
    VJ_BEAT_GROUP_DESCENDING,
    VJ_BEAT_GROUP_SUM_LIMIT,
    VJ_BEAT_GROUP_MUTEX,
    VJ_BEAT_GROUP_LAST = VJ_BEAT_GROUP_MUTEX
} vj_beat_group_relation_t;

#define VJ_BEAT_F_REJECT          (1u << 0)
#define VJ_BEAT_F_CONTINUOUS      (1u << 1)
#define VJ_BEAT_F_DISCRETE        (1u << 2)
#define VJ_BEAT_F_STRUCTURAL      (1u << 3)
#define VJ_BEAT_F_PHRASE_ONLY     (1u << 4)
#define VJ_BEAT_F_CLIMAX_ONLY     (1u << 5)
#define VJ_BEAT_F_IMPULSE         (1u << 6)
#define VJ_BEAT_F_SIGN_LOCK       (1u << 7)
#define VJ_BEAT_F_WRAP            (1u << 8)
#define VJ_BEAT_F_LOG             (1u << 9)
#define VJ_BEAT_F_SQUARED         (1u << 10)
#define VJ_BEAT_F_REBUILDS_STATE  (1u << 11)
#define VJ_BEAT_F_NO_ZERO_CROSS   (1u << 12)
#define VJ_BEAT_F_INVERTED        (1u << 13)

typedef struct {
    int version;
    int klass;
    uint32_t flags;

    int source;
    int operator_type;
    int polarity;
    int curve;

    int soft_min;
    int soft_max;

    int normal_depth_pct;
    int climax_depth_pct;

    int attack_ms;
    int release_ms;
    int hold_ms;

    int step;
    int update_ms;
    int cost;
    int priority;

    int group_id;
    int group_order;
    int group_relation;
    int group_margin;
} vj_beat_param_hint_t;

#define VJ_BEAT_HINT_V2(_klass,_flags,_source,_operator,_polarity,_curve,_soft_min,_soft_max,_normal,_climax,_attack,_release,_hold,_step,_update,_cost,_priority,_group_id,_group_order,_group_relation,_group_margin) \
    { VJ_BEAT_HINT_VERSION_2, (_klass), (uint32_t)(_flags), (_source), (_operator), (_polarity), (_curve), (_soft_min), (_soft_max), (_normal), (_climax), (_attack), (_release), (_hold), (_step), (_update), (_cost), (_priority), (_group_id), (_group_order), (_group_relation), (_group_margin) }

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
	vj_beat_param_hint_t *beat_hints;
	int (*is_transition_ready_func)(void *ptr, int w, int h); //FIXME pass in private data
    int (*prepare)(void *fx_instance, VJFrame *frame);
    int (*apply)(void *fx_instance, VJFrame *frame, int *parameter_values);
    int (*apply2)(void *fx_instance, VJFrame *frame, VJFrame *frame2, int *parameter_values);
    void (*instantiate)(VJFrame *frame);
    int (*destroy)(void *fx_instance);
} vj_effect;

#endif
