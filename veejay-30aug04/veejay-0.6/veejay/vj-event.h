/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */

#ifndef VJ_EVENT_H
#define VJ_EVENT_H
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
enum {
	NET_RECORD_DATAFORMAT			=	10,
	NET_SET_PLAIN_MODE			= 	1,
	NET_INIT_GUI_SCREEN			=	11,
	NET_SWITCH_CLIP_TAG			=	13,
	NET_RESIZE_SDL_SCREEN			=	12,
	NET_SET_PLAY_MODE			=	2,
	NET_EFFECT_LIST				=	15,
	NET_EDITLIST_LIST			=	201,
	NET_SET_MODE_AND_GO			=	3,
	NET_EDITLIST_PASTE_AT			=	20,
	NET_EDITLIST_COPY			=	21,
	NET_EDITLIST_DEL			=	22,
	NET_EDITLIST_CROP			=	23,
	NET_EDITLIST_CUT			=	24,
	NET_EDITLIST_ADD			=	25,
	NET_EDITLIST_ADD_CLIP			=	26,
	NET_EDITLIST_SAVE			=	28,
	NET_EDITLIST_LOAD			=	29,
	NET_BUNDLE				=	50,
	NET_DEL_BUNDLE				=	51,
	NET_ADD_BUNDLE				=	52,
	NET_BUNDLE_ATTACH_KEY			=	53,
	NET_SCREENSHOT				=	54,
	NET_BUNDLE_FILE				=	55,
	NET_BUNDLE_SAVE				=	56,
	NET_VIDEO_INFORMATION			=	202,
	NET_AUDIO_ENABLE			=	4,
	NET_AUDIO_DISABLE			=	5,
	NET_VIDEO_PLAY_FORWARD			=	80,
	NET_VIDEO_PLAY_BACKWARD			=	81,
	NET_VIDEO_PLAY_STOP			=	82,
	NET_VIDEO_SKIP_FRAME			=	83,
	NET_VIDEO_PREV_FRAME			=	84,
	NET_VIDEO_SKIP_SECOND			=	85,
	NET_VIDEO_PREV_SECOND			=	86,
	NET_VIDEO_GOTO_START			=	87,
	NET_VIDEO_GOTO_END			=	88,
	NET_VIDEO_SET_FRAME			=	89,
	NET_VIDEO_SET_SPEED			=	90,
	NET_VIDEO_SET_SLOW			=	91,
	NET_SET_CLIP_START			=	92,
	NET_SET_CLIP_END			=	93,
	NET_CLIP_NEW				=	99,
	NET_CLIP_SELECT				=	100,
	NET_CLIP_DEL				=	101,
	NET_CLIP_SET_LOOPTYPE			=	102,
	NET_CLIP_SET_DESCRIPTION		=	103,
	NET_CLIP_SET_SPEED			=	104,
	NET_CLIP_SET_START			=	105,
	NET_CLIP_SET_END			=	106,
	NET_CLIP_SET_DUP			=	107,
	NET_CLIP_SET_FREEZE			=	108,		
	NET_CLIP_SET_FREEZE_MODE		=	109,
	NET_CLIP_SET_FREEZE_PFRAMES		=	110,
	NET_CLIP_SET_FREEZE_NFRAMES		=	111,
	NET_CLIP_SET_AUDIO_VOL			=	112,
	NET_CLIP_SET_MARKER_START		=	113,
	NET_CLIP_SET_MARKER_END			= 	114,
	NET_CLIP_SET_MARKER			=	115,
	NET_CLIP_CLEAR_MARKER			=	116,
	NET_CLIP_CLEAR_FREEZE			=	117,
	NET_CLIP_HISTORY_SET_ENTRY		=	118,
	NET_CLIP_HISTORY_ENTRY_AS_NEW		=	119,
	NET_CLIP_HISTORY_CLEAR_ENTRY		=	120,
	NET_CLIP_HISTORY_LOCK_ENTRY		=	121,
	NET_CLIP_HISTORY_UNLOCK_ENTRY		=	122,
	NET_CLIP_HISTORY_PLAY_ENTRY		=	123,
	NET_CLIP_LOAD_CLIPLIST			=	124,
	NET_CLIP_SAVE_CLIPLIST			=	125,
	NET_CLIP_HISTORY_LIST			=	126,
	NET_CLIP_DEL_ALL			=	135,
	NET_CLIP_COPY				=	139,
	NET_CLIP_LIST				=	205,
	NET_SET_MARKER_START			=	127,
	NET_SET_MARKER_END			= 	128,
	NET_CLIP_REC_START			=	130,
	NET_CLIP_REC_STOP			=	131,
	NET_CLIP_CHAIN_ENABLE			=	132,
	NET_CLIP_CHAIN_DISABLE			=	133,
        NET_CLIP_ADD_WAVE			=	136,
	NET_CLIP_DEL_WAVE			=	137,
	NET_CLIP_UPDATE				=	134,
	NET_TAG_SELECT				=	140,
	NET_TAG_ACTIVATE			=	141,
	NET_TAG_DEACTIVATE			=	142,
	NET_TAG_DELETE				=	143,
	NET_TAG_NEW_V4L				=	144,
	NET_TAG_NEW_VLOOP_BY_NAME		=	145,
	NET_TAG_NEW_VLOOP_BY_ID			=	146,
	NET_TAG_NEW_Y4M				=	147,
	NET_TAG_NEW_RAW				=	148,
	NET_TAG_OFFLINE_REC_START		=	150,
	NET_TAG_OFFLINE_REC_STOP		=	151,
	NET_TAG_REC_START			=	152,
	NET_TAG_REC_STOP			=	153,
	NET_TAG_LOAD_TAGLIST			=	154,
	NET_TAG_SAVE_TAGLIST			=	155,
	NET_TAG_LIST				=	206,
	NET_TAG_DEVICES				=	207,
	NET_TAG_CHAIN_ENABLE			=	156,
	NET_TAG_CHAIN_DISABLE			=	157,
	NET_TAG_SET_BRIGHTNESS			=	160,
	NET_TAG_SET_CONTRAST			=	161,
	NET_TAG_SET_HUE				=	162,
	NET_TAG_SET_COLOR			=	163,
	NET_CHAIN_CHANNEL_INC			=	170,
	NET_CHAIN_CHANNEL_DEC			=	171,
	NET_CHAIN_GET_ENTRY			=	208,
	NET_CHAIN_TOGGLE_ALL			=	172,
	NET_CHAIN_COPY_TO_BUF			=	173,
	NET_CHAIN_PASTE_AS_NEW			=	174,
	NET_CHAIN_ENABLE			=	175,
	NET_CHAIN_DISABLE			=	176,
	NET_CHAIN_CLEAR				=	177,
	NET_CHAIN_FADE_IN			=	178,
	NET_CHAIN_FADE_OUT			=	179,
	NET_CHAIN_LIST				=	209,
	NET_CHAIN_SET_ENTRY			=	180,
	NET_CHAIN_ENTRY_SET_EFFECT		=	181,
	NET_CHAIN_ENTRY_SET_PRESET		=	182,
	NET_CHAIN_ENTRY_SET_ARG_VAL		=	183,
	NET_CHAIN_ENTRY_SET_VIDEO_ON		=	184,
	NET_CHAIN_ENTRY_SET_VIDEO_OFF		=	185,
	NET_CHAIN_ENTRY_SET_AUDIO_ON		=	186,
	NET_CHAIN_ENTRY_SET_AUDIO_OFF		=	187,
	NET_CHAIN_ENTRY_SET_AUDIO_VOL		=	188,
	NET_CHAIN_ENTRY_SET_DEFAULTS		=	189,
	NET_CHAIN_ENTRY_SET_CHANNEL		=	190,
	NET_CHAIN_ENTRY_SET_SOURCE		=	191,
	NET_CHAIN_ENTRY_SET_SOURCE_CHANNEL 	=	192,
	NET_CHAIN_ENTRY_CLEAR			=	193,
	NET_CHAIN_ENTRY_SET_AUTOMATIC		=	194,
	NET_CHAIN_ENTRY_DEL_AUTOMATIC		=	195,
	NET_CHAIN_ENTRY_ENABLE_AUTOMATIC	=	196,
	NET_CHAIN_ENTRY_DISABLE_AUTOMATIC	=	197,
	NET_EFFECT_SET_BG			=	200,
	NET_OUTPUT_VLOOPBACK_START		=	70,
	NET_SET_VOLUME				=	220,
	NET_OUTPUT_Y4M_START			=	71,
	NET_OUTPUT_Y4M_STOP			=	72,
	NET_OUTPUT_RAW_START			=	73,
	NET_OUTPUT_RAW_STOP			=	74,
	NET_OUTPUT_VLOOPBACK_STARTN		=	75,
	NET_OUTPUT_VLOOPBACK_STOP		=	76,
	NET_SAMPLE_MODE			=	250,
	NET_BEZERK				=	251,
	NET_DEBUG_LEVEL				=	252,
	NET_SHM_OPEN				=	253,
	NET_SUSPEND				=	254,
	NET_QUIT				=	255,
};

void 	vj_event_fmt_arg			(	int *args, 	char *str, 	const char format[], 	va_list ap);
void 	vj_event_init				();
void	vj_event_print_range			(	int n1,		int n2);
void    vj_event_xml_new_keyb_event		( 	xmlDocPtr doc, 	xmlNodePtr cur );

int vj_event_get_num_args(int net_id);

void 	vj_event_chain_arg_inc			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_arg_set			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_disable			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_enable			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_entry_audio_toggle	(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_entry_audio_vol_inc	(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_entry_audio_volume	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_channel		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_channel_inc	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_del		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_inc		(	void *ptr,	const char format[], 	va_list ap	);
void	vj_event_chain_entry_channel_dec	(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_entry_preset		(	void *ptr,	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_select		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_chain_entry_set_arg_val	(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_entry_set		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_chain_entry_src_toggle		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_entry_source		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_srccha		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_video_toggle	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_disable_audio	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_enable_audio	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_disable_video	(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_entry_enable_video	(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_chain_entry_set_defaults	(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_fade_in			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_fade_out			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_toggle			(	void *ptr,	const char format[],	va_list ap	); 
void	vj_event_chain_clear			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_dec_frame			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_effect_add			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_effect_dec			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_effect_inc			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_copy			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_crop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_el_cut				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_del				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_el_paste_at			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_el_load_editlist		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_save_editlist		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_add_video_his		(	void *ptr,	const char format[],	va_list ap	);	
void	vj_event_el_add_video_clip		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_el_add_video			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_entry_down			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_entry_up			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_goto_end			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_goto_start			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_inc_frame			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_misc_start_rec			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_misc_start_rec_auto		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_misc_stop_rec			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_next_second			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_none				(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_output_vloopback_start		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_output_vloopback_startn	(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_output_vloopback_stop		(	void *ptr, 	const char format[], 	va_list sp	);
void 	vj_event_output_y4m_start		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_output_y4m_stop		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_output_raw_start		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_output_raw_stop		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_play_forward			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_play_reverse			(	void *ptr,	const char format[], 	va_list ap	); 
void 	vj_event_play_speed			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_play_slow			( 	void *ptr,	const char format[], 	va_list ap	);
void 	vj_event_play_stop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_prev_second			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_clear_all			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_copy			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_del			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_end			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_clip_his_del_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_his_entry_to_new		(	void *ptr, 	const char format[],	va_list ap	);
void	vj_event_clip_his_play_entry		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_clip_his_lock_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_his_render_entry		(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_clip_his_set_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_his_unlock_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_load_list			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_clip_rec_start			( 	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_rec_stop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_clip_save_list			(	void *ptr, 	const char format[], 	va_list ap	); 
void	vj_event_clip_select			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_clip_set_descr			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_set_end			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_clip_set_freeze_play		(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_clip_set_loop_type		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_set_speed			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_clip_set_nl			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_set_no			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_clip_set_num_loops		(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_clip_set_pp			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_set_start			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_clip_start			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_clip_set_marker_start		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_clip_set_marker_end		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_clip_set_marker_clear		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_clip_set_marker		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_set_frame			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_set_play_mode			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_set_play_mode_go		(	void *ptr,	const char format[], 	va_list ap	); 
void	vj_event_switch_clip_tag		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_set_screen_size		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_clip_set_dup			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_tag_del			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_tag_new_raw			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_new_v4l			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_new_vloopback_name		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_tag_new_vloopback_id		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_tag_new_y4m			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_rec_offline_start		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_rec_offline_stop		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_rec_start			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_rec_stop			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_tag_select			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_toggle			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_select_id			(	void *ptr,	const char format[], 	va_list ap	);
void 	vj_event_select_bank			( 	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_enable_audio			( 	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_disable_audio			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_print_info			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_tag_list			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_clip_list			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_chain_list		( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_chain_entry		( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_clip_history_list		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_send_video_information		( 	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_send_editlist			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_devices			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_effect_list		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_clip_new			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_do_bundled_msg			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_bundled_msg_del		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_clip_del_wave			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_clip_add_wave			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_bundled_msg_add		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_read_file			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_attach_key_to_bundle		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_write_actionfile		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_screenshot			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_clip_chain_enable		(	void *ptr,	const char format[],	va_list ap 	);
void	vj_event_clip_chain_disable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_chain_enable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_brightness		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_contrast		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_color			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_hue			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_chain_entry_efk_set		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_chain_entry_efk_del		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_chain_entry_efk_enable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_chain_entry_efk_disable	(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_chain_disable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_all_clips_chain_toggle		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_clip_rel_start			(	void *ptr, 	const char format[], 	va_list ap	);
void    vj_event_effect_set_bg			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_quit				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_suspend			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_set_format			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_init_gui_screen		( 	void *ptr,	const char format[],	va_list ap 	);
void	vj_event_set_volume			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_new_shm			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_debug_level			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_bezerk				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_sample_mode		(	void *ptr,	const char format[],	va_list ap 	);
#endif
