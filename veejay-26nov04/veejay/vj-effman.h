/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
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

#ifndef VJ_EFFMAN_H
#define VJ_EFFMAN_H

#include "vj-effect.h"

enum {
    BLEND_ADDITIVE = 0,
    BLEND_SUBSTRACTIVE = 1,
    BLEND_MULTIPLY = 2,
    BLEND_DIVIDE = 3,
    BLEND_LIGHTEN = 4,
    BLEND_HARDLIGHT = 5,
    BLEND_DIFFERENCE = 6,
    BLEND_DIFFNEGATE = 7,
    BLEND_EXCLUSIVE = 8,
    BLEND_BASECOLOR = 9,
    BLEND_FREEZE = 10,
    BLEND_UNFREEZE = 11,
    BLEND_MULSUB = 12,
    BLEND_SOFTBURN = 13,
    BLEND_INVERSEBURN = 14,
    BLEND_COLORDODGE = 15,
    BLEND_ADDDISTORT = 16,
    BLEND_SUBDISTORT = 17,
};


typedef struct {
    int clip_id;
    int depth;			/* 0 = dont follow underlying clips (skip effects on chain) */
    int delay;
    int value;
    int tmp[10];
    int is_tag;
} vj_clip_instr;





typedef struct {
    long current_row;
    int column_nr;
} vj_pattern_instr;





int vj_effman_apply_first(vj_clip_instr * todo_info,
			  VJFrame **frames, VJFrameInfo *frameinfo, int e, int c, int n_frame);

int vj_effman_get_subformat(vj_clip_instr * todo_info, int effect_id);
#endif
