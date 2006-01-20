/*
 * Copyright (C) 2002-2004 Niels Elburg <nelburg@looze.net>
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
#include <config.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <libvjmem/vjmem.h>

#ifdef USE_SWSCALER
#define MAX_EFFECTS 117
#else
#define MAX_EFFECTS 116
#endif
#define PARAM_WIDTH	    (1<<0x2)
#define PARAM_HEIGHT	(1<<0x3)
#define PARAM_FADER  	(1<<0x1)

// keyframe-able parameter sets
typedef struct
{
	int type;
	int tmp[10];
	int ref;
} vjp_kf;   


typedef struct VJFrame_t 
{
	uint8_t *data[3];
	int	uv_len;
	int	len;
	int	uv_width;
	int	uv_height;
	int	shift_v;
	int	shift_h;
	int	format;
	int 	width;
	int	height;
} VJFrame;

typedef struct VJRectangle_t
{
	int top;
	int bottom;
	int left;
	int right;
} VJRectangle;

typedef struct VJFrameInfo_t
{
	int width;
	int height;
	float fps;
	int64_t timecode;
	uint8_t inverse;
} VJFrameInfo;



typedef struct vj_effect_t {

    char *description;			// name of effect
    int num_params;				// num parameters (optionals dont exist)

    char **param_description;	// unused.
    int *defaults;			
	int *flags;					// parameter flags
    int *limits[2];				// [0] = min, [1] = max

    int extra_frame;			// effect requires a secundary frame
    int sub_format;				// if set to '1', data must be in 4:4:4 !!!
    int has_user;				// has private data?
    int static_bg;				// unused
    int has_help;				// unused
    int rgb_conv;			// temporary fix for color effects
    void *user_data;			// private effect data
} vj_effect;

// initialize library
extern void vj_effect_initialize(int width, int height);
extern void vj_effect_shutdown();

// convert effect number to internal num
extern int vj_effect_real_to_sequence(int effect_id);
extern int vj_effect_get_real_id( int entry_num );

// get effect information                 
extern char *vj_effect_get_description(int effect_id);
extern char *vj_effect_get_param_description(int effect_id, int param_nr);
extern int vj_effect_get_extra_frame(int effect_id);
extern int vj_effect_get_num_params(int effect_id);
extern int vj_effect_get_default(int effect_id, int param_nr);
extern int vj_effect_get_min_limit(int effect_id, int param_nr);
extern int vj_effect_get_max_limit(int effect_id, int param_nr);
extern int vj_effect_valid_value(int effect_id, int param_nr, int value);
extern int vj_effect_get_subformat(int effect_id);
extern int vj_effect_has_cb(int effect_id);
// if effect has rgbkey its always p1,p2,p3 (r,g,b)
extern int vj_effect_has_rgbkey(int effect_id);
extern int vj_effect_is_valid(int effect_id);
extern int vj_effect_get_summary(int entry, char *dst);

// activate an effect 
extern int vj_effect_activate(int e);
extern int vj_effect_deactivate(int e);
extern int vj_effect_initialized(int e);

extern int vj_effect_get_min_i();
extern int vj_effect_get_max_i();
extern int vj_effect_get_min_v();
extern int vj_effect_get_max_v();

extern int vj_effect_get_by_name(char *name);

extern int	vj_effect_apply( VJFrame **frames, VJFrameInfo *frameinfo, vjp_kf *kf, int selector, int *arguments );
extern int	vj_effect_prepare( VJFrame *frame, VJFrameInfo *frameinfo, int selector);

extern	void	vj_effect_dump(void);

// see veejay/vj-perform 'apply_first' for an example on how to use this.

extern int	rgb_parameter_conversion_type_;

#endif
