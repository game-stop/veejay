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

#define STATUS_LENGTH	1024
#define VEEJAY_CODENAME VERSION

#define ELAPSED_TIME	0
#define PLAY_MODE	2
#define CURRENT_ID	3
#define SAMPLE_FX	4
#define SAMPLE_START	5
#define SAMPLE_END	6
#define SAMPLE_SPEED	7
#define SAMPLE_LOOP	8
#define SAMPLE_COUNT 12
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
#define SUBRENDER	26
#define FADE_METHOD	27
#define FADE_ENTRY 28
#define FADE_ALPHA 29
#define SAMPLE_LOOP_STAT	30
#define SAMPLE_LOOP_STAT_STOP	31
#define SAMPLE_INV_COUNT	36
#define FEEDBACK 35
#define GLOBAL_CHAIN 37
#define MESSAGE_FORWARDING 38
#define AUDIO_BEAT_ENABLED      39
#define AUDIO_BEAT_OPEN         40
#define AUDIO_BEAT_LEVEL        41  /* 0..100 */
#define AUDIO_BEAT_TRANSIENT    42  /* 0..100 */
#define AUDIO_BEAT_HITS         43

#define AUDIO_BEAT_ENVELOPE     44  /* 0..100 */
#define AUDIO_BEAT_FLUX         45  /* 0..100 */
#define AUDIO_BEAT_BASS         46  /* 0..100 */
#define AUDIO_BEAT_MID          47  /* 0..100 */
#define AUDIO_BEAT_HIGH         48  /* 0..100 */
#define AUDIO_BEAT_PULSE        49  /* 0..100 */
#define AUDIO_BEAT_GATE         50  /* 0..100 */
#define AUDIO_BEAT_BPM_X10      51  /* BPM * 10 */
#define AUDIO_BEAT_AGE_MS       52
#define AUDIO_BEAT_SAMPLE_RATE  53
#define AUDIO_BEAT_HIT_SEQ      54
#define AUDIO_MUTED             55
#define RECORD_AUDIO_SOURCE     56

#define AUDIO_SYNC_ENABLED       57
#define AUDIO_SYNC_OPEN          58
#define AUDIO_SYNC_RUNNING       59
#define AUDIO_SYNC_MODE          60
#define AUDIO_SYNC_SOURCE        61
#define AUDIO_SYNC_CHANNELS      62
#define AUDIO_SYNC_SAMPLE_RATE   63
#define AUDIO_SYNC_LEVEL_PCT     64
#define AUDIO_SYNC_TRANSIENT_PCT 65
#define AUDIO_SYNC_BPM_X10       66
#define AUDIO_SYNC_PHASE_PCT     67
#define AUDIO_SYNC_CONFIDENCE    68
#define AUDIO_SYNC_BRIDGE_ACTIVE 69
#define AUDIO_SYNC_RATIO_X1000   70
#define AUDIO_SYNC_CORRECTION    71  /* actual applied bridge pull, x100; 100 == 1.00 */
#define AUDIO_SYNC_TARGET_MODE   72  /* 0 manual, 1 current clip */
#define AUDIO_SYNC_TARGET_BPM_X10 73 /* target BPM * 10 */
#define AUDIO_SYNC_TARGET_CONFIDENCE 74 /* 0..100 */
#define AUDIO_SYNC_MAX_CORRECTION 75 /* configured max correction %, 0..25 */
#define AUDIO_SYNC_BRIDGE_STATE  76  /* 0 idle, 1 wait source, 2 wait target, 3 locked, 4 hold, 5 fallback */
#define AUDIO_SYNC_TRACK_ALIGN_LOCKED         77 /* 0/1 */
#define AUDIO_SYNC_TRACK_ALIGN_OFFSET_MS      78 /* signed ms: positive means video/reference is late */
#define AUDIO_SYNC_TRACK_ALIGN_CONFIDENCE     79 /* 0..100 */
#define AUDIO_SYNC_TRACK_ALIGN_CORRECTION_PPM 80 /* signed ppm video-rate trim */
#define AUDIO_SYNC_TRACK_ALIGN_STATE          81 /* 0 idle, 1 wait source, 2 wait target, 3 searching, 4 locked, 5 hold, 6 fallback */

#define SAMPLE_TRANSITION_ACTIVE  32
#define SAMPLE_TRANSITION_LENGTH  33
#define SAMPLE_TRANSITION_SHAPE  34
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
#define STREAM_AVF 12

#define __MAX_TRACKS 16

/* One spare slot keeps older loops that use <= highest-index style safe. */
#define STATUS_ARRAY_SIZE (VIMS_STATUS_TOKENS+1)
#define HISTORY_PLAYMODES 4

#endif
