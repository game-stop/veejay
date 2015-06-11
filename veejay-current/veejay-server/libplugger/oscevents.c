/* veejay - Linux VeeJay
 * 	     (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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



#include <config.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <libvevo/libvevo.h>
#include <veejay/defs.h>
#include <veejay/veejay.h>
#include <veejay/libveejay.h>
#include <libplugger/plugload.h>
#include <vevosample/vevosample.h>
#include <vevosample/uifactory.h>
#include <lo/lo.h>
#include <ctype.h>

void	osc_sample_edl_paste_at( void *ptr,const char *path, const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	if(sample_edl_paste_from_buffer( ptr, (uint64_t) argv[0]->h ))
		veejay_msg( VEEJAY_MSG_INFO, "Pasted frames from buffer to position %lld",
				argv[0]->h );
	else
		veejay_msg(VEEJAY_MSG_INFO, "Unable to paste frames at position %lld",
				argv[0]->h );
}
void	osc_sample_edl_cut( void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	if(sample_edl_cut_to_buffer( sample, (uint64_t) argv[0]->h, (uint64_t) argv[1]->h ))
		veejay_msg(VEEJAY_MSG_INFO, "Cut frames %lld - %lld to buffer",argv[0]->h,argv[1]->h);
	else
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to cut frames %lld - %lld",argv[0]->h,argv[1]->h);
}

void	osc_sample_edl_copy(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	if( sample_edl_copy( sample, (uint64_t) argv[0]->h,(uint64_t) argv[1]->h ))
		veejay_msg(VEEJAY_MSG_INFO, "Copied frames %lld - %lld to buffer",argv[0]->h,argv[1]->h);

	else
		veejay_msg(VEEJAY_MSG_INFO, "Unable to copy frames %lld - %lld",argv[0]->h,argv[1]->h);
}
void	osc_sample_edl_del(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	if( sample_edl_delete( sample, (uint64_t) argv[0]->h,(uint64_t) argv[1]->h ))
		veejay_msg(VEEJAY_MSG_INFO, "Deleted frames %lld - %lld to buffer",argv[0]->h,argv[1]->h);

	else
		veejay_msg(VEEJAY_MSG_INFO, "Unable to delete frames %lld - %lld",argv[0]->h,argv[1]->h);
}
void	osc_sample_play_forward(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	int speed = sample_get_speed( sample );
        if(speed <= 0)
        {
            if(speed == 0)
              speed = -1;
            speed *= -1;
	    
            sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&speed);
        }
}
void	osc_sample_play_reverse(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	int speed = sample_get_speed( sample );
        if(speed >= 0)
        {
            if(speed == 0)
              speed = -1;
            speed *= -1;
	    
            sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&speed);
        }
}
void	osc_sample_play_pause(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	int speed = sample_get_speed( sample );
        if(speed != 0)
        {
            speed = 0;
            sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&speed);
        }
}

void	osc_sample_set_frame(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	uint64_t pos = sample_get_current_pos( sample );
	uint64_t user_pos = argv[0]->h;
        if(pos != user_pos)
        {
        	if( sample_valid_pos( sample, user_pos ))
                {
                	sample_set_property_ptr(sample, "current_pos",
					VEVO_ATOM_TYPE_UINT64, &user_pos );
                        veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld",user_pos);
		}
                else
                {
                      veejay_msg(VEEJAY_MSG_ERROR, "Position %lld outside sample",user_pos);
                }
	}
}
void	osc_sample_set_next_frame(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	uint64_t pos = sample_get_current_pos( sample );
	uint64_t user_pos = pos + 1;
        if(pos != user_pos)
        {
        	if( sample_valid_pos( sample, user_pos ))
                {
                	sample_set_property_ptr(sample, "current_pos",
					VEVO_ATOM_TYPE_UINT64, &user_pos );
                        veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld",user_pos);
		}
                else
                {
                      veejay_msg(VEEJAY_MSG_ERROR, "Position %lld outside sample",user_pos);
                }
	}
}

void	osc_sample_set_prev_frame(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;
	uint64_t pos = sample_get_current_pos( sample );
	uint64_t user_pos = pos - 1;
        if(pos != user_pos)
        {
        	if( sample_valid_pos( sample, user_pos ))
                {
                	sample_set_property_ptr(sample, "current_pos",
					VEVO_ATOM_TYPE_UINT64, &user_pos );
                        veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld",user_pos);
		}
                else
                {
                      veejay_msg(VEEJAY_MSG_ERROR, "Position %lld outside sample",user_pos);
                }
	}
}

void	osc_sample_goto_start(void *sample, const char *path, const char *types, void **dargv, void *raw )
{
	uint64_t pos = sample_get_start_pos( sample );
                sample_set_property_ptr( sample, "current_pos",
				VEVO_ATOM_TYPE_UINT64, &pos );
}
void	osc_sample_goto_end(void *sample, const char *path, const char *types, void **dargv , void *raw)
{
	uint64_t pos = sample_get_end_pos( sample );
                sample_set_property_ptr( sample, "current_pos",
				VEVO_ATOM_TYPE_UINT64, &pos );
   //     veejay_msg(VEEJAY_MSG_INFO, "Position changed to %lld", pos);
}
void	osc_sample_reset_fx(void *sample,const char *path,  const char *types, void **dargv, void *raw )
{
	sample_fx_chain_reset( sample );
}

void	osc_sample_reset_fx_entry( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
	 if(sample_fx_chain_entry_clear( sample, fx_id ) )
                veejay_msg(VEEJAY_MSG_INFO, "Cleared FX slot %d",fx_id );
}
void	osc_sample_alpha_fx_entry( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}

	double alpha = argv[0]->d;
	sample_set_fx_alpha( sample, fx_id, alpha );
}
void	osc_sample_status_fx_entry( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	sample_set_fx_status( sample, fx_id, argv[0]->i );
}

void	osc_sample_channel_fx_entry( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}

	int seq = argv[0]->i;
	int id = argv[1]->i;
	
	if( find_sample(id) )
	{
		int n = sample_fx_set_in_channel( sample, fx_id, seq,id );
		if(n)
                        veejay_msg(VEEJAY_MSG_ERROR, "Input channel %d set to sample %d", seq, id );
	}
}
void	osc_sample_set_fx_entry( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}

	char *s = (char*) &(argv[0]->s);
	int fx = plug_get_fx_id_by_name( s );
        if( fx < 0 )
               veejay_msg(VEEJAY_MSG_ERROR, "FX '%s' not found", s );
        else
        {
              if( sample_fx_set( sample, fx_id,fx ) )
                     veejay_msg(VEEJAY_MSG_INFO,
                                       "Added '%s' to fx_%d",
                                                s,fx_id);
	}
}
void 	osc_sample_bind_fx_entry( void *sample, const char *path, const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
        void *src_entry = sample_get_fx_port_ptr( sample,fx_id );
        if(src_entry == NULL )
        {
                veejay_msg(0,"Invalid fx entry");
                return;
        }

       int error = sample_new_bind( sample, src_entry, argv[0]->i, argv[1]->i, argv[2]->i );
       if( error == VEVO_NO_ERROR )
       {
            veejay_msg(0, "Pushing out parameters on entry %d parameter %d -> Entry %d parameter %d",
                                                fx_id,argv[0]->i,argv[1]->i,argv[2]->i);
       }
}

void 	osc_sample_del_bind( void *sample, const char *path, const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
        void *src_entry = sample_get_fx_port_ptr( sample,fx_id );
        if(src_entry == NULL )
        {
                veejay_msg(0,"Invalid fx entry");
                return;
        }
	veejay_msg(0, "sample_del_bind on FX %d, Parameter %d, RFX %d, RIP %d",
			fx_id,  argv[0]->i, argv[1]->i, argv[2]->i);
 	int error = sample_del_bind( sample, src_entry, fx_id, argv[0]->i,argv[1]->i, argv[2]->i );
        if( error == VEVO_NO_ERROR )
        {
            veejay_msg(0, "Detached output parameter %d on entry %d", argv[0]->i,fx_id );
        }
}

void 	osc_sample_release_fx_entry( void *sample, const char *path, const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
        void *src_entry = sample_get_fx_port_ptr( sample,fx_id );
        if(src_entry == NULL )
        {
                veejay_msg(0,"Invalid fx entry");
                return;
        }

//@ delete entire port!
	
 	int error = sample_del_bind_occ( sample, src_entry,argv[0]->i,fx_id);
        if( error == VEVO_NO_ERROR )
        {
            veejay_msg(0, "Detached output parameter %d on entry %d", argv[0]->i,fx_id );
        }
}

/*void 	osc_sample_bind_osc_fx_entry( void *sample, const char *path, const char *types, void **dargv)
{
	lo_arg **argv = (lo_arg**) dargv;

	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
	sample_add_osc_path( sample, (char*) &(argv[0]->s), fx_id );
}
void 	osc_sample_release_osc_fx_entry( void *sample, const char *path, const char *types, void **dargv)
{
	lo_arg **argv = (lo_arg**) dargv;

	int fx_id = sample_extract_fx_entry_from_path(sample, path );
	if( fx_id < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Outside chain boundary");
		return;
	}
	sample_del_osc_path( sample, fx_id );
}


void	osc_sample_parameter_sender_start( void *sample,const char *path,  const char *types, void **dargv)
{
	lo_arg **argv = (lo_arg**) dargv;
	sample_new_osc_sender( sample, (char*)&(argv[0]->s), (char*) &(argv[1]->s) );
}

void	osc_sample_parameter_sender_stop( void *sample,const char *path,  const char *types, void **dargv)
{
	sample_close_osc_sender( sample );
}*/

void	osc_sample_config_record(void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int error = sample_configure_recorder( sample,
			argv[0]->i, (char*) &(argv[2]->s), argv[1]->i );
        if( error )
               veejay_msg(0, "Unable to configure the recorder");
}
void	osc_sample_record_start(void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int error = sample_start_recorder( sample );
        if( error )
               veejay_msg(0, "Unable to start the sample recorder");
}
void	osc_sample_record_stop(void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int error = sample_stop_recorder( sample );
        if( error )
               veejay_msg(0, "Unable to stop the sample recorder");
}

void	osc_veejay_ui_create_sample( void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int id = argv[0]->i;
	veejay_t *v = (veejay_t*) info;
	if( id == 0 )
		id = sample_get_key_ptr(v->current_sample);
	
	veejay_create_sample_ui( info, id );
}

void	osc_veejay_ui_init( void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	char *uri = osc_get_uri( raw );
	veejay_add_client( info,  uri );

	veejay_init_ui( info, uri );

	free(uri);
}
void	osc_veejay_setup_preview( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	lo_arg **argv = (lo_arg**) dargv;

	veejay_t *v = (veejay_t*) info;
	if( performer_setup_preview( v, argv[0]->i, argv[1]->i ) )
		veejay_msg(0, "Configured preview , reduce by factor %d, mode %d",argv[0]->i,argv[1]->i );
}
void	osc_veejay_ui_tick( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	char *uri = osc_get_uri( raw );

	samplebank_tick_ui_client( uri );

	free(uri);

}
void	osc_veejay_ui_blreq_window( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	veejay_t *v = (veejay_t*) info;
	lo_arg **argv = (lo_arg**) dargv;
	void *sample = find_sample( argv[0]->i );
	if(!sample)
	{
		veejay_msg(0,"sample %d does not exist",argv[0]->i);
		return;
	}
	vevosample_ui_get_bind_list( sample, (char*) &argv[1]->s );
}


void	osc_veejay_ui_ipreq_window( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	veejay_t *v = (veejay_t*) info;
	lo_arg **argv = (lo_arg**) dargv;
	void *sample = find_sample( argv[0]->i );
	if(!sample)
	{
		veejay_msg(0,"sample %d does not exist",argv[0]->i);
		return;
	}
	if( sample_has_fx( sample, argv[1]->i ))
	{
		vevosample_ui_get_input_parameter_list( sample, argv[1]->i, (char*) &argv[2]->s );

	}
}

void	osc_veejay_ui_bindreq_window( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	veejay_t *v = (veejay_t*) info;
	lo_arg **argv = (lo_arg**) dargv;
	void *sample = find_sample( argv[0]->i );
	if(!sample)
	{
		veejay_msg(0,"sample %d does not exist",argv[0]->i);
		return;
	}
	if( sample_has_fx( sample, argv[1]->i ))
	{
		vevosample_ui_construct_fx_bind_window( sample, argv[1]->i, argv[2]->i );
	}
	else
		veejay_msg(0, "FX %d on sample %d not active", argv[0]->i,
				argv[1]->i);
}

void	osc_veejay_clone_sample( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	veejay_t *v = (veejay_t*) info;
	lo_arg **argv = (lo_arg**) dargv;
	void *sample = find_sample( argv[0]->i );
	if(!sample)
	{
		veejay_msg(0,"sample %d does not exist",argv[0]->i);
		return;
	}

	if( sample_clone_from( v, sample, v->video_info ))
	{
		veejay_msg(0, "Cloned sample %d" );
	}

}
		
void	osc_veejay_ui_request_window( void *info, const char *path, const char *types, void **dargv, void *raw )
{
	veejay_t *v = (veejay_t*) info;
	lo_arg **argv = (lo_arg**) dargv;
	void *sample = find_sample( argv[0]->i );
	if(!sample)
	{
		veejay_msg(0,"sample %d does not exist",argv[0]->i);
		return;
	}
	if( sample_has_fx( sample, argv[1]->i ))
	{
		vevosample_ui_construct_fx_window( sample, argv[1]->i );
		veejay_msg(0,"constructed fx panel");
	}
	else
		veejay_msg(0, "FX %d on sample %d not active", argv[0]->i,
				argv[1]->i);
}

void	osc_veejay_quit(void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	veejay_change_state( info, VEEJAY_STATE_STOP );
}
void	osc_veejay_resize(void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	veejay_resize_screen( info, argv[0]->i,argv[1]->i,argv[2]->i,argv[3]->i);
}
void	osc_veejay_fullscreen(void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	veejay_fullscreen( info, argv[0]->i );
}
void	osc_veejay_select( void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int id = argv[0]->i;
	veejay_t *v = (veejay_t*) info;
	void *sample = v->current_sample;
	void *res    = sample;
	if( id > 0 )
	{
		res = find_sample( id );
	}
	else if( id == -1 )
		res = sample_last();

	if(sample == res)
        {
            uint64_t pos = sample_get_start_pos( v->current_sample );
            sample_set_property_ptr(
                    v->current_sample, "current_pos", VEVO_ATOM_TYPE_UINT64, &pos );
        }
        else
        {
	     veejay_msg(VEEJAY_MSG_INFO, "Play sample %d", id );
            // sample_save_cache_data( v->current_sample );
             v->current_sample = res;
        }       
}
void	osc_veejay_new_sample(void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int    type  = argv[0]->i;
	int    token = argv[1]->i;
	char   *str  = &(argv[2]->s);
	veejay_t *v  = (veejay_t*) info;

	
	void *sample = sample_new( type );
	if(!sample)
	{
		veejay_msg(0, "Invalid type");
		return;
	}
	if(sample_open( sample, str,token, v->video_info))
        {
        	int  new_id = samplebank_add_sample( sample );
		const char *type_str = sample_describe_type( type );

                sample_set_user_data( sample,info,new_id );
                veejay_msg(VEEJAY_MSG_INFO,"Created new %s from %s as Sample %d",
                               type_str, str, new_id );
	}       
        else
        {
                 veejay_msg(VEEJAY_MSG_ERROR,"Could not create sample from %s",token);
        }       
}

void	osc_veejay_del_sample(void *info,const char *path,  const char *types, void **dargv, void *raw)
{
	lo_arg **argv = (lo_arg**) dargv;
	int    id     = argv[0]->i;
	veejay_t *v   = (veejay_t*) info;

	void   *sample = find_sample(id);

	if(sample == v->current_sample)
		veejay_msg(0, "Cannot delete current playing sample");
	else
		if(sample_delete( id ) )
			veejay_msg(0, "Deleted sample %d", id );
}

void	osc_sample_print( void *sample,const char *path,  const char *types, void **dargv, void *raw)
{
	sample_osc_namespace( sample );
}

/*
void	osc_sample_khagan( void *sample,const char *path,  const char *types, void **dargv)
{
	sample_produce_khagan_file( sample );
}*/


static struct
{
	const char *name;
	const char *format;
	const char *args[4];
	const char *descr;
	vevo_event_f func;
} fx_events_[] = {
	{	"clear",	NULL, {NULL,NULL,NULL,NULL },	"Delete plugin"	,		osc_sample_reset_fx_entry },
	{	"alpha",	"d",  {"Alpha value 0.0 - 1.0",NULL,NULL,NULL }, "Opacity",	osc_sample_alpha_fx_entry },
	{	"status",	"i",  {"On=1, Off=0",NULL,NULL,NULL }, "Status",		osc_sample_status_fx_entry },
	{	"input_channel","ii", {"Input Channel", "Sample ID", NULL,NULL}, "Set a plugin's input channel",
												osc_sample_channel_fx_entry },
	{	"set",		"s",  {"FX plugin name", NULL,NULL,NULL }, "Initialize a plugin",
												osc_sample_set_fx_entry },
	{	"bind",		"iii",{"Output parameter ID",
				       "Bind to FX entry",
			 	       "Input Parameter ID",
				       NULL }, "Bind output parameter to some input parameter",
											osc_sample_bind_fx_entry },
	{	"release",	"i", { NULL,NULL,NULL }, "Release bind between output and input parameter",
											osc_sample_release_fx_entry },
	{	"unbind",	"iii", { "Output parameter ID",
					"Bind to FX entry",
					"Input Parameter ID",NULL }, "Release a single bind",
											osc_sample_del_bind },
	/*{	"bind_osc",	"s",  { "OSC message", NULL,NULL,NULL }, "Bind an OSC Path to an output parameter",
											osc_sample_bind_osc_fx_entry },
	{	"release_osc",	NULL, { NULL,NULL,NULL,NULL},	"Release OSC Path",
											osc_sample_release_osc_fx_entry }, */
	{	NULL,		NULL, { NULL,NULL,NULL,NULL},	NULL,				NULL	}
};

static struct
{
	const char *name;
	const char *format;
	const char *args[4];
	const char *descr;
	vevo_event_f func;
} sample_generic_events_[] =
{
	{	"reset_fx",	NULL,	{ NULL,NULL,NULL,NULL }, "Delete all plugins",			osc_sample_reset_fx },
/*	{	"register_param_sender","ss", { "Address" , "Port Number",NULL,NULL },
	      							 "Initialize a new OSC sender",
							 						osc_sample_parameter_sender_start },
	{ 	"unregister_param_sender", NULL, { NULL, NULL,NULL,NULL },"Close OSC sender",
													osc_sample_parameter_sender_stop },*/
	{	"rec/config",	"iis", { "Format", "Frames", "Filename" , NULL },"Configure sample recorder",
													osc_sample_config_record },
	{	"rec/start",	NULL,	{ NULL,NULL,NULL,NULL },"Start recording from sample",
													osc_sample_record_start },	
	{	"rec/stop",	NULL,	{ NULL,NULL,NULL,NULL },"Stop recording from sample",
													osc_sample_record_stop },
	{	"print",	NULL,	{ NULL,NULL,NULL,NULL },"Print OSC namespace",			osc_sample_print	},
//	{	"khagan",	NULL,	{ NULL,NULL,NULL,NULL },"Write XML file for khagan",		osc_sample_khagan },
	{ 	NULL,	NULL,		{ NULL,NULL,NULL,NULL },NULL,					NULL },
	
};

static struct
{
	const char *name;
	const char *format;
	const char *args[4];
	const char *descr;
	vevo_event_f func;
} sample_nonstream_events_[] =
{
	{	"video/play",	NULL,		{ NULL,NULL,NULL,NULL },"Play video forward",		osc_sample_play_forward },
	{	"video/reverse",NULL,		{ NULL,NULL,NULL,NULL },"Play video backward",		osc_sample_play_reverse },
	{	"video/pause",	NULL,		{ NULL,NULL,NULL,NULL },"Pause video",			osc_sample_play_pause   },
	{	"video/frame",	"h",		{ "Frame number",NULL,NULL,NULL }, "Set a frame",	osc_sample_set_frame	},
	{	"video/next_frame",NULL,	{ NULL,NULL,NULL,NULL },"Goto next frame",		osc_sample_set_next_frame },
	{	"video/prev_frame",NULL,	{ NULL,NULL,NULL,NULL },"Goto previous frame",		osc_sample_set_prev_frame },
	{	"video/goto_start", NULL,	{ NULL,NULL,NULL,NULL },"Goto starting position",	osc_sample_goto_start },
	{	"video/goto_end", NULL,		{ NULL,NULL,NULL,NULL },"Goto ending position",		osc_sample_goto_end },
	{	"edl/paste_at",	"h",		{ "Destination frame number", NULL,NULL,NULL },"Paste from buffer at position", osc_sample_edl_paste_at },
	{	"edl/cut",	"hh",		{ "Selection start","Selection end",NULL,NULL },"Cut to buffer from selection",	osc_sample_edl_cut },
	{	"edl/copy",	"hh",		{ "Selection start","Selection end",NULL,NULL },"Copy to buffer from selection",osc_sample_edl_copy },
	{	"edl/del",	"hh",		{ "Selection start","Selection end",NULL,NULL },"Delete without using buffer",	osc_sample_edl_del },
	{ 	NULL,	NULL,		{ NULL,NULL,NULL,NULL },			NULL },
	
};


static struct
{
	const char *name;
	const char *format;
	const char *args[4];
	const char *descr;
	vevo_event_f func;
} veejay_events_[] =
{
	{	"quit",	NULL,		{ NULL,NULL,NULL,NULL },"Quit Veejay-NG",		osc_veejay_quit },	
	{	"resize", "iiii",	{ "X offset", "Y offset", "Width", "Height" },"Resize video window", 	osc_veejay_resize },
	{	"fullscreen","i",	{ "On=1, Off=0",NULL,NULL,NULL },"Fullscreen video window",		osc_veejay_fullscreen },
	{	"select", "i",		{ "Sample ID", NULL,NULL,NULL },"Select a sample",		osc_veejay_select },
	{	"new"	, "iis",	{ "Sample Type", "Numeric value", "Filename" , NULL }, "Create a new sample", osc_veejay_new_sample },
	{	"del"	, "i",		{ "Sample ID", NULL, NULL, NULL },"Delete sample",		osc_veejay_del_sample },
	{	"ui"	, NULL,		{ NULL,NULL , NULL,NULL}, "Remote is UI ",	osc_veejay_ui_init },
	{	"show"  , "i",		{ "Sample ID", NULL, NULL, NULL }, "View a sample",		osc_veejay_ui_create_sample },
	{	"request", "ii",	{ "Sample ID" , "FX Id",NULL,NULL }, "Request fx panel",	osc_veejay_ui_request_window },
	{	"bindreq", "iii",	{ "Sample ID" , "FX Id","Parameter ID",NULL }, "Request a bind panel",	osc_veejay_ui_bindreq_window },
	{	"ipreq", "iis",		{ "Sample ID" , "FX Id","Window Name",NULL }, "Request a parameter list",osc_veejay_ui_ipreq_window },
	{	"blreq", "is",		{ "Sample ID" , "Window Name",NULL,NULL }, "Request a bind list",osc_veejay_ui_blreq_window },
	{	"tick", NULL,		{ NULL,NULL,NULL,NULL },		"UI is still alive",	osc_veejay_ui_tick },
	{	"previewconfig","ii",	{ "Reduce Factor", "Preview Mode",NULL,NULL }, "Configure preview", osc_veejay_setup_preview },
	{	"clone",	"i",  { "Sample ID", NULL, NULL,NULL }, "Clone sample", osc_veejay_clone_sample },
	{ 	NULL,	NULL,		{ NULL,NULL,NULL,NULL },		NULL,	NULL },

};


static void		osc_fx_generic_event(
		lo_server_thread *st,
		void *user_data,
		void *osc_port,
		void *vevo_port,
		const char *base,
		const char *key,
	        const char *format,
		const char **args,
		const char *descr,
		vevo_event_f *func	)
{
	vevosample_new_event(
			user_data,
			osc_port,
			vevo_port, // Instance!
			base,
			key,
			format,
			args,
			descr,
			func,
			-1
	);
}
void		osc_add_sample_generic_events( lo_server_thread *st, void *user_data, void *osc_port, void *vevo_port, const char *base, int chain_len )
{
	int i;
	for( i = 0; sample_generic_events_[i].name != NULL ; i ++ )
	{
		vevosample_new_event(
			user_data,	
			osc_port,
			vevo_port,
			base,
			sample_generic_events_[i].name,
			sample_generic_events_[i].format,
			sample_generic_events_[i].args,
			sample_generic_events_[i].descr,
			sample_generic_events_[i].func,
			-1 );
	}

	for( i =0; i < chain_len ; i ++ )
	{
		char name[20];
		sprintf(name, "%s/fx_%d", base, i );
	
		int k;
		for( k = 0; fx_events_[k].name != NULL ; k ++ )
		{
			vevosample_new_event(
				user_data,
				osc_port,
				vevo_port,
				name,
				fx_events_[k].name,
				fx_events_[k].format,
				fx_events_[k].args,
				fx_events_[k].descr,
				fx_events_[k].func,
			        -1
			);
		}		
	}
}


void		osc_add_sample_nonstream_events( lo_server_thread *st, void *user_data, void *osc_port, void *vevo_port, const char *base )
{
	int i;
	for( i =0 ; sample_nonstream_events_[i].name != NULL ; i ++ )
	{
		vevosample_new_event(
			user_data,
			osc_port,
			vevo_port,
			base,
			sample_nonstream_events_[i].name,
			sample_nonstream_events_[i].format,
			sample_nonstream_events_[i].args,
			sample_nonstream_events_[i].descr,
			sample_nonstream_events_[i].func,
			0
		);
	}
}

void		osc_add_veejay_events( lo_server_thread *st, void *user_data, void *osc_port, const char *base )
{
	int i;
	for( i = 0; veejay_events_[i].name != NULL ; i ++ )
	{
		veejay_new_event(
			user_data,
			osc_port,
			user_data,
			base,
			veejay_events_[i].name,
			veejay_events_[i].format,
			veejay_events_[i].args,
			veejay_events_[i].descr,
			veejay_events_[i].func,
			0
		);
	}
}


