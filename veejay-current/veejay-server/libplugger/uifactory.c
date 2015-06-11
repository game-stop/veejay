/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
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


#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vevosample/vevosample.h>
#include <vevosample/defs.h>
#include <vevosample/uifactory.h>
#include <vevosample/ldefs.h>
#include <vevosample/defs.h>
#include <libplugger/ldefs.h>
#include <libvevo/libvevo.h>

#include <veejay/oscsend.h>

static	void	vevosample_ui_type_shared( void *sample, char *window );
static	void	vevosample_ui_type_capture( void *sample, const char *window, const char *frame);
static	void	vevosample_ui_type_none( void *sample, const char *window, const char *frame);
static  void	vevosample_ui_construct_fx_contents( void *sample, int entry_id, const char *window , const char *fx_frame);
static	char *	vevosample_ui_new_window( sample_runtime_data *src, const char *window_prefix, const int id, const char *title, const char *infix, int suffix );


void		vevosample_ui_new_vframe( void *sample, const char *window, const char *frame, const char *label )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	veejay_ui_bundle_add(
			osc_send,
			"/create/vframe",
			"sssx",
			window,
			frame,
			label );
}	
void		vevosample_ui_new_frame( void *sample, const char *window, const char *frame, const char *label )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	veejay_ui_bundle_add(
			osc_send,
			"/create/frame",
			"sssx",
			window,
			frame,
			label );
}	

void		vevosample_ui_new_button( void *sample, const char *window, const char *frame, const char *label,
						const char *path, const char *tooltip )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;

	veejay_ui_bundle_add( 
			osc_send,
			"/create/button",
			"sssssx",
			window,
			frame,
			label,
			path,
		        tooltip	);
}
void		vevosample_ui_new_label( void *sample, const char *window, const char *frame, const char *label )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;

	veejay_ui_bundle_add( 
			osc_send,
			"/create/label",
			"sssx",
			window,
			frame,
			label );
}


void		vevosample_ui_new_numeric( void *sample, const char *window, const char *frame, const char *label,
		double min, double max, double value, int wrap, int extra, const char *widget_name,
		const char *path, const char *format, const char *tooltip)

{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	veejay_ui_bundle_add(
			osc_send,
			"/create/numeric",
			"sssdddiissssx",
			window,
			frame,
			label,
			min,
			max,
			value,
			wrap,
			extra,
			widget_name,
			path,
			format,
			tooltip);
}

void		vevosample_ui_new_radiogroup( void *osc, const char *window, const char *framename , const char *prefix,const char *label_prefix, int n_buttons, int active_button )
{
	veejay_ui_bundle_add( osc, "/create/radiogroup", "ssssii", window,framename,prefix,label_prefix,
			n_buttons, active_button );
}
void		vevosample_ui_new_switch( void *sample, const char *window, const char *framename , const char *widget,const char *label, int active, const char *path, const char *tooltip)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	veejay_ui_bundle_add( osc_send, "/create/switch", "ssssissx", window,framename,widget,label,active,path,tooltip );
}




static void		vevosample_ui_construct_fx_contents( void *sample, int entry_id, const char *window , const char *fx_frame)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender( srd->user_data );
	int id = 0;
	if(!osc_send)
		return;
	fx_slot_t *slot = sample_get_fx_port_ptr( srd, entry_id );
	if(slot->window)
		free(slot->window);
	slot->window = strdup( window );
			
	int error = vevo_property_get( srd->info_port, "primary_key", 0, &id );
	void *filter_template = NULL;
	error = vevo_property_get( slot->fx_instance, "filter_templ",0 ,&filter_template );
	char *label_str = vevo_property_get_string( filter_template, "name" );
	char label[128];
	sprintf( label, "FX slot %d with  \"%s\" ", entry_id, label_str );
	free(label_str);
	
	sprintf(fx_frame, "fx_%d", entry_id ); 

	veejay_ui_bundle_add( osc_send, "/create/frame", "sssx", window, fx_frame, label );

	char alpha_tmp[128];
	sprintf( alpha_tmp, "/sample_%d/fx_%d/alpha", id, entry_id );
			
	veejay_ui_bundle_add( osc_send, "/create/numeric", "sssdddiissssx",window,fx_frame,
			"Alpha", 0.0, 1.0, slot->alpha,2,0,"VSlider",alpha_tmp, "d", "Transparency" );

	
	sprintf( alpha_tmp, "/sample_%d/fx_%d/status", id, entry_id);

	veejay_ui_bundle_add( osc_send, "/create/switch", "ssssissx", window, fx_frame, "Check",
			"Enabled", slot->active, alpha_tmp, "Turn on/off FX" );
	
	int q;
	int n = vevo_property_num_elements( slot->fx_instance, "in_parameters" );

	for( q = 0; q < n ; q ++ )
	{
		void *parameter = NULL;
		error = vevo_property_get( slot->fx_instance, "in_parameters", q, &parameter );
		void *parameter_templ = NULL;
		error = vevo_property_get( parameter, "parent_template",0,&parameter_templ);
		int kind = 0;
		error = vevo_property_get( parameter_templ, "HOST_kind",0,&kind );
		char *parameter_name = vevo_property_get_string(parameter_templ, "name" );
		char *osc_path 		= vevo_property_get_string(parameter, "HOST_osc_path" );
		int ival = 0;
		double gval = 0.0;
		int imin = 0, imax = 0;
		double gmin = 0.0, gmax = 0.0;
		char *hint = vevo_property_get_string( parameter_templ, "description" );
		switch( kind )
		{
			case HOST_PARAM_INDEX:
				vevo_property_get( parameter, "value",0,&ival);
				gval = (double) ival;
				vevo_property_get( parameter_templ, "min",0,&imin );
				gmin = (double) imin;
				vevo_property_get( parameter_templ, "max",0,&imax );
				gmax = (double) imax;
				veejay_ui_bundle_add( osc_send, "/create/numeric", "sssdddiissssx", window, fx_frame,
						parameter_name,gmin,gmax,gval,0,0,"VSlider", osc_path, "i", hint );
				
				break;
			case HOST_PARAM_NUMBER:
				vevo_property_get( parameter, "value",0,&gval);
				vevo_property_get( parameter_templ, "min",0,&gmin );
				vevo_property_get( parameter_templ, "max",0,&gmax );
				veejay_ui_bundle_add( osc_send, "/create/numeric", "sssdddiissssx",
						window,fx_frame,parameter_name,gmin,gmax,gval,2,0,"VSlider",osc_path,"d", hint);
				break;
			case HOST_PARAM_SWITCH:
				vevo_property_get( parameter, "value",0,&ival );
				veejay_ui_bundle_add( osc_send, "/create/switch", "ssssissx", window,fx_frame, "Check",
						parameter_name, ival ,osc_path, hint );
				break;
			default:
				break;
		}
		free(hint);
		free(parameter_name);
		free(osc_path);
	}
	
	n = vevo_property_num_elements( slot->fx_instance, "in_channels" );

veejay_msg(0, "UI Factory: There are %d input channels", n );
	
	if( n > 1 )
	{
		char ch_path[128];
		sprintf( ch_path, "/sample_%d/fx_%d/input_channel", id, entry_id);


		void *msg = veejay_message_new_widget( osc_send, window, 
					ch_path, n-1 );

		void *ch = sample_get_fx_port_channels_ptr( id , entry_id );
		char **items = vevo_list_properties( ch );
		for( q = 0; items[q] != NULL ;q  ++ )
		{
			void *sample = NULL;
			vevo_property_get( ch, items[q],0,&sample);
			if(sample && strncasecmp( items[q] , "slot0", 5 ) != 0)
			{
				int id = sample_get_run_id( sample );
				char name[32];
				sprintf(name , "S%d", id );
				veejay_message_add_argument( osc_send, msg, "s", name );
			veejay_msg(0, "Added '%s' ", name);
			}
			free(items[q]);
		}
		free(items);
		//	veejay_message_add_argument( osc_send, msg, "s", name );

		veejay_message_widget_done( osc_send, msg );
	}

	n =  vevo_property_num_elements( slot->fx_instance, "out_parameters" );
	if( n <= 0 )
	{
		return;
	}
	
	char long_label[128];
	snprintf(long_label,128,"Output parameters", label);
	sprintf(fx_frame, "bind_%d", entry_id ); 
	veejay_ui_bundle_add( osc_send, "/create/frame", "sssx", window, fx_frame, long_label );

	char box_name[128];
	sprintf( box_name, "%s_box", fx_frame );
	
	veejay_ui_bundle_add( osc_send, "/create/box", "sssix", window,fx_frame, box_name, 0 );
	
	for( q = 0; q < n ; q ++ )
	{
		void *parameter = NULL;
		error = vevo_property_get( slot->fx_instance, "out_parameters", q, &parameter );
		void *parameter_templ = NULL;
		error = vevo_property_get( parameter, "parent_template",0,&parameter_templ);
		int kind = 0;
		error = vevo_property_get( parameter_templ, "HOST_kind",0,&kind );
		if( kind == HOST_PARAM_NUMBER || kind == HOST_PARAM_SWITCH || kind == HOST_PARAM_INDEX )
		{
			char *parameter_name = vevo_property_get_string(parameter_templ, "name" );
			sprintf(fx_frame, "fxb_%d",q );
			veejay_ui_bundle_add( osc_send, "/create/vframe", "ssssx", window, box_name,fx_frame, parameter_name );
			char param_id[32];
			sprintf(param_id,"o%02d",q );

			char *parameter_value = vevo_sprintf_property_value(
					parameter, "value " );
			
			veejay_ui_bundle_add( osc_send, "/create/label",
				"ssssx", window,fx_frame,param_id, (parameter_value==NULL ? "   " : parameter_value) ); //@ updated by apply_bind !!!
			if(parameter_value)
				free(parameter_value);
			free(parameter_name );

			
			
			char hint[128];
			snprintf(hint, 128,"Bind %s to an Input Parameter of another FX slot");
			char bindname[32];
			char unbindname[32];
			sprintf(bindname,"bind_p%d", q);
			sprintf(unbindname,"unbind_p%d",q );

			//@ special button
			veejay_ui_bundle_add( osc_send, "/create/button","sssssx",
				window, fx_frame, "B", bindname, hint );


			//@ normal button, special parameters
			veejay_ui_bundle_add( osc_send, "/create/button","sssssx",
				window, fx_frame, "C", unbindname, "Reset binding");
		}
	}
	
}
static	char	*_get_out_parameter_name( void *instance, int id )
{
	void *p = NULL;
        int error =	vevo_property_get( instance, "out_parameters", id, &p );
	if( error != VEVO_NO_ERROR )
		return NULL;
	void *t = NULL;
	error = vevo_property_get( p, "parent_template", 0, &t );
	if( error == VEVO_NO_ERROR )
		return vevo_property_get_string( t, "name" );
	return NULL;
}
static	char	*_get_in_parameter_name( void *instance, int id )
{
	void *p = NULL;
        int error =	vevo_property_get( instance, "in_parameters", id, &p );
	if( error != VEVO_NO_ERROR )
		return NULL;
	void *t = NULL;
	error = vevo_property_get( p, "parent_template", 0, &t );
	if( error == VEVO_NO_ERROR )
		return vevo_property_get_string( t, "name" );
	return NULL;
}
static	char	*_get_in_channel_name( void *instance, int id )
{
	void *p = NULL;
        int error =	vevo_property_get( instance, "in_channels", id, &p );
	if( error != VEVO_NO_ERROR )
		return NULL;
	void *t = NULL;
	error = vevo_property_get( p, "parent_template", 0, &t );
	if( error == VEVO_NO_ERROR )
		return vevo_property_get_string( t, "name" );
	return NULL;
}

void	vevosample_ui_get_input_parameter_list( void *sample, int fx_id, const char *window )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *rel = (fx_slot_t*) sample_get_fx_port_ptr( srd, fx_id);
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;

//@ we also update path!
	char bind_path[128];
	sprintf(bind_path, "/sample_%d/fx_%d/bind", srd->primary_key,fx_id);
	
	
//@ pulldown abused for updatin ! see pulldown_done_update  , it calls an update method in uiosc.c	
	void *msg = veejay_message_new_pulldown( osc_send, window, "n/a", "combobox_list_ip", "Input Parameter", bind_path, fx_id, "none" );

	int n = vevo_property_num_elements( rel->fx_instance, "in_parameters" );
	int i;
	veejay_msg(0, "Get FX Slot %d of sample %d, with %d parameters",
			fx_id, srd->primary_key, n );
	for( i = 0; i < n ; i++ )
	{
		char *pname = _get_in_parameter_name( rel->fx_instance, i );
		veejay_msg(0, "\t added parameter '%s'", pname );
		veejay_message_add_argument(
				osc_send, msg, "s", pname );
			
		free(pname);
	}
	veejay_message_pulldown_done_update(
			osc_send, msg );
}

void	vevosample_ui_get_bind_list( void *sample, const char *window )
{
	int fx_id = 0;
	int p_num = 0;
	int dummy = 0;
	sscanf(window, "SampleBind%dFX%dOP%d",&dummy,&fx_id,&p_num);

	
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *sl = (fx_slot_t*) sample_get_fx_port_ptr( srd, fx_id);
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;

//@ we also update path!
	char bind_path[128];
	sprintf(bind_path, "/sample_%d/fx_%d/unbind", srd->primary_key,fx_id);
	
	
//@ pulldown abused for updatin ! see pulldown_done_update  , it calls an update method in uiosc.c	
	void *msg = veejay_message_new_pulldown( osc_send, window, "n/a", "combobox_release_bind", "Choose Item to release", bind_path, 0, "None" );

//	veejay_message_add_argument(
//				osc_send, msg, "s", "None" );

	char **items = vevo_list_properties( sl->bind );
	if(! items )
	{
		veejay_message_pulldown_done_update(
			osc_send, msg );

		return;
	}
	int i;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		int n[3];
		sscanf( items[i], "bp%d_%d_%d", &n[BIND_OUT_P],&n[BIND_ENTRY],&n[BIND_IN_P] );
#endif
		fx_slot_t *rel = (fx_slot_t*) sample_get_fx_port_ptr( srd, n[BIND_ENTRY]);

		if( n[BIND_OUT_P] == p_num && vevo_property_get(sl->bind, items[i],0,NULL) == VEVO_NO_ERROR )
		{
			veejay_msg(0, "'%s' is a valid bind",items[i]);
			void *filter_template = NULL;
			int error = vevo_property_get( rel->fx_instance, "filter_templ",0 ,&filter_template );
			char *fxname = vevo_property_get_string( filter_template, "name" );

			char *pname = _get_in_parameter_name( rel->fx_instance, n[BIND_IN_P] );
			char list_item[128];
			snprintf(list_item, 128, "fx_%d '%s' p%d '%s'",
					n[BIND_ENTRY], fxname,n[BIND_IN_P], pname );

			veejay_message_add_argument( osc_send, msg, "s", list_item );

			free(fxname);
			free(pname);	


		}
		free(items[i]);
	}
	free(items);
	
	veejay_message_pulldown_done_update(
			osc_send, msg );
}

static	void	vevosample_ui_construct_bind_list( void *sample, int k, int p_num, void *dslot, const char *window,
	       	const char *fx_frame	)
{
	//@ list all binds
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = (fx_slot_t*) dslot;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;

	char bind_path[128];
	sprintf(bind_path, "/sample_%d/fx_%d/unbind", srd->primary_key,k);

	void *msg = veejay_message_new_pulldown(
			osc_send, window, fx_frame, "combobox_release_bind", "Choose item to release", bind_path, 0, "None" );
	
	veejay_message_add_argument( osc_send, msg, "s", "None");

	int i;
	if(!slot->bind )
	{
		veejay_message_pulldown_done(osc_send, msg);
		return;
	}

	char **items = vevo_list_properties( slot->bind );
	if(! items )
	{
		veejay_message_pulldown_done(osc_send, msg);
		return;
	}
	for( i = 0; items[i] != NULL ; i ++ )
	{
		int n[3];
		sscanf( items[i], "bp%d_%d_%d", &n[BIND_OUT_P],&n[BIND_ENTRY],&n[BIND_IN_P] );
		fx_slot_t *rel = (fx_slot_t*) sample_get_fx_port_ptr( srd, n[BIND_ENTRY]);

		if( n[BIND_OUT_P] == p_num && vevo_property_get( slot->bind, items[i],0,NULL) == VEVO_NO_ERROR)
		{
			void *filter_template = NULL;
			int error = vevo_property_get( rel->fx_instance, "filter_templ",0 ,&filter_template );
			char *fxname = vevo_property_get_string( filter_template, "name" );

			char *pname = _get_in_parameter_name( rel->fx_instance, n[BIND_IN_P] );
			char list_item[128];
			snprintf(list_item, 128, "fx_%d '%s' p%d '%s'",
					n[BIND_ENTRY], fxname, n[BIND_IN_P],pname );

			veejay_message_add_argument( osc_send, msg, "s", list_item );

			free(fxname);
			free(pname);	
		}
		free(items[i]);
	}
	free(items);
	veejay_message_pulldown_done(osc_send, msg);

}


void		vevosample_ui_construct_fx_bind_window( void *sample, int k, int p_id )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int id = srd->primary_key;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	fx_slot_t *sl = sample_get_fx_port_ptr( srd, k);
	if(!sl->fx_instance)
		return;

	int i;
	char window_title[128];
	char fx_frame[128];
	char *pname = _get_out_parameter_name( sl->fx_instance, p_id );
	void *filter_template = NULL;
	int error = vevo_property_get( sl->fx_instance, "filter_templ",0 ,&filter_template );
	char *fxname = vevo_property_get_string( filter_template, "name" );
	char fx_label[128];
	char owner[64];
	sprintf(owner,"SampleBind%dFX%d", id,k );

	snprintf(window_title,128, "Bind output from %s",fxname);
	snprintf(fx_label,128,     "Bind '%s' to", pname );
	
	char *window = vevosample_ui_new_window( srd, owner, k, window_title, "OP", p_id );

	if(!window)
		return;
	
	sprintf(fx_frame, "NewBind%d_%d_%d",k,p_id,id );
	char bindpath[128];
	sprintf(bindpath , "/sample_%d/fx_%d/bind", id,k );
	
	veejay_ui_bundle_add( osc_send, "/create/frame", "sssx", window, fx_frame, fx_label );

	//@ create pulldown with available Slots	
	void *msg = veejay_message_new_pulldown( osc_send, window, fx_frame, "combobox_load_ip", "FX Slot","/veejay/ipreq", 0, "none" );
	veejay_message_add_argument( osc_send, msg, "s", "None" );

	char list_item[128];
	for( i = 0;i  < SAMPLE_CHAIN_LEN ; i ++ )
	{
		fx_slot_t *slot = sample_get_fx_port_ptr( srd, i );
		if(slot->fx_instance)
		{
			void *ft = NULL;
			int error = vevo_property_get( slot->fx_instance, "filter_templ",0,&ft );
			char *plug = vevo_property_get_string( ft, "name" );
			sprintf(list_item, "FX %d %s", i, plug );
			free(plug);
			veejay_message_add_argument( osc_send, msg, "s", list_item );
		}
	}
	veejay_message_pulldown_done( osc_send, msg );
	
	void *msg2 = veejay_message_new_pulldown(
			osc_send, window, fx_frame, "combobox_list_ip", "Input Parameter", bindpath,0,"none" );
	veejay_message_add_argument( osc_send, msg2, "s", "none" );
	veejay_message_pulldown_done(osc_send, msg2);

	sprintf(fx_frame, "NewBindC%d_%d_%d",k,p_id,id );
	veejay_ui_bundle_add( osc_send, "/create/frame", "sssx", window, fx_frame, "Existing bindings" );
	
	vevosample_ui_construct_bind_list( sample,k, p_id, sl, window, fx_frame );
	veejay_ui_bundle_add( osc_send, "/show/window", "sx", window );

	free(fxname);
	free(pname);
	free(window);
}

void		vevosample_ui_construct_fx_window( void *sample, int k )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int id = srd->primary_key;
	void *osc_send = veejay_get_osc_sender(srd->user_data );
	if(!osc_send)
		return;
	char window_title[128];
	char fx_frame[128];
	char window[128];

	sprintf(window, "Sample%dFX%d", id,k );
	sprintf(fx_frame, "fx_%d", k);
	sprintf(window_title, "Sample %d FX slot %d", id,k );

	if( !vevosample_ui_new_window( srd, "Sample", id, window_title, "FX", k ) )
		return;
	
	vevosample_ui_construct_fx_contents( sample, k,window, fx_frame );
	
	veejay_ui_bundle_add( osc_send, "/show/window", "sx", window );
}


static	char *	vevosample_ui_new_window( sample_runtime_data *src, const char *window_prefix, const int id, const char *title, const char *window_infix, const int suffix)
{

	void *osc_send = veejay_get_osc_sender(src->user_data );
	if(!osc_send)
		return NULL;
	char window[128];
	if(window_infix)
		sprintf(window,"%s%d%s%d", window_prefix, id, window_infix, suffix );
	else
		sprintf(window, "%s%d", window_prefix, id );
	veejay_osc_set_window( osc_send , window );
	veejay_ui_bundle_add( osc_send, "/create/window", "ssx", window, title );
	return strdup(window);
}

//@ create widget that only type none has
static	void	vevosample_ui_type_none( void *sample, const char *window, const char *frame)
{
	uint64_t end = sample_get_end_pos(sample);
	uint64_t sta = sample_get_start_pos(sample);
	char osc_path[128];	
	char tmp_label[128];
	double ns = end-sta;
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *osc_send = veejay_get_osc_sender(srd->user_data );

	int id = srd->primary_key;
	if(ns > 16 ) ns = 16;

	vevosample_ui_new_frame( sample, window, frame, "Navigation" );

	snprintf(osc_path,128, "/sample_%d/video/goto_start", id );
		vevosample_ui_new_button( sample,window, frame, "gs", osc_path,"Goto starting position of sample");
	sprintf(osc_path, "/sample_%d/video/play", id );
		vevosample_ui_new_button( sample,window, frame, ">", osc_path, "Play forward" );		
	sprintf(osc_path, "/sample_%d/video/pause", id );
		vevosample_ui_new_button( sample, window, frame, "||", osc_path, "Pause playing" );
	sprintf(osc_path, "/sample_%d/video/reverse", id );
		vevosample_ui_new_button( sample, window, frame, "r", osc_path, "Play backward" );
	sprintf(osc_path, "/sample_%d/video/goto_end", id );
		vevosample_ui_new_button( sample, window, frame, "ge", osc_path, "Goto ending position of sample" );
	sprintf(osc_path, "/sample_%d/video/prev_frame", id );
		vevosample_ui_new_button( sample, window, frame, "pf", osc_path, "Previous frame" );
	sprintf(osc_path, "/sample_%d/video/next_frame", id );
		vevosample_ui_new_button( sample, window, frame, "nf", osc_path, "Next frame" );
	sprintf(osc_path, "/sample_%d/speed", id );
		vevosample_ui_new_numeric( sample, window, frame, "Speed",
				-1 * ns, ns ,(double) sample_get_speed(sample),
				0,0, "HSlider", osc_path, "i", "none" );

	sprintf(frame, "sample%d_videobar", id );
	vevosample_ui_new_frame( sample,window, frame, "Timeline" );
	sprintf(osc_path, "/sample_%d/current_pos", id );
		vevosample_ui_new_numeric( sample, window, frame, "Position",
				(double) sta, (double) end, (double) sample_get_current_pos(sample),0,0,
				"HSlider", osc_path, "h","none" );


	sprintf(frame, "sample%d_ctrl", id );
	sprintf(tmp_label, "%s","Settings" );
		vevosample_ui_new_frame( sample,window, frame, tmp_label );
	sprintf(osc_path, "/sample_%d/looptype", id );

	void *msg = veejay_message_new_pulldown( osc_send,
			window, frame,"combobox_looptype_list", "Looptype", osc_path, (double) sample_get_looptype( sample ), "none" );

	veejay_message_add_argument( osc_send, msg, "sss", "None","Normal" , "Bounce" );

	veejay_message_pulldown_done( osc_send, msg );

}

//@ create widgets for capture type
static	void	vevosample_ui_type_capture( void *sample, const char *window, const char *frame)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int i;
	char **list = vj_unicap_get_list( srd->data );
	double min=0.0;
	double max=0.0;
	char format[5];
	double dv = 0;
	char path[128];
	int id = srd->primary_key;

	void *osc_send = veejay_get_osc_sender(srd->user_data );

	vevosample_ui_new_frame( sample, window, "capframe", "Settings" );
	vevosample_ui_new_frame( sample, window, "capframe2", "Settings" );

	for( i = 0; list[i] != NULL ; i++ )
	{
		if(vj_unicap_get_range( srd->data, list[i], &min,&max ))
		{
			char *tkey = sample_translate_property(srd,list[i]); // translate property
			
			int type = vevo_property_atom_type( srd->info_port, list[i]);
			if(type==VEVO_ATOM_TYPE_DOUBLE)
				sprintf(format, "%s", "d" );
			else
				sprintf(format, "%s", "i" );

			sprintf(path, "/sample_%d/%s", id, tkey );

			if( vj_unicap_property_is_menu( srd->data, list[i] ))
			{
				vevo_property_get( srd->info_port, list[i],0,&dv );
			//	void *msg = veejay_message_new_pulldown( osc_send,window,"combobox_capframe", "capframe2", list[i], path,dv,"none" );
				void *msg = veejay_message_new_pulldown( osc_send,window,"capframe2", "combobox_capframe", list[i], path,dv,"none" );
	

				vj_unicap_pack_menu( srd->data, list[i], osc_send, msg );
				veejay_message_pulldown_done( osc_send, msg );
			}
			else if( vj_unicap_property_is_range( srd->data, list[i] ) )
			{
				vevosample_ui_new_numeric( sample,window, "capframe",
				       list[i], min,max, (max*0.5), 0,0, "VSlider",
			       		path, format, "none"	       );
			}
			free(tkey);
		}
		free(list[i]);
	}
	free(list);
}

static	void	vevosample_ui_type_shared( void *sample, char *window )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int id = srd->primary_key;
	char osc_path[128];
	char frame_name[128];
	void *osc_send = veejay_get_osc_sender(srd->user_data );


	sprintf(frame_name, "%s", "fxchain" );

	vevosample_ui_new_frame( sample, window, frame_name, "FX slots" );

		vevosample_ui_new_radiogroup(osc_send, window, frame_name, "fx", "fx", SAMPLE_CHAIN_LEN, 0 );
	sprintf(frame_name, "%s", "fx_list" );
		vevosample_ui_new_frame( sample, window, frame_name, "Available plugins" );

	sprintf(osc_path, "/sample_%d/fx_X/set", id );
	void *msg = veejay_message_new_linked_pulldown( osc_send,
			window,frame_name,"Plugins", osc_path, "s", "Add plugin to FX slot" );
		plug_concatenate_all( osc_send, msg );
	veejay_message_linked_pulldown_done( osc_send, msg );
	vevosample_ui_new_button( sample, window, frame_name, "clear", "none", "Clear selected FX slot" );

	
	sprintf(frame_name, "%s", "sample_action" );
	vevosample_ui_new_frame( sample, window, frame_name, "Actions" );
	sprintf(osc_path, "/sample_%d/rec/start", id );
	vevosample_ui_new_button( sample,window, frame_name, "rec start", osc_path, "Start recording from sample" );
	sprintf(osc_path, "/sample_%d/rec/stop", id );
	vevosample_ui_new_button( sample,window, frame_name, "rec stop", osc_path, "Stop recording from sample" );
	sprintf(osc_path, "/sample_%d/print", id );
	vevosample_ui_new_button( sample, window, frame_name, "osc", osc_path, "Print OSC message space" );
	sprintf(osc_path, "/sample_%d/delete", id );
	vevosample_ui_new_button( sample, window, frame_name, "destroy", osc_path, "Destry this sample" );

	if(srd->type == VJ_TAG_TYPE_NONE)
		vevosample_ui_new_button( sample, window, frame_name, "clone", "clone","Copy sample to new sample" );
	
}

void		vevosample_construct_ui_fx_chain(void *sample)
{
	int k;
	for( k = 0; k < SAMPLE_CHAIN_LEN ; k ++ )
	{
		if( sample_get_fx_status( sample, k ))
			vevosample_ui_construct_fx_window( sample, k );
	}
}


char *		vevosample_construct_ui(void *sample)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	char fx_frame[128];
	char window_title[128];
	void *osc_send = veejay_get_osc_sender(srd->user_data );


	sprintf(window_title, "Sample %d", srd->primary_key );

	char *window = vevosample_ui_new_window( srd, "Sample", srd->primary_key, window_title ,NULL,0);
	if(!window)
		return NULL;	
	
	char tmp_label[128];
	char osc_path[128];
	char frame_name[128];
	sprintf(frame_name, "sample%dactions", srd->primary_key );

	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
			vevosample_ui_type_none( sample, window, frame_name );
			break;
		case VJ_TAG_TYPE_CAPTURE:
			vevosample_ui_type_capture( sample,window,frame_name );
			break;
	}

	
	vevosample_ui_type_shared( sample, window );

	veejay_ui_bundle_add( osc_send, "/show/window", "sx", window );

	return window;
}

