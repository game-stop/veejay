/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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


#ifndef VIMS_H
#define VIMS_H

/*
 * VIMS selectors
 */

enum {
	VIMS_BUNDLE_START	=	500,
	VIMS_BUNDLE_END		=	599,	
	VIMS_MAX		=	702,
};

enum {
/* query information */
	VIMS_EFFECT_LIST			=	401,
	VIMS_EDITLIST_LIST			=	402,
	VIMS_BUNDLE_LIST			=	403,
	VIMS_STREAM_DEVICES			=	406,
	VIMS_SAMPLE_LIST			=	408,
	VIMS_CHAIN_GET_ENTRY			=	410,
	VIMS_VIMS_LIST				=	411,
	VIMS_LOG				=	412,
	VIMS_SAMPLE_INFO			=	413,
/* general controls */
	VIMS_SET_VOLUME				=	300,
	VIMS_FULLSCREEN				=	301,
	VIMS_QUIT				=	600,
	VIMS_RECORD_DATAFORMAT			=	302,
	VIMS_AUDIO_SET_ACTIVE			=	306,
	VIMS_REC_AUTO_START			=	320,	
	VIMS_REC_STOP				=	321,
	VIMS_REC_START				=	322,
	VIMS_BEZERK				=	324,
	VIMS_DEBUG_LEVEL			=	325,
	VIMS_RESIZE_SCREEN			=	326,
	VIMS_SCREENSHOT				=	330,
	VIMS_RGB24_IMAGE			=	333,

/* video controls */
	VIMS_VIDEO_PLAY_FORWARD			=	10,
	VIMS_VIDEO_PLAY_BACKWARD		=	11,
	VIMS_VIDEO_PLAY_STOP			=	12,
	VIMS_VIDEO_SKIP_FRAME			=	13,
	VIMS_VIDEO_PREV_FRAME			=	14,
	VIMS_VIDEO_SKIP_SECOND			=	15,
	VIMS_VIDEO_PREV_SECOND			=	16,
	VIMS_VIDEO_GOTO_START			=	17,
	VIMS_VIDEO_GOTO_END			=	18,
	VIMS_VIDEO_SET_FRAME			=	19,
	VIMS_VIDEO_SET_SPEED			=	20,
//	VIMS_VIDEO_MCAST_START			=	22,
//	VIMS_VIDEO_MCAST_STOP			=	23,
		
/* editlist commands */

	VIMS_SAMPLE_NEW				=	100,
	VIMS_SAMPLE_SELECT			=	101,
	VIMS_SAMPLE_DEL				=	120,
	VIMS_SAMPLE_SET_PROPERTIES		=	103,
	VIMS_SAMPLE_LOAD_SAMPLELIST		=	125,
	VIMS_SAMPLE_SAVE_SAMPLELIST		=	126,
	VIMS_SAMPLE_DEL_ALL			=	121,
	VIMS_SAMPLE_COPY			=	127,
	VIMS_SAMPLE_REC_START			=	130,
	VIMS_SAMPLE_REC_STOP			=	131,
	VIMS_SAMPLE_CHAIN_ACTIVE		=	112,

	VIMS_EDITLIST_PASTE_AT			=	50,
	VIMS_EDITLIST_COPY			=	51,
	VIMS_EDITLIST_DEL			=	52,
	VIMS_EDITLIST_CROP			=	53,
	VIMS_EDITLIST_CUT			=	54,
	VIMS_EDITLIST_ADD			=	55,
	VIMS_EDITLIST_ADD_SAMPLE		=	56,
	VIMS_EDITLIST_SAVE			=	58,
	VIMS_EDITLIST_LOAD			=	59,

	VIMS_CHAIN_SET_ACTIVE			=	353,
	VIMS_CHAIN_CLEAR			=	355,
	VIMS_CHAIN_LIST				=	358,
	VIMS_CHAIN_SET_ENTRY			=	359,
	VIMS_CHAIN_ENTRY_SET_FX			=	360,
	VIMS_CHAIN_ENTRY_SET_PRESET		=	361,
	VIMS_CHAIN_ENTRY_SET_ACTIVE		=	363,
	VIMS_CHAIN_ENTRY_SET_VALUE		=	366,
	VIMS_CHAIN_ENTRY_SET_INPUT		=	367,
	VIMS_CHAIN_ENTRY_CLEAR			=	369,
	VIMS_CHAIN_ENTRY_SET_ALPHA		=	370,

	VIMS_CHAIN_ENTRY_SET_STATE		=	377,

	VIMS_GET_FRAME				=	42,

	VIMS_SET_PORT				=	650,
	VIMS_GET_PORT				=	649,
	
};
#endif
