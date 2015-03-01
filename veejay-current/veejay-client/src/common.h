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
#ifndef GVRCOMMON_H
#define GVRCOMMON_H
#define STATUS_BYTES 	150
#define STATUS_TOKENS 	26 //@ previous 23
#define VEEJAY_CODENAME VERSION
/* Status bytes */

#define ELAPSED_TIME	0
#define PLAY_MODE	2
#define CURRENT_ID	3
#define SAMPLE_FX	4
#define SAMPLE_START	5
#define SAMPLE_END	6
#define SAMPLE_SPEED	7
#define SAMPLE_LOOP	8
#define SAMPLE_MARKER_START   13
#define STREAM_TYPE	13
#define SAMPLE_MARKER_END     14
#define FRAME_NUM	1
#define TOTAL_FRAMES	6
#define TOTAL_SLOTS	16
#define TOTAL_MEM	17
#define CURRENT_FPS     18
#define CYCLE_LO	19
#define CYCLE_HI	20
#define SEQ_ACT		21
#define SEQ_CUR		22
#define CHAIN_FADE      23
#define FRAME_DUP	24
#define	MACRO		25
#define CURRENT_ENTRY	15
#define	MODE_PLAIN	2
#define MODE_SAMPLE	0
#define MODE_PATTERN    3
#define MODE_STREAM	1
#define STREAM_COL_R	5
#define STREAM_COL_G	6
#define STREAM_COL_B	7

#define STREAM_RECORDED  11
#define STREAM_DURATION  10
#define STREAM_RECORDING 9

#define MAX_UI_PARAMETERS  16

#endif
