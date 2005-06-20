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
#include <config.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif

void 	vj_event_fmt_arg			(	int *args, 	char *str, 	const char format[], 	va_list ap);
void 	vj_event_init				();
void	vj_event_print_range			(	int n1,		int n2);
int	veejay_finish_action_file(void *ptr, char *filename );
int	veejay_load_action_file( void *ptr, char *filename );
#ifdef HAVE_SDL
#ifdef HAVE_XML2
void    vj_event_xml_new_keyb_event		( 	void *v,	xmlDocPtr doc, 	xmlNodePtr cur );
#endif
#endif
int vj_event_get_video_format(void);
int vj_event_get_num_args(int net_id);
void vj_event_update_remote(void *ptr);
void	vj_event_dump(void);
void	vj_event_set_stream_color		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_chain_arg_inc			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_arg_set			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_disable			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_chain_enable			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_entry_audio_toggle	(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_chain_entry_audio_vol_inc	(	void *ptr, 	const char format[], 	va_list ap	); 
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
void	vj_event_el_add_video_sample		(	void *ptr,	const char format[],	va_list ap	);
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
void 	vj_event_sample_clear_all			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_copy			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_del			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_end			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_sample_his_del_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_his_entry_to_new		(	void *ptr, 	const char format[],	va_list ap	);
void	vj_event_sample_his_play_entry		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_sample_his_lock_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_his_render_entry		(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_sample_his_set_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_his_unlock_entry		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_load_list			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_sample_rec_start			( 	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_sample_move_render		(	void *ptr,	const char format[],	va_list	ap );
void 	vj_event_sample_ren_start			( 	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_sample_sel_render		(	void *ptr,	const char format[],	va_list ap  );

void 	vj_event_sample_rec_stop			(	void *ptr, 	const char format[], 	va_list ap	); 
#ifdef HAVE_XML2
void 	vj_event_sample_save_list			(	void *ptr, 	const char format[], 	va_list ap	); 
#endif
void	vj_event_sample_select			(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_sample_set_descr			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_set_end			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_sample_set_freeze_play		(	void *ptr, 	const char format[],	va_list ap	);
void 	vj_event_sample_set_loop_type		(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_set_speed			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_sample_set_nl			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_set_no			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_sample_set_num_loops		(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_sample_set_pp			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_set_start			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_sample_start			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_sample_set_marker_start		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_sample_set_marker_end		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_sample_set_marker_clear		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_sample_set_marker		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_set_frame			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_set_play_mode			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_set_play_mode_go		(	void *ptr,	const char format[], 	va_list ap	); 
void	vj_event_switch_sample_tag		(	void *ptr,	const char format[],	va_list ap	);
#ifdef HAVE_SDL
void 	vj_event_set_screen_size		(	void *ptr, 	const char format[], 	va_list ap	);
#endif
void	vj_event_sample_set_dup			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_tag_del			(	void *ptr, 	const char format[], 	va_list ap	); 
void 	vj_event_tag_new_raw			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_tag_new_avformat		(	void *ptr,	const char format[],	va_list ap	);
#ifdef USE_GDK_PIXBUF
void	vj_event_tag_new_picture		(	void *ptr,	const char format[],	va_list ap	);
#endif
void 	vj_event_tag_new_v4l			(	void *ptr, 	const char format[], 	va_list ap	);
#ifdef SUPPORT_READ_DV2
void	vj_event_tag_new_dv1394			( 	void *ptr,	const char format[],	va_list ap	);
#endif
void	vj_event_tag_new_color			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_tag_new_y4m			(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_tag_new_net			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_new_mcast			(	void *ptr,	const char format[], 	va_list ap	);
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
void	vj_event_send_sample_list			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_log			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_chain_list		( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_chain_entry		( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_sample_history_list		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_send_video_information		( 	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_send_editlist			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_devices			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_frame			(	void *ptr,	const char fomrat[],	va_list ap 	);
void	vj_event_send_effect_list		(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_sample_new			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_do_bundled_msg			(	void *ptr, 	const char format[], 	va_list ap	);
void 	vj_event_bundled_msg_del		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_bundled_msg_add		(	void *ptr, 	const char format[], 	va_list ap	);
void	vj_event_read_file			(	void *ptr,	const char format[],	va_list ap	);
#ifdef HAVE_SDL
void	vj_event_attach_detach_key		(	void *ptr,	const char format[],	va_list ap	);
#endif
void	vj_event_write_actionfile		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_screenshot			(	void *ptr,	const char format[],	va_list ap	);
void 	vj_event_sample_chain_enable		(	void *ptr,	const char format[],	va_list ap 	);
void	vj_event_sample_chain_disable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_chain_enable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_brightness		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_contrast		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_color			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_hue			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_v4l_set_white			(	void *ptr,	const char format[], 	va_list ap	);
void	vj_event_v4l_get_info			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_manual_chain_fade(void *ptr, const char format[], va_list ap);
void	vj_event_tag_chain_disable		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_all_samples_chain_toggle		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_sample_rel_start			(	void *ptr, 	const char format[], 	va_list ap	);
void    vj_event_effect_set_bg			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_quit				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_suspend			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_set_format			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_set_volume			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_tag_new_shm			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_debug_level			( 	void *ptr,	const char format[],	va_list ap	);
void	vj_event_bezerk				(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_plugin_command			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_load_plugin			(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_unload_plugin			(	void *ptr,	const char format[], 	va_list ap	);
void	vj_event_fullscreen			( void *ptr, const char format[],	va_list ap );
void	vj_event_sample_mode		(	void *ptr,	const char format[],	va_list ap 	);
void	vj_event_set_rgb_parameter_type		(	void *ptr,const char format[],	va_list ap	);
void	vj_event_tag_set_descr		(	void *ptr,	const char format[],	va_list ap	);
void	vj_event_send_vimslist		(	void *ptr,	const char format[],	va_list ap	);	
void	vj_event_send_bundles	(void *ptr, const char format[], va_list ap);

void	vj_event_mcast_start(	void *ptr,	const char format[], va_list ap);

void	vj_event_mcast_stop( void *ptr,		const char format[], va_list ap);
#ifdef HAVE_V4L
void	vj_event_vloopback_start		(	void *ptr,	const char format[], 	va_list ap	);
void	vj_event_vloopback_stop			(	void *ptr,	const char format[],	va_list ap	);
#endif
void	vj_event_quick_bundle  (void *ptr, const char format[], va_list ap);

void	vj_event_sample_rand_start(	void *ptr, const char format[], va_list ap);
void	vj_event_sample_rand_stop(	void *ptr, const char format[], va_list ap);

#endif
