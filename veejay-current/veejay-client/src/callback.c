/* gveejay - Linux VeeJay - GVeejay GTK+-2/Glade User Interface
 *           (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
 *           (C)      2006 Matthijs van Henten <matthijs.vanhenten@gmail.com>
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
#include <ctype.h>
#include <veejay/vj-msg.h>
#include <gtktimeselection.h>
#include <veejay/vims.h>
static int config_file_status = 0;
static gchar *config_file = NULL;
static int srt_locked_ = 0;
static int srt_seq_ = 0;

static int bg_[4];
static int fg_[4];
static int ln_[4];

static int sample_calctime();

static	void change_box_color_rgb( GtkWidget *box, int r, int g, int b,int a, int fill );

void	text_defaults()
{
	bg_[0] = 255; bg_[1] = 255; bg_[2] = 255; bg_[3] = 0;
	fg_[0] = 0;   fg_[1] = 0;   fg_[2] = 0;   fg_[3] = 0;
	ln_[0] = 200; ln_[1] = 255; ln_[1] = 255; ln_[3] = 0;
	srt_seq_ = 0;
}

void	on_no_caching_clicked( GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_NO_CACHING );
}

void	on_report_a_bug1_activate(GtkWidget *w, gpointer user_data )
{
	reportbug();
}
void	on_donate_activate( GtkWidget *w, gpointer user_data ) {
	donatenow();
}
void	on_button_085_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_SKIP_SECOND);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_SKIP_SECOND );
}
void	on_button_086_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PREV_SECOND );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PREV_SECOND );

}
void	on_button_080_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_FORWARD);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_FORWARD );

}
void	on_button_081_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims(VIMS_VIDEO_PLAY_BACKWARD);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_BACKWARD );
}
void	on_button_082_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PLAY_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PLAY_STOP );
}
void	on_button_083_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_SKIP_FRAME );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_SKIP_FRAME );
}
void 	on_button_084_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_PREV_FRAME );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_PREV_FRAME );
}
void	on_button_087_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_START );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_GOTO_START );
}
void	on_button_088_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_VIDEO_GOTO_END);
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_VIDEO_GOTO_END );
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

		vj_midi_learning_vims_simple( info->midi, "videobar", VIMS_VIDEO_SET_FRAME );
	}
}

void	toggle_subrender_clicked(GtkWidget *widget, gpointer user_data)
{	
	multi_vims( VIMS_SUB_RENDER,"%d",0); 
}

void	on_button_001_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SET_PLAIN_MODE );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_SET_PLAIN_MODE );
}

void	on_feedbackbutton_toggled( GtkWidget *widget, gpointer data )
{
	int val = is_button_toggled( "feedbackbutton" ) ? 1:0;
	multi_vims( VIMS_FEEDBACK, "%d", val );
}

void	on_fx_followfade_toggled( GtkWidget *widget, gpointer data )
{
	int val = is_button_toggled( "fx_followfade" ) ? 1:0;
	multi_vims( VIMS_CHAIN_FOLLOW_FADE,"%d", val );
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
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_BEZERK );
	vj_msg(VEEJAY_MSG_INFO, "Bezerk mode toggled");
}	

void	on_entry_samplename_button_clicked( GtkWidget *widget, gpointer user_data )
{
	gchar *title = get_text( "entry_samplename" );
	multi_vims( VIMS_SAMPLE_SET_DESCRIPTION, "%d %s", 0,title );

	//@ dont reload sample, print info to samplebank
	int i,j;
	for( i= 0; i < NUM_BANKS; i ++ )
	{
		for( j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
		{
			if( (info->sample_banks[i]->slot[j]->sample_id ==
			    info->status_tokens[CURRENT_ID]) ) 
			{
				gtk_frame_set_label( GTK_FRAME( info->sample_banks[i]->gui_slot[j]->frame ) , title );
				return;
			}
		}
	}
}

void	on_button_054_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *ext = get_text( "screenshotformat" );
	if(ext)
	{
		gchar filename[100];
		sprintf(filename, "frame-%d.%s", info->status_tokens[FRAME_NUM] + 1 , ext);
		gint w = get_nums("screenshot_width");
		gint h = get_nums("screenshot_height");
		multi_vims( VIMS_SCREENSHOT,"%d %d %s",w,h,filename );
		vj_msg(VEEJAY_MSG_INFO, "Requested screenshot '%s' of frame %d",
			filename, info->status_tokens[FRAME_NUM] + 1 );
	}
}
void	on_button_200_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_EFFECT_SET_BG ); 
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_EFFECT_SET_BG );
	vj_msg(VEEJAY_MSG_INFO,
		"Requested background mask of frame %d",
			info->status_tokens[FRAME_NUM] + 1 );
}
void	on_button_5_4_clicked(GtkWidget *widget, gpointer user_data)
{
	if( is_button_toggled("button_5_4") )
	{
		single_vims( VIMS_AUDIO_ENABLE );
		vj_msg(VEEJAY_MSG_INFO, "Audio is enabled");
		vj_midi_learning_vims_simple(info->midi, NULL, VIMS_AUDIO_ENABLE );
	}
	else
	{
		single_vims( VIMS_AUDIO_DISABLE );	
		vj_msg(VEEJAY_MSG_INFO, "Audio is disabled");
		vj_midi_learning_vims_simple(info->midi, NULL, VIMS_AUDIO_DISABLE );
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
	vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d",
		info->sample[0], info->sample[1]);
	gveejay_new_slot(MODE_SAMPLE);
}

void	on_button_veejay_clicked(GtkWidget *widget, gpointer user_data)
{
#ifdef STRICT_CHECKING
	//assert( info->watch.state == STATE_WAIT_FOR_USER );
#endif
	info->watch.state = STATE_CONNECT;
}
void	on_button_sendvims_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *text = get_text("vimsmessage");
	if(strncasecmp( text, "600:;",5 ) == 0)
		veejay_quit();
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'",text );
	msg_vims( text );
}
void	on_vimsmessage_activate(GtkWidget *widget, gpointer user_data)
{
	msg_vims( get_text( "vimsmessage") );
	vj_midi_learning_vims( info->midi, NULL, get_text("vimsmessage"),0);
	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'", get_text("vimsmessage"));
}

void	on_button_fadedur_value_changed(GtkWidget *widget, gpointer user_data)
{

}

void	on_button_fadeout_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	char *timenow = format_time( num, info->el.fps );
	multi_vims( VIMS_CHAIN_FADE_OUT, "0 %d", num );
	vj_midi_learning_vims_complex( info->midi, "button_fadedur", VIMS_CHAIN_FADE_OUT, 0,2 );
	vj_msg(VEEJAY_MSG_INFO, "Fade out duration %s (frames %d)",
		timenow,
		num );
	if(timenow) free(timenow);
}

void	on_button_fadein_clicked(GtkWidget *w, gpointer user_data)
{
	gint num = (gint)get_numd( "button_fadedur");
	char *timenow = format_time( num, info->el.fps );
	multi_vims( VIMS_CHAIN_FADE_IN, "0 %d", num );
	vj_midi_learning_vims_complex( info->midi, "button_fadedur", VIMS_CHAIN_FADE_IN, 0,2 );
	
	vj_msg(VEEJAY_MSG_INFO, "Fade in duration %s (frames %d)",
		timenow,
		num );
	if(timenow) free(timenow);

}

void	on_manualopacity_value_changed(GtkWidget *w, gpointer user_data)
{
	if(info->status_lock)
		return;

	gdouble val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	
	if( is_button_toggled( "loglinear" ))
	{
		double v = val;
		if( v > 0.0 )
			v = 255.0/v;
		if( v > 0.0 )
			v = log( val / 0.01 ) / log( 1.0 / 0.01 );
		val = 255.0 * v;	
	}

	multi_vims( VIMS_CHAIN_MANUAL_FADE, "0 %d",
		(int)(val));
	
	vj_midi_learning_vims_complex( info->midi, "loglinear", VIMS_CHAIN_FADE_IN, 0,1 );
	
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
	char *text = format_time( info->selection[0], info->el.fps );
	update_label_str( "label_el_startpos", text);
	free(text);
	el_selection_update();
}

void	on_button_el_selend_value_changed(GtkWidget *w, gpointer user_data)
{
	info->selection[1] = get_nums( "button_el_selend" );
	if(info->selection[1] < info->selection[0])
		update_spin_value( "button_el_selstart", info->selection[1]);
	char *text = format_time( info->selection[1], info->el.fps);
	update_label_str( "label_el_endpos", text);
	free(text);
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
		char *time1 = format_time( info->selection[0], info->el.fps );
		char *time2 = format_time( info->selection[1], info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Cut %s - %s from EditList to buffer",
			time1, time2 );
		free(time1);
		free(time2);
		info->uc.reload_hint[HINT_EL] = 1;
	}
}
void	on_button_el_del_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_DEL, "%d %d",
			info->selection[0], info->selection[1]);
		char *time1 = format_time( info->selection[0],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s from EditList",
			time1, time2 );
		free(time1);
		free(time2);
		update_spin_value( "button_el_selstart", 0 );
		update_spin_value( "button_el_selend", 0);
	}
}
void	on_button_el_crop_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_CROP, "%d %d",
			info->selection[0], info->selection[1]);
		char *total = format_time( info->status_tokens[TOTAL_FRAMES],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		char *time1 = format_time( info->selection[0],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Delete 00:00:00 - %s and %s - %s from EditList",
			time1, time2, total );
		free(time1);
		free(time2);
		free(total);

	}
}
void	on_button_el_copy_clicked(GtkWidget *w, gpointer *user_data)
{
	if(verify_selection())
	{
		multi_vims( VIMS_EDITLIST_COPY, "%d %d",
			info->selection[0], info->selection[1] );
		char *time1 = format_time( info->selection[0],info->el.fps );
		char *time2 = format_time( info->selection[1],info->el.fps );
		vj_msg(VEEJAY_MSG_INFO, "Copy %s - %s to buffer",
			time1,time2);
		free(time1);
		free(time2);
	}
}

void	on_button_el_newclip_clicked(GtkWidget *w, gpointer *user)
{
	if(verify_selection())
	{
		multi_vims( VIMS_SAMPLE_NEW, "%d %d",
			info->selection[0], info->selection[1] );
		vj_msg(VEEJAY_MSG_INFO, "New sample from EditList %d - %d" ,
			info->selection[0], info->selection[1] );
		gveejay_new_slot(MODE_SAMPLE);
	}


}

void	on_button_el_pasteat_clicked(GtkWidget *w, gpointer *user_data)
{
	gint val = get_nums( "button_el_selpaste" );
	info->selection[2] = val;
	multi_vims( VIMS_EDITLIST_PASTE_AT, "%d",
		info->selection[2]);
	char *time1 = format_time( info->selection[2],info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents from buffer to frame %d (timecode %s)",
		info->selection[2], time1);
	free(time1);
	info->uc.reload_hint[HINT_EL] = 1;

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
	gchar *filename = dialog_open_file( "Append videofile to EditList",0 );
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
	gchar *filename = dialog_open_file( "Append videofile (and create sample)",0);
	if( !filename )
		return;
	
	multi_vims( VIMS_EDITLIST_ADD_SAMPLE, "%d %s", 0, filename );
	g_free(filename);
}

void	on_button_el_delfile_clicked(GtkWidget *w, gpointer *user_data)
{
	int frame = _el_ref_start_frame( info->uc.selected_el_entry );
	int first_frame = frame;
	int last_frame = _el_ref_end_frame( info->uc.selected_el_entry );
	multi_vims( VIMS_EDITLIST_DEL, "%d %d", first_frame, last_frame );
	char *time1 = format_time( first_frame,info->el.fps );
	char *time2 = format_time( last_frame,info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Delete %s - %s",
		time1,time2);
	free(time1);
	free(time2);
} 
void	on_button_fx_clearchain_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_CLEAR, "%d",0);
	info->uc.reload_hint[HINT_CHAIN] = 1;
	info->uc.reload_hint[HINT_ENTRY] = 1;
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_CHAIN_CLEAR,0 );
	vj_msg(VEEJAY_MSG_INFO, "Clear FX Chain");
}

void	on_button_entry_toggle_clicked(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint val = is_button_toggled( "button_entry_toggle" );
		int vims_id = VIMS_CHAIN_ENTRY_SET_VIDEO_OFF;
		if(val)
			vims_id = VIMS_CHAIN_ENTRY_SET_VIDEO_ON;
		multi_vims( vims_id,"%d %d", 0, info->uc.selected_chain_entry );

		vj_midi_learning_vims_msg2( info->midi, NULL, vims_id, 0, info->uc.selected_chain_entry );
		vj_msg(VEEJAY_MSG_INFO, "Chain Entry %d is %s",
			info->uc.selected_chain_entry,
			(val ? "Enabled" : "Disabled" ));
		info->uc.reload_hint[HINT_ENTRY] = 1;
	}	
}

void	on_button_fx_entry_value_changed(GtkWidget *w, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_CHAIN_SET_ENTRY, "%d",
			(gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(w))
		);
		vj_midi_learning_vims_spin( info->midi, "fx_entry", VIMS_CHAIN_SET_ENTRY );
	}  	
}

void	on_button_fx_del_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_CHAIN_ENTRY_CLEAR, "%d %d", 0, 
		info->uc.selected_chain_entry );
	info->uc.reload_hint[HINT_ENTRY] = 1;
	info->uc.reload_hint[HINT_CHAIN] = 1;
	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_CHAIN_ENTRY_CLEAR, 0, info->uc.selected_chain_entry );
	vj_msg(VEEJAY_MSG_INFO, "Clear Effect from Entry %d",
		info->uc.selected_chain_entry);
}

#define	slider_changed( arg_num, value ) \
{\
if(!info->status_lock && !info->parameter_lock)\
	{\
info->parameter_lock = 1;\
multi_vims( VIMS_CHAIN_ENTRY_SET_ARG_VAL, "%d %d %d %d", 0, info->uc.selected_chain_entry,arg_num, value );\
vj_midi_learning_vims_fx( info->midi, arg_num, VIMS_CHAIN_ENTRY_SET_ARG_VAL, 0,info->uc.selected_chain_entry, arg_num,1 );\
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
vj_midi_learning_vims_fx( info->midi, arg_num, VIMS_CHAIN_ENTRY_SET_ARG_VAL, 0, info->uc.selected_chain_entry,arg_num,2 );\
if(info->uc.selected_rgbkey) update_rgbkey_from_slider(); \
info->parameter_lock = 0;\
}\
}


#define kf_changed( arg_num ) \
{\
if(!info->status_lock && arg_num != info->uc.selected_parameter_id)\
{\
vj_kf_select_parameter(arg_num);\
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

void	on_slider_p8_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 8, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}

void	on_slider_p9_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 9, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}

void	on_slider_p10_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 10, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p12_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 12, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p13_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 13, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p14_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 14, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
}
void	on_slider_p15_value_changed(GtkWidget *w, gpointer user_data)
{
	slider_changed( 15, (gint)GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value );
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
void	on_inc_p8_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(8, 1 , "slider_p8" );
}
void	on_dec_p8_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 8, -1, "slider_p8");
}
void	on_inc_p9_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(9, 1 , "slider_p9" );
}
void	on_dec_p9_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 9, -1, "slider_p9");
}
void	on_inc_p10_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(10, 1 , "slider_p10" );
}
void	on_dec_p10_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 10, -1, "slider_p10");
}
void	on_inc_p11_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(11, 1 , "slider_p11" );
}
void	on_dec_p11_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 11, -1, "slider_p11");
}
void	on_inc_p12_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(12, 1 , "slider_p12" );
}
void	on_dec_p12_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 12, -1, "slider_p12");
}
void	on_inc_p13_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(13, 1 , "slider_p13" );
}
void	on_dec_p13_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 13, -1, "slider_p13");
}
void	on_inc_p14_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(14, 1 , "slider_p14" );
}
void	on_dec_p14_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 14, -1, "slider_p14");
}
void	on_inc_p15_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed(15, 1 , "slider_p15" );
}
void	on_dec_p15_clicked(GtkWidget *w, gpointer user_data)
{
	param_changed( 15, -1, "slider_p15");
}

void	on_button_stoplaunch_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->watch.state == STATE_PLAYING)
	{	
		info->watch.state = STATE_DISCONNECT;
	}
}

void	on_button_sample_play_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->selection_slot)
	{
		multi_vims( VIMS_SET_MODE_AND_GO , "%d %d" ,
			info->selection_slot->sample_type,		
			info->selection_slot->sample_id );

		vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO, info->selection_slot->sample_type,
						info->selection_slot->sample_id );
	}
}
void	on_button_sample_del_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->selection_slot )	
		remove_sample_from_slot();
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

	gchar *filename = dialog_open_file( "Open samplelist",1);
	if(filename)
	{
		if(erase_all)
		{
			single_vims( VIMS_SAMPLE_DEL_ALL ); 
		}
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
	}
}
static char samplelist_name[1024];
static int  has_samplelist_name = 0;

void	on_button_samplelist_save_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save samplelist");
	if(filename)
	{
		multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
		strncpy( samplelist_name, filename,strlen(filename));
		has_samplelist_name = 1;
		g_free(filename);
	}
}

void	on_button_samplelist_qsave_clicked(GtkWidget *widget, gpointer user_data)
{
	if( has_samplelist_name == 0 ) {
		gchar *filename = dialog_save_file( "Save samplelist");
		if(filename)
		{
			multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s", filename );
			vj_msg(VEEJAY_MSG_INFO, "Saved samples to %s", filename);
			strncpy( samplelist_name, filename, strlen(filename));
			g_free(filename);
			has_samplelist_name = 1;
		}
	}
	else {
		multi_vims( VIMS_SAMPLE_SAVE_SAMPLELIST, "%s" , samplelist_name );
		vj_msg(VEEJAY_MSG_INFO, "Quick saved samples to %s" , samplelist_name );
	}
}


void	on_spin_samplestart_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		char *time1 = format_time(value,info->el.fps);
		multi_vims(VIMS_SAMPLE_SET_START, "%d %d",0, value );
		vj_msg(VEEJAY_MSG_INFO, "Set sample's starting position to %d (timecode %s)",
			value, time1);
		free(time1);
	}
}

void	on_spin_sampleend_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		char *time1 = format_time(value,info->el.fps);
		
		multi_vims(VIMS_SAMPLE_SET_END, "%d %d", 0, value );
		vj_msg(VEEJAY_MSG_INFO, "Set sample's ending position to %d (timecode %s)",
			value, time1);
		free(time1);

	}
}

void	on_slow_slider_value_changed( GtkWidget *widget, gpointer user_data )
{
	if(!info->status_lock) {
		gint value = (gint) get_slider_val("slow_slider");
		multi_vims(VIMS_VIDEO_SET_SLOW, "%d", value );	
		value ++;
		vj_msg(VEEJAY_MSG_INFO, "Slow video to %2.2f fps",	
			info->el.fps / (float) value );
		vj_midi_learning_vims_simple(info->midi, "slow_slider",VIMS_VIDEO_SET_SLOW);
	}
}


void	on_speed_slider_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) get_slider_val( "speed_slider" );
	//	value *= info->play_direction;
		multi_vims( VIMS_VIDEO_SET_SPEED, "%d", value );
			vj_msg(VEEJAY_MSG_INFO, "Change video playback speed to %d",
			value );
		vj_midi_learning_vims_simple( info->midi, "speed_slider", VIMS_VIDEO_SET_SPEED );
	}
}

void	on_spin_samplespeed_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gint value = (gint) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget) );
		value *= info->play_direction;
		multi_vims( VIMS_SAMPLE_SET_SPEED, "%d %d",0, value );
		vj_midi_learning_vims_complex( info->midi, "spin_samplespeed", VIMS_SAMPLE_SET_SPEED,0,2 );
		vj_msg(VEEJAY_MSG_INFO, "Change video playback speed to %d",
			value );
	}
}

void	on_v4l_brightness_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_BRIGHTNESS, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
		vj_midi_learning_vims_complex( info->midi, "v4l_brightness", VIMS_STREAM_SET_BRIGHTNESS, info->selected_slot->sample_id,
			1 );
	}
}

void	on_v4l_contrast_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_CONTRAST, "%d %d", 
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
		vj_midi_learning_vims_complex( info->midi, "v4l_contrast", VIMS_STREAM_SET_CONTRAST, info->selected_slot->sample_id,
			1 );

	}
}

void	on_v4l_hue_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_HUE, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
		vj_midi_learning_vims_complex( info->midi, "v4l_hue", VIMS_STREAM_SET_HUE, info->selected_slot->sample_id,
			1 );

	}
}	

void	on_v4l_white_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_WHITE, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0 ) );
		vj_midi_learning_vims_complex( info->midi, "v4l_white", VIMS_STREAM_SET_WHITE, info->selected_slot->sample_id,
			1 );

	}
}

void	on_v4l_color_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_COLOR, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	vj_midi_learning_vims_complex( info->midi, "v4l_color", VIMS_STREAM_SET_COLOR, info->selected_slot->sample_id,
			1 );

	}
}
void	on_v4l_saturation_value_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_STREAM_SET_SATURATION, "%d %d",
			info->selected_slot->sample_id,
			(gint) (GTK_ADJUSTMENT(GTK_RANGE(widget)->adjustment)->value * 65535.0) );
	vj_midi_learning_vims_complex( info->midi, "v4l_saturation", VIMS_STREAM_SET_SATURATION, info->selected_slot->sample_id,
			1 );

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

void	on_seq_rec_stop_clicked( GtkWidget *w, gpointer data )
{
	single_vims( VIMS_SAMPLE_REC_STOP );
}

void	on_rec_seq_start_clicked( GtkWidget *w, gpointer data )
{
	GtkComboBox *combo = GTK_COMBO_BOX( GTK_WIDGET(glade_xml_get_widget(info->main_window,"combo_samplecodec")));
	gchar *gformat = (gchar*)gtk_combo_box_get_active_text(combo) ;
	gchar *format = gformat;
	if(format != NULL && strlen(format) > 2)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}	
	else
	{
		format = NULL;
	}	

	multi_vims( VIMS_SAMPLE_REC_START,
		"%d %d",
		0,
		0 );

	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SAMPLE_REC_START, 0, 0 );
}

void	on_stream_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{

	gint nframes = get_nums( "spin_streamduration");
	gint autoplay = is_button_toggled("button_stream_autoplay"); 
	GtkComboBox *combo = GTK_COMBO_BOX( GTK_WIDGET(glade_xml_get_widget(info->main_window,"combo_streamcodec")));
	gchar *gformat = (gchar*)gtk_combo_box_get_active_text(combo) ;
	gchar *format = gformat;
	if(format != NULL && strlen(format) > 2)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}	
	else
	{
		format = NULL;
	}	

	multi_vims( VIMS_STREAM_REC_START,
		"%d %d",
		nframes,
		autoplay );
	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_STREAM_REC_START, nframes, autoplay );

	char *time1 = format_time( nframes,info->el.fps );
	if(format)
		vj_msg(VEEJAY_MSG_INFO, "Record in %s,  duration: %s",format, time1); 
	else
		vj_msg(VEEJAY_MSG_INFO, "Recording in default format (MJPEG) , duration: %s", time1);

	free(time1);
	g_free(format);	
}

void	on_stream_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_STREAM_REC_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_STREAM_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Stream record stop");
}

void	on_spin_streamduration_value_changed(GtkWidget *widget , gpointer user_data)
{
	gint n_frames = get_nums( "spin_streamduration" );
	char *time = format_time(n_frames,info->el.fps);
	update_label_str( "label_streamrecord_duration", time );
	free(time);
}
void 	on_new_shm_stream_clicked(GtkWidget *wid, gpointer data)
{
	char	*port = get_text("shm_text_port");
	multi_vims( VIMS_STREAM_NEW_SHARED, "%s", port );
	gveejay_new_slot(MODE_STREAM);
}

void	on_new_generator_stream_clicked(GtkWidget *wid, gpointer data)
{
	// generator_text_args optionals args
	// tree_generators get selected row
	//        VIMS_STREAM_NEW_GENERATOR
	veejay_msg(0, "implemented me");
}

void	on_shm_3490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 3490 );
	gveejay_new_slot(MODE_STREAM);
}
void	on_shm_4490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 4490 );
	gveejay_new_slot(MODE_STREAM);
}
void	on_shm_5490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 5490 );
	gveejay_new_slot(MODE_STREAM);
}
void	on_shm_6490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 6490 );
	gveejay_new_slot(MODE_STREAM);
}
void	on_shm_7490_clicked(GtkWidget *w, gpointer data)
{
	multi_vims( VIMS_STREAM_NEW_SHARED, "%d", 7490 );
	gveejay_new_slot(MODE_STREAM);
}

void	on_button_sample_recordstart_clicked(GtkWidget *widget, gpointer user_data)
{

	gint autoplay = is_button_toggled("button_sample_autoplay"); 
	GtkComboBox *combo = GTK_COMBO_BOX( GTK_WIDGET(glade_xml_get_widget(info->main_window,"combo_samplecodec")));

	gchar *format = (gchar*) gtk_combo_box_get_active_text(combo);
	gint n_frames = 0;

	gint dur_val = get_nums( "spin_sampleduration" );
	if( is_button_toggled( "sample_mulloop" ) )
	{
		int base = sample_calctime();
		n_frames = base * dur_val;
	}
	else
	{
		n_frames = dur_val;
	}

	if(format != NULL)
	{
		multi_vims( VIMS_RECORD_DATAFORMAT,"%s",
			format );
	}		

	multi_vims( VIMS_SAMPLE_REC_START,
		"%d %d",
		n_frames,
		autoplay );

	vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SAMPLE_REC_START, n_frames, autoplay );

	char *time1 = format_time(n_frames,info->el.fps);
	if( autoplay ) {
		vj_msg(VEEJAY_MSG_INFO, "Recording %s from current sample, autoplaying	 new sample when finished.",time1);
	} else {
		vj_msg(VEEJAY_MSG_INFO,"Recording %s from current sample",time1);
	}
	free(time1);
	g_free(format);	
}

void	on_button_sample_recordstop_clicked(GtkWidget *widget, gpointer user_data)
{
	single_vims( VIMS_SAMPLE_REC_STOP );
	vj_midi_learning_vims_simple( info->midi, NULL, VIMS_SAMPLE_REC_STOP );
	vj_msg(VEEJAY_MSG_INFO, "Sample record stop");
}

static	int	sample_calctime()
{
	int n_frames = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];
	if( info->status_tokens[SAMPLE_LOOP] == 2 )
		n_frames *= 2;
	if( info->status_tokens[FRAME_DUP] > 0 )
		n_frames *= info->status_tokens[FRAME_DUP];
	return n_frames;
}

void	on_spin_sampleduration_value_changed(GtkWidget *widget , gpointer user_data)
{
	// get num and display label_samplerecord_duration
	gint n_frames = sample_calctime();
	if( is_button_toggled( "sample_mulloop" ))
		n_frames *= get_nums( "spin_sampleduration" );
	else
		n_frames = get_nums( "spin_sampleduration" );
	char *time = format_time( n_frames,info->el.fps );
	update_label_str( "label_samplerecord_duration", time );
	free(time);
}

void	on_sample_mulloop_clicked(GtkWidget *w, gpointer user_data)
{
	gint n_frames = sample_calctime();
	if( is_button_toggled( "sample_mulloop" ))
		n_frames *= get_nums( "spin_sampleduration");
	char *time = format_time( n_frames,info->el.fps );
	update_label_str( "label_samplerecord_duration", time );
	free(time);
}

void	on_sample_mulframes_clicked(GtkWidget *w, gpointer user_data)
{
	gint n_frames = get_nums( "spin_sampleduration" );
	char *time = format_time( n_frames,info->el.fps );
	update_label_str( "label_samplerecord_duration", time );
	free(time);
}

void	on_spin_mudplay_value_changed(GtkWidget *widget, gpointer user_data)
{
}
void	on_check_samplefx_clicked(GtkWidget *widget , gpointer user_data)
{
	if(!info->status_lock)
	{
		int vims_id = VIMS_SAMPLE_CHAIN_DISABLE;
		
		if( is_button_toggled( "check_samplefx" ) )
			vims_id = VIMS_SAMPLE_CHAIN_ENABLE;
		multi_vims( vims_id, "%d", 0 );

		vj_midi_learning_vims_msg( info->midi, NULL, vims_id, 0 );
	}
}
void	on_check_streamfx_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int vims_id = VIMS_STREAM_CHAIN_DISABLE;
		if( is_button_toggled( "check_streamfx"))
			vims_id = VIMS_STREAM_CHAIN_ENABLE;
		multi_vims( VIMS_STREAM_CHAIN_DISABLE, "%d", 0 );	
		vj_midi_learning_vims_msg( info->midi, NULL, vims_id, 0 );
	}
}

void	on_loop_none_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0, 0 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,0 );
	}
}

void	on_loop_normal_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0, 1 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,1 );
	}
}
void	on_loop_random_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0,3 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,3 );

	}
}

void	on_loop_pingpong_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{	multi_vims( VIMS_SAMPLE_SET_LOOPTYPE,
			"%d %d", 0,2 );
		vj_midi_learning_vims_msg2(info->midi,NULL,VIMS_SAMPLE_SET_LOOPTYPE,0,2 );

	}
}
/*
void	on_check_marker_bind_clicked(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
		timeline_set_bind( info->tl, is_button_toggled("check_marker_bind"));
}*/

void	on_button_clearmarker_clicked(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_SAMPLE_CLEAR_MARKER, 0 );
	char *dur = format_time( 0,info->el.fps );
	update_label_str( "label_markerduration", dur );
	free(dur);
}


void	on_check_audio_mute_clicked(GtkWidget *widget, gpointer user_data)
{
}
void	on_button_samplelist_open_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->status_tokens[TOTAL_SLOTS] > 0 )
	{
		vj_msg(VEEJAY_MSG_WARNING, "Any existing samples will be deleted.");
	}

	gchar *filename = dialog_open_file( "Open samplelist",1);
	if(filename)
	{
		single_vims( VIMS_SAMPLE_DEL_ALL ); 
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
	}
}
void	on_button_samplelist_append_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Append a samplelist",1);
	if(filename)
	{
		multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename );
		g_free(filename );
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
	char *time1 = format_time( info->status_tokens[FRAME_NUM],info->el.fps );
	vj_msg(VEEJAY_MSG_INFO, "Paste contents of buffer at Frame %d (Tiemcode %s)",
		info->status_tokens[FRAME_NUM], time1);
	free(time1);
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
	vj_msg(VEEJAY_MSG_INFO, "Create a new bundle from FX Chain. See View -> VIMS Bundles");
}
void	on_button_key_detach_clicked(GtkWidget *widget, gpointer user)
{
#ifdef HAVE_SDL
	int key_val  = info->uc.pressed_key;
	int key_mod  = info->uc.pressed_mod;

	if( key_val > 0 )
	{
		multi_vims( 
			VIMS_BUNDLE_ATTACH_KEY,
			"1 %d %d %s",
			key_val, key_mod, "dummy" );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		char *strmod = sdlmod_by_id( key_mod );
		char *strkey = sdlkey_by_id( key_val);
		if(key_mod)
			vj_msg(VEEJAY_MSG_INFO, "Key binding %s + %s released",strmod,strkey); 
		else
			vj_msg(VEEJAY_MSG_INFO, "Key binding %s released", strkey );
	}
#endif
}

void	on_vims_key_clicked( GtkWidget *widget, gpointer user_data)
{
#ifdef HAVE_SDL
	char which_vims[128];
	sprintf(which_vims, "Press a key to bind VIMS %03d",
			info->uc.selected_vims_entry );

	int n = prompt_keydialog(
			which_vims,
			"Key combination" );

	if( n == GTK_RESPONSE_ACCEPT )
	{
		int event_id = info->uc.selected_vims_entry;
		int key_val  = info->uc.pressed_key;
		int mod	     = info->uc.pressed_mod;
		char *buf    = info->uc.selected_vims_args;

		if( event_id > 0 && key_val > 0 )
		{
			multi_vims( 
				VIMS_BUNDLE_ATTACH_KEY,
				"%d %d %d %s",
				event_id, key_val, mod, buf ? buf : "dummy" );
			info->uc.reload_hint[HINT_BUNDLES] = 1; 
			char *strmod = sdlmod_by_id( mod );
			if(strmod)
				vj_msg(VEEJAY_MSG_INFO, "VIMS %d attached to key combination %s + %s",
				event_id, strmod, sdlkey_by_id( key_val));
			else 
				vj_msg(VEEJAY_MSG_INFO, "VIMS %d attached to key %s",
					event_id, sdlkey_by_id(key_val) );
		}
	}	
#endif
}


void	on_button_vimsupdate_clicked(GtkWidget *widget, gpointer user_data)
{
	if(count_textview_buffer( "vimsview" ) > 0 )
	{
		gchar *buf = get_textview_buffer( "vimsview" );

		if( info->uc.selected_vims_type == 0 )
		{
			multi_vims( VIMS_BUNDLE_ADD, "%d %s",
				info->uc.selected_vims_entry, buf );
		}
		else
		{
			multi_vims( 
				VIMS_BUNDLE_ATTACH_KEY,
				"2 %d %d %s",
				info->uc.selected_vims_accel[1],
				info->uc.selected_vims_accel[0],
				info->uc.selected_vims_args );
		}
		info->uc.reload_hint[HINT_BUNDLES] = 1; 
	}

}

void	on_vims_clear_clicked(GtkWidget *widget, gpointer user_data)
{
	clear_textview_buffer( "vimsview" );
}

void	on_vims_delete_clicked(GtkWidget *widget, gpointer user_data)
{
	if( info->uc.selected_vims_entry >= VIMS_BUNDLE_START &&
	    info->uc.selected_vims_entry < VIMS_BUNDLE_END )
	{
		multi_vims( VIMS_BUNDLE_DEL, "%d", info->uc.selected_vims_entry );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		vj_msg(VEEJAY_MSG_INFO, "Delete bundle %d from VIMS event list",
			info->uc.selected_vims_entry );
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "VIMS %d is not a bundle.", info->uc.selected_vims_entry );
	}
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
	gchar *filename = dialog_open_file( "Load liveset / configfile",2);

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

	g_free(filename);
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
		vj_msg(VEEJAY_MSG_INFO, "If your VIMS document was valid,you'll find it in the list.");
	}
	else
	{
		vj_msg(VEEJAY_MSG_ERROR, "VIMS document is empty, type text first.");
	}
}

void	on_button_openactionfile_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Load Bundles",2 );
	if(filename)
	{
		multi_vims( VIMS_BUNDLE_FILE, "%s", filename );
		g_free( filename );
		info->uc.reload_hint[HINT_BUNDLES] = 1;
		vj_msg(VEEJAY_MSG_INFO ,"Tried to load '%s'",filename);
	}
}

static	void	load_server_files(char *buf, int len)
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "server_files");
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));	
	GtkListStore *store = GTK_LIST_STORE(model);
#ifdef STRICT_CHECKING
	assert(tree != NULL );
#endif
	int i = 0;
	int idx = 0;
	char *ptr = buf;
	while( i < len ) {
		int filelen = 0;
		char name[1024];
		char header[5];
		memset(header,0,sizeof(header));
		memset(name, 0,sizeof(name));
		strncpy(header,ptr, 4 );
		if(sscanf(header,"%04d", &filelen)==1) {
			strncpy( name, ptr+4, filelen);
			gchar *filename = _utf8str( name );
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0,filename,-1);
			gtk_tree_view_set_model(GTK_TREE_VIEW(tree),
					GTK_TREE_MODEL(store));
			idx ++;
			ptr += filelen;
		}
		ptr += 4;
		i+=4;
		i+=filelen;
	}
}

void	on_button_browse_clicked(GtkWidget *widget, gpointer user_data)
{
	// open file browser for launcher
/*	gchar *filename = dialog_open_file( "Open Videofile or EditList",0 );
	if(filename)
	{
		put_text( "entry_filename", filename );
		g_free(filename);
	}*/
	single_vims(VIMS_WORKINGDIR);

	gint len = 0;
	gchar *test = recv_vims( 8, &len );

	if(!test || len <= 0 ) {
		return ;
	}

	reset_tree( "server_files");

	load_server_files( test,len);
free(test);
}

void	on_button_clipcopy_clicked(GtkWidget *widget, gpointer user_data)
{
	if(info->selection_slot)
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

	gsize bw = 0;
	gsize br = 0;

	if(!remote_)
	{
		error_dialog("Error", "Not a valid hostname. Try 'localhost' or '127.0.0.1'");
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		return;
	}

	gchar *remote = g_locale_from_utf8(
			remote_ , -1, &br, &bw, NULL ); 

	if( !remote || strlen(remote) <= 1 )
	{
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		error_dialog("Error", "Not a valid hostname. Try 'localhost' or '127.0.0.1'");
		return;
	}

	remote[strlen(remote)] = '\0';

	if(bw == 0 || br == 0 || port <= 0 )
	{
		GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
		gtk_widget_hide( dialog );
		error_dialog("Error", "You must enter a valid remote address and/or port number");
		return;
	}


	if(mcast)
		multi_vims( VIMS_STREAM_NEW_MCAST,"%d %s", port, remote );
	else
		multi_vims( VIMS_STREAM_NEW_UNICAST, "%d %s", port, remote );


	gveejay_new_slot(MODE_STREAM);	

	if(remote) g_free(remote);

	GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputstream_window" );
	gtk_widget_hide( dialog );

}

void	on_inputstream_filebrowse_clicked(GtkWidget *w, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Open new input stream",3 );
	if(filename)
	{
		put_text( "inputstream_filename", filename );
		g_free(filename);
	}
}
/*
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
		error_dialog("Error", "Please enter a filename");
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
}*/

void	on_samplerand_toggled(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		int arg = is_button_toggled( "freestyle" );
		int start = is_button_toggled( "samplerand" );

		int vims_id = VIMS_SAMPLE_RAND_START;
		if( start == 0 )
			vims_id = VIMS_SAMPLE_RAND_STOP;

		if( vims_id == VIMS_SAMPLE_RAND_START )
		{
			multi_vims( vims_id,"%d", arg );
			vj_midi_learning_vims_msg( info->midi,NULL, vims_id, arg );
		}
		else
		{
			single_vims( vims_id );
			vj_midi_learning_vims_simple(info->midi, NULL, vims_id );
		}

		vj_msg(VEEJAY_MSG_INFO, "You should restart the sample randomizer now.");

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
	if( info->watch.state == STATE_PLAYING)
	{	
		info->watch.state = STATE_DISCONNECT;

		GtkWidget *w = glade_xml_get_widget_(info->main_window, "veejay_connection" );
		gtk_widget_show( w );
	}
	else 
		gveejay_quit(NULL,NULL);
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


void	on_image_calibration1_activate	(GtkMenuItem	*menuitem, gpointer data)
{
	GtkWidget *win = glade_xml_get_widget(info->main_window,"calibration_window" );
	gtk_widget_show(win);
	cali_onoff = 1;
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

void	on_cali_save_button_clicked( GtkButton *button, gpointer user_data)
{
	gchar *filename = dialog_save_file( "Save calibration to file");
	if( filename ) {
		multi_vims( VIMS_V4L_CALI, "%s", filename );
	}
}

void	on_load_calibration1_activate( GtkMenuItem     *menuitem,
					     gpointer         user_data)
{
	gchar	*filename = dialog_open_file("Select calibration file to load",0);
	if(filename)
	{
		multi_vims( VIMS_STREAM_NEW_CALI, "%s", filename );
		vj_msg(VEEJAY_MSG_INFO ,"Loaded calibration file %s",filename);
		g_free(filename);
	
		gveejay_new_slot(MODE_STREAM);
	}
}

void	on_cali_take_button_clicked(	GtkButton *button, gpointer data )
{
	gint method = 0;
	gint kernel = 0;

	if( info->uc.cali_stage == 1 )
		method = 1;

	if( info->uc.cali_duration > 0 ) {
		error_dialog( "Error", "Already taking calibration images");
		return;
	}

	if( is_button_toggled( "cali_method_median" ))
	{
		kernel = get_nums( "cali_kernelsize_spin");
	}
	gint duration=get_nums( "cali_duration_spin" );

	if( cali_stream_id <= 0 )  {
		if(info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX )
			cali_stream_id =
				info->status_tokens[CURRENT_ID];
	}

	if( cali_stream_id <= 0 ) {
		error_dialog( "Error", "No source selected to calibrate. Play a Live stream or double click one in the List");
		
		return;
	}

	multi_vims( VIMS_V4L_BLACKFRAME, "%d %d %d %d",
			cali_stream_id,
			duration,
			kernel,
			method );

	info->uc.cali_duration = duration;

	//@ substract duration in status pipe
	
}

void	on_cali_darkframe_clicked( GtkButton *button, gpointer data ) 
{
	get_and_draw_frame( 0, "image_darkframe" );
}

void	on_cali_lightframe_clicked( GtkButton *button, gpointer data ) 
{
	get_and_draw_frame( 1, "image_lightframe" );
}

void	on_cali_flatframe_clicked( GtkButton *button, gpointer data )
{
	get_and_draw_frame( 2, "image_flatframe" );
}

void	on_cali_image_clicked( GtkButton *button, gpointer data )
{

//	get_and_draw_frame( 0, "image_darkframe" );
//	get_and_draw_frame( 1, "image_lightframe");
//	get_and_draw_frame( 2, "image_flatframe");

}

void	on_cali_reset_button_clicked( 	GtkButton *button, gpointer data )
{
	if( cali_stream_id <= 0 ) {
		if(info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX )
			cali_stream_id =
				info->status_tokens[CURRENT_ID];
	}

	if( cali_stream_id <= 0 ) {
		error_dialog( "Error", "No source selected to calibrate. Play a Live stream or double click one in the List");
		
		return;
	}

	info->uc.cali_stage = 0;
	update_label_str("current_step_label","Please take an image with the cap on the lens.");

	multi_vims( VIMS_V4L_BLACKFRAME, "%d 0 0 0", cali_stream_id );

	reset_cali_images(0, "image_darkframe");
	reset_cali_images(1, "image_lightframe");
	reset_cali_images(2, "image_flatframe");

	GtkWidget *tb = glade_xml_get_widget_( info->main_window, "cali_take_button");
	gtk_button_set_label( GTK_BUTTON(tb), "Take Black Frames");

	disable_widget( "cali_save_button" );

}

/*
 * Handler to show the VIMS_Bundles-dialog
 */ 
void on_vims_bundles_activate               (GtkMenuItem     *menuitem,
					     gpointer         user_data)
{
	GtkWidget *vims_bundles_window = glade_xml_get_widget(info->main_window, "vims_bundles");
	gtk_widget_show(vims_bundles_window);	
}


/*
 * Handler to close the VIMS_Bundles-dialog
 */ 
void on_vims_bundles_close                  (GtkDialog       *dialog,
					     gpointer         user_data)
{
	GtkWidget *vims_bundles_window = glade_xml_get_widget(info->main_window, "vims_bundles");
	gtk_widget_hide(vims_bundles_window);	
}

/* Menu entries */
void	on_quit1_activate( GtkWidget *w, gpointer user_data )
{
	gveejay_quit(NULL,NULL);
}
/* depending on the state, we either load an action file or a sample list !*/
void	on_open2_activate( GtkWidget *w, gpointer user_data)
{
	gchar *filename = NULL;
	switch( info->watch.state )
	{
		case STATE_STOPPED:
			filename = dialog_open_file( "Open Action file / Liveset",2 );
			if(filename)	
			{
				if(config_file)
				    g_free(config_file);
				config_file = g_strdup( filename );
				config_file_status = 1; 
				g_free(filename);
			}
			break;
		case STATE_PLAYING:
			filename = dialog_open_file( "Open Samplelist ",1);
			if(filename)
			{
				single_vims( VIMS_SAMPLE_DEL_ALL );
				multi_vims( VIMS_SAMPLE_LOAD_SAMPLELIST, "%s", filename);
				g_free(filename);
			}
			break;
		default:
			vj_msg(VEEJAY_MSG_INFO, "Invalid state !");
			break;
	}
}
void	on_save1_activate( GtkWidget *w, gpointer user_data )
{
	if(info->watch.state == STATE_PLAYING)
		on_button_samplelist_save_clicked( NULL, NULL );
	else
		vj_msg(VEEJAY_MSG_ERROR, "Nothing to save (start or connect to veejay first)");
}
void	on_about1_activate(GtkWidget *widget, gpointer user_data)
{
	about_dialog();	
}
void	on_new_input_stream1_activate(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputdialog" );
	gtk_dialog_run( GTK_DIALOG( dialog ));
	gtk_widget_hide( dialog );
}

void	on_istream_cancel_clicked(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *dialog = glade_xml_get_widget_( info->main_window, "inputdialog" );
	gtk_widget_hide( dialog );
}

void	on_curve_togglerun_toggled(GtkWidget *widget , gpointer user_data)
{
}

void	on_stream_length_value_changed( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	multi_vims( VIMS_STREAM_SET_LENGTH, "%d", get_nums("stream_length") );
}

int	on_curve_buttontime_clicked()
{
	return 1;
}


void	on_framerate_inc_clicked( GtkWidget *w, gpointer data )
{
	double cur = get_slider_val( "framerate" );
	cur += 1.0;
	update_slider_value( "framerate", (int) cur, 0 );
}

void    on_framerate_dec_clicked( GtkWidget *w, gpointer data )
{
        double cur = get_slider_val( "framerate" );
        cur -= 1.0;
        update_slider_value( "framerate", (int) cur, 0 );
}


void	on_frameratenormal_clicked( GtkWidget *w, gpointer data )
{
	update_slider_value( "framerate", (int) info->el.fps,0 );
}

void	on_framerate_value_changed( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;

	gdouble slider_val = GTK_ADJUSTMENT(GTK_RANGE(w)->adjustment)->value;
	int value = (int)(100.0 * slider_val);
	multi_vims( VIMS_FRAMERATE, "%d", value );
	vj_midi_learning_vims_simple( info->midi, "framerate", VIMS_FRAMERATE );
}

void	on_sync_correction_clicked( GtkWidget *w, gpointer data )
{
	int status = is_button_toggled( "sync_correction" );

	multi_vims( VIMS_SYNC_CORRECTION, "%d", status );
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_SYNC_CORRECTION, status );
}

void	on_curve_clear_parameter_clicked( GtkWidget *widget, gpointer user_data ) 
{
	multi_vims( VIMS_SAMPLE_KF_CLEAR, "%d %d", info->uc.selected_chain_entry, info->uc.selected_parameter_id  );
	info->uc.reload_hint[HINT_KF] = 1;
}

void	on_curve_buttonstore_clicked(GtkWidget *widget, gpointer user_data )
{
	int i = info->uc.selected_chain_entry;
	int j = info->uc.selected_parameter_id;
	int id = info->uc.entry_tokens[ENTRY_FXID];

	int end = get_nums( "curve_spinend" );
	int start = get_nums( "curve_spinstart" );

	const int length = end - start + 1;


	if( (length) <= 0 || id <= 0 )	
	{
		if( id <= 0 )
			vj_msg(VEEJAY_MSG_INFO, "No FX set on entry %d",i);
		else
			vj_msg(VEEJAY_MSG_INFO, "Length of animation is 0");
		return;
	}
	
	GtkWidget *curve = glade_xml_get_widget_( info->main_window, "curve");

	int type = 0;
	if( is_button_toggled( "curve_typelinear" ) ) {
		type = 0; //GTK_CURVE_TYPE_LINEAR;
	} else if ( is_button_toggled( "curve_typespline" ) ) {
		type = 1; //GTK_CURVE_TYPE_SPLINE;
	} else if ( is_button_toggled( "curve_typefreehand" ) ) {
		type = 2; //GTK_CURVE_TYPE_FREE;
	}
	
	int min=0,max=0;
	float *data = (float*) vj_calloc( sizeof(float) * length );

	_effect_get_minmax( id, &min, &max,j );

	get_points_from_curve( curve, length, data );

	char header[34];
	
	int msg_len = 25 + (4*length); /*K00000000 */

	snprintf(header,sizeof(header), "K%08dkey%02d%02d%08d%08d%02d",msg_len,i,j,start,end,type );
	
	unsigned char *buf = (unsigned char*) vj_calloc( sizeof(unsigned char) * msg_len + 9 );
	strncpy( (char*) buf, header, sizeof(header));
	
	unsigned char *ptr = buf + sizeof(header);
	int k;
	for( k = 0 ; k < length ; k++ ) {
		int pval = (int) ( (float) min + ( data[k] * ((float) max) ));
		ptr[0] = pval & 0xff;
		ptr[1] = (pval >> 8) & 0xff;
		ptr[2] = (pval >> 16) & 0xff;
		ptr[3] = (pval >> 24) & 0xff;

		ptr += 4;
		veejay_msg( VEEJAY_MSG_DEBUG, "(%d/%d) FX set value %d on frame %d",k,length,pval, start+k );
	}

	vj_client_send_buf( info->client, V_CMD, buf, msg_len + 9  );

	vj_msg( VEEJAY_MSG_INFO, "Saved new animation for parameter %d on entry %d, start at frame %d and end at frame %d",j,i,start,end );
				
	free(buf);
	free(data);
}

void	on_curve_buttonclear_clicked(GtkWidget *widget, gpointer user_data)
{
	gint id = info->status_tokens[ENTRY_FXID];
	if( id < 0 )
		id = 0;
	int i = info->uc.selected_chain_entry;
	GtkWidget *curve = glade_xml_get_widget_( info->main_window, "curve");
	reset_curve( curve ); 

	multi_vims( VIMS_SAMPLE_KF_RESET, "%d", i );
}

void	on_curve_typelinear_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("curve_typelinear"))
	{
		sample_slot_t *s = info->selected_slot;
		if(!s)
			return;
		GtkWidget *curve = glade_xml_get_widget_( info->main_window, "curve");
		set_points_in_curve( GTK_CURVE_TYPE_LINEAR, curve );
	}
}	
void	on_curve_typespline_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("curve_typespline"))
	{
		sample_slot_t *s = info->selected_slot;
		if(!s)
			return;
		GtkWidget *curve = glade_xml_get_widget_( info->main_window, "curve");
		set_points_in_curve( GTK_CURVE_TYPE_SPLINE, curve );
	}
}	
void	on_curve_typefreehand_toggled(GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
	if( is_button_toggled("curve_typefreehand"))
	{
		sample_slot_t *s = info->selected_slot;
		if(!s)
			return;
		GtkWidget *curve = glade_xml_get_widget_( info->main_window, "curve");
		set_points_in_curve( GTK_CURVE_TYPE_FREE, curve );
	}

}

void	on_curve_toggleentry_toggled( GtkWidget *widget, gpointer user_data)
{
	int k = is_button_toggled( "curve_toggleentry" );
	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(glade_xml_get_widget_(
					info->main_window, "tree_chain") ));

	gtk_tree_model_foreach(
			model,
			chain_update_row, (gpointer*) info );

	info->uc.reload_hint[HINT_ENTRY] = 1;
	if(info->status_lock)
		return;

	int i = info->uc.selected_chain_entry;
	int type = 0;
	if(  is_button_toggled("curve_typespline")) {
		type = 1;
	} else if ( is_button_toggled("curve_typefreehand")) {
		type = 2;
	} else if (is_button_toggled("curve_typelinear")) {
		type = 0;
	}

	multi_vims( VIMS_SAMPLE_KF_STATUS, "%d %d %d", i, k,type );
}

void	on_kf_p0_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;
	if(is_button_toggled("kf_p0"))
		kf_changed( 0 );
}
void	on_kf_p1_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p1"))
		kf_changed( 1 );
}
void	on_kf_p2_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p2"))
		kf_changed( 2 );
}
void	on_kf_p3_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p3"))
		kf_changed( 3 );
}
void	on_kf_p4_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p4"))
		kf_changed( 4 );
}
void	on_kf_p5_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p5"))
		kf_changed( 5 );
}
void	on_kf_p6_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p6"))
		kf_changed( 6 );
}
void	on_kf_p7_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p7"))
		kf_changed( 7 );
}
void	on_kf_p8_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p8"))
		kf_changed( 8 );
}
void	on_kf_p9_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p9"))
		kf_changed( 9 );
}
void	on_kf_p10_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p10"))
		kf_changed( 10 );
}

void	on_kf_p11_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p11"))
		kf_changed( 11 );
}

void	on_kf_p12_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p12"))
		kf_changed( 12 );
}


void	on_kf_p13_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p13"))
		kf_changed( 13 );
}


void	on_kf_p14_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p14"))
		kf_changed( 14 );
}

void	on_kf_p15_toggled( GtkWidget *widget, gpointer user_data)
{
	if(info->status_lock)
		return;

	if( is_button_toggled("kf_p15"))
		kf_changed( 15 );
}

void	on_curve_toggleglobal_toggled(GtkWidget *widget, gpointer user_data)
{
}
void	on_button_videobook_clicked(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *n = glade_xml_get_widget_( info->main_window, "videobook" );
	// set page 1 from notebook panels
	gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
	if(page == 1 )
		gtk_notebook_prev_page(GTK_NOTEBOOK(n) );
	if(info->selected_slot)	
	{
		/* Only if we are not playing it */
		if(info->status_tokens[PLAY_MODE] != 
			info->selected_slot->sample_type &&
		   info->status_tokens[CURRENT_ID] !=
			info->selected_slot->sample_id )
		multi_vims( VIMS_SET_MODE_AND_GO, "%d %d",
			info->selected_slot->sample_type,
			info->selected_slot->sample_id );
		vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO,
				info->selected_slot->sample_type,
				info->selected_slot->sample_id );
	}
}

void	on_samplepage_clicked(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *m = glade_xml_get_widget_(info->main_window , "notebook18");
	gtk_notebook_set_page( GTK_NOTEBOOK(m), 5 );

	GtkWidget *n = glade_xml_get_widget_( info->main_window, "panels" );

	gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
	
	gint page_needed = 2;

	switch( info->status_tokens[PLAY_MODE] )
	{
		case MODE_SAMPLE:
			page_needed =0 ; break;
		case MODE_STREAM:
			page_needed = 1; break;
		case MODE_PLAIN:
			page_needed = 2; break;
		default:
			break;
	}	
	
	if( page_needed != page )
		gtk_notebook_set_page(
				GTK_NOTEBOOK(n),
				page_needed );
}

void	on_timeline_cleared(GtkWidget *widget, gpointer user_data)
{
	multi_vims( VIMS_SAMPLE_CLEAR_MARKER, "%d", 0 );
	vj_midi_learning_vims_msg( info->midi, NULL, VIMS_SAMPLE_CLEAR_MARKER, 0 );
}

void	on_timeline_bind_toggled( GtkWidget *widget, gpointer user_data)
{
//	gboolean toggled = timeline_get_bind( TIMELINE_SELECTION(widget)) ;
//	set_toggle_button( "check_marker_bind", (toggled ? 1 :0) );
}

void	on_timeline_value_changed( GtkWidget *widget, gpointer user_data )
{
	if(!info->status_lock)
	{
		gdouble pos = timeline_get_pos( TIMELINE_SELECTION(widget) );
		multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (gint)pos );
	}
}

void	on_timeline_out_point_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{

		gdouble pos1 = timeline_get_in_point( TIMELINE_SELECTION(widget) );
		gdouble pos2 = timeline_get_out_point( TIMELINE_SELECTION(widget) );
		pos1 *= info->status_tokens[TOTAL_FRAMES];
		pos2 *= info->status_tokens[TOTAL_FRAMES];
		if(pos2 > pos1 )
		{
			multi_vims( VIMS_SAMPLE_SET_MARKER , "%d %d %d", 0,(gint) pos1, (gint) pos2 );
			char *dur = format_time( pos2 - pos1,info->el.fps );
			update_label_str( "label_markerduration", dur );
			free(dur);
		}
		else
			vj_msg(VEEJAY_MSG_INFO, "Set Out point after In point !");
		

	}
}

void	on_timeline_in_point_changed(GtkWidget *widget, gpointer user_data)
{
	if(!info->status_lock)
	{
		gdouble pos1 = timeline_get_in_point( TIMELINE_SELECTION(widget) );
		gdouble pos2 = timeline_get_out_point( TIMELINE_SELECTION(widget) );
		pos1 *= info->status_tokens[TOTAL_FRAMES];
		pos2 *= info->status_tokens[TOTAL_FRAMES];
		if(pos1 < pos2 )
		{
			multi_vims( VIMS_SAMPLE_SET_MARKER , "%d %d %d", 0, (gint) pos1, (gint) pos2 );
			char *dur = format_time( pos2 - pos1,info->el.fps );
			update_label_str( "label_markerduration", dur );
			free(dur);
		}
		else
			vj_msg(VEEJAY_MSG_INFO,"Set In Point before Out Point !");
	}
}

void	on_sampleadd_clicked(GtkWidget *widget, gpointer user_data)
{
	gchar *filename = dialog_open_file( "Add videofile as new sample",0 );
	if(filename)
	{
		multi_vims( VIMS_EDITLIST_ADD_SAMPLE, "%d %s", 0, filename );
		g_free(filename);
	}
}

void	on_streamnew_clicked(GtkWidget *widget, gpointer user_data)
{
	// inputstream_window
	GtkWidget *w = glade_xml_get_widget(info->main_window, "inputstream_window");
	scan_devices( "tree_v4ldevices" );
	gtk_widget_show(w);
	
}

void	on_inputstream_close_clicked(GtkWidget *w,  gpointer user_data)
{
	GtkWidget *wid = glade_xml_get_widget(info->main_window, "inputstream_window");
	gtk_widget_hide(wid);	
}

void 	on_button_sdlclose_clicked(GtkWidget *w, gpointer user_data)
{
	multi_vims( VIMS_RESIZE_SDL_SCREEN, "%d %d %d %d",
			0,0,0,0 );

}


void	on_quicklaunch_clicked(GtkWidget *widget, gpointer user_data)
{
/*	if( info->watch.state == STATE_STOPPED )
	{ 
		vj_fork_or_connect_veejay( config_file );
	}
*/	
}

static void _update_vs()
{
	if( info->config.norm == 0 )
	{
		update_spin_value( "vs_size0", 720 );
		update_spin_value( "vs_size1", 576 );
		gtk_spin_button_set_value (
			GTK_SPIN_BUTTON( glade_xml_get_widget_( info->main_window, "vs_fps" ) ), 25.0 );
	}
	else
	{
		update_spin_value( "vs_size0", 720 );
		update_spin_value( "vs_size1", 480 );
		gtk_spin_button_set_value (
			GTK_SPIN_BUTTON( glade_xml_get_widget_( info->main_window, "vs_fps" ) ), 29.97 );
	}
	set_toggle_button( "vs_custom", 0 );
}

void	on_vs_pal_toggled( GtkWidget *w, gpointer user_data )
{
	info->config.norm = is_button_toggled( "vs_pal" ) ? 0 : 1;
	_update_vs();
}
void	on_vs_ntsc_toggled( GtkWidget *w , gpointer user_data)
{
	info->config.norm = is_button_toggled( "vs_ntsc" ) ? 1 :0;
	_update_vs();
}
void	on_vs_custom_toggled( GtkWidget *w, gpointer user_data)
{
	if(is_button_toggled( "vs_custom" ))
		enable_widget( "vs_frame");
	else
		disable_widget( "vs_frame");
}

static void _rgroup_audio(void)
{
	if(is_button_toggled( "vs_noaudio"))
		info->config.audio_rate = 0;
	else
	{
		if(is_button_toggled("vs_audio44") )
			info->config.audio_rate = 48000;
		else
			info->config.audio_rate = 44000;
	}
}

void	on_vs_audio48_toggled(GtkWidget *w, gpointer user_data)
{
	_rgroup_audio();
}

void	on_vs_audio44_toggled(GtkWidget *w, gpointer user_data)
{
	_rgroup_audio();
}
void	on_vs_noaudio_toggled(GtkWidget *w, gpointer user_data)
{
	_rgroup_audio();
}
void	on_vs_avsync_toggled(GtkWidget *w, gpointer user_data)
{
	info->config.sync = is_button_toggled( "vs_avsync" );
}
void	on_vs_avtimer_toggled(GtkWidget *w, gpointer user_data)
{
	info->config.timer = is_button_toggled( "vs_avtimer" );
}
void	on_vs_deinter_toggled(GtkWidget *w, gpointer user_data)
{
	info->config.deinter = is_button_toggled( "vs_deinter" );
}
void	on_vs_yuv420_toggled( GtkWidget *w , gpointer user_data)
{
	if( is_button_toggled( "vs_yuv420" )) 
		info->config.pixel_format = 0;
}
void	on_vs_yuv422_toggled( GtkWidget *w, gpointer user_data)
{
	if( is_button_toggled("vs_yuv422") )
		info->config.pixel_format = 1;
}
void	on_vs_sample0_toggled( GtkWidget *w , gpointer user_data)
{
	if( is_button_toggled("vs_sample0"))
		info->config.sampling = 1;
}
void	on_vs_sample1_toggled( GtkWidget *w, gpointer user_data)
{
	if( is_button_toggled("vs_sample1"))
		info->config.sampling = 0;
}
void	on_vs_size0_value_changed(GtkWidget *w, gpointer user_data)
{
	info->config.w = get_nums( "vs_size0");
}
void	on_vs_size1_value_changed(GtkWidget *w, gpointer user_data)
{
	info->config.h = get_nums( "vs_size1");
}
void	on_vs_fps_value_changed(GtkWidget *w, gpointer user_data)
{
	info->config.fps = get_numd( "vs_fps");
}
void	on_vs_close_clicked( GtkWidget *w, gpointer user_data)
{
	GtkWidget *vs = glade_xml_get_widget(info->main_window, "vs");
	gtk_widget_hide(vs);	
}

void	on_vs_mcastosc_toggle_toggled( GtkWidget *w, gpointer user_data)
{
	info->config.osc = is_button_toggled( "vs_mcastosc_toggle" );
	if(info->config.osc)
	{
		if(info->config.mcast_osc)
			g_free( info->config.mcast_osc );
		info->config.mcast_osc = get_text( "vs_mcastvims" );
	}

}
void	on_vs_mcastvims_toggle_toggled(GtkWidget *w, gpointer user_data)
{
	info->config.vims = is_button_toggled( "vs_mcastvims_toggle" );
	if(info->config.vims)
	{
		if(info->config.mcast_vims)
			g_free( info->config.mcast_vims );
		info->config.mcast_vims = get_text( "vs_mcastvims" );
	}
}

void	on_vs_mcastosc_changed( GtkWidget *w, gpointer user_data)
{
//	if(info->config.mcast_osc)
//		g_free(info->config.mcast_osc);
//	info->config.mcast_osc = get_text( "vs_mcastosc" );
}
void	on_vs_mcastvims_changed( GtkWidget *w, gpointer user_data)
{
//	if(info->config.mcast_vims)
//		g_free(info->config.mcast_vims);
//	info->config.mcast_vims = get_text( "vs_mcastvims" );
}

void	on_inputstream_window_delete_event(GtkWidget *w, gpointer user_data)
{
	GtkWidget *vs = glade_xml_get_widget(info->main_window, "inputstream_window");
	gtk_widget_hide(vs);
}

void	on_calibration_window_delete_event(GtkWidget *w,gpointer data)
{
	GtkWidget *win = glade_xml_get_widget(info->main_window, "calibration_window");
	cali_onoff = 0;
	gtk_widget_hide(win);
}


void	on_vs_delete_event( GtkWidget *w, gpointer user_data)
{
	GtkWidget *vs = glade_xml_get_widget(info->main_window, "vs");
	gtk_widget_hide(vs);	
}
void	on_configure1_activate( GtkWidget *w, gpointer user_data)
{
	GtkWidget *vs = glade_xml_get_widget(info->main_window, "vs");
/* load options from config */

	update_spin_value( "vs_size0", info->config.w );
	update_spin_value( "vs_size1", info->config.h );	
	update_spin_value( "vs_fps",   info->config.fps );
	
	set_toggle_button( "vs_avsync", info->config.sync );	
	set_toggle_button( "vs_avtimer", info->config.timer );
	set_toggle_button( "vs_deinter", info->config.deinter );
	if(info->config.pixel_format == 0)
		set_toggle_button( "vs_yuv420", 1 );
	else
		set_toggle_button( "vs_yuv422", 1 );

	if(info->config.sampling == 1 )
		set_toggle_button( "vs_sample0", 1 );
	else
		set_toggle_button( "vs_sample1", 1 );
	if(info->config.norm == 0 && info->config.w == 720 && info->config.h == 576 )
		set_toggle_button( "vs_pal", 1 );
	else
	{
		if(info->config.norm == 1 && info->config.w == 720 && info->config.h == 480 )
			set_toggle_button( "vs_ntsc", 1 );
		else
			set_toggle_button( "vs_custom", 1 );
	}
	
	if( is_button_toggled( "vs_custom" ))
		enable_widget( "vs_frame" );
	else	
		disable_widget( "vs_frame" );

	if( info->config.audio_rate == 0 )
		set_toggle_button( "vs_noaudio" , 1 );
	else
	{
		if( info->config.audio_rate == 44000 )
			set_toggle_button( "vs_audio44" , 1 );
		else
			set_toggle_button( "vs_audio48", 1 );
	}

	/* set osc , vims mcast */
	if(info->config.mcast_osc)
		put_text( "vs_mcastosc", info->config.mcast_osc );
	if(info->config.mcast_vims)
		put_text( "vs_mcastvims", info->config.mcast_vims );
	set_toggle_button( "vs_mcastosc_toggle", info->config.osc );
	set_toggle_button( "vs_mcastvims_toggle", info->config.vims );

	gtk_widget_show(vs);	
}

void	on_quit_veejay1_activate( GtkWidget *w, gpointer user_data)
{
	veejay_quit();
}

void	on_curve_spinend_value_changed(GtkWidget *w, gpointer user_data)
{
	int end_pos = get_nums( "curve_spinend" );
	char *end_time = format_time(
			end_pos,info->el.fps );
	update_label_str( "curve_endtime", end_time );
	free(end_time);
}

void	on_curve_spinstart_value_changed(GtkWidget *w, gpointer user_data)
{
	int start_pos = get_nums( "curve_spinstart" );

	char *start_time = format_time(
			start_pos,info->el.fps );
	update_label_str( "curve_endtime", start_time );
	free(start_time);
}

void	on_veejayevent_enter_notify_event(GtkWidget *w, gpointer user_data)
{
	info->key_now = TRUE;
}
void	on_veejayevent_leave_notify_event(GtkWidget *w , gpointer user_data)
{
	info->key_now = FALSE;
}

void 	on_spin_framedelay_value_changed(GtkWidget *w, gpointer user_data)
{
	if( info->status_lock )
		return;

	multi_vims(VIMS_VIDEO_SET_SLOW, "%d", get_nums("spin_framedelay"));

	vj_midi_learning_vims_spin( info->midi, "framedelay", VIMS_VIDEO_SET_SLOW );
}

void	on_mixing_effects_toggled(GtkWidget *w, gpointer user_data)
{
	 GtkWidget *n = glade_xml_get_widget_( info->main_window, "effectspanel" );
	 gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
         if(page == 1)
		gtk_notebook_prev_page( GTK_NOTEBOOK(n) );
       
}

void	on_image_effects_toggled(GtkWidget *w, gpointer user_data)
{
	 GtkWidget *n = glade_xml_get_widget_( info->main_window, "effectspanel" );
	 gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK(n) );
         if(page == 0)
		gtk_notebook_next_page( GTK_NOTEBOOK(n) );

}

void	on_console1_activate(GtkWidget *w, gpointer user_data)
{
	GtkWidget *n = glade_xml_get_widget_( info->main_window, "panels" );
	gint page = gtk_notebook_get_current_page( GTK_NOTEBOOK( n ) );

	if( page == MODE_PLAIN )
		gtk_notebook_set_page( GTK_NOTEBOOK(n),
				info->status_tokens[PLAY_MODE] );
	else
		gtk_notebook_set_page( GTK_NOTEBOOK(n),
				MODE_PLAIN );
}

gboolean	on_entry_hostname_focus_in_event( GtkWidget *w, gpointer user_data)
{
	update_label_str( "runlabel", "Connect");
	return FALSE;
}

gboolean	on_entry_hostname_focus_out_event( GtkWidget *w, gpointer user_data)
{
//	update_label_str( "runlabel", "Run" );
	return FALSE;
}


gboolean 	on_entry_filename_focus_in_event( GtkWidget *w, gpointer user_data)
{
//	update_label_str( "runlabel", "Run" );
	return FALSE;
}

void		on_previewbw_toggled( GtkWidget *w , gpointer user_data)
{
	single_vims( VIMS_PREVIEW_BW );
	

}

void		on_previewtoggle_toggled(GtkWidget *w, gpointer user_data)
{
	multitrack_toggle_preview( info->mt, -1, is_button_toggled("previewtoggle"),glade_xml_get_widget(info->main_window, "imageA") );
}

void		on_previewspeed_value_changed( GtkWidget *widget, gpointer user_data)
{
}

void		on_previewscale_value_changed( GtkWidget *widget, gpointer user_data)
{
}

void		on_preview_width_value_changed( GtkWidget *w, gpointer user_data)
{
}
void		on_preview_height_value_changed( GtkWidget *w, gpointer user_data)
{
}

void	on_mt_new_activate( GtkWidget *w, gpointer user_data)
{
	multitrack_add_track( info->mt );
}

void	on_mt_delete_activate( GtkWidget *w, gpointer user_data)
{
	multitrack_close_track( info->mt );
}



void	on_mt_sync_start_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_start( info->mt );
}

void	on_mt_sync_stop_clicked( GtkWidget *w , gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_STOP,0 );
}
void	on_mt_sync_play_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_FORWARD,0 );
}
void	on_mt_sync_backward_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PLAY_BACKWARD,0);
}
void	on_mt_sync_gotostart_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_GOTO_START,0 );
}
void	on_mt_sync_gotoend_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_GOTO_END,0 );
}
void	on_mt_sync_decspeed_clicked( GtkWidget *w, gpointer user_data)
{
	int n = info->status_tokens[SAMPLE_SPEED];
	if( n < 0 ) n += 1;
	if( n > 0 ) n -= 1;
	multitrack_sync_simple_cmd2( info->mt, VIMS_VIDEO_SET_SPEED, n );

}
void	on_mt_sync_incspeed_clicked( GtkWidget *w, gpointer user_data)
{
	int n = info->status_tokens[SAMPLE_SPEED];
	if( n < 0 ) n -= 1;
	if( n > 0 ) n += 1;
	multitrack_sync_simple_cmd2( info->mt, VIMS_VIDEO_SET_SPEED, n );
}
void	on_mt_sync_prev_clicked( GtkWidget *w , gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_PREV_FRAME ,0 );
}
void	on_mt_sync_next_clicked( GtkWidget *w, gpointer user_data)
{
	multitrack_sync_simple_cmd( info->mt, VIMS_VIDEO_SKIP_FRAME, 0 );
}

void	on_delete1_activate(GtkWidget *w, gpointer user_data)
{
}
void	on_new_source1_activate( GtkWidget *w , gpointer data )
{
}
void	on_add_file1_activate(GtkWidget *w, gpointer user_data)
{
}
void	on_colorselection_color_changed( GtkWidget *w, gpointer user_data)
{
}
static 
gchar *get_clipboard_fx_buffer()
{
	int	len = 0;
	int	p[16];
	int 	i;
	for(i=0; i <16;i++)
		p[i] = 0;	
	multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, 
		info->uc.selected_chain_entry );

	gchar *answer = recv_vims(3,&len);
	if(len <= 0 || answer == NULL )
	{
		gveejay_popup_err( "Error", "Nothing in FX clipboard");
	
		if(answer) g_free(answer);
		return NULL;
	}

	i = sscanf( answer, "%d %d %d %d %d %d %d %d %d %d %d",
			&p[0], //fx id
			&p[1], //2 video
			&p[2], //n params
			&p[3], //p0
			&p[4], //p1
			&p[5], //p2
			&p[6], //p3
			&p[7],//p4
			&p[8],//p5
			&p[9],//p6
			&p[10] //p7
			);	
	
	char preset[512];
	sprintf(preset, "%d", p[0]);
	for(i=0;  i < p[2] ;i++)
	{
		char tmp[10];
		sprintf(tmp, " %d", p[3+i] );
		strcat( preset,tmp);
	}
	g_free(answer);
	return strdup(preset);
}	 

static	gchar* last_fx_buf = NULL;
void	on_button_fx_cut_clicked( GtkWidget *w, gpointer user_data)
{
	if(last_fx_buf)
		free(last_fx_buf);
	
	last_fx_buf = get_clipboard_fx_buffer();

	on_button_fx_del_clicked( NULL,NULL );
}

void	on_button_fx_paste_clicked( GtkWidget *w, gpointer user_data)
{
	int i = info->uc.selected_chain_entry;
	sample_slot_t *s = info->selected_slot;

	if( last_fx_buf && s)
	{
		char msg[256];
		sprintf( msg, "%03d:%d %d %s;",
			VIMS_CHAIN_ENTRY_SET_PRESET,
			s->sample_id,
			i,
			last_fx_buf );
		msg_vims(msg);
		info->uc.reload_hint[HINT_ENTRY]=1;
	}
	
	
}	
void	on_button_fx_copy_clicked(GtkWidget *w, gpointer user_data)
{
	if(last_fx_buf)
		free(last_fx_buf);
	
	last_fx_buf = get_clipboard_fx_buffer();
}
void	on_copy1_activate( GtkWidget *w, gpointer user_data)
{
}
void	on_new_color1_activate(GtkWidget *w , gpointer user_data)
{
}
void	on_delete2_activate( GtkWidget *w, gpointer user_data)
{
}
void
on_spin_samplebank_select_value_changed
                                        (GtkSpinButton   *spinbutton,
                                        gpointer         user_data)
{
        GtkNotebook *samplebank = GTK_NOTEBOOK( info->sample_bank_pad );

        gint max_page = gtk_notebook_get_n_pages(samplebank);
        
        gint page = gtk_spin_button_get_value_as_int(spinbutton);

        if(page >= max_page){ /* @mvh I know this is not pretty but why make it difficult */
                 page = 0; 
                 gtk_spin_button_set_value(spinbutton, page);
        } else if( page < 0 ) {
		page = max_page;
		gtk_spin_button_set_value(spinbutton,page);
	}
        gtk_notebook_set_current_page_(samplebank, page);        
}
void
on_button_samplebank_prev_clicked      (GtkButton       *button,
                                        gpointer         user_data)
{
	GtkNotebook *samplebank = GTK_NOTEBOOK( info->sample_bank_pad );
	gtk_notebook_prev_page(samplebank);        
}


void
on_button_samplebank_next_clicked      (GtkButton       *button,
                                        gpointer         user_data)
{
	GtkNotebook *samplebank = GTK_NOTEBOOK( info->sample_bank_pad );
    	gtk_notebook_next_page(samplebank);        
}

void
on_vims_messenger_rewind_clicked( GtkButton *togglebutton, gpointer user_data)
{
	info->vims_line = 0;
}

void
on_vims_messenger_clear_clicked( GtkButton *togglebutton, gpointer user_data)
{
	clear_textview_buffer( "vims_messenger_textview");
}

void
on_vims_messenger_single_clicked( void )
{
	GtkTextView *t= GTK_TEXT_VIEW(GTK_WIDGET(
				glade_xml_get_widget(
					info->main_window,
					"vims_messenger_textview"))
			);
  
	GtkTextBuffer* buffer =  gtk_text_view_get_buffer(t);
  	int lc = gtk_text_buffer_get_line_count(buffer);
	
	if(info->vims_line > lc )
		info->vims_line = 0;
	
	while(info->vims_line < lc )
	{
		GtkTextIter start, end;
       		gtk_text_buffer_get_iter_at_line(buffer, &start, info->vims_line);
  
 	 	end = start;
                
	        gtk_text_iter_forward_sentence_end(&end);
       		gchar *str = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);

		info->vims_line++;

	        if(str[0] != '+')
		{
       		 	vj_msg(VEEJAY_MSG_INFO, "User defined VIMS message sent '%s'",str );
                	msg_vims( str );
			break;
        	}
	}
}

static	gint	srt_load_subtitle(int sid)
{
	gint len = 0;
	gint seq_id = 0;
	gint tc1l=0;
	gint tc2l=0;
	char tc1[20];
	char tc2[20];
	char tmp[1000];
	gint tlen=0;
	gint ln[4];
	gint fg[4];
	gint bg[4];
	gint use_bg = 0;
	gint outline = 0;
	gint size = 0;
	gint font = 0;
	gint x =0;
	gint y = 0;

	multi_vims( VIMS_SRT_INFO, "%d", sid );
	gchar *text = recv_vims( 6,&len );
	veejay_memset(tmp,0,sizeof(tmp));
	veejay_memset(tc1,0,sizeof(tc1));
	veejay_memset(tc2,0,sizeof(tc2));

	clear_textview_buffer( "textview_text" );
	int s1=0,s2=0;
	if(text && len > 0 )
	{
		sscanf( text,"%5d%9d%9d%2d", &seq_id ,&s1,&s2,&tc1l );
		strncpy( tc1, text+7+18,tc1l );	
		sscanf( text+7+18+tc1l,"%2d", &tc2l );
		strncpy( tc2, text+7+18+tc1l + 2, tc2l );
		sscanf( text+7+18+tc1l+2+tc2l, "%3d", &tlen );
		strncpy( tmp, text + 7 + 18 + tc1l + 2 + tc2l + 3, tlen );
		sscanf( text+7+18 + tc1l+2+tc2l+3+tlen,"%04d%04d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d%03d",
			&x,&y, &font, &size, &bg[0],&bg[1],&bg[2],&fg[0],&fg[1],&fg[2],&use_bg,&outline,&ln[0],&ln[1],
			&ln[2],&bg[3],&fg[3],&ln[3] );
	}

	srt_locked_ = 1;
	
	update_spin_range( "spin_text_start",0, get_total_frames(),s1);
	update_spin_range( "spin_text_end",0, get_total_frames(),s2);

	set_textview_buffer( "textview_text", tmp );

	update_spin_value( "spin_text_start" ,s1);
	update_spin_value( "spin_text_end", s2 );

	change_box_color_rgb(
		 glade_xml_get_widget(info->main_window, "boxbg" ),
		 bg[0],bg[1],bg[2],bg[3], (is_button_toggled( "textcolorbg" ) ? 1 : 0 ) );

	change_box_color_rgb(
		 glade_xml_get_widget(info->main_window, "boxtext" ),
		 fg[0],fg[1],fg[2],fg[3], (is_button_toggled( "textcolorfg" ) ? 1: 0) );


	change_box_color_rgb(
		glade_xml_get_widget( info->main_window, "boxln" ),
		ln[0],ln[1],ln[2],ln[3], (is_button_toggled( "textcolorln" ) ? 1: 0) );

	memcpy( bg_, bg, sizeof(bg_));
	memcpy( fg_, fg, sizeof(fg_));
	memcpy( ln_, ln, sizeof(ln_));

	set_toggle_button( "use_bg", use_bg );
	set_toggle_button( "use_outline", outline);
	update_spin_value( "spin_text_size", size );
	update_spin_value( "spin_text_x", x );
	update_spin_value( "spin_text_y", y );	

	if(is_button_toggled( "textcolorfg") )
	{
		update_slider_value( "textcolorred", fg_[0],0 );
		update_slider_value( "textcolorblue",fg_[2],0 );
		update_slider_value( "textcolorgreen",fg_[1],0);
		update_slider_value( "textcoloralpha", fg_[3],0);
	}
	else if( is_button_toggled( "textcolorbg") )
	{
		update_slider_value( "textcolorred", bg_[0],0 );
		update_slider_value( "textcolorblue",bg_[2],0 );
		update_slider_value( "textcolorgreen",bg_[1],0);
		update_slider_value( "textcoloralpha",bg_[3],0);
	}
	else if ( is_button_toggled( "textcolorln" ))
	{
		update_slider_value( "textcolorred", ln_[0],0 );
		update_slider_value( "textcolorblue",ln_[2],0 );
		update_slider_value( "textcolorgreen",ln_[1],0);
		update_slider_value( "textcoloralpha", ln_[3],0);
	}
	GtkWidget *combo = glade_xml_get_widget( info->main_window, "combobox_fonts" );
	gtk_combo_box_set_active( GTK_COMBO_BOX( combo ), font );


//	glade_xml_get_widget( info->main_window, "combobox_textsrt" );
//      gtk_combo_box_set_active( GTK_COMBO_BOX( combo ), seq_id-1 );

//	if(len > 0 )
//		enable_widget( "SRTframe");


	srt_locked_ = 0;

	if(text) free(text);

	return seq_id;
}

void	on_button_text_new_clicked( GtkWidget *w, gpointer data )
{
	gint x = get_nums( "spin_text_x" );
	gint y = get_nums( "spin_text_y" );
	gint s1 = 0;
	gint s2 = 0;
	if( info->status_tokens[PLAY_MODE] == MODE_SAMPLE )
        {
              s1 = info->status_tokens[SAMPLE_START];
              s2 = info->status_tokens[SAMPLE_END];
        }
        else
        {
          	s1 = 0;
                s2 = get_nums("stream_length");
        }

	gchar *text = strdup(" ");
	
	multi_vims( VIMS_SRT_ADD, "%d %d %d %d %d %s",
		0,s1,s2,x,y,text );

	int tmp = 0;
	gchar *new_srt_id = recv_vims( 5, &tmp );
	int id = 0;
	if(new_srt_id && tmp > 0)
	{
		sscanf( new_srt_id, "%d", &id );
		if(id > 0 )
		  srt_seq_ = id;
	}
	if( new_srt_id )
		free(new_srt_id);
	
	free(text);
	if( id > 0 )
		info->uc.reload_hint[HINT_HISTORY] = 1;

}

void	on_button_text_del_clicked( GtkWidget *w, gpointer data )
{
	multi_vims( VIMS_SRT_DEL, "%d", srt_seq_ );
	info->uc.reload_hint[HINT_HISTORY] = 1;
}

void	on_spin_text_start_value_changed( GtkWidget *w, gpointer data )
{
	gint start = get_nums( "spin_text_end");
	char *text = format_time( start, info->el.fps );
	update_label_str( "labeltextstart", text );
	free(text);	
}

void	on_spin_text_end_value_changed( GtkWidget *w, gpointer data )
{
	gint end = get_nums( "spin_text_end" );
	char *text = format_time( end, info->el.fps );
	update_label_str( "labeltextend", text );
	free(text);
}
void	on_spin_text_x_value_changed( GtkWidget *w, gpointer data )
{
	if( srt_locked_)
		return;

	gint x = get_nums( "spin_text_x" );
	gint y = get_nums( "spin_text_y");
	multi_vims( VIMS_FONT_POS,"%d %d", x,y );
}
void	on_spin_text_y_value_changed( GtkWidget *w, gpointer data )
{
	if( srt_locked_)
		return;

	gint x = get_nums( "spin_text_x" );
	gint y = get_nums( "spin_text_y");
	multi_vims( VIMS_FONT_POS,"%d %d", x,y );
}
void	on_button_srt_save_clicked( GtkWidget *w, gpointer data )
{
	gchar *filename = dialog_save_file("Save SRT file");
	if( filename )
	{
		multi_vims( VIMS_SRT_SAVE, "%s", filename );
		g_free(filename);
	}
}
void	on_button_srt_load_clicked( GtkWidget *w, gpointer data )
{
	gchar *filename = dialog_open_file("Load SRT file",4);
	if( filename )
	{
		multi_vims( VIMS_SRT_LOAD, "%s", filename );
		g_free(filename);
	}
}


void	on_combobox_fonts_changed( GtkWidget *w, gpointer data )
{
	if(srt_locked_)
		return;
	gint font = gtk_combo_box_get_active( GTK_COMBO_BOX( w ) );
	gint size = get_nums( "spin_text_size" );
	if( font < 0 || size < 0 )
		return;
	multi_vims( VIMS_FONT_SIZE_FONT, "%d %d", font , size );
}
void	on_spin_text_size_value_changed( GtkWidget *w, gpointer data )
{
	if( srt_locked_)
		return;
	GtkWidget *ww = glade_xml_get_widget( info->main_window,
			"combobox_fonts" );
	gint font = gtk_combo_box_get_active( GTK_COMBO_BOX( ww ) );
	gint size = get_nums( "spin_text_size" );

	multi_vims( VIMS_FONT_SIZE_FONT, "%d %d", font , size );
}

void	on_button_text_update_clicked(GtkWidget *w, gpointer data)
{
	gint s1 = get_nums( "spin_text_start" );
	gint s2 = get_nums( "spin_text_end" );
	gchar *text = get_textview_buffer( "textview_text" );
	if(text)
		multi_vims( VIMS_SRT_UPDATE, "%d %d %d %s", srt_seq_, s1,s2,text );
}

static	void change_box_color_rgb( GtkWidget *box, int r, int g, int b,int a, int fill )
{
	GdkGC *gc = gdk_gc_new( box->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.red = 255.0 * r;
	col.green = 255.0 * g;
	col.blue = 255.0 * b;

	if(fill)
	{
		update_slider_value( "textcolorred", r ,0);
		update_slider_value( "textcolorgreen",g,0 );
		update_slider_value( "textcolorblue",b,0);
		update_slider_value( "textcoloralpha",a,0);
	}
	gdk_color_alloc( gtk_widget_get_colormap( box ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			box->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );
}

void	on_combobox_textsrt_changed( GtkWidget *w, gpointer data)
{
	if(info->status_lock)
		return;

	gchar *k = gtk_combo_box_get_active_text( GTK_COMBO_BOX(w) );
	int sid  = atoi(k);
	if( sid > 0)
	{
		multi_vims( VIMS_SRT_SELECT, "%d", sid );
		srt_seq_ = sid;
		gint res = srt_load_subtitle(sid);
		char text[32];	
		sprintf(text, "Edit SRT %d", res );
		update_label_str( "srtid_label", text );	
	}
}


static	void change_box_color( GtkWidget *box, double val, int plane, int fill )
{
	GdkGC *gc = gdk_gc_new( box->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	double v = (1.0 / 255.0) * val;

	int r = get_slider_val( "textcolorred" );
	int b =  get_slider_val( "textcolorgreen" );
	int g =  get_slider_val( "textcolorblue" );
	int a = get_slider_val("textcoloralpha" );
	
	if(plane==0)
	{
		col.red = 65535.0 * v;
		switch(fill)
		{
			case 0:	fg_[0] = r; break;
			case 1: bg_[0] = r; break;
			case 2: ln_[0] = r; break;
		}
	}
	if(plane==1)
	{
		col.green = 65535 * v;
		switch(fill)
		{
			case 0:	fg_[1] = g; break;
			case 1: bg_[1] = g; break;
			case 2: ln_[1] = g; break;
		}
	}
	if(plane==2)
	{
		col.blue = 65535 * v;
		switch(fill)
		{
			case 0:	fg_[2] = b; break;
			case 1: bg_[2] = b; break;
			case 2: ln_[2] = b; break;
		}

	}
	if(plane==-1)
	{
		col.red = 255.0 * r;
		col.green = 255.0 * g;
		col.blue = 255.0 * b;
		switch(fill)
		{
			case 0:	fg_[0] = r; fg_[1] = g; fg_[2] = b; fg_[3] = a; break;
			case 1: bg_[0] = r; bg_[1] = g; bg_[2] = b; bg_[3] = a; break;
			case 2: ln_[0] = r; ln_[1] = g; ln_[2] = b; ln_[3] = a; break;
		}

	}

	
	gdk_color_alloc( gtk_widget_get_colormap( box ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			box->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );
}

static	void	colbox( const char *name1,const char *name2, int plane )
{	
	int fg = is_button_toggled("textcolorfg");
	int bg = is_button_toggled("textcolorbg");
	int ln = is_button_toggled("textcolorln");

	int v  = get_slider_val( name2 );
	change_box_color(
			glade_xml_get_widget( info->main_window,name1 ) ,
			v, 
			plane,
			-1 ); //green

	if(fg)
		change_box_color(
				glade_xml_get_widget( info->main_window, "boxtext" ),
				0.0,
				-1,
			        0	);
	if(bg)
		change_box_color(
				glade_xml_get_widget( info->main_window, "boxbg" ),
				0.0,
				-1,
			        1	);

	if(ln)
		change_box_color(
				glade_xml_get_widget( info->main_window, "boxln" ),
				0.0,
				-1,
			        2	);

}
	
void	on_textcoloralpha_value_changed(GtkWidget *w, gpointer data )
{
	if(info->status_lock || srt_locked_)
		return;

	int fg = is_button_toggled("textcolorfg");
	int bg = is_button_toggled("textcolorbg");
	int ln = is_button_toggled("textcolorln");
	gint r = get_slider_val( "textcolorred" );
	gint g = get_slider_val( "textcolorgreen" );
	gint b = get_slider_val( "textcolorblue" );
	gint a = get_slider_val( "textcoloralpha" );
	
	int m = 0;
	if( fg )
	{
		fg_[3] = a;
		m = 1;
	}	
	if( bg )
	{
		bg_[3] = a;
		m = 2;
	}
	if( ln )
	{
		ln_[3] = a;
		m = 3;
	}
		
	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", r,g,b,a, m );	
}

void	on_textcolorred_value_changed(GtkWidget *w , gpointer data )
{
	colbox( "boxred", "textcolorred", 0 );
}


void	on_textcolorgreen_value_changed(GtkWidget *w , gpointer data )
{
	colbox( "boxgreen", "textcolorgreen", 1 );
}

void	on_textcolorblue_value_changed(GtkWidget *w , gpointer data )
{
	colbox( "boxblue", "textcolorblue", 2  );	
}

void	on_textcolorfg_toggled( GtkWidget *w, gpointer data )
{
	if( is_button_toggled( "textcolorfg" ) )
	{
		update_slider_value( "textcolorred", fg_[0],0 );
		update_slider_value( "textcolorgreen", fg_[1],0 );
		update_slider_value( "textcolorblue", fg_[2],0);
		update_slider_value( "textcoloralpha", fg_[3],0);
	}
}
void	on_textcolorbg_toggled( GtkWidget *w, gpointer data )
{
	if( is_button_toggled( "textcolorbg" ) )
	{
		update_slider_value( "textcolorred", bg_[0],0 );
		update_slider_value( "textcolorgreen", bg_[1],0 );
		update_slider_value( "textcolorblue", bg_[2],0);
		update_slider_value( "textcoloralpha", bg_[3],0);
	}
}
void	on_textcolorln_toggled( GtkWidget *w, gpointer data )
{
	if( is_button_toggled( "textcolorln" ) )
	{
		update_slider_value( "textcolorred", ln_[0],0 );
		update_slider_value( "textcolorgreen", ln_[1],0 );
		update_slider_value( "textcolorblue", ln_[2],0);
		update_slider_value( "textcoloralpha", ln_[3],0);
	}
}

void	on_use_bg_toggled( GtkWidget *w , gpointer data)
{
	if(srt_locked_ || info->status_lock)
		return;

	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", 
			is_button_toggled("use_outline"),
			is_button_toggled("use_bg"),
			0,
			0,
			0 );
}	

void	on_use_outline_toggled( GtkWidget *w, gpointer data)
{
	if(srt_locked_ || info->status_lock)
		return;

	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", 
			is_button_toggled("use_outline"),
			is_button_toggled("use_bg"),
			0,
			0,
			0 );

}

void	on_buttonfg_clicked( GtkWidget *w, gpointer data )
{
	gint r = get_slider_val( "textcolorred" );
	gint g = get_slider_val( "textcolorgreen" );
	gint b = get_slider_val( "textcolorblue" );
	gint a = get_slider_val( "textcoloralpha");
	fg_[0] = r;
	fg_[1] = g;
	fg_[2] = b;
	fg_[3] = a;
	
	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", r,g,b,a, 1 );
}
void	on_buttonbg_clicked( GtkWidget *w, gpointer data )
{
	gint r = get_slider_val( "textcolorred" );
	gint g = get_slider_val( "textcolorgreen" );
	gint b = get_slider_val( "textcolorblue" );
	gint a = get_slider_val( "textcoloralpha" );
	
	bg_[0] = r;
	bg_[1] = g;
	bg_[2] = b;
	bg_[3] = a;
	
	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", r,g,b,a, 2 );	
}
void	on_buttonln_clicked( GtkWidget *w, gpointer data )
{
	gint r = get_slider_val( "textcolorred" );
	gint g = get_slider_val( "textcolorgreen" );
	gint b = get_slider_val( "textcolorblue" );
	gint a = get_slider_val( "textcoloralpha" );
	
	ln_[0] = r;
	ln_[1] = g;
	ln_[2] = b;
	ln_[3] = a;
	
	multi_vims( VIMS_FONT_COL, "%d %d %d %d %d", r,g,b,a, 3 );	
}

gboolean	boxfg_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.red = 255.0 * fg_[0];
	col.green = 255.0 * fg_[1];
	col.blue = 255.0 * fg_[2];

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}

gboolean	boxbg_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.red = 255.0 * bg_[0];
	col.green = 255.0 * bg_[1];
	col.blue = 255.0 * bg_[2];

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}

gboolean	boxln_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.red = 255.0 * ln_[0];
	col.green = 255.0 * ln_[1];
	col.blue = 255.0 * ln_[2];

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}

gboolean	boxred_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.red = 255 * get_slider_val( "textcolorred" );

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}


gboolean	boxgreen_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.green = 0xff * get_slider_val( "textcolorgreen" );

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}
gboolean	boxblue_expose_event(GtkWidget *w,
		GdkEventExpose *event, gpointer data )
{
	gdk_window_clear_area( w->window,
			event->area.x, event->area.y,
			event->area.width,event->area.height );

	
	GdkGC *gc = gdk_gc_new( w->window );
	GdkColor col;

	memset( &col,0, sizeof( GdkColor ) );
	col.blue = 255 * get_slider_val( "textcolorblue" );

	gdk_color_alloc( gtk_widget_get_colormap( w ), &col );
	
	gdk_gc_set_foreground( gc, &col );
	
	gdk_draw_rectangle( 
			w->window,
			gc,
			TRUE,
			0,
			0,
			24,
			24 );

	gdk_gc_unref( gc );

	return TRUE;
}


void	on_osdbutton_clicked(GtkWidget *w, gpointer data )
{
	single_vims(VIMS_OSD);
}

void	on_seqactive_toggled( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	multi_vims( VIMS_SEQUENCE_STATUS, "%d" , is_button_toggled("seqactive" ) ? 1 : 0 );
	vj_midi_learning_vims_msg(info->midi,NULL, VIMS_SEQUENCE_STATUS, is_button_toggled("seqactive"));
}

void	on_hqbutton_clicked( GtkWidget *w, gpointer data ) // 1/1
{
	multitrack_set_quality( info->mt, 0 );
}
void	on_lqbutton_clicked( GtkWidget *w, gpointer data ) // 1/2
{
	multitrack_set_quality( info->mt, 1 );
}
void	on_bq_button_clicked( GtkWidget *w, gpointer data ) // 1/4
{
	multitrack_set_quality( info->mt, 2 );
}
void	on_uq_button_clicked( GtkWidget *w, gpointer data ) // 1/8
{
	multitrack_set_quality( info->mt, 3 );
}

void	on_record_vp_clicked( GtkWidget *w, gpointer data )
{
	single_vims( VIMS_RECVIEWPORT );
}

void	on_macroplay_toggled( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	if( is_button_toggled( "macroplay" ))
	{	
		multi_vims( VIMS_MACRO, "%d %d", 2,1 );
		vj_midi_learning_vims_msg2( info->midi,NULL,VIMS_MACRO,2,1 );
	}
}

void	on_macrorecord_toggled( GtkWidget *w, gpointer data  )
{
	if(info->status_lock)
		return;
	if( is_button_toggled( "macrorecord"))
	{	
		multi_vims( VIMS_MACRO, "%d %d", 1, 1 );
		vj_midi_learning_vims_msg2( info->midi,NULL,VIMS_MACRO,1,1 );
	}
}

void	on_macrostop_toggled( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	if( is_button_toggled( "macrostop"))
	{
		multi_vims( VIMS_MACRO, "%d %d", 0 , 1 );
		vj_midi_learning_vims_msg2( info->midi,NULL,VIMS_MACRO,0,1 );
	}
}

void	on_macroclear_clicked( GtkWidget *w, gpointer data )
{
	if(info->status_lock)
		return;
	
	multi_vims( VIMS_MACRO, "%d %d", 0, 0 );
	vj_midi_learning_vims_msg2( info->midi,NULL, VIMS_MACRO, 0, 0);
}

void	on_midilearn_toggled( GtkWidget *w, gpointer data )
{
	vj_midi_learn( info->midi);
}

void	on_midievent_toggled( GtkWidget *w, gpointer data )
{
	vj_midi_play( info->midi );
}


void	on_load_midi_layout1_activate( GtkWidget *w , gpointer data )
{
	gchar *filename = dialog_open_file( "Select MIDI configuration file to load",0);
	if( filename ) {
		vj_midi_load( info->midi, filename );
		g_free(filename);
	}
}
void	on_save_midi_layout1_activate( GtkWidget *w, gpointer data )
{
	gchar *filename = dialog_save_file( "Save MIDI configuration to file");
	if(filename)
		vj_midi_save( info->midi, filename );
}

void on_clear_midi_layout1_activate( GtkWidget *w, gpointer data )
{
	vj_midi_reset(info->midi);
}	
