/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nelburg@looze.net> 
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
#define DBG_C() { 	vj_msg(VEEJAY_MSG_DEBUG, "Implement %s", __FUNCTION__ ); }  

void	on_button_085_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_SKIP_SECOND);
}
void	on_button_084_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PREV_FRAME );
}
void	on_button_080_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_FORWARD);
}
void	on_button_081_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_BACKWARD);
}
void	on_button_082_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PLAY_STOP );
}
void	on_button_083_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_SKIP_FRAME );
}
void 	on_button_086_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PREV_SECOND );
}
void	on_button_087_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_START );
}
void	on_button_088_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_END);
}
void	on_videobar_move_slider(GtkWidget *widget, gpointer user_data)
{
	DBG_C();
}

void	on_videobar_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gdouble scale = info->status_tokens[TOTAL_FRAMES] / 100.0;
		GtkWidget *w = glade_xml_get_widget(
					info->main_window, "videobar");
		gdouble slider_val = scale * GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (gint) slider_val );
			
		info->slider_lock = 0;
	}
}

void	on_audiovolume_value_changed(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *w = glade_xml_get_widget(info->main_window, "audiovolume");
	gdouble val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	multi_vims( VIMS_SET_VOLUME, "%d", (gint) val );
}

void	on_button_001_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SET_PLAIN_MODE );
}

void	on_button_252_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_DEBUG_LEVEL );
	if(is_button_toggled( "button_252" ))
		vims_verbosity = 1;
	else
		vims_verbosity = 0;
}

void	on_button_251_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BEZERK );
}	

void	on_button_054_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SCREENSHOT );
}
void	on_button_200_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_EFFECT_SET_BG ); 
}
void	on_button_5_4_clicked(GtkWidget *widget, gpointer user_data)
{
	if( is_button_toggled("button_5_4") )
		single_vims( VIMS_AUDIO_ENABLE );
	else
		single_vims( VIMS_AUDIO_DISABLE );	
}
void	on_button_samplestart_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[0] = info->status_tokens[FRAME_NUM];
}
void	on_button_sampleend_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[1] = info->status_tokens[FRAME_NUM];
	multi_vims( VIMS_CLIP_NEW, "%d %d", info->sample[0],info->sample[1]);
	if(info->status_tokens[PLAY_MODE] == MODE_PLAIN )
		info->uc.reload_hint[HINT_SLIST] = 1;
}

void	on_button_veejay_clicked(GtkWidget *widget, gpointer user_data)
{
	vj_fork_or_connect_veejay();
}
void	on_button_sendvims_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *text = get_text("vimsmessage");
	if(strncasecmp( text, "255:;",5 ) == 0)
	{
		if( prompt_dialog("Quit veejay", "Close Veejay ? All unsaved work will be lost.")
			== GTK_RESPONSE_REJECT)
			return;
	}
	msg_vims( text );
}
void	on_vimsmessage_activate(GtkWidget *widget, gpointer user_data)
{
	msg_vims( get_text( "vimsmessage") );
}
void	on_button_vimshelp_clicked(GtkWidget *widget, gpointer user_data)
{
	about_dialog();	
}

void	on_button_fadeout_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_FADE_OUT, "0 %d",
		(int)get_numd( "button_fadedur"));
}

void	on_button_fadein_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_FADE_IN, "0 %d",
		(int)get_numd( "button_fadedur"));
}

void	on_manualopacity_value_changed(GtkWidget *w, gpointer user_data)
{
	gdouble val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	multi_vims( VIMS_CHAIN_MANUAL_FADE, "0 %d",
		(int)(255.0 * val));
}

static void	el_selection_update()
{
	gchar *text = format_selection_time(info->selection[0], info->selection[1]);
	update_label_str( "label_el_selection", text );
	g_free(text);
}

void	on_button_el_selstart_value_changed(GtkWidget *w, gpointer user_data)
{
	info->selection[0] = get_nums("button_el_selstart");
	if( info->selection[0] > info->selection[1])
		update_spin_value( "button_el_selend", info->selection[0]);
	gchar *text = format_time( info->selection[0] );
	update_label_str( "label_el_startpos", text);
	g_free(text);
  	el_selection_update();
}

void	on_button_el_selend_value_changed(GtkWidget *w, gpointer user_data)
{
	info->selection[1] = get_nums( "button_el_selend" );
	if(info->selection[1] < info->selection[0])
		update_spin_value( "button_el_selstart", info->selection[1]);
	gchar *text = format_time( info->selection[1]);
	update_label_str( "label_el_endpos", text);
	g_free(text);
	el_selection_update();
}

static	gboolean verify_selection()
{
	if( (info->selection[1] - info->selection[0] ) <= 0)
	{
		vj_msg(VEEJAY_MSG_ERROR, "Invalid EditList selection %d - %d",
			info->selection[0], info->selection[1]);
		return FALSE;
	}
	return TRUE;
}

void	on_button_el_cut_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{	multi_vims( VIMS_EDITLIST_CUT, "%d %d",
			info->selection[0], info->selection[1]);
	}
}
void	on_button_el_del_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_DEL, "%d %d",
			info->selection[0], info->selection[1]);
	}
}
void	on_button_el_crop_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_CROP, "%d %d",
			info->selection[0], info->selection[1]);
	}
}
void	on_button_el_copy_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_COPY, "%d %d",
			info->selection[0], info->selection[1] );
	}
}

void	on_button_el_newclip_clicked(GtkWidget *w, gpointer *user)
{
	if(verify_selection())
	{
		multi_vims( VIMS_CLIP_NEW, "%d %d",
			info->selection[0], info->selection[1] );
		info->uc.reload_hint[HINT_SLIST] = 1;
	}


}

void	on_button_el_pasteat_clicked(GtkWidget *w, gpointer *user_data)
{
	gint val = get_nums( "button_el_selpaste" );
	info->selection[2] = val;
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->selection[2]);
}
void	on_button_el_save_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_save_file( "Save EditList" );
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_SAVE, "%s %d %d",
			filename, 0, info->el.num_frames );
		g_free(filename);
	}
}
void	on_button_el_savesel_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_save_file( "Save EditList Selection" );
	if(filename)
	{
		gint start = get_nums( "button_el_selstart" );
		gint end = get_nums( "button_el_selend" );
		multi_vims( VIMS_EDITLIST_SAVE, "%s %d %d",
			filename, start, end ); 
		g_free(filename);
	}
}

void	on_button_el_add_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_open_file( "Append videofile to EditList" );
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_ADD, "%s",
		filename );
		g_free(filename);
	}
}
void	on_button_el_addclip_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_open_file( "Append videofile (and create sample)");
	if( filename )
	{
		multi_vims( VIMS_EDITLIST_ADD_CLIP, "%s", filename );
		g_free(filename);
	}
}
void	on_button_el_delfile_clicked(GtkWidget *w, gpointer *user_data)
{
	int frame = _el_ref_start_frame( info->uc.selected_el_entry );
	int first_frame = frame;
	int last_frame = _el_ref_end_frame( info->uc.selected_el_entry );
	multi_vims( VIMS_EDITLIST_DEL, "%d %d", first_frame, last_frame );
} 
/*
void	on_button_fx_add_clicked(GtkWidget *w, gpointer user_data)
{
	if(info->uc.selected_effect_id > 0)
	{
		multi_vims( VIMS_CHAIN_ENTRY_SET_EFFECT, "%d %d %d",	
			0, info->uc.selected_chain_entry, info->uc.selected_effect_id );
		info->uc.reload_hint[HINT_CHAIN] = 1;
		info->uc.reload_hint[HINT_ENTRY] = 1;
	}
}*/
void	on_button_fx_clearchain_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_CLEAR, "%d",0);
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
}

void	on_button_entry_toggle_clicked(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint val = is_button_toggled( "button_entry_toggle" );
		if(val)
			multi_vims( VIMS_CHAIN_ENTRY_SET_VIDEO_ON,
				"%d %d", 0, info->uc.selected_chain_entry );
		else
			multi_vims( VIMS_CHAIN_ENTRY_SET_VIDEO_OFF,
				"%d %d", 0, info->uc.selected_chain_entry );
	}	
}

void	on_button_fx_entry_value_changed(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock)
	{
		int  *p = &(info->uc.entry_tokens[0]);
		multi_vims( VIMS_CHAIN_SET_ENTRY, "%d",
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w))
		);

		multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, 
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w)) );
		int len = 0;
		gchar *answer = recv_vims(3,&len);
		if(len>0)	
		{
			int res = sscanf( answer,
				"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
				p+0,p+1,p+2,p+3,p+4,p+5,p+6,p+7,p+8,p+9,p+10,
				p+11,p+12,p+13,p+14,p+15);
			if( res <= 0 )
				memset( p, 0, 16 );    
			g_free(answer);
		}
	}  	
				
}
void	on_button_fx_del_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_ENTRY_CLEAR, "%d %d", 0, 
		info->uc.selected_chain_entry );
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
}
/*
void	on_button_fx_mixapply_clicked(GtkWidget *w, gpointer user_data)
{
	if(info->uc.selected_mix_sample_id)
		multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,"%d %d %d %d",
			0,
			info->uc.selected_chain_entry,
			0,
			info->uc.selected_mix_sample_id
			);

	if(info->uc.selected_mix_stream_id)
		multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL, "%d %d %d %d",
			0,
			info->uc.selected_chain_entry,
			1,
			info->uc.selected_mix_stream_id
			);
}
*/

#define	slider_changed( arg_num, value, name ) \
{\
if(!info->status_lock && !info->parameter_lock)\
{\
info->parameter_lock = 1;\
multi_vims( VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d", 0, info->uc.selected_chain_entry,arg_num, value );\
if(info->uc.selected_rgbkey) update_rgbkey_from_slider(); \
info->parameter_lock = 0;\
}\
}

#define	param_changed( arg_num, fraction, name ) \
{\
if(!info->status_lock && !info->parameter_lock)\
{\
info->parameter_lock = 1;\
multi_vims( VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d", 0, info->uc.selected_chain_entry,arg_num, (get_slider_val(name) + fraction) );\
update_slider_value( name, (get_slider_val(name) + fraction), 0 );\
if(info->uc.selected_rgbkey) update_rgbkey_from_slider(); \
info->parameter_lock = 0;\
}\
}


void	on_slider_p0_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 0, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p0" );
}
void	on_slider_p1_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 1, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p1" );
}
void	on_slider_p2_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 2, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p2" );
}

void	on_slider_p3_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 3, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p3" );
}
void	on_slider_p4_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 4, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p4" );
}

void	on_slider_p5_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 5, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p5" );
}
void	on_slider_p6_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 6, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p6" );
}

void	on_slider_p7_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 7, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value, "spin_p7" );
}

void	on_inc_p0_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 0, 1 , "slider_p0" );
}
void	on_dec_p0_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 0, -1, "slider_p0");
}
void	on_inc_p1_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 1, 1 , "slider_p1" );
}
void	on_dec_p1_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 1, -1, "slider_p1");

}
void	on_inc_p2_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 2, 1 , "slider_p2" );
}
void	on_dec_p2_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 2, -1, "slider_p2");
}
void	on_inc_p3_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 3, 1 , "slider_p3" );
}
void	on_dec_p3_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 3, -1, "slider_p3");
}
void	on_inc_p4_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(4, 1 , "slider_p4" );
}
void	on_dec_p4_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 4, -1, "slider_p4");
}

void	on_inc_p5_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(5, 1 , "slider_p5" );
}
void	on_dec_p5_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 5, -1, "slider_p5");
}

void	on_inc_p6_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(6, 1 , "slider_p6" );
}
void	on_dec_p6_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 6, -1, "slider_p6");
}

void	on_inc_p7_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(7, 1 , "slider_p7" );
}
void	on_dec_p7_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 7, -1, "slider_p7");
}



void	on_button_stoplaunch_clicked(GtkWidget *widget, gpointer user_data)
{
	vj_gui_stop_launch();
}

void	on_button_sample_play_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->uc.selected_sample_id != 0)
	{
		multi_vims( VIMS_SET_MODE_AND_GO , "%d %d" , 0,
			info->uc.selected_sample_id );
	}
	if(info->uc.selected_stream_id != 0)
	{
		multi_vims( VIMS_SET_MODE_AND_GO, "%d %d", 1,
			info->uc.selected_stream_id );
	}

}
void	on_button_sample_del_clicked(GtkWidget *widget, gpointer user_data)
{
	int query = 0;
	if(info->uc.selected_sample_id )
	{
		multi_vims( VIMS_CLIP_DEL, "%d",
			info->uc.selected_sample_id );
		query = 1;
	}
	if(info->uc.selected_stream_id)
	{
		multi_vims( VIMS_STREAM_DELETE, "%d",
			info->uc.selected_stream_id );
		query = 1;
	}

	if(query)
		info->uc.reload_hint[HINT_SLIST] = 1;
}
void	on_button_samplelist_load_clicked(GtkWidget *widget, gpointer user_data)
{
	gint erase_all = 0;
	if(info->uc.list_length[MODE_SAMPLE] > 0 )
	{
		if(prompt_dialog("Load cliplist",
			"Loading a cliplist will delete any existing samples" ) == GTK_RESPONSE_REJECT)
			return;
		else
			erase_all = 1;
	}

	gchar *filename = dialog_open_file( "Open samplelist");
	if(filename)
	{
		if(erase_all)
		{
			single_vims( VIMS_SET_PLAIN_MODE );
			single_vims( VIMS_CLIP_DEL_ALL ); 
		}
		multi_vims( VIMS_CLIP_LOAD_CLIPLIST, "%s", filename );
		g_free(filename );
	}
}
void	on_button_samplelist_save_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save samplelist");
	if(filename)
	{
		multi_vims( VIMS_CLIP_SAVE_CLIPLIST, "%s", filename );
		g_free(filename);
	}
}

void	on_spin_samplestart_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims(VIMS_CLIP_SET_START, "%d %d",0,
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) ) );
	}
}

void	on_spin_sampleend_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims(VIMS_CLIP_SET_END, "%d %d", 0,
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget)) );
	}
}

void	on_speedslider_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{	
		multi_vims( VIMS_VIDEO_SET_SPEED, "%d",
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value ) );
	}
}

void	on_spin_samplespeed_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_CLIP_SET_SPEED, "%d %d",0,
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) ) );
	}
}

void	on_v4l_brightness_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_BRIGHTNESS, "%d %d",
			info->uc.current_stream_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	}
}

void	on_v4l_contrast_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_CONTRAST, "%d %d", 
			info->uc.current_stream_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
	}
}

void	on_v4l_hue_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_HUE, "%d %d",
			info->uc.current_stream_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
	}
}	

void	on_v4l_white_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_WHITE, "%d %d",
			info->uc.current_stream_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
	}
}

void	on_v4l_color_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_COLOR, "%d %d",
			info->uc.current_stream_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	}
}


void	on_stream_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{

	gint nframes = get_nums( "spin_streamduration");
	gint autoplay = is_button_toggled("button_stream_autoplay"); 
	GtkComboBox *combo = GTK_COMBO_BOX( GTK_WIDGET(glade_xml_get_widget(info->main_window,"combo_streamcodec")));
	gchar *format = (gchar*)gtk_combo_box_get_active_text(combo) ;

	if(nframes <= 0)
		return;

	if(format != NULL)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}		

	multi_vims( VIMS_STREAM_REC_START,
		"%d %d",
		nframes,
		autoplay );
	
}

void	on_stream_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_STREAM_REC_STOP );
}

void	on_spin_streamduration_value_changed(GtkWidget *widget , gpointer user_data)
{
	gint n_frames = get_nums( "spin_streamduration" );
	gchar *time = format_time(n_frames);
	update_label_str( "label_streamrecord_duration", time );
	g_free(time);
}


void	on_button_sample_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{

	gint autoplay = is_button_toggled("button_sample_autoplay"); 
	GtkComboBox *combo = GTK_COMBO_BOX( GTK_WIDGET(glade_xml_get_widget(info->main_window,"combo_samplecodec")));

	gchar *format = (gchar*) gtk_combo_box_get_active_text(combo);

	gint nframes = info->uc.sample_rec_duration;
	if(nframes <= 0)
		return;
	
	if(format != NULL)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}		

	multi_vims( VIMS_CLIP_REC_START,
		"%d %d",
		nframes,
		autoplay );
	
}

void	on_button_sample_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_CLIP_REC_STOP );
}

static	int	sample_calctime()
{
	gint n_frames = get_nums( "spin_sampleduration");
	if( is_button_toggled( "sample_mulloop" ) )
	{
		if(n_frames > 0)
		{
			n_frames *= (info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START]);
			if( info->status_tokens[SAMPLE_LOOP] == 2 )
				n_frames *= 2;
		}
	}
	return n_frames;
}

void	on_spin_sampleduration_value_changed(GtkWidget *widget , gpointer user_data)
{
	// get num and display label_samplerecord_duration
	gint n_frames = sample_calctime();
	gchar *time = format_time( n_frames );
	update_label_str( "label_samplerecord_duration", time );
	info->uc.sample_rec_duration = n_frames;
	g_free(time);
}

void	on_sample_mulloop_clicked(GtkWidget *w, gpointer user_data)
{
	gint n_frames = sample_calctime();
	gchar *time = format_time( n_frames );
	update_label_str( "label_samplerecord_duration", time );
	info->uc.sample_rec_duration = n_frames;
	g_free(time);
}

void	on_sample_mulframes_clicked(GtkWidget *w, gpointer user_data)
{
	gint n_frames = sample_calctime();
	gchar *time = format_time( n_frames );
	update_label_str( "label_samplerecord_duration", time );
	info->uc.sample_rec_duration = n_frames;
	g_free(time);
}

void	on_spin_mudplay_value_changed(GtkWidget *widget, gpointer user_data)
{
	DBG_C();	
}
void	on_check_samplefx_clicked(GtkWidget *widget , gpointer user_data)
{
	if(!info->status_lock)
	{
		if( is_button_toggled( "check_samplefx" ) )
			multi_vims( VIMS_CLIP_CHAIN_ENABLE , "%d", 0 );
		else
			multi_vims( VIMS_CLIP_CHAIN_DISABLE, "%d", 0 );
	}
}
void	on_check_streamfx_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		if( is_button_toggled( "check_streamfx"))
			multi_vims( VIMS_STREAM_CHAIN_ENABLE, "%d", 0 );
		else
			multi_vims( VIMS_STREAM_CHAIN_DISABLE, "%d", 0 );	
	}
}

void	on_loop_none_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		multi_vims( VIMS_CLIP_SET_LOOPTYPE,
			"%d %d", 0,
			get_loop_value() );
}
void	on_loop_normal_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		multi_vims( VIMS_CLIP_SET_LOOPTYPE,
			"%d %d", 0,
			get_loop_value() );

}
void	on_loop_pingpong_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		multi_vims( VIMS_CLIP_SET_LOOPTYPE,
			"%d %d", 0,
			get_loop_value() );

}

#define atom_marker(name,value) {\
info->uc.marker.lock=1;\
update_slider_value(name, info->uc.marker.start+1,0);\
info->uc.marker.lock=0;\
}

/* sample marker */
void	on_slider_m0_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->uc.marker.lock && !info->status_lock)
	{
		info->uc.marker.start = (gint)GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
		if(info->uc.marker.bind)
		{
			int val = info->uc.marker.upper_bound - info->uc.marker.start; 
			atom_marker("slider_m1", val);
		}
		if(info->uc.marker.bind)
			multi_vims( VIMS_CLIP_SET_MARKER , "%d %d %d", 0,
				info->uc.marker.start, get_slider_val( "slider_m1" ) );
		else
			multi_vims( VIMS_CLIP_SET_MARKER_START, "%d %d", 0,
				info->uc.marker.start );
	}
	
}
void	on_slider_m1_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->uc.marker.lock && !info->status_lock)
	{
		info->uc.marker.end = (gint)GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
		if(info->uc.marker.bind)
		{
			int val = info->uc.marker.upper_bound - info->uc.marker.start; 
			atom_marker("slider_m0", val);
		}
		if(info->uc.marker.bind)
			multi_vims( VIMS_CLIP_SET_MARKER , "%d %d %d", 0,
				get_slider_val( "slider_m0" ), info->uc.marker.end );
		else
			multi_vims( VIMS_CLIP_SET_MARKER_END, "%d %d", 0,
				info->uc.marker.end );

	}

}
void	on_check_marker_bind_clicked(GtkWidget *widget, gpointer user_data)
{
	// might need to adjust slider m0,m1
	if( is_button_toggled( "check_marker_bind" ) )
	{
		info->uc.marker.bind = 1;
		if(info->uc.marker.start >= info->uc.marker.end)
			atom_marker("slider_m0", 0 );    
	}
	else
	{
		info->uc.marker.bind = 0;
		if(info->uc.marker.end <= info->uc.marker.start)
			atom_marker("slider_m1", info->uc->upper_bound );
	}

}
void	on_button_clearmarker_clicked(GtkWidget *widget, gpointer user_data)
{
	info->uc.marker.start = 0;
	info->uc.marker.end = 0;
	atom_marker( "slider_m0", info->uc.marker.lower_bound );
	atom_marker( "slider_m1", info->uc.marker.upper_bound );
	multi_vims( VIMS_CLIP_CLEAR_MARKER, "%d", 0 );

	info->uc.reload_hint[ HINT_MARKER ] = 1;
 
}


void	on_check_audio_mute_clicked(GtkWidget *widget, gpointer user_data)
{
	DBG_C();
}
void	on_button_samplelist_open_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file("Load samplelist");
	if( filename )
	{
		multi_vims( VIMS_CLIP_LOAD_CLIPLIST, "%s", filename );
		g_free(filename);
	}
}

void	on_veejay_ctrl_expander_activate(GtkWidget *exp, gpointer user_data)
{
	DBG_C();
}

void	on_button_el_takestart_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value( "button_el_selstart",
			info->status_tokens[FRAME_NUM] );
}
void	on_button_el_takeend_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value ("button_el_selend", 
			info->status_tokens[FRAME_NUM] );	
}
void	on_button_el_paste_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->status_tokens[FRAME_NUM] );
}
void	on_new_colorstream_clicked(GtkWidget *widget, gpointer user_data)
{

	GdkColor current_color;
	GtkWidget *colorsel = glade_xml_get_widget(info->main_window,
			"colorselection" );
	gtk_color_selection_get_current_color(
		GTK_COLOR_SELECTION( colorsel ),
		&current_color );

	// scale to 0 - 255
	gint red = current_color.red / 256.0;
	gint green = current_color.green / 256.0;
	gint blue = current_color.blue / 256.0;

	info->uc.reload_hint[HINT_SLIST] = 1;
	multi_vims( VIMS_STREAM_NEW_COLOR, "%d %d %d",
		red,green,blue );
}

#define atom_aspect_ratio(name,type) {\
info->uc.priout_lock=1;\
gint value = (type == 0 ? resize_primary_ratio_x() : resize_primary_ratio_y() );\
update_spin_value(name, value);\
info->uc.priout_lock=0;\
}
void	on_priout_width_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock && !info->uc.priout_lock)
	if( is_button_toggled( "priout_ratio" ))
		atom_aspect_ratio( "priout_height", 1 );
}
void	on_priout_height_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock && !info->uc.priout_lock)
	if( is_button_toggled( "priout_ratio" ))
		atom_aspect_ratio( "priout_width", 0 );
}
void	on_priout_apply_clicked(GtkWidget *widget, gpointer user_data)
{
	gint width = get_nums( "priout_width" );
	gint height = get_nums( "priout_height" );
	gint x = get_nums("priout_x" );
	gint y = get_nums("priout_y" );

	if( width > 0 && height > 0 )
	{
		multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			width,height,x , y );
	}

}


void	on_vims_take_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BUNDLE_CAPTURE );
	info->uc.reload_hint[HINT_BUNDLES] = 1;
}
void	on_button_key_detach_clicked(GtkWidget *widget, gpointer user)
{
	int key_val  = info->uc.selected_key_sym;
	int key_mod  = info->uc.selected_key_mod;


	if( key_val > 0 )
	{
		multi_vims( 
			VIMS_BUNDLE_ATTACH_KEY,
			"0 %d %d",
			key_val, key_mod );
		info->uc.reload_hint[HINT_BUNDLES] = 1; 
	}
}

void	on_vims_key_clicked( GtkWidget *widget, gpointer user_data)
{
	int n = prompt_keydialog(
			"Press Key",
			"Key for Bundle " );

	if( n == GTK_RESPONSE_ACCEPT )
	{
		int event_id = info->uc.selected_vims_entry;
		int key_val  = gdk2sdl_key( info->uc.pressed_key );
		int mod	     = gdk2sdl_mod( info->uc.pressed_mod );
		if( event_id > 0 && key_val > 0 )
		{
			multi_vims( 
				VIMS_BUNDLE_ATTACH_KEY,
				"%d %d %d",
				event_id, key_val, mod );
			info->uc.reload_hint[HINT_BUNDLES] = 1; 
		}
	}	


}


void	on_button_vimsupdate_clicked(GtkWidget *widget, gpointer user_data)
{
	if(count_textview_buffer( "vimsview" ) > 0 )
	{
		gchar *buf = get_textview_buffer( "vimsview" );
		multi_vims( VIMS_BUNDLE_ADD, "%d %s",
			info->uc.selected_vims_entry, buf );
		info->uc.reload_hint[HINT_BUNDLES] = 1; 
	} 
// passent current and overwrite if bundle is valdi

}

void	on_vims_clear_clicked(GtkWidget *widget, gpointer user_data)
{
	clear_textview_buffer( "vimsview" );
}

void	on_vims_delete_clicked(GtkWidget *widget, gpointer user_data)
{
	// delete selected bundle
	multi_vims( VIMS_BUNDLE_DEL, "%d", info->uc.selected_vims_entry );
	info->uc.reload_hint[HINT_BUNDLES] = 1;
}

void	on_button_saveactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save Bundles");
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_SAVE, "%s", filename );
		g_free(filename);
	} 	
}

void	on_button_newbundle_clicked(GtkWidget *widget, gpointer user_data)
{
	if(count_textview_buffer( "vimsview" ) > 0 )
	{
		gchar *buf = get_textview_buffer( "vimsview" );
		multi_vims( VIMS_BUNDLE_ADD, "%d %s", 0, buf );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
	}
}

void	on_button_openactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Load Bundles" );
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_FILE, "%s", filename );
		g_free( filename );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
	}
}

void	on_button_browse_clicked(GtkWidget *widget, gpointer user_data)
{
	// open file browser for launcher
	gchar *filename = dialog_open_file( "Open Videofile or EditList" );
	if(filename)
	{
		put_text( "entry_filename", filename );
		g_free(filename);
	}
}

void	on_button_historyrec_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->uc.selected_history_entry > 0)
	{
		multi_vims( VIMS_CLIP_RENDER_TO, "%d %d", 0, info->uc.selected_history_entry );
	}
	info->uc.render_record=1;
}

void	on_button_historymove_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->uc.selected_history_entry > 0 )
	{
		multi_vims( VIMS_CLIP_RENDER_MOVE, "%d %d", 0, info->uc.selected_history_entry );
	}
	info->uc.reload_hint[HINT_EL] =1;
	info->uc.reload_hint[HINT_HISTORY] = 1;
}

void	on_button_clipcopy_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->uc.selected_sample_id != 0)
	{
		multi_vims( VIMS_CLIP_COPY , "%d",
			info->uc.selected_sample_id );
		if(info->status_tokens[PLAY_MODE] == MODE_PLAIN )
			info->uc.reload_hint[HINT_SLIST] = 1;
	}
}

void	on_check_priout_fullscreen_clicked(
		GtkWidget *widget, gpointer user_data)
{
	gint on = 0;
	if(is_button_toggled( "check_priout_fullscreen" ) )
		on = 1;
	multi_vims ( VIMS_FULLSCREEN, "%d", on );
}
