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
 
#include <gveejay/vj-api.h>
#include <veejay/vj-global.h>

#define DBG_C() { 	vj_msg_detail(VEEJAY_MSG_DEBUG, "Implement %s", __FUNCTION__ ); }  

static int config_file_status = 0;
static gchar *config_file = NULL;

void	on_button_085_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_SKIP_SECOND);
	vj_msg(VEEJAY_MSG_INFO, "Next second"); 
}
void	on_button_086_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PREV_SECOND );
	vj_msg(VEEJAY_MSG_INFO, "Previous second");
}
void	on_button_080_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_FORWARD);
	vj_msg(VEEJAY_MSG_INFO, "Play Forward");
}
void	on_button_081_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_BACKWARD);
	vj_msg(VEEJAY_MSG_INFO, "Play Backward");
}
void	on_button_082_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PLAY_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Stop");
}
void	on_button_083_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_SKIP_FRAME );
	vj_msg(VEEJAY_MSG_INFO, "Next frame");
}
void 	on_button_084_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PREV_FRAME );
	vj_msg(VEEJAY_MSG_INFO, "Previous frame");
}
void	on_button_087_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_START );
	vj_msg(VEEJAY_MSG_INFO, "Goto starting position");
}
void	on_button_088_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_END);
	vj_msg(VEEJAY_MSG_INFO, "Goto ending position");
}

void	on_videobar_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gdouble slider_val = GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
		gint val = 0;
		switch(info->status_tokens[PLAY_MODE])
		{
			case MODE_PLAIN:
				val = slider_val * info->status_tokens[TOTAL_FRAMES];
				break;
			case MODE_SAMPLE:
				val = slider_val * (info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START]);
				val += info->status_tokens[SAMPLE_START];
				break;
			default:
				return;
		}
		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", val );
	}
}

void	on_audiovolume_value_changed(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *w = glade_xml_get_widget(info->main_window, "audiovolume");
	gdouble val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	multi_vims( VIMS_SET_VOLUME, "%d", (gint) (val * 100.0) );
	vj_msg(VEEJAY_MSG_INFO,"Set volume to %d", (gint)(val * 100.0));
}

void	on_button_001_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SET_PLAIN_MODE );
	vj_msg(VEEJAY_MSG_INFO, "Playing plain video (EditList operations allowed)");
}

void	on_button_252_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_DEBUG_LEVEL );
	if(is_button_toggled( "button_252" ))
		vims_verbosity = 1;
	else
		vims_verbosity = 0;
	vj_msg(VEEJAY_MSG_INFO, "%s debug information",
		vims_verbosity ? "Displaying" : "Not displaying" );
}

void	on_button_251_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BEZERK );
	vj_msg(VEEJAY_MSG_INFO, "Bezerk mode toggled");
}	

void	on_button_054_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *ext = get_text( "screenshotformat" );
	if(ext)
	{
		gchar filename[100];
		sprintf(filename, "frame-%d.%s", info->status_tokens[FRAME_NUM] + 1 , ext);
		multi_vims( VIMS_SCREENSHOT,"%d %d %s",0,0,filename );
		vj_msg(VEEJAY_MSG_INFO, "Requested veejay to take screenshot of frame %d",
			info->status_tokens[FRAME_NUM] + 1 );
	}
}
void	on_button_200_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_EFFECT_SET_BG ); 
	vj_msg(VEEJAY_MSG_INFO,
		"Requested veejay to take background mask of frame %d",
			info->status_tokens[FRAME_NUM] + 1 );
}
void	on_button_5_4_clicked(GtkWidget *widget, gpointer user_data)
{
	if( is_button_toggled("button_5_4") )
	{
		single_vims( VIMS_AUDIO_ENABLE );
		vj_msg(VEEJAY_MSG_INFO, "Audio is enabled");
	}
	else
	{
		single_vims( VIMS_AUDIO_DISABLE );	
		vj_msg(VEEJAY_MSG_INFO, "Audio is disabled");
	}

}
void	on_button_samplestart_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[0] = info->status_tokens[FRAME_NUM];
	vj_msg(VEEJAY_MSG_INFO, "New sample startings position is %d",
		info->sample[0] );
}
void	on_button_sampleend_clicked(GtkWidget *widget, gpointer user_data)
{
	info->sample[1] = info->status_tokens[FRAME_NUM];
	multi_vims( VIMS_SAMPLE_NEW, "%d %d", info->sample[0],info->sample[1]);
	if(info->status_tokens[PLAY_MODE] == MODE_PLAIN)
		info->uc.reload_hint[HINT_SLIST] = 1;
	vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d",
		info->sample[0], info->sample[1]);
}

void	on_button_veejay_clicked(GtkWidget *widget, gpointer user_data)
{
	vj_fork_or_connect_veejay( config_file );
}
void	on_button_sendvims_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *text = get_text("vimsmessage");
	if(strncasecmp( text, "600:;",5 ) == 0)
	{
		vj_msg(VEEJAY_MSG_INFO, "Do you want to quit?");
		if( prompt_dialog("Quit veejay", "Close Veejay ? All unsaved work will be lost.")
			== GTK_RESPONSE_REJECT)
			return;
	}
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'",text );
	msg_vims( text );
}
void	on_vimsmessage_activate(GtkWidget *widget, gpointer user_data)
{
	msg_vims( get_text( "vimsmessage") );
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'", get_text("vimsmessage"));
}
void	on_button_vimshelp_clicked(GtkWidget *widget, gpointer user_data)
{
	about_dialog();	
}

void	on_button_fadedur_value_changed(GtkWidget *widget, gpointer user_data)
{

}

void	on_button_fadeout_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	gchar *timenow = format_time( num );
	multi_vims( VIMS_CHAIN_FADE_OUT, "0 %d", num );
	vj_msg(VEEJAY_MSG_INFO, "Fade out duration %s (frames %d)",
		timenow,
		num );
	if(timenow) g_free(timenow);
}

void	on_button_fadein_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	gchar *timenow = format_time( num );
	multi_vims( VIMS_CHAIN_FADE_IN, "0 %d", num );
	vj_msg(VEEJAY_MSG_INFO, "Fade in duration %s (frames %d)",
		timenow,
		num );
	if(timenow) g_free(timenow);

}

void	on_manualopacity_value_changed(GtkWidget *w, gpointer user_data)
{
	gdouble min_val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->lower;
	gdouble max_val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->upper;
	gdouble val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	gdouble nval = (val - min_val) / (max_val - min_val);
	
	if(val < 0.0 )
		val = 0.0;
	if(val > 1.0) 
		val = 1.0;

	if( is_button_toggled( "loglinear" ))
		if(val > 0.0) val = log( val /0.1 ) / log( max_val / 0.1 );	

	multi_vims( VIMS_CHAIN_MANUAL_FADE, "0 %d",
		(int)(255.0 * val));

	vj_msg(VEEJAY_MSG_INFO, "FX Opacity set to %1.2f", val ); 
}

static void	el_selection_update()
{
	gchar *text = format_selection_time(info->selection[0], info->selection[1]);
	update_label_str( "label_el_selection", text );
	vj_msg( VEEJAY_MSG_INFO, "Updated EditList selection %d - %d Timecode: %s",
		info->selection[0], info->selection[1], text );
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
		gchar *time1 = format_time( info->selection[0] );
		gchar *time2 = format_time( info->selection[1] );
		vj_msg(VEEJAY_MSG_INFO, "Cut %s - %s from EditList to buffer",
			time1, time2 );
		g_free(time1);
		g_free(time2);
	}
}
void	on_button_el_del_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_DEL, "%d %d",
			info->selection[0], info->selection[1]);
		gchar *time1 = format_time( info->selection[0] );
		gchar *time2 = format_time( info->selection[1] );
		vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s from EditList",
			time1, time2 );
		g_free(time1);
		g_free(time2);

	}
}
void	on_button_el_crop_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_CROP, "%d %d",
			info->selection[0], info->selection[1]);
		gchar *total = format_time( info->status_tokens[TOTAL_FRAMES] );
		gchar *time2 = format_time( info->selection[1] );
		gchar *time1 = format_time( info->selection[0] );
		vj_msg(VEEJAY_MSG_INFO, "Delete 00:00:00 - %s and %s - %s from EditList",
			time1, time2, total );
		g_free(time1);
		g_free(time2);
		g_free(total);

	}
}
void	on_button_el_copy_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_COPY, "%d %d",
			info->selection[0], info->selection[1] );
		gchar *time1 = format_time( info->selection[0] );
		gchar *time2 = format_time( info->selection[1] );
		vj_msg(VEEJAY_MSG_INFO, "Copy %s - %s to buffer",
			time1,time2);
		g_free(time1);
		g_free(time2);
	}
}

void	on_button_el_newclip_clicked(GtkWidget *w, gpointer *user)
{
	if(verify_selection())
	{
		multi_vims( VIMS_SAMPLE_NEW, "%d %d",
			info->selection[0], info->selection[1] );
		info->uc.reload_hint[HINT_SLIST] = 1;
		vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d" ,
			info->selection[0], info->selection[1] );
	}


}

void	on_button_el_pasteat_clicked(GtkWidget *w, gpointer *user_data)
{
	gint val = get_nums( "button_el_selpaste" );
	info->selection[2] = val;
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->selection[2]);
	gchar *time1 = format_time( info->selection[2] );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents from buffer to frame %d (timecode %s)",
		info->selection[2], time1);
	g_free(time1);
}
void	on_button_el_save_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_save_file( "Save EditList" );
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_SAVE, "%s %d %d",
			filename, 0, info->el.num_frames );
		vj_msg(VEEJAY_MSG_INFO, "Saved EditList to %s", filename);
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
		vj_msg(VEEJAY_MSG_INFO,"Save EditList selection to %s", filename); 
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
		vj_msg(VEEJAY_MSG_INFO, "Try to add file '%s' to EditList", filename);
		g_free(filename);
	}
}
void	on_button_el_addsample_clicked(GtkWidget *w, gpointer *user_data)
{
	gchar *filename = dialog_open_file( "Append videofile (and create sample)");
	if( !filename )
		return;
	
	int sample_id = 0;
	int result_len = 0;
	multi_vims( VIMS_EDITLIST_ADD_SAMPLE, "%s", filename );

	gchar *result = recv_vims( 3, &result_len );
	if(result_len > 0 )
	{
		sscanf( result, "%5d", &sample_id );
		vj_msg(VEEJAY_MSG_INFO, "Created new sample %d from file %s", sample_id, filename);
		g_free(result);
		// force reloading of sample list
		info->uc.reload_hint[HINT_SLIST] = 1;
	}
	
	g_free(filename );
}
void	on_button_el_delfile_clicked(GtkWidget *w, gpointer *user_data)
{
	int frame = _el_ref_start_frame( info->uc.selected_el_entry );
	int first_frame = frame;
	int last_frame = _el_ref_end_frame( info->uc.selected_el_entry );
	multi_vims( VIMS_EDITLIST_DEL, "%d %d", first_frame, last_frame );
	gchar *time1 = format_time( first_frame );
	gchar *time2 = format_time( last_frame );
	vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s",
		time1,time2);
	g_free(time1);
	g_free(time2);
} 
void	on_button_fx_clearchain_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_CLEAR, "%d",0);
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Clear FX Chain");
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
		vj_msg(VEEJAY_MSG_INFO, "Chain Entry %d is %s",
			info->uc.selected_chain_entry,
			(val ? "Enabled" : "Disabled" ));
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
		info->uc.reload_hint[HINT_ENTRY] = 1;	
	}  	
				
}
void	on_button_fx_del_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_ENTRY_CLEAR, "%d %d", 0, 
		info->uc.selected_chain_entry );
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Clear Effect from Entry %d",
		info->uc.selected_chain_entry);
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

#define	slider_changed( arg_num, value ) \
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
	slider_changed( 0, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p1_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 1, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p2_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 2, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}

void	on_slider_p3_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 3, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p4_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 4, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}

void	on_slider_p5_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 5, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p6_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 6, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}

void	on_slider_p7_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 7, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
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
	if(info->selected_slot)
	{
		multi_vims( VIMS_SET_MODE_AND_GO , "%d %d" ,
			info->selected_slot->sample_id,		
			info->selected_slot->sample_type );
	}
}
void	on_button_sample_del_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->selected_slot )	
	{
		remove_sample_from_slot();
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "No slot selected\n");
	}
}
void	on_button_samplelist_load_clicked(GtkWidget *widget, gpointer user_data)
{
	gint erase_all = 0;
	if(info->status_tokens[TOTAL_SLOTS] > 0 )
	{
		if(prompt_dialog("Load samplelist",
			"Loading a samplelist will delete any existing samples" ) == GTK_RESPONSE_REJECT)
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
			single_vims( VIMS_SAMPLE_DEL_ALL ); 
		}
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
	}
}
void	on_button_samplelist_save_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save samplelist");
	if(filename)
	{
		multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
		g_free(filename);
	}
}

void	on_spin_samplestart_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value[1];
		value[0] = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		gchar *time1 = format_time(value[0]);
		gchar buf[256];
		sprintf( buf, "Set sample's starting position to %d (timecode %s)",value[0], time1);
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_START, &value, &buf);		
		g_free(time1);
	}
}

void	on_spin_sampleend_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value[1];
		value[0] = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		gchar *time1 = format_time(value[0]);
		gchar buf[256];
		sprintf( buf, "Set sample's ending position to %d (timecode %s)",value[0], time1);
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_END, &value, &buf);		
		g_free(time1);

	}
}

void	on_speedslider_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{	
		gint value = (gint)GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
		value *= info->play_direction;
		multi_vims( VIMS_VIDEO_SET_SPEED, "%d",value );
		vj_msg(VEEJAY_MSG_INFO, "Change video playback speed to %d",
			value );
	}
}

void	on_spin_samplespeed_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value[1];
		value[0] = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		value[0] *= info->play_direction;		
		gchar buf[256];
		sprintf( buf, "Change video playback speed to %d",value[0] );
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_SPEED, &value, &buf);		
	}
}

void	on_v4l_brightness_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_BRIGHTNESS, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	}
}

void	on_v4l_contrast_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_CONTRAST, "%d %d", 
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );

	}
}

void	on_v4l_hue_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_HUE, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
	}
}	

void	on_v4l_white_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_WHITE, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
	}
}

void	on_v4l_color_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_COLOR, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	}
}

#ifndef HAVE_GTK2_6
static gchar	*my_gtk_combo_box_get_active_text(GtkComboBox *combo )
{
 GtkTreeIter _iter = { 0 };
 gchar *_format = NULL;
 GtkTreeModel *_model=NULL;
 g_return_val_if_fail( GTK_IS_COMBO_BOX(combo),NULL);
 _model = gtk_combo_box_get_model(combo);
 g_return_val_if_fail( GTK_IS_LIST_STORE(_model),NULL);
 if(gtk_combo_box_get_active_iter(combo,&_iter))
	gtk_tree_model_get(_model, &_iter,0,&_format,-1);
 return _format;
}
#define gtk_combo_box_get_active_text( combo ) my_gtk_combo_box_get_active_text(combo)
#endif

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
		vj_msg(VEEJAY_MSG_INFO, "Set recording format to %s", format);
	}		

	multi_vims( VIMS_STREAM_REC_START,
		"%d %d",
		nframes,
		autoplay );
	
	gchar *time1 = format_time( nframes );
	vj_msg(VEEJAY_MSG_INFO, "Record duration: %s", time1); 
	g_free(time1);
	g_free(format);	
}

void	on_stream_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_STREAM_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Stream record stop");
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

	multi_vims( VIMS_SAMPLE_REC_START,
		"%d %d",
		nframes,
		autoplay );

	gchar *time1 = format_time(nframes);
	vj_msg(VEEJAY_MSG_INFO,"Record duration: %s",
		time1);
	g_free(time1);

	g_free(format);	
}

void	on_button_sample_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SAMPLE_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Sample record stop");
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
	    {
	    gchar buf[256];
	    sprintf( buf, "Effect chain toggled on");
	    vj_gui_change_slot_option(VIMS_SAMPLE_CHAIN_ENABLE , NULL, &buf);			
	    }
	else
	    {
	    gchar buf[256];
	    sprintf( buf, "Effect chain toggled off");
	    vj_gui_change_slot_option(VIMS_SAMPLE_CHAIN_DISABLE , NULL, &buf);			
	    }
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
		{		
		gint value[1];
		value[0] = get_loop_value();
		gchar buf[256];
		sprintf( buf, "Change loop type to no loop");
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_LOOPTYPE, &value, &buf);		
		}
		
}

void	on_loop_normal_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		{	
		gint value[1];
		value[0] = get_loop_value();
		gchar buf[256];
		sprintf( buf, "Change loop type to normal loop");
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_LOOPTYPE, &value, &buf);		
		}	
}

void	on_loop_pingpong_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		{		
		gint value[1];
		value[0] = get_loop_value();
		gchar buf[256];
		sprintf( buf, "Change loop type to ping pong");
		vj_gui_change_slot_option(VIMS_SAMPLE_SET_LOOPTYPE, &value, &buf);		
		}		
}

#define atom_marker(name,value) {\
info->uc.marker.lock=1;\
update_slider_gvalue(name, value);\
info->uc.marker.lock=0;\
}

/* sample marker */
void	on_slider_m0_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->uc.marker.lock && !info->status_lock)
	{
		int real_len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
		
		info->uc.marker.start = GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
		if(info->uc.marker.bind)
		{
			info->uc.marker.end = 1.0 - info->uc.marker.start - info->uc.marker.bind_len;
			if(info->uc.marker.end < 0.0)
				info->uc.marker.end = 0.0;
			if(info->uc.marker.end > 1.0)
				info->uc.marker.end = 1.0;
		}

		multi_vims( VIMS_SAMPLE_SET_MARKER , "%d %d %d", 0,
			(gint ) (info->uc.marker.start * real_len), (gint)(info->uc.marker.end * real_len));

	}
	
}
void	on_slider_m1_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->uc.marker.lock && !info->status_lock)
	{
		int real_len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
		info->uc.marker.end = GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value;
	
		if(info->uc.marker.bind)
		{	
			info->uc.marker.start = 
					1.0 - info->uc.marker.end - info->uc.marker.bind_len;
			if( info->uc.marker.start  > 1.0 )
				info->uc.marker.start = 1.0;
			if( info->uc.marker.start < 0.0 )
				info->uc.marker.start = 0.0;
		}

		multi_vims( VIMS_SAMPLE_SET_MARKER , "%d %d %d", 0,
			(gint ) (info->uc.marker.start * real_len), (gint)(info->uc.marker.end * real_len));

	}

}
void	on_check_marker_bind_clicked(GtkWidget *widget, gpointer user_data)
{
	// might need to adjust slider m0,m1
	if(info->status_lock)
		return;

	info->uc.marker.bind = is_button_toggled( "check_marker_bind");
	if(info->uc.marker.bind)
	{
		vj_msg(VEEJAY_MSG_INFO, "Marker is bound");
		GtkWidget *w1 = glade_xml_get_widget_( info->main_window, "slider_m0" );
		GtkWidget *w2 = glade_xml_get_widget_( info->main_window, "slider_m1" );
		gdouble start = GTK_ADJUSTMENT(GTK_RANGE(w1)->adjustment)->value;
		gdouble end   = GTK_ADJUSTMENT(GTK_RANGE(w2)->adjustment)->value;
		info->uc.marker.bind_len = 1.0 - start - end;
		if(info->uc.marker.bind_len < 0.0 )
		{
			set_toggle_button( "check_marker_bind", 0 );
		}
	}
	else
	{
		vj_msg(VEEJAY_MSG_INFO, "Marker is released");
		info->uc.marker.bind_len = 0;
	}
}
void	on_button_clearmarker_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );
	info->uc.reload_hint[ HINT_MARKER ] = 1;
	memset( &(info->uc.marker), 0, sizeof( sample_marker_t ));
 	vj_msg(VEEJAY_MSG_INFO, "Clear Marker");
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
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO, "Try to load sample list %s", filename);
		g_free(filename);
	}
}
void	on_veejay_expander_activate(GtkWidget *exp, gpointer user_data)
{
}
void	on_veejay_ctrl_expander_activate(GtkWidget *exp, gpointer user_data)
{
	gint width= 0;
	gint height = 0;

	GtkWindow *window = GTK_WINDOW( glade_xml_get_widget_( info->main_window, "gveejay_window"));

	gtk_window_get_size( window, &width, &height );

	if(!gtk_expander_get_expanded(GTK_EXPANDER(exp)))
	{
		gtk_widget_set_size_request(
			glade_xml_get_widget_(info->main_window, "veejaypanel" ), 
		 width,
		 400 );

		gtk_window_resize( window, width, 600 );

	}
	else
	{	
		gtk_widget_set_size_request(
			glade_xml_get_widget_(info->main_window, "veejaypanel" ), 
			 width,
			 0 );
		gtk_window_resize( window, width, 100 );
	}
}

void	on_button_el_takestart_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value( "button_el_selstart",
			info->status_tokens[FRAME_NUM] );
	vj_msg(VEEJAY_MSG_INFO, "Set current frame %d as editlist starting position",
		info->status_tokens[FRAME_NUM]);
}
void	on_button_el_takeend_clicked(GtkWidget *widget, gpointer user_data)
{
	update_spin_value ("button_el_selend", 
			info->status_tokens[FRAME_NUM] );	
	vj_msg(VEEJAY_MSG_INFO, "Set current frame %d as editlist ending position",
		info->status_tokens[FRAME_NUM]);
}
void	on_button_el_paste_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->status_tokens[FRAME_NUM] );
	gchar *time1 = format_time( info->status_tokens[FRAME_NUM] );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents of buffer at Frame %d (Tiemcode %s)",
		info->status_tokens[FRAME_NUM], time1);
	g_free(time1);
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
	multi_vims( VIMS_STREAM_NEW_COLOR, "%d %d %d",
		red,green,blue );

	gveejay_new_slot(MODE_STREAM);
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
		vj_msg(VEEJAY_MSG_INFO, "Resize Video Window to %dx%d", width,height);
	}

}


void	on_vims_take_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_BUNDLE_CAPTURE );
	info->uc.reload_hint[HINT_BUNDLES] = 1;
	vj_msg(VEEJAY_MSG_INFO, "Create a new bundle from FX Chain");
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
	vj_msg(VEEJAY_MSG_INFO, "Delete bundle %d from VIMS event list",
		info->uc.selected_vims_entry );
}

void	on_button_saveactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save Bundles");
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_SAVE, "%d %s",0, filename );
		vj_msg(VEEJAY_MSG_INFO, "Save Bundles and Keybindings to %s", filename );
		g_free(filename);
	} 	
}

void	on_button_loadconfigfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Load liveset / configfile");

	if(!filename)
		return;

	if( info->run_state == RUN_STATE_REMOTE )
	{
		multi_vims( VIMS_BUNDLE_FILE, "%s", filename );
	}
	else
	{
		if(config_file)
			g_free(config_file);
		config_file = g_strdup( filename );
		config_file_status = 1;	
		vj_msg(VEEJAY_MSG_INFO, "You can launch Veejay now");
	}
}

void	on_button_saveconfigfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save liveset / configfile");
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_SAVE, "%d %s", 1, filename );
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
		multi_vims( VIMS_SAMPLE_RENDER_TO, "%d %d", 0, info->uc.selected_history_entry );
		vj_msg(VEEJAY_MSG_INFO, "Rendering sample to inline EditList no. %d",
			info->uc.selected_history_entry );
	}
	info->uc.render_record=1;
}

void	on_button_historymove_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->uc.selected_history_entry > 0 )
	{
		multi_vims( VIMS_SAMPLE_RENDER_MOVE, "%d %d", 0, info->uc.selected_history_entry );
		vj_msg(VEEJAY_MSG_INFO, "Move inline Editlist %d to EditList",
			info->uc.selected_history_entry );
	}
	info->uc.reload_hint[HINT_EL] =1;
	info->uc.reload_hint[HINT_HISTORY] = 1;
}

void	on_button_clipcopy_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->selected_slot)
	{
		multi_vims( VIMS_SAMPLE_COPY , "%d",
			info->selected_slot->sample_id );
		gveejay_new_slot(MODE_SAMPLE);
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


void	on_inputstream_button_clicked(GtkWidget *widget, gpointer user_data)
{
	gint mcast = is_button_toggled( "inputstream_networktype" );
	gchar *remote_ = get_text( "inputstream_remote" );
	gint port = get_nums( "inputstream_portnum" );

	gint bw = 0;
	gint br = 0;

	gchar *remote = g_locale_from_utf8(
			remote_ , -1, &br, &bw, NULL ); 

	veejay_msg(VEEJAY_MSG_ERROR, 
		"%d, [%s], %d ~ %d", mcast,remote, port, strlen(remote) );

	remote[strlen(remote)] = '\0';

	if(bw == 0 || br == 0 || port <= 0 )
	{
		vj_msg(VEEJAY_MSG_ERROR, "You must enter a valid remote address and/or port number");
		return;
	}

	if(mcast)
		multi_vims( VIMS_STREAM_NEW_MCAST,"%d %s", port, remote );
	else
		multi_vims( VIMS_STREAM_NEW_UNICAST, "%d %s", port, remote );

	gveejay_new_slot(MODE_STREAM);	
	if(remote) g_free(remote);
	if(remote_) g_free(remote_);
}

void	on_inputstream_filebrowse_clicked(GtkWidget *w, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Open new input stream" );
	if(filename)
	{
		put_text( "inputstream_filename", filename );
		g_free(filename);
	}
}

void	on_inputstream_file_button_clicked(GtkWidget *w, gpointer user_data)
{
	gint use_y4m = is_button_toggled( "inputstream_filey4m" );
	gint use_ffmpeg = is_button_toggled( "inputstream_fileffmpeg");
	gint use_pic = is_button_toggled( "inputstream_filepixbuf");
	

	gchar *file = get_text( "inputstream_filename" );	
	gint br = 0;
	gint bw = 0;
	gchar *filename = g_locale_from_utf8( file, -1, &br , &bw, NULL );
	if( br == 0 || bw == 0 )
	{
		vj_msg(VEEJAY_MSG_ERROR, "No filename given");
		return;
	}
	if(use_y4m)
		multi_vims( VIMS_STREAM_NEW_Y4M, "%s", filename );
	if(use_ffmpeg)
		multi_vims( VIMS_STREAM_NEW_AVFORMAT, "%s", filename );
#ifdef USE_GDK_PIXBUF
	if(use_pic)
		multi_vims( VIMS_STREAM_NEW_PICTURE, "%s", filename);
#endif
	
	gveejay_new_slot(MODE_STREAM);

	if(filename) g_free( filename );
}

void	on_samplerand_toggled(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int arg = is_button_toggled( "freestyle" );
		int start = is_button_toggled( "samplerand" );

		if( start == 0 )
			single_vims( VIMS_SAMPLE_RAND_STOP );
		else
			multi_vims( VIMS_SAMPLE_RAND_START, "%d", arg );
	}
}



/* 
 * Handler to open the veejay_connection-dialog via menu
 */
void on_openConnection_activate             (GtkMenuItem     *menuitem,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *veejay_conncection_window = glade_xml_get_widget(info->main_window, "veejay_connection");
	gtk_widget_show(veejay_conncection_window);	
	} 
}


/* 
 * Handler to close the veejay_connection-dialog
 */
void on_veejay_connection_close             (GtkDialog       *dialog,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *veejay_conncection_window = glade_xml_get_widget(info->main_window, "veejay_connection");
	gtk_widget_hide(veejay_conncection_window);	
	} 
}


/* 
 * Handler to show the video_settings-dialog via menu
 */
void on_VideoSettings_activate              (GtkMenuItem     *menuitem,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *veejay_settings_window = glade_xml_get_widget(info->main_window, "video_options");
	gtk_widget_show(veejay_settings_window);	
	} 
}



/* 
 * Handler to close the video_settings-dialog 
 */
void on_video_options_close                 (GtkDialog       *dialog,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *veejay_settings_window = glade_xml_get_widget(info->main_window, "video_options");
	gtk_widget_hide(veejay_settings_window);	
	}
}


/* 
 * Handler to apply the settings of the video_settings-dialog 
 */
void on_video_options_apply_clicked         (GtkButton       *button,
	                                     gpointer         user_data)
{
	gint width = get_nums( "priout_width" );
	gint height = get_nums( "priout_height" );
	gint x = get_nums("priout_x" );
	gint y = get_nums("priout_y" );

	if( width > 0 && height > 0 )
	{
		multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			width,height,x , y );
		vj_msg(VEEJAY_MSG_INFO, "Resize Video Window to %dx%d", width,height);
	}
}

/* 
 * Handler to apply the settings of the video_settings-dialog AND close the dialog 
 */
void on_video_options_ok_clicked            (GtkButton       *button,
	                                     gpointer         user_data)
{
    on_video_options_apply_clicked(button,user_data);
    if(!info->status_lock)
	{
	GtkWidget *veejay_settings_window = glade_xml_get_widget(info->main_window, "video_options");
	gtk_widget_hide(veejay_settings_window);	
	}
}


/*
 * Handler to show the VIMS_Bundles-dialog
 */ 
void on_vims_bundles_activate               (GtkMenuItem     *menuitem,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *vims_bundles_window = glade_xml_get_widget(info->main_window, "vims_bundles");
	gtk_widget_show(vims_bundles_window);	
	} 
}


/*
 * Handler to close the VIMS_Bundles-dialog
 */ 
void on_vims_bundles_close                  (GtkDialog       *dialog,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *vims_bundles_window = glade_xml_get_widget(info->main_window, "vims_bundles");
	gtk_widget_hide(vims_bundles_window);	
	} 
}


/*
 * Handler to show the Sample-Options-dialog
 */ 
void on_samples_options_clicked             (GtkButton       *button,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *sample_options_window = glade_xml_get_widget(info->main_window, "sample_options");
	gtk_widget_show(sample_options_window);	
	vj_gui_show_sample_options();
	} 
}

/*
 * Handler to show the Sample-Options-dialog
 */ 
void on_sample_options_close                (GtkDialog       *dialog,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *sample_options_window = glade_xml_get_widget(info->main_window, "sample_options");
	gtk_widget_hide(sample_options_window);	
	} 
}


/* 
 * Handler to show the EDL and directly choose the selected sample
 */
void on_edit_sample_clicked                 (GtkButton       *button,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *edl_window = glade_xml_get_widget(info->main_window, "sample_edit");
	gtk_widget_show(edl_window);	
	} 
}


/* 
 * Handler to close the EDL 
 */
void on_sample_edit_close                   (GtkDialog       *dialog,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *edl_window = glade_xml_get_widget(info->main_window, "sample_edit");
	gtk_widget_hide(edl_window);	
	} 
}


/*
 * Handler to show the open-samples-dialog
 */ 
void on_open_samples_clicked                (GtkButton       *button,
	                                     gpointer         user_data)
{
    if(!info->status_lock)
	{
	GtkWidget *open_samples_window = glade_xml_get_widget(info->main_window, "open_samples");
	gtk_widget_show(open_samples_window);	
	} 	
}


/*
 * Handler to close the open-samples-dialog and add the selected sample/stream
 * gives the filename and the type of the desired input to vj-api.c
 */ 
void
on_open_samples_close                  (GtkDialog       *dialog,
                                        gpointer         user_data)
{
    gchar *filename = NULL;
    gint mode;

    if(!info->status_lock)
	{
	GtkWidget *open_samples_window = glade_xml_get_widget(info->main_window, "open_samples");
	gtk_widget_hide(open_samples_window);	
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (open_samples_window));
	if (filename !=NULL) 
	    { 
	    // the function gets a status flag defined in veejay/vj-global.h to know, that here is loaded an video-sample, not a stream
	    // see void vj_event_send_sample_info in vj-event.c for further informations
	    vj_gui_add_sample(filename,VJ_PLAYBACK_MODE_SAMPLE);
	    g_free(filename );
	    }
	} 
}

