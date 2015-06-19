/*
 * Copyright (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
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

#define FX_LIMIT	1024

#define MAX_EFFECTS 140
#define PARAM_WIDTH	    (1<<0x2)
#define PARAM_HEIGHT	(1<<0x3)
#define PARAM_FADER  	(1<<0x1)


typedef struct
{
	int type;
	int tmp[10];
	int ref;
} vjp_kf;   


typedef struct VJFrame_t 
{
	uint8_t *data[4];
	int	uv_len;
	int	len;
	int	uv_width;
	int	uv_height;
	int	shift_v;
	int	shift_h;
	int	format;
	int 	width;
	int	height;
	int	ssm;
	int	alpha;
	int	stride[4];
	int	stand; //ccir/jpeg
	float	fps;
	double	timecode;
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

    char *description;			
    int num_params;			
    char **param_description;	
    int *defaults;			
    int *flags;		
    int *limits[2];		
    int extra_frame;		
    int sub_format;		
    int has_user;		
    int static_bg;
    int has_help;
    int rgb_conv;
    int n_out;
    int instance;
    void *user_data;		
    char padding[4];
    int parallel;
} vj_effect;

typedef struct vj_effect_instance_t {
	uint8_t *buffer[4];
	int	 len;
	int	 iparams[16];
	float	 fparams[8];
	int	*iarr;
	float	*farr;
} vj_fx_instance;

extern int	get_pixel_range_min_Y();
extern int	get_pixel_range_min_UV();
extern void vj_effect_initialize(int width, int height, int range);
extern void vj_effect_shutdown();
extern int vj_effect_max_effects();
extern int vj_effect_real_to_sequence(int effect_id);
extern int vj_effect_get_real_id( int entry_num );
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
extern int vj_effect_has_rgbkey(int effect_id);
extern int vj_effect_is_valid(int effect_id);
extern int vj_effect_get_summary(int entry, char *dst);
extern int vj_effect_get_summary_len(int entry);
extern void *vj_effect_activate(int e, int *retcode);
extern int vj_effect_deactivate(int e, void *ptr);
extern int vj_effect_initialized(int e, void *ptr);
extern int vj_effect_get_min_i();
extern int vj_effect_get_max_i();
extern int vj_effect_get_min_v();
extern int vj_effect_get_max_v();
extern int vj_effect_get_by_name(char *name);
extern int	vj_effect_apply( VJFrame **frames, VJFrameInfo *frameinfo, vjp_kf *kf, int selector, int *arguments, void *ptr);
extern int	vj_effect_prepare( VJFrame *frame, int selector);
extern	void	vj_effect_dump(void);
extern int	rgb_parameter_conversion_type_;
extern	int	vj_effect_is_plugin( int fx_id );
extern void	*vj_effect_get_data( int seq_id );
extern int vj_effect_is_parallel(int effect_id);
#endif
