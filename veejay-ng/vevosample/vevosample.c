/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
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

/** \defgroup vevo_sample VeVo Samples
 *
 *   Each video related object in veejay is called a Sample.      
 *   
 *   Types of samples:
 *     -# Edit Descision Lists
 *     -# MJPEG avi file(s)
 *     -# YUV4MPEG stream
 *     -# Source generator (solid color)
 */



#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include	<config.h>
#include	<string.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	<libxml/xmlmemory.h>
#include	<libxml/parser.h>
#include	<vevosample/xmlutil.h>
#include	<libvjmsg/vj-common.h>
#include	<libhash/hash.h>
#include	<libvevo/libvevo.h>
#include	<libel/vj-el.h>
#include	<libel/lav_io.h>
#include	<veejay/defs.h>
#include	<vevosample/defs.h>
#include	<vevosample/vevosample.h>
#include	<vevosample/ldefs.h>
#include	<libplugger/plugload.h>
#include	<libvjnet/vj-client.h>
#include	<vevosample/vj-yuv4mpeg.h>
#include	<libplugger/defs.h>
#include	<libplugger/ldefs.h>
#include	<libvjaudio/audio.h>
#include	<veejay/oscsend.h>
#ifdef USE_GDK_PIXBUF
#include	<libel/pixbuf.h>
#endif
#define XMLTAG_SAMPLES	"veejay_samples"
#define XMLTAG_SAMPLE	"sample"

#define	KEY_LEN		64
#define SAMPLE_LIMIT		4096

#define	VEVO_SAMPLE_PORT 	700				/* Port identifiers, hashtables */
#define	VEVO_SAMPLE_FX_PORT	707
#define	VEVO_SAMPLE_FX_ENTRY	709
#define	VEVO_SAMPLE_BANK_PORT	800

#define VEVO_VALUES_PORT	46				/* Linked list of values (cached) */
#define	VEVO_FRAMES_PORT	47				/* Linked list of channel identifiers */
#define VEVO_FX_ENTRY_PORT	45
#define VEVO_FX_VALUES_PORT	44
#define VEVO_FX_CHANNELS_PORT	43


#define my_cmp(a,b) xmlStrcmp( a->name, (const xmlChar*) b )

static	int	num_samples_ = 0;				/* total count of samples */
static	int	free_slots_[SAMPLE_LIMIT];				/* deleted sample id's */

static	void	*sample_bank_ = NULL;				/* root of samplebank */
static	void	*unicap_data_ = NULL;

static	void	*ui_register_ = NULL;




/* forward */
static	void	sample_fx_clean_up( void *port, void *user_data );
static	void	*sample_get_fx_port_values_ptr( int id, int fx_entry );
static	void	sample_expand_properties( void *sample, const char *key, xmlNodePtr root );
static	void	sample_expand_port( void *port, xmlNodePtr node );
static  void	sample_close( sample_runtime_data *srd );
static	void	sample_new_fx_chain(void *sample);
char     *sample_translate_property( void *sample, char *name ) ;
static char     *sample_reverse_translate_property( void *sample, char *name ) ;
static void		sample_ui_close_all( void *sample );
	
/*
 *	1. cache sample properties to sampleinfo
 *      2. do events
 *      3. update sample properties 
 *      4. do work
 *      5. uncache 
 *      6. goto 1
 *
 */



/*
 * to reduce vevo set/get , cache each step */


static	struct
{
	const char *name;
} protected_properties[] = 
{
 	{	"type"		},	
	{	"audio_spas"	},
	{ 	"bits"		},
	{	"rate"		},
	{	"has_audio"	},
	{	"channels"	},
	{	"fx_osc"	},
	{	"primary_key"	},
	{	"fps"		},
	{	"bps"		},
	{	NULL		}
};


int	compare_elements( char **p1, char **p2 ) { return strcoll(*p1,*p2); }


void	trap_vevo_sample() {}

/**
 * elements in random access sample
 *
 */
static struct
{
	const char *name;
	int   atom_type;
} sample_property_list[] =
{
	{	"start_pos",	VEVO_ATOM_TYPE_UINT64	},	/* Starting position */
	{	"end_pos",	VEVO_ATOM_TYPE_UINT64	},	/* Ending position */
	{	"speed",	VEVO_ATOM_TYPE_INT	},	/* Trickplay, speed */
	{	"repeat",	VEVO_ATOM_TYPE_INT	},
	{	"current_pos",  VEVO_ATOM_TYPE_UINT64	},	/* Current position */
	{	"fps",		VEVO_ATOM_TYPE_DOUBLE	},	/* video fps */
	{	"looptype",	VEVO_ATOM_TYPE_INT	},	/* Loop type , normal, pingpong or none */
	{	"in_point",	VEVO_ATOM_TYPE_UINT64	},	/* In marker point */
	{	"out_point",	VEVO_ATOM_TYPE_UINT64	},
	{	"marker_lock",	VEVO_ATOM_TYPE_INT	},	/* Marker length locked */
	{	"fx_fade",	VEVO_ATOM_TYPE_INT	},	/* Fade FX -> Original sample */
	{	"fx_fade_value", VEVO_ATOM_TYPE_DOUBLE	},
	{	"fx_fade_dir",	VEVO_ATOM_TYPE_INT	},
	{	"current_entry", VEVO_ATOM_TYPE_INT	},	/* Current active Entry */
	{	NULL,		0			}
};

/**
 * elements common in all types of samples
 */
static struct
{
	const char *name;
	int atom_type;
} common_property_list[] = 
{
	{	"title",	VEVO_ATOM_TYPE_STRING	},	/* Title of this sample */
	{	"primary_key",	VEVO_ATOM_TYPE_INT	},	/* Primary key of this sample */
	{	NULL,		0			}
};

/**
 *  sample recorder elements
 */
static struct
{
	const char *name;
	int atom_type;
} recorder_state_list[] = {
	{	"format",	VEVO_ATOM_TYPE_INT	},	
	{	"basename",	VEVO_ATOM_TYPE_STRING	},
	{	"sequence",	VEVO_ATOM_TYPE_STRING	},
	{	"frames_done",	VEVO_ATOM_TYPE_INT	},
	{	"paused",	VEVO_ATOM_TYPE_BOOL	},	
	{	"switch",	VEVO_ATOM_TYPE_BOOL	},
	{	NULL,		0			}
};

/**
 * 	stream properties
 */
static	struct
{
	const char *name;
	int atom_type;
} stream_property_list[] = {
	{	"active",	VEVO_ATOM_TYPE_INT	},
	{	"data",		VEVO_ATOM_TYPE_VOIDPTR	},
	{	NULL,		0			}
};

static	struct
{
	const char *name;
	int	atom_type;
} stream_socket_list[] = {
	{	"hostname",	VEVO_ATOM_TYPE_STRING	},
	{	"port",		VEVO_ATOM_TYPE_INT	},
	{ 	NULL,		0			}
};

static	struct
{
	const char *name;
	int	atom_type;
} stream_mcast_list[] = {
	{	"hostname",	VEVO_ATOM_TYPE_STRING	},
	{	"port",		VEVO_ATOM_TYPE_INT	},
	{	NULL,		0			}
};

static	struct
{
	const char *name;
	int	atom_type;
} stream_file_list[] = {
	{	"filename",	VEVO_ATOM_TYPE_STRING	},
	{	NULL,		0			}
};

static	struct
{
	const char *name;
	int	atom_type;
} stream_color_list[] = {
	{	"red",		VEVO_ATOM_TYPE_DOUBLE	},
	{	"green",	VEVO_ATOM_TYPE_DOUBLE	},
	{	"blue",		VEVO_ATOM_TYPE_DOUBLE	},
	{	"alpha",	VEVO_ATOM_TYPE_DOUBLE	},
	{	NULL,		0			}
};

static	struct
{
	const	char	*name;
	int	atom_type;
} stream_picture_list[] = 
{
	{	"filename",	VEVO_ATOM_TYPE_STRING 	},
	{	NULL,		0			}
};




void	*find_sample(int id)
{
	if( id < 0 || id > SAMPLE_LIMIT )
		return NULL;
	
	char key[20];
	void *info = NULL;
	int error;

	sprintf(key, "sample%04x", id );
	error = vevo_property_get( sample_bank_, key, 0,  &info );

	if(error != VEVO_NO_ERROR)
	{
#ifdef STRICT_CHECKING
		veejay_msg(0, "%s: '%s' not found in samplebank", __FUNCTION__, key );
#endif	
		return NULL;
	}
#ifdef STRICT_CHECKING
	assert( info != NULL );
#endif
	return info;
}

static	void	*find_sample_by_key(const char *key)
{
	void *info = NULL;
	int error = vevo_property_get( sample_bank_, key, 0,  &info );

	if( error == VEVO_NO_ERROR && info == NULL )
		return NULL;
	return info;
}

static	int	find_slot(void)
{
	int i;
	for(i = 0; i < SAMPLE_LIMIT; i++ )
	{
		if(free_slots_[i])
			return i;
	}
	return -1;
}

static	void	claim_slot(int idx)
{
	free_slots_[idx] = 0;
}

static	void	free_slot(int key)
{
	int i;
	for(i = 0; i < SAMPLE_LIMIT; i++ )
	{
		if(!free_slots_[i])
		{
			free_slots_[i] = key;
			break;
		}
	}
}


static	int	sample_property_is_protected( void *sample , const char *key )
{
	int i;
	for( i = 0; protected_properties[i].name != NULL ;i ++ )
	{
		if(strcasecmp(key,protected_properties[i].name ) == 0 )
			return 1;
	}
	return 0;
}

int		sample_get_run_id( void *info )
{
	sample_runtime_data *srd = (sample_runtime_data*) info;
	return srd->primary_key;
}

int		sample_get_key_ptr( void *info )
{
	if(!info)
		return 0;
	int pk = 0;
	sample_runtime_data *srd = (sample_runtime_data*) info;

	int error = vevo_property_get( srd->info_port, "primary_key", 0, &pk );
#ifdef STRICT_CHECKING
	if( error != VEVO_NO_ERROR )
		veejay_msg(0, "primary_key does not exist, error %d",error);
	assert( error == VEVO_NO_ERROR );
#endif	
	return pk;
}

void	sample_delete_ptr( void *info )
{
	sample_runtime_data *srd = (sample_runtime_data*) info;
	//@ FIXME: get default channel configuratioe_data *srd = (sample_runtime_data*) info;
#ifdef STRICT_CHECKING
	assert( info != NULL );
	assert( srd->info_port != NULL );
#endif

	sample_ui_close_all( info );
	
	void *sender = veejay_get_osc_sender( srd->user_data );
	if(sender)
		veejay_ui_bundle_add( sender, "/destroy/sample",
				"ix", srd->primary_key );
	
	int pk = 0;
	int error = vevo_property_get( srd->info_port, "primary_key", 0, &pk );

	void *osc_space = NULL;
	error = vevo_property_get( srd->info_port, "HOST_osc",0,&osc_space );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	
	veejay_osc_del_methods( srd->user_data, osc_space,srd->info_port ,srd);

//	sample_fx_clean_up( info, srd->user_data );

	sample_fx_chain_reset( info );
	
	sample_close( srd );
	
	vevo_port_recursive_free( srd->info_port );
	vevo_port_free( srd->fmt_port );
	
	free_slot( pk );
	free(srd->info);
	free(srd->record);
	free(srd);

	if( error == VEVO_NO_ERROR )
	{
		char pri_key[20];
		sprintf(pri_key, "sample%04x", pk );
		error = vevo_property_set( sample_bank_, pri_key, VEVO_ATOM_TYPE_VOIDPTR, 0, NULL );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	}
}

void	sample_fx_chain_reset( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;

	sample_fx_clean_up( sample,srd->user_data );

	sample_new_fx_chain ( sample );
}

void	sample_fx_set_parameter( int id, int fx_entry, int param_id,int n_elem, void *value )
{
	fx_slot_t *port = (fx_slot_t*) sample_get_fx_port( id, fx_entry );
#ifdef STRICT_CHECKING
	assert( port->fx_instance != NULL );
#endif
	plug_set_parameter( port->fx_instance, param_id, n_elem,value );	
}

//@ can optimize sprintf(pley, .. )
void	sample_fx_get_parameter( int id, int fx_entry, int param_id, int idx, void *dst)
{
	void *fx_values = sample_get_fx_port_values_ptr( id, fx_entry );
	char pkey[KEY_LEN];
	sprintf(pkey, "p%02d", param_id );	
	vevo_property_get( fx_values, pkey, idx, dst );	
	//@FIXME
}

void	sample_flush_bundle(void *info, const char * key )
{
	sample_runtime_data *srd = (sample_runtime_data*) info;
	void *sender = veejay_get_osc_sender_by_uri( srd->user_data , key );
	if(sender)
	{
		veejay_bundle_send( sender );
		veejay_bundle_destroy( sender );
	}
}

static	void	sample_notify_parameter( void *sample, void *parameter, void *value )
{
	char *osc_path = vevo_property_get_string( parameter, "HOST_osc_path" );
#ifdef STRICT_CHECKING
	assert( osc_path != NULL );
#endif
	char *osc_types = vevo_property_get_string( parameter, "HOST_osc_types");
	
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int fx_entry = sample_extract_fx_entry_from_path( sample, osc_path );
	void *sender = veejay_get_osc_sender( srd->user_data );

	if( fx_entry >= 0 && fx_entry < SAMPLE_CHAIN_LEN && sender)
	{
		fx_slot_t *slot = sample_get_fx_port_ptr( sample, fx_entry );
		veejay_bundle_plugin_add( sender, slot->frame, osc_path, osc_types, value );
	}
}


int	sample_fx_set( void *info, int fx_entry, const int new_fx )
{
 	fx_slot_t *slot = (fx_slot_t*) sample_get_fx_port_ptr( info,fx_entry );
#ifdef STRICT_CHECKING
	assert(slot!=NULL);
#endif
	sample_runtime_data *srd = (sample_runtime_data*) info;
		
	if( slot->fx_instance )
	{
		veejay_msg(0, "FX entry %d is used. Please select another",
				fx_entry);
		return 0;
	}
	
	slot->fx_instance = plug_activate( new_fx );
	if(!slot->fx_instance)
	{
		veejay_msg(0, "Unable to initialize plugin %d", new_fx );
		return 0;
	}

	slot->fx_id = new_fx;
	slot->id    = fx_entry;
	char tmp[128];
	sprintf( tmp, "Sample%dFX%d", srd->primary_key, fx_entry );
	slot->frame = strdup( tmp );

	plug_get_defaults( slot->fx_instance, slot->in_values );
	plug_set_defaults( slot->fx_instance, slot->in_values );

	int i;
	int n_channels = vevo_property_num_elements( slot->fx_instance, "in_channels" );

	if( n_channels <= 0)
	{
		veejay_msg(0, "Veejay cannot handle generator plugins yet");
		return 0;
	}
	
	for(i=0; i < n_channels; i ++ )
		sample_fx_set_in_channel(info,fx_entry,i, sample_get_key_ptr(info));

	int pk = 0;
	vevo_property_get( srd->info_port, "primary_key", 0, &pk );

	plug_build_name_space( new_fx, slot->fx_instance, srd->user_data, fx_entry ,pk,
			sample_notify_parameter, info );

	vevosample_ui_construct_fx_window( info, fx_entry );
	
	return 1;
}

int	sample_osc_verify_format( void *vevo_port, char const *types )
{
	char *format = vevo_property_get_string( vevo_port, "format" );
	int   n = strlen(types);
	if(!format)
	{
		if( n == 0 && format == NULL )
			return 1;
		return 0;
	}
	if( strcasecmp( types,format ) == 0 )
	{
		free(format);
		return 1;
	}
	free(format);
	return 0;
}
static void 	sample_osc_print( void *osc_port )
{
	char **osc_events = vevo_list_properties ( osc_port );
	int i;
	for( i = 0; osc_events[i] != NULL ; i ++ )
	{
		void *osc_info = NULL;
		int error = vevo_property_get( osc_port, osc_events[i], 0, &osc_info );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		char *format = get_str_vevo( osc_info, "format" );
		veejay_msg(VEEJAY_MSG_INFO, "OSC PATH %s",osc_events[i] );
		char *descr = get_str_vevo( osc_info, "description" );
		if(descr)
		{
			veejay_msg(VEEJAY_MSG_INFO,"\t%s", descr);		
			free(descr);
		}
		
		veejay_msg(VEEJAY_MSG_INFO, "\tFormat=%s", format );


		if(format)
		{
			int   n_args = strlen(format);
			char  key[10];
			int j;
			for( j = 0; j < n_args ; j ++ )
			{
				sprintf(key, "help_%d", j );
				char *help_str = get_str_vevo( osc_info, key );
				if(help_str)
				{
				  veejay_msg(VEEJAY_MSG_INFO,"\t\tArgument %d : %s", j, help_str );
				  free(help_str);
				}
			}
			free(format);
		}
		veejay_msg(VEEJAY_MSG_INFO,"\t");
		free( osc_events[i]);
	}
	free(osc_events);

}
void	sample_osc_namespace(void *sample)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int k;
	void *osc_namespace = NULL;
	int error = vevo_property_get( srd->info_port, "HOST_osc",0, &osc_namespace);

	if( error != VEVO_NO_ERROR) //@ plug has no namespace
		return;
	
	veejay_msg(VEEJAY_MSG_INFO,"OSC namespace:");

	sample_osc_print( osc_namespace );
	
	veejay_msg(VEEJAY_MSG_INFO,"FX:");
	for( k = 0; k < SAMPLE_CHAIN_LEN ; k ++ )
	{
		fx_slot_t *slot = sample_get_fx_port_ptr( srd, k );
		if(slot->fx_instance)
		{
			void *space = plug_get_name_space( slot->fx_instance );
			if(space) sample_osc_print( space );
		}
	}
	veejay_msg(VEEJAY_MSG_INFO,"End of OSC namespace");
}


void	sample_osc_help( void *sample, const char *path )
{

}

int	sample_extract_fx_entry_from_path(void *sample, const char *path )
{
	char *my_path = strdup(path);
	char *token   = strtok( my_path, "/");

	char *res = NULL;
	while( (res = strtok(NULL, "/" ) ))
	{
		if(strncasecmp( res, "fx_", 3 ) == 0 )
		{
			int id = 0;
			if(sscanf( res+3,"%d", &id ) == 1 )
				return id;
		}
	}
	
	free(my_path);
	return -1;
}


int		sample_osc_property_calls_event( void *sample, const char *path, char *types, void **argv[], void *raw )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *vevo_port = srd->info_port;
	void *osc_port = NULL;
	int error = vevo_property_get( srd->info_port, "HOST_osc", 0, &osc_port );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int atom_type = vevo_property_atom_type( osc_port, path );
	if( atom_type == VEVO_ATOM_TYPE_PORTPTR )
	{
		void *port = NULL;
		error = vevo_property_get( osc_port, path,0,&port );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		if(error == VEVO_NO_ERROR )
		{
			vevo_event_f f;
			if( sample_osc_verify_format( port, types ) )
			{
				error = vevo_property_get( port, "func",0,&f );
				if( error == VEVO_NO_ERROR )
				{
					(*f)( sample, path,types, argv, raw);
					return 1;
				}
			}
		}
	}
	return 0;
}
int	sample_fx_set_in_channel( void *info, int fx_entry, int seq_num, const int sample_id )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( info, fx_entry );
	int       max_in = 0;
	void      *inputs = sample_scan_in_channels( info, fx_entry, &max_in );
	char	  key[20];

	if (seq_num < 0 || seq_num >= max_in )
	{
		veejay_msg(0, "FX has at most %d input channel(s)",
				max_in );
		return 0;
	}
	
	void *sample = find_sample( sample_id );
	sprintf(key,"slot%d",seq_num );
	
#ifdef STRICT_CHECKING
	assert( sample != NULL );
#endif
	int error = vevo_property_set( inputs, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &sample );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	veejay_msg(0, "Set input channel %d, sample %s", seq_num, key );
	
	return 1;
}

int	sample_fx_push_in_channel( void *info, int fx_entry, int seq_num, void *frame_info )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( info,fx_entry );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
#endif
	plug_push_frame( slot->fx_instance, 0,seq_num, frame_info );
	return 1;
}

int	sample_fx_push_out_channel( void *info, int fx_entry, int seq_num, void *frame_info )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( info,fx_entry );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
#endif	
	plug_push_frame( slot->fx_instance, 1,seq_num, frame_info );
	return 1;
}

double	sample_get_fx_alpha( void *data, int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( data,fx_entry );
	return slot->alpha;
}
void	sample_set_fx_alpha( void *data, int fx_entry, double v )
{
	sample_runtime_data *srd = (sample_runtime_data*) data;

	fx_slot_t *slot = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
#endif
	if( v > 1.0 ) v = 1.0; else if (v < 0.0 ) v = 0.0;
	if( v != slot->alpha )
	{
		void *sender = veejay_get_osc_sender( srd->user_data );
		if(sender)
			veejay_bundle_sample_fx_add(sender, srd->primary_key,fx_entry, "alpha", "d", v );

	}
	slot->alpha = v;
}

int	sample_get_fx_status( void *data, int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( data,fx_entry );
	return slot->active;
}
void	sample_set_fx_status( void *data, int fx_entry, int status )
{
	sample_runtime_data *srd = (sample_runtime_data*) data;
	if( fx_entry < 0 || fx_entry > SAMPLE_CHAIN_LEN )
		return;
	
	fx_slot_t *slot = sample_get_fx_port_ptr( data,fx_entry );
	if(status && !slot->fx_instance)
	{
		veejay_msg(0, "Nothing to enable in FX slot %d", status);
		return;
	}	
	slot->active = status;
	void *sender = veejay_get_osc_sender( srd->user_data );
	if(sender)
		veejay_bundle_sample_fx_add(sender, srd->primary_key, fx_entry, "status", "i", status );

}

int	sample_scan_out_channels( void *sample, int i )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( sample,i );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
	assert( slot->fx_instance != NULL );
#endif
	return vevo_property_num_elements( slot->fx_instance, "out_channels" );
}

void	*sample_scan_in_channels( void *sample, int i , int *num)
{
	fx_slot_t *slot = sample_get_fx_port_ptr( sample,i );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
	assert( slot->fx_instance != NULL );
#endif
	*num = vevo_property_num_elements( slot->fx_instance, "in_channels" );
	
	//return vevo_property_num_elements( slot->fx_instance, "in_channels" );
	return slot->in_channels;
}

// frame buffer passed by performer
int	sample_process_fx( void *sample, int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( sample,fx_entry );
#ifdef STRICT_CHECKING
	assert( slot != NULL );
#endif		
	plug_clone_from_parameters( slot->fx_instance, slot->in_values );

	plug_process( slot->fx_instance );

	plug_clone_from_output_parameters( slot->fx_instance, slot->out_values );
		
	if(slot->bind)	sample_apply_bind( sample, slot , fx_entry );
	
	return VEVO_NO_ERROR;
}

void	*sample_get_fx_port_ptr( void *data, int fx_entry )
{
	sample_runtime_data *info = (sample_runtime_data*) data;
	void *port = NULL;
	char fx[KEY_LEN];
	sprintf(fx, "fx_%x", fx_entry );
#ifdef STRICT_CHECKING
	assert( info != NULL );
	assert( info->info_port != NULL );
#endif
	int error = vevo_property_get( info->info_port, fx, 0, &port );

#ifdef STRICT_CHECKING
	if(error != 0 )
		veejay_msg(0,"%s: Entry %d returns error code %d", __FUNCTION__,fx_entry, error );
	assert( error == 0 );
	assert( port != NULL );
#endif
	return port;
}

int	sample_get_fx_id( void *data , int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( data, fx_entry );
	return slot->fx_id;
}

int	sample_has_fx( void *sample, int fx_entry )
{
	fx_slot_t *slot = (fx_slot_t*) sample_get_fx_port_ptr( sample, fx_entry );
	if(!slot)
		return 0;
	
	if(slot->fx_instance)
		return 1;
	return 0;
}

static	void	*sample_get_fx_port_values_ptr( int id, int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port( id, fx_entry );
	return slot->in_values;
}

void	*sample_get_fx_port_channels_ptr( int id, int fx_entry )
{
	fx_slot_t *slot = sample_get_fx_port( id, fx_entry );
	return slot->in_channels;
}	

void	*sample_get_fx_port( int id, int fx_entry )
{
	return sample_get_fx_port_ptr( find_sample(id),fx_entry );
}

int	 sample_set_property_ptr( void *ptr, const char *key, int atom_type, void *value )
{
	sample_runtime_data *info = (sample_runtime_data*) ptr;
	if( info->type == VJ_TAG_TYPE_CAPTURE)
	{
		char *rkey = sample_translate_property( ptr, key );
		if(rkey)
		{	vj_unicap_select_value( info->data,rkey, atom_type,value ); 		
			free(rkey);
		}
		return VEVO_NO_ERROR;
	}
	return vevo_property_set( info->info_port, key, atom_type,1, value );	
}

void	 sample_set_property( int id, const char *key, int atom_type, void *value )
{
	sample_runtime_data *info = (sample_runtime_data*) find_sample( id );
	if( info->type == VJ_TAG_TYPE_CAPTURE)
	{
		char *rkey = sample_translate_property( info, key );
		if(rkey)
		{
			vj_unicap_select_value( info->data,rkey, atom_type,value ); 
			void *sender = veejay_get_osc_sender( info->user_data );
			if(sender) //@ att , unicap class deals just with doubles for now
				veejay_bundle_sample_add_atom(sender, info->primary_key, rkey, "d",VEVO_ATOM_TYPE_DOUBLE, value );
		
			free(rkey);
		}
	}
	if(info)
		if(vevo_property_set( info->info_port, key, atom_type,1, value ) == VEVO_NO_ERROR )
		{
			void *sender = veejay_get_osc_sender( info->user_data );
			if(sender)
			{
				char *format = vevo_property_get_string( info->fmt_port, key );
				veejay_bundle_sample_add_atom( sender, info->primary_key, key, format, atom_type,value );
				free(format);
			}
		}	
}

static	char	*strip_key( const char *path)
{
	char *start = strdup(path);
	int k = strlen(path);
	int i;
	int n = 0;
	char *token = strtok( start, "/" );
	char *s = start;
	char *res = NULL;
	while( (token = strtok( NULL, "/" ) ) != NULL )
	{
		if(res && token)
			free(res);
		res = strdup(token);
	}
	free(start);
	return res;
}

static	char	*strip_port( const char *path)
{
	char *start = strdup(path);
	int k = strlen(path);
	int i;
	int n = 0;
	char *token = strtok( start, "/" );
	char *s = start;
	char *res = NULL;
	char *port = NULL;
	while( (token = strtok( NULL, "/" ) ) != NULL )
	{
		if(res)
		{
			if(port)
				free(port);
			port = strdup(res);
			free(res);
			res= NULL;
		}
		res = strdup(token);
	}
	free(start);
	if(res) free(res);
	return port;
}

void	sample_set_property_from_path( void *sample, const char *path,int elements, void *value )
{
	sample_runtime_data *info = (sample_runtime_data*) sample;
	int len = strlen(path);
	int n = 0;
	int i;
	int error;
	for( i =0; i < len ; i ++ )
		if( path[i] == '/' )
			n++;

	veejay_msg(0, "Change '%s'", path );
	
	char *key = strip_key( path );
	if(!key)
	{
		veejay_msg(0, "Error getting key from path '%s'", path );
		return;
	}

	if( info->type == VJ_TAG_TYPE_CAPTURE && vevo_property_get(info->info_port,key,0,NULL) != VEVO_NO_ERROR )
	{
		char *my_key = sample_reverse_translate_property(sample, key );
		if(my_key)
		{
			free(key);
			key = my_key;
		}
	}
	//@ TYPE CAPTURE !
	int atom_type = vevo_property_atom_type( info->info_port, key );
	if( n == 3 )
	{
		char *pk = strip_port(path);
		void *port = NULL;
		int error = vevo_property_get( info->info_port, pk, 0, &port );
		if( port && error == VEVO_NO_ERROR)
		{
			atom_type = vevo_property_atom_type( port , key );
			if(atom_type)
			{
				error = vevo_property_set( port,key, atom_type,1, value );	
				if( error == VEVO_NO_ERROR )
				{
					veejay_msg(VEEJAY_MSG_INFO,
						"OSC path '%s' sets property '%s' %d", path, key,atom_type );
					void *sender = veejay_get_osc_sender( info->user_data );
					if(sender)
					{
						char *format = vevo_property_get_string( info->fmt_port, key );
						veejay_bundle_add_atom( sender, path, format, atom_type,value );
						free(format);
					}
				}
				if(pk) free(pk);
				return;
			}
		}
		if(pk)
			free(pk);
		return;
	}
	else if (n == 2 )
	{
		int error = vevo_property_get( info->info_port, key, 0, NULL );

       		if( error != VEVO_ERROR_PROPERTY_EMPTY && error != VEVO_NO_ERROR )
		{
			veejay_msg(0, "Unable to get property '%s'",key);
			if(key)
				free(key);
			return;
		}	
	}
	else
		return;
	
	if( info->type == VJ_TAG_TYPE_CAPTURE)
	{
		vj_unicap_select_value( info->data,key, VEVO_ATOM_TYPE_DOUBLE,value ); 		
	}

	if(info)
		error = vevo_property_set( info->info_port,key, atom_type,elements, value );	
	if( error == VEVO_NO_ERROR )
	{
		veejay_msg(VEEJAY_MSG_INFO,
			"OSC path '%s' sets property '%s' %d", path, key,atom_type );
		void *sender = veejay_get_osc_sender( info->user_data );
		if(sender)
		{
			char *format = vevo_property_get_string( info->fmt_port, key );
			veejay_bundle_add_atom( sender, path, format, atom_type,value );
			free(format);
		}
	}
		
	free(key);
}


void	 sample_get_property( int id, const char *key, void *dst )
{
	sample_runtime_data *info = (sample_runtime_data*) find_sample( id );
	if(info)
		vevo_property_get( info->info_port, key, 0, dst );	
}

//@ only call after sample_new(), 
void	sample_set_user_data( void *sample, void *data, int id )
{
	sample_runtime_data *info = (sample_runtime_data*) sample;
	info->user_data = data;
	sample_init_namespace( data, sample, id );
	void *sender = veejay_get_osc_sender( data );
	if(sender)
	{
		char name[64];
		sprintf(name, "Sample %d",id );
		veejay_ui_bundle_add( sender, "/update/sample",
				"isx", id, name );
	}			
}

void	*sample_get_user_data( void *sample )
{
	sample_runtime_data *info = (sample_runtime_data*) sample;
	return (void*) info->user_data;
}

void	 sample_get_property_ptr( void *ptr, const char *key, void *dst )
{
	sample_runtime_data *info = (sample_runtime_data*) ptr;
#ifdef STRICT_CHECKING
	assert( info != NULL );
#endif
	vevo_property_get( info->info_port, key, 0, dst );	
}

static int	sample_new_ext(void *info )
{
	int i;
	for(i = 0 ; sample_property_list[i].name != NULL ; i ++ )
		vevo_property_set( info, sample_property_list[i].name, sample_property_list[i].atom_type, 0, NULL );
	return 1;
}

static int	sample_new_stream(void *info, int type )
{
	int i ;
	double v = 0.0;
	for(i = 0 ; stream_property_list[i].name != NULL ; i ++ )
		vevo_property_set( info, stream_property_list[i].name, stream_property_list[i].atom_type, 0, NULL );
#ifdef STRICT_CHECKING
	assert( type != VJ_TAG_TYPE_NONE );
#endif
	switch(type)
	{
		case	VJ_TAG_TYPE_PICTURE:
			for(i = 0 ; stream_picture_list[i].name != NULL ; i ++ )
				vevo_property_set( info, stream_picture_list[i].name, stream_picture_list[i].atom_type, 0, NULL );
			break;
		case	VJ_TAG_TYPE_COLOR:
			for(i = 0 ; stream_color_list[i].name != NULL ; i ++ )
				vevo_property_set( info, stream_color_list[i].name, stream_color_list[i].atom_type, 1, &v );
			break;
		case	VJ_TAG_TYPE_CAPTURE:
			break;
		case	VJ_TAG_TYPE_YUV4MPEG:
			for( i = 0; stream_file_list[i].name != NULL; i ++ )
				vevo_property_set( info, stream_file_list[i].name, stream_file_list[i].atom_type,0,NULL);
			break;
		case	VJ_TAG_TYPE_NET:
			for( i = 0; stream_socket_list[i].name != NULL; i ++ )
				vevo_property_set( info, stream_socket_list[i].name, stream_socket_list[i].atom_type,0,NULL);
			break;
		case	VJ_TAG_TYPE_MCAST:
			for( i = 0; stream_mcast_list[i].name != NULL; i ++ )
				vevo_property_set( info, stream_mcast_list[i].name, stream_mcast_list[i].atom_type,0,NULL);
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;	
	}

	return 1;
}

const	char	*sample_describe_type( int type )
{
	switch(type)
	{
		case	VJ_TAG_TYPE_PICTURE:
			return "Picture";
			break;
		case	VJ_TAG_TYPE_COLOR:
			return "Solid Color stream";
			break;
		case	VJ_TAG_TYPE_CAPTURE:
			return "Video4Linux stream";
			break;
		case	VJ_TAG_TYPE_YUV4MPEG:
			return "YUV4Mpeg stream";
			break;
		case	VJ_TAG_TYPE_NET:
			return "TCP unicast stream";	
			break;
		case	VJ_TAG_TYPE_MCAST:
			return "UDP multicast stream";
			break;
		case 	VJ_TAG_TYPE_NONE:
			return "AVI Sample";
			break;
		default:
			break;	
	}

	
	return NULL;
}

static	void	sample_ui_close( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *sender = veejay_get_osc_sender( srd->user_data );
	if(sender)
	{
		char name[128];
		sprintf(name, "Sample%d", srd->primary_key );
		veejay_ui_bundle_add( sender, "/destroy/window", "sx", name );
	}
}

static	void	sample_ui_fx_close( void *sample, int fx_entry , void *user_data)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = sample_get_fx_port_ptr( srd, fx_entry );

	void *sender = veejay_get_osc_sender( user_data );
	if(sender)
	{
		veejay_msg(0, "Close '%s'", slot->window);
		veejay_ui_bundle_add( sender, "/destroy/window", "sx", slot->window );
	}
}

static void		sample_ui_close_all( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int i;
//	for( i = 0; i < SAMPLE_CHAIN_LEN ; i ++ )
//	{
//		sample_ui_fx_close( sample, i, srd->user_data );
//	}	

	sample_ui_close(sample);	
}


static	void	sample_fx_entry_clean_up( void *sample, int id, void *user_data )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = sample_get_fx_port_ptr( srd, id );
	
	if(slot->window)
		sample_ui_fx_close( sample, id, user_data );
	else
		veejay_msg(0, "FX slot %d has no window, sample %d", id,
			 sample_get_key_ptr(sample) );
	if(slot->window)
		free(slot->window);
	if(slot->frame)
		free(slot->frame);
	
	if( slot->fx_instance )
	{
		plug_clear_namespace( slot->fx_instance, user_data );
		plug_deactivate( slot->fx_instance );
	}

	vevo_port_free( slot->in_values );
	vevo_port_free( slot->out_values );
	vevo_port_free( slot->out_channels );
	
	if( slot->bind )
	{
		sample_reset_bind ( sample, slot );	
	}

	free(slot);

	//@ set fx port ptr to NULL
	char fx[KEY_LEN];
	sprintf(fx, "fx_%x", id );
	vevo_property_set( srd->info_port, fx, VEVO_ATOM_TYPE_PORTPTR,0,NULL );
	
}

static	void	sample_fx_clean_up( void *sample, void *user_data )
{
	int i;
	int error;
	for( i = 0 ; i < SAMPLE_CHAIN_LEN; i ++ )
	{
		sample_fx_entry_clean_up( sample,i,user_data );
	}

}

static	void	sample_new_fx_chain_entry( void *sample, int id )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	
	char entry_key[64];
	fx_slot_t *slot = (fx_slot_t*) vj_malloc(sizeof(fx_slot_t));
	memset( slot,0,sizeof(fx_slot_t));
		
	sprintf(entry_key, "fx_%x", id );
	slot->in_values = vpn( VEVO_FX_VALUES_PORT );
	slot->in_channels = vpn( VEVO_ANONYMOUS_PORT );
	slot->out_values = vpn( VEVO_ANONYMOUS_PORT );
	slot->out_channels = vpn( VEVO_ANONYMOUS_PORT );

	int error =	vevo_property_set( srd->info_port, entry_key, VEVO_ATOM_TYPE_PORTPTR,1, &slot );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

}

static	void	sample_new_fx_chain(void *sample)
{
	int i;
	int error;
	int dstatus = 0;
	for( i = 0 ; i < SAMPLE_CHAIN_LEN; i ++ )
	{
		sample_new_fx_chain_entry( sample, i );
	}
}

int	sample_fx_chain_entry_clear(void *info, int id )
{
	sample_runtime_data *sample = (sample_runtime_data*) info;
	
	if( id >= 0 && id < SAMPLE_CHAIN_LEN )
	{
		sample_fx_entry_clean_up( info, id,sample->user_data );
		sample_new_fx_chain_entry( info, id );
		return 1;
	}
	else
		veejay_msg(0, "Invalid FX slot: %d",id);
	return 0;
}

void	*sample_new( int type )
{
	int i;
	sample_runtime_data *rtdata = (sample_runtime_data*) vj_malloc(sizeof( sample_runtime_data ) );
	memset( rtdata,0,sizeof(sample_runtime_data));
	sampleinfo_t *sit = (sampleinfo_t*) vj_malloc(sizeof(sampleinfo_t));
	memset( sit,0,sizeof(sampleinfo_t));
	rtdata->info = sit;
	rtdata->record = (samplerecord_t*) vj_malloc(sizeof(samplerecord_t));
	memset(rtdata->record,0,sizeof(samplerecord_t));
	rtdata->type	  = type;
	rtdata->info_port = (void*) vpn( VEVO_SAMPLE_PORT );
	rtdata->fmt_port  =(void*) vpn( VEVO_ANONYMOUS_PORT );
	for(i = 0 ; common_property_list[i].name != NULL ; i ++ )
		vevo_property_set(
			rtdata->info_port,
			common_property_list[i].name,
			common_property_list[i].atom_type,
			0,
			NULL );

	void *res = NULL;
	sit->type = type;

	if( type == VJ_TAG_TYPE_NONE )
	{
		if( sample_new_ext( rtdata->info_port ))
			res = (void*) rtdata;
	}
	else	
	{
		if( sample_new_stream( rtdata->info_port, type ))
			res = (void*) rtdata;
	}

	
	sample_new_fx_chain( rtdata );

	return res;
}

char	*sample_property_format_osc( void *sample, const char *path )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int n = 0;
	int i = 0;
	int len = strlen(path);
	for( i =0; i < len ; i ++ )
		if( path[i] == '/' )
			n++;
	if(n==2||n==3)
	{
		char *key = strip_key(path);
		char *rkey = NULL;
		char *fmt = NULL;
		if( key == NULL )
			return NULL;

		if( srd->type == VJ_TAG_TYPE_CAPTURE && vevo_property_get( srd->info_port, key,0,NULL ) != VEVO_NO_ERROR )
		{
			char *tmpkey = sample_reverse_translate_property(sample, key );
			if(tmpkey)
			{
				free(key);
				key = tmpkey;
			}
		}
		
		if( sample_property_is_protected( srd->info_port ,key)==1 )
		{
			free(key);
			return NULL;
		}
		
		int atom_type = 0;
		int n_elem = vevo_property_num_elements( srd->info_port , key );
		if(n_elem == 0 ) n_elem = 1;

		if(n==2)
			atom_type = vevo_property_atom_type( srd->info_port, key );
		else
		{
			void *port = NULL;
			int   id   = 0;
			char *pk = strip_port(path);
			int error = vevo_property_get( srd->info_port, pk, 0, &port );
			if( port )
			{
				atom_type = vevo_property_atom_type( port , key );
				n_elem = vevo_property_num_elements( port , key );
					if(n_elem == 0 ) n_elem = 1;
				fmt = (char*) vj_malloc( n_elem + 1);
				bzero(fmt,n_elem+1);
			}
			if(pk)
				free(pk);
			if( port )
			{
				if( sample_property_is_protected( port ,key)==1 )
				{
					if(fmt) free(fmt);
					if(key)free(key);
					return NULL;
				}

			}
		}

		if(!fmt)
		{
			fmt = (char*) vj_malloc( n_elem + 1 );
			bzero(fmt,n_elem+1);
		}

		for( i = 0; i < n_elem ; i ++ )
		{
			if( atom_type == VEVO_ATOM_TYPE_DOUBLE ) {
				fmt[i] = 'd';
			} else if (atom_type == VEVO_ATOM_TYPE_INT ) {
				fmt[i] = 'i';
			} else if (atom_type == VEVO_ATOM_TYPE_UINT64 ) {
				fmt[i] = 'h';
			} else if (atom_type == VEVO_ATOM_TYPE_BOOL ) {
				fmt[i] = 'i';
			} else if( atom_type == VEVO_ATOM_TYPE_STRING ){
				fmt[i] = 's';
			}
			else {
				free(fmt);
				free(key);
				return NULL;
			}
		}
		veejay_msg(0, "++ '%s' has format '%s'", key,fmt );
		free(key);
		return fmt;
	}
	return NULL;
}

void	sample_init_namespace( void *data, void *sample , int id )
{
	sample_runtime_data *rtdata = (sample_runtime_data*) sample;
	
	char	name[256];
	int	error;
	sprintf(name, "/sample_%d",id);
	char	**namespace = vevo_port_recurse_namespace( rtdata->info_port, name );
	int n = 0;
	int k;
	
	if(!namespace)
	{
		veejay_msg(0, "Sample %d has no OSC namespace",id);
		return;
	}
	
	
	void *osc_namespace = vpn(VEVO_ANONYMOUS_PORT);
	for ( k = 0; namespace[k] != NULL ; k ++ )
	{
		char *format = sample_property_format_osc( sample, namespace[k] );
		
		if(format == NULL )
		{
			if( vevo_property_atom_type( rtdata->info_port, namespace[k] ) == VEVO_ATOM_TYPE_PORTPTR )
			{
				char **ns_entries = vevo_port_recurse_namespace(rtdata->info_port,namespace[k]);
				if(ns_entries)
				{
					int q;
					void *port = NULL;
					error = vevo_property_get( rtdata->info_port, namespace[k],0,&port );
#ifdef STRICT_CHECKING
					assert( error == VEVO_NO_ERROR );
#endif
					for( q = 0; ns_entries[q] != NULL ; q ++ ) 
					{
						char *fmt = sample_property_format_osc( port, ns_entries[q] );
						vevosample_new_event( data, osc_namespace, sample,NULL, ns_entries[q], fmt, NULL,NULL, NULL,-1 );
						if(fmt)
						{
							error = vevo_property_set( rtdata->fmt_port, ns_entries[q], 
									VEVO_ATOM_TYPE_STRING,1,&fmt );
						}
						if(fmt)	free(fmt);
						free(ns_entries[q]);
					}	
					free(ns_entries);
				}
			}
		}	
		else
		{
			vevosample_new_event( data, osc_namespace, sample, NULL, namespace[k], format,NULL, NULL,NULL, 0 );
			vevo_property_set( rtdata->fmt_port, namespace[k], VEVO_ATOM_TYPE_STRING,1,&format );

			free(format);

		}
		free(namespace[k]);
	}
	free(namespace);


	///FIXME
	if(rtdata->type == VJ_TAG_TYPE_NONE )
		veejay_osc_add_sample_nonstream_events(data,osc_namespace,rtdata,name);
	veejay_osc_add_sample_generic_events( data,osc_namespace,rtdata,name, SAMPLE_CHAIN_LEN );

	
	error = vevo_property_set(
			rtdata->info_port, "HOST_osc",VEVO_ATOM_TYPE_PORTPTR,1,&osc_namespace);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

}

int	vevo_num_devices()
{
	return vj_unicap_num_capture_devices( unicap_data_ );
}

void	samplebank_init()
{
	sample_bank_ = (void*) vpn( VEVO_SAMPLE_BANK_PORT );
#ifdef STRICT_CHECKING
	veejay_msg(2,"VEVO Sampler initialized. (max=%d)", SAMPLE_LIMIT );
#endif

	unicap_data_ = (void*) vj_unicap_init();
#ifdef STRICT_CHECKING
	assert( unicap_data_ != NULL );
#endif
	ui_register_ = 	(void*) vpn( VEVO_SAMPLE_BANK_PORT );

}

char	*samplebank_sprint_list()
{
	char *res = NULL;
	int len   = 0;
	char **props = (char**) vevo_list_properties( sample_bank_ );
	if(!props)
		return NULL;

	int i = 0;
	for( i = 0; props[i] != NULL ; i ++ )
	{
		if(props[i][0] == 's')
			len += strlen( props[i] ) + 1;
	}

	res = (char*) vj_malloc(sizeof(char) * len );
	memset(res,0,len);
	
	char *p = res;
	for( i = 0; props[i] != NULL ; i ++ )
	{
		if(props[i][0] == 's' )
		{
			sprintf(p, "%s:", props[i]);
			p += strlen( props[i] ) + 1;
		}		
	}

	return res;
}
typedef struct
{
	struct timeval t;
} ui_alarm_t;
static	void	*alarm_init(void)
{
	ui_alarm_t *uia = (ui_alarm_t*) vj_malloc(sizeof(ui_alarm_t));
	gettimeofday( &(uia->t),NULL);
	return (void*) uia;
}

void	samplebank_tick_ui_client( char *uri )
{
	void *alarm = alarm_init();
	void *old_alarm = NULL;
	int error = vevo_property_get( ui_register_, uri, 0,&old_alarm );
	if( old_alarm )
		free( old_alarm );
	
	error = vevo_property_set( ui_register_,
				uri, VEVO_ATOM_TYPE_VOIDPTR, 1, &alarm );
}

int	samplebank_flush_ui_client( char *uri, struct timeval *now )
{
	void *alarm = NULL;
	if(vevo_property_get( ui_register_, uri, 0,&alarm )==VEVO_NO_ERROR)
	{
		ui_alarm_t *uia = (ui_alarm_t*) alarm;
		if( now->tv_sec > (uia->t.tv_sec + 3))
		{
			veejay_msg(0, "'%s' timeout", uri);
			free(alarm);
			vevo_property_set( ui_register_, uri,VEVO_ATOM_TYPE_VOIDPTR,0,NULL);
			return 0;
		}
	}
	return 1;
}

void	samplebank_flush_osc(void *info, void *clients)
{
	struct timeval now;
	char **ccl 	= vevo_list_properties( clients );
	if(!ccl)
		return;
	char **props = (char**) vevo_list_properties( sample_bank_ );
	if(!props)
		return;
	int i,k;
	
	gettimeofday(&now,NULL);
	for( k = 0; ccl[k] != NULL ; k ++ )
	{
		if(!samplebank_flush_ui_client( ccl[k], &now ))
		{
			veejay_msg(0, "URI '%s' is not alive anymore?", ccl[k]);
			void *sender = NULL;
			if( vevo_property_get(clients, ccl[k],0,&sender) == VEVO_NO_ERROR )
			{
				veejay_free_osc_sender(sender);
				vevo_property_set( clients, ccl[k], VEVO_ATOM_TYPE_VOIDPTR,0,NULL );
			}
		}
		free(ccl[k]);
	}
	free(ccl);

	char **cl 	= vevo_list_properties( clients );
	if(!cl)
		return;

	for( i = 0; props[i] != NULL;i ++ )
	{
		if( props[i][0] == 's' ) 
		{
			void *info = find_sample_by_key( props[i] );
			if(info)
			{
				for( k = 0; cl[k] != NULL ; k ++ )
					sample_flush_bundle(info, cl[k] );
			}
		}
		free(props[i]);
	}
	free(props);
	
	for( k = 0; cl[k] != NULL ; k ++ )
		free( cl[k] );
	free( cl );

}

//waste 
int	samplebank_guess_row_sequence( void *osc, int id )
{
	char **props = (char**) vevo_list_properties( sample_bank_ );
	if(!props)
		return 0;
	int i;
	for( i = 0; props[i] != NULL;i ++ )
	{
		if( props[i][0] == 's' ) 
		{
			void *info = find_sample_by_key( props[i] );
			sample_runtime_data *s = (sample_runtime_data*) info;
			if(s && s->primary_key == id )
				return i;
		}
		free(props[i]);
	}
	free(props);
	return 0; // take the first
}

void	samplefx_push_pulldown_items( void *osc, void *msg)
{
	int i;
	for( i = 0; i < SAMPLE_CHAIN_LEN; i ++ )
	{
		char name[32];
		sprintf(name,"fx %d", i );
		veejay_message_add_argument( osc,msg, "s", name );
	}
}

void	samplebank_push_pulldown_items( void *osc, void *msg )
{
	char **props = (char**) vevo_list_properties( sample_bank_ );
	if(!props)
		return;
	int i;
	veejay_message_add_argument( osc, msg, "s", "none" );
	for( i = 0; props[i] != NULL;i ++ )
	{
		if( props[i][0] == 's' ) 
		{
			void *info = find_sample_by_key( props[i] );
			if(info)
			{
				veejay_message_add_argument( osc, msg, "s", props[i] );
			}	
		}
		free(props[i]);
	}
	free(props);
}

void	samplebank_free()
{
	int i = 0;
#ifdef STRICT_CHECKING
	assert( sample_bank_ != NULL );
#endif
	char **props = (char**) vevo_list_properties( sample_bank_ );
	if(!props)
		return;

	for( i = 0; props[i] != NULL;i ++ )
	{
		if( props[i][0] == 's' ) 
		{
			void *info = find_sample_by_key( props[i] );
	veejay_msg(0, "Sample '%s' -> %p", props[i], info );
			if( info )
			 sample_delete_ptr( info );
		}
		free(props[i]);
	}
	free(props);
	vevo_port_free( sample_bank_ );
/*#ifdef STRICT_CHECKING
	port_frees_ ++;
#endif*/
	num_samples_ = 0;

	vj_unicap_deinit( unicap_data_ );
}


void	samplebank_send_all_samples(void *sender)
{
	char **items = vevo_list_properties( sample_bank_ );
	if(!items )
	{
		veejay_msg(0, "Samplebank empty");
		return;
	}
	int i;
	for( i = 0 ; items[i] != NULL ; i ++ )
	{
		void *sample = NULL;
		int error = vevo_property_get( sample_bank_ , items[i],0,&sample );
		if(items[i][0] == 's' )
		{
		if( error == VEVO_NO_ERROR )
		{
			sample_runtime_data *dd = (sample_runtime_data*) sample;
			veejay_ui_bundle_add( sender, "/update/sample",
				"isx", dd->primary_key, items[i] );
			veejay_msg(0,"\t /update/sample %d", dd->primary_key );
		}
		else
			veejay_msg(0, "\tError in '%s'", items[i]);
		}
		free(items[i]);
	}
		
	free(items);
}

int	samplebank_add_sample( void *sample )
{
	char pri_key[20];
	int error = 0;
	int pk = 0;

	sample_runtime_data *srd = (sample_runtime_data*) sample;
	void *port =srd->info_port;
	
	error = vevo_property_get( port, "primary_key", 0, &pk );
	if( error != VEVO_NO_ERROR )
	{
		int	tslot = find_slot();
		pk    = (tslot >= 0 ? free_slots_[ tslot ] : num_samples_ + 1 );
	}
#ifdef STRICT_CHECKING
	assert( pk > 0 );
#endif
	error = vevo_property_set( port, "primary_key", VEVO_ATOM_TYPE_INT, 1, &pk );
#ifdef STRICT_CHECKING
	if(error != 0)
		veejay_msg(0, "Fatal error: %d", error );
	assert( error == VEVO_NO_ERROR );
#endif

	sprintf(pri_key, "sample%04x", pk );	
	error = vevo_property_set( sample_bank_, pri_key, VEVO_ATOM_TYPE_VOIDPTR, 1, &sample );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	num_samples_ += 1;
	srd->primary_key = pk;
	

	
	return pk;
}


int	samplebank_size()
{
	return num_samples_;
}

void	*sample_last(void)
{
	int id = num_samples_;
	void *found = NULL;
	while(!found && id > 0)
	{
		found = find_sample( id );
		id --;	
	}
	return found;
}


// "Video source" --> "video_source"
char		*sample_translate_property( void *sample, char *name )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	char *result =vevo_property_get_string( srd->mapping, name );
	return result;
}

// "video_source" --> "Video source"
static char		*sample_reverse_translate_property( void *sample, char *name )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	char *result =vevo_property_get_string( srd->rmapping, name );
	return result;
}

static	void	sample_collect_unicap_properties( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	char **props = vj_unicap_get_list( srd->data );
	int i;
	for( i = 0; props[i] != NULL ; i ++ )
	{
		double dvalue = 0.0;
		int n = vj_unicap_get_value( srd->data, props[i], VEVO_ATOM_TYPE_DOUBLE, &dvalue );

		char *realname = veejay_valid_osc_name( props[i] );
		
		vevo_property_set( srd->info_port, props[i], VEVO_ATOM_TYPE_DOUBLE,1,&dvalue );
		vevo_property_set( srd->mapping,   props[i], VEVO_ATOM_TYPE_STRING,1,&realname);
		vevo_property_set( srd->rmapping,   realname, VEVO_ATOM_TYPE_STRING,1,&(props[i]));

		free( props[i] );
		free( realname );
	}
}

//@ what pixelformat are we supposed to play,
/// what pixel format is file openened in ?
int	sample_open( void *sample, const char *token, int extra_token , sample_video_info_t *project_settings )
{
//	void *info = find_sample(id);
	int	res = 0;
	int	n_samples = 0;
	int 	my_palette = 0;
	if(!sample)
		return 0;

	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
			
	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
			srd->data = (void*) vj_el_open_video_file( token );
			if(!srd->data)
			{
				veejay_msg(0, "Unable to create sample from %s", token );
				srd->data = NULL;
				return 0;
			}
#ifdef STRICT_CHECKING
			assert( srd->data != NULL );
			assert( project_settings != NULL );
#endif
			res = vj_el_match( (void*) project_settings, srd->data );
			if(!res)
			{
				veejay_msg(0, "%s does not match current project properties",
						token );
			//	vj_el_free( srd->data );
			//	return 0;
			}
			vj_el_setup_cache( srd->data );
			sit->end_pos = vj_el_get_num_frames(srd->data);
			sit->looptype = 1;
			sit->speed = 1;
			sit->repeat = 0;
			sit->fps = (double) vj_el_get_fps(srd->data);
			sit->has_audio = vj_el_get_audio_rate( srd->data ) > 0 ? 1:0;
			sit->rate = (uint64_t) vj_el_get_audio_rate( srd->data );
			sit->bits = vj_el_get_audio_bits( srd->data );
			sit->bps = vj_el_get_audio_bps( srd->data );
			sit->channels = vj_el_get_audio_chans( srd->data );
			n_samples = sit->rate / sit->fps;
			sample_set_property_ptr( sample, "end_pos", VEVO_ATOM_TYPE_INT, &(sit->end_pos));
			sample_set_property_ptr( sample, "looptype",VEVO_ATOM_TYPE_INT, &(sit->looptype));
			sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&(sit->speed));
			sample_set_property_ptr( sample, "fps", VEVO_ATOM_TYPE_DOUBLE,&(sit->fps) );
			sample_set_property_ptr( sample, "has_audio", VEVO_ATOM_TYPE_INT, &(sit->has_audio) );
			sample_set_property_ptr( sample, "rate", VEVO_ATOM_TYPE_UINT64, &(sit->rate));
			sample_set_property_ptr( sample, "repeat", VEVO_ATOM_TYPE_INT, &(sit->repeat));
			sample_set_property_ptr( sample, "bps", VEVO_ATOM_TYPE_INT, &(sit->bps));
			sample_set_property_ptr( sample, "bits", VEVO_ATOM_TYPE_INT, &(sit->bits));
			sample_set_property_ptr( sample, "channels", VEVO_ATOM_TYPE_INT,&(sit->channels));
			sample_set_property_ptr( sample, "audio_spas", VEVO_ATOM_TYPE_INT, &n_samples);
			
			break;

		case VJ_TAG_TYPE_CAPTURE:
			srd->data = vj_unicap_new_device( unicap_data_,
						extra_token );
			srd->mapping = vpn( VEVO_ANONYMOUS_PORT );
			srd->rmapping = vpn( VEVO_ANONYMOUS_PORT );
			if(!vj_unicap_configure_device( srd->data,
						project_settings->fmt,
						project_settings->w,
						project_settings->h ))
			{
				veejay_msg(0, "Unable to configure device %d", extra_token);
				vj_unicap_free_device( srd->data );
				return NULL;
			}
			res=1;
			sit->speed = 1;
			sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&(sit->speed));
			sample_collect_unicap_properties( sample );

			break;
		case VJ_TAG_TYPE_NET:
			srd->data = (void*) vj_client_alloc( project_settings->w,project_settings->h, project_settings->fmt );
			break;
		case VJ_TAG_TYPE_YUV4MPEG:
			srd->data = vj_yuv4mpeg_alloc( 
					project_settings->fps,
					project_settings->w,
					project_settings->h,
					project_settings->sar_w,
					project_settings->sar_h);
			
			res = vj_yuv_stream_start_read( srd->data, token, project_settings->w, project_settings->h );
			if(!res)
				vj_yuv4mpeg_free( srd->data );
			break;
#ifdef USE_GDK_PIXBUF
		case VJ_TAG_TYPE_PICTURE:
			if( vj_picture_probe ( token )  )
				srd->data = vj_picture_open( token,project_settings->w,project_settings->h,0 );
//@ fixme: project!
			break;
#endif
		case VJ_TAG_TYPE_COLOR:
			sit->fps = (double) project_settings->fps;
			sit->has_audio = project_settings->has_audio;
			sit->rate = (uint64_t) project_settings->rate;
			sit->bits = project_settings->bits;
			sit->bps = project_settings->bps;
			sit->channels = project_settings->chans;
//			srd->data = vj_audio_init( 16384 * 100, sit->channels, 0 );
			sit->speed = 1;
			sit->end_pos = 32;
			sit->looptype = 1;

			double freq = 200.94;
			double amp = 5.0;
//			n_samples = vj_audio_gen_tone( srd->data, 0.04 , sit->rate,freq,amp );
//			veejay_msg(2, "Generated tone of %d samples. Freq %2.2f. Amplitude %2.2f", n_samples,freq,amp);


			uint64_t rate = (uint64_t) project_settings->rate;
			sample_set_property_ptr( sample, "rate", VEVO_ATOM_TYPE_UINT64, &(sit->rate));
			sample_set_property_ptr( sample, "has_audio", VEVO_ATOM_TYPE_INT, &(sit->has_audio ));
			sample_set_property_ptr( sample, "bps", VEVO_ATOM_TYPE_INT, &(sit->bps));
			sample_set_property_ptr( sample, "bits", VEVO_ATOM_TYPE_INT, &(sit->bits));
			sample_set_property_ptr( sample, "channels", VEVO_ATOM_TYPE_INT,&(sit->channels));
			sample_set_property_ptr( sample, "audio_spas", VEVO_ATOM_TYPE_INT, &n_samples);
			sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT, &(sit->speed));
			sample_set_property_ptr( sample, "end_pos", VEVO_ATOM_TYPE_INT, &(sit->end_pos));
			sample_set_property_ptr( sample, "looptype",VEVO_ATOM_TYPE_INT, &(sit->looptype));

			res = 1;
			break;
	}
	return res;
}

int	sample_append_file( const char *filename, long n1, long n2, long n3 )
{
	return 1;
}

int	sample_get_audio_properties( void *current_sample, int *bits, int *bps, int *num_chans, long *rate )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	sampleinfo_t *sit = srd->info;
		
	*rate = (long) sit->rate;
	*bits  = sit->bits;
	*bps   = sit->bps;
	*num_chans = sit->channels;
	return 1;
}

int	sample_edl_copy( void *current_sample, uint64_t start, uint64_t end )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	if(srd->type != VJ_TAG_TYPE_NONE )
	{
		veejay_msg(0, "This sample has no EDL");
		return 0;
	}

	uint64_t n = vj_el_get_num_frames( srd->data );
	if( start < 0 || start >= n || end < 0 || end >= n )
	{
		veejay_msg(0, "Invalid start or end position");
		return 0;
	}

	uint64_t len = 0;
	void  *frames = (void*) vj_el_edit_copy( srd->data,start,end,&len );
	
	if(!frames || len <= 0 )
	{
		veejay_msg(0, "EDL is empty");
	}
	
	void *prevlist = NULL;
	if( vevo_property_get( srd->info_port, "edl_buffer", 0, &prevlist ) == VEVO_NO_ERROR )
	{
		free(prevlist);	
	}
	
	int error = vevo_property_set( srd->info_port, "edl_buffer", VEVO_ATOM_TYPE_VOIDPTR,1,&frames );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	error = vevo_property_set( srd->info_port, "edl_buffer_len", VEVO_ATOM_TYPE_UINT64,1,&len );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	veejay_msg(2, "Copied frames %ld - %ld to buffer", start,end);
	return 1;
}

int	sample_edl_delete( void *current_sample, uint64_t start, uint64_t end )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	if(srd->type != VJ_TAG_TYPE_NONE )
	{
		veejay_msg(0, "This sample has no EDL");
		return 0;
	}

	uint64_t n = vj_el_get_num_frames( srd->data );
	if( start < 0 || start >= n || end < 0 || end >= n )
	{
		veejay_msg(0, "Invalid start or end position");
		return 0;
	}
	int success = vj_el_edit_del( srd->data, start, end );
	if(success)
	{
		veejay_msg(0, "Deleted frames %ld - %ld from EDL",start,end);
		sampleinfo_t *sit = srd->info;
		n = vj_el_get_num_frames(srd->data);
		if( sit->start_pos > n )
			sit->start_pos = 0;
		if( sit->end_pos > n )
			sit->end_pos = n;
		return 1;
	}
	return 0;
}

int	sample_edl_paste_from_buffer( void *current_sample, uint64_t insert_at )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	if(srd->type != VJ_TAG_TYPE_NONE )
	{
		veejay_msg(0, "This sample has no EDL");
		return 0;
	}

	void *prevlist = NULL;
	if( vevo_property_get( srd->info_port, "edl_buffer", 0, &prevlist ) != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Nothing in buffer to paste");
		return 0;
	}

	if( insert_at < 0 || insert_at >= vj_el_get_num_frames( srd->data ) )
	{
		veejay_msg(0, "Cannot paste beyond last frame of EDL");
		return 0;
	}

	uint64_t *fl = (uint64_t*) prevlist;
	uint64_t len = 0;

	int error = vevo_property_get( srd->info_port, "edl_buffer_len", 0, &len );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int n = vj_el_edit_paste( srd->data, insert_at, fl, len );
	
	if( n <= 0 )
	{
		veejay_msg(0, "Cannot paste buffer at position %ld", insert_at );
		return 0;
	}
	
	veejay_msg(1, "Pasted %ld frames from buffer to position %ld", len, insert_at );
	
	return 1;
}

int	sample_edl_cut_to_buffer( void *current_sample, uint64_t start_pos, uint64_t end_pos )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	sampleinfo_t *sit = srd->info;


	if(srd->type != VJ_TAG_TYPE_NONE )
	{
		veejay_msg(0, "This sample has no EDL");
		return 0;
	}
	
	uint64_t len = 0;
	void  *frames = (void*) vj_el_edit_copy( srd->data,start_pos,end_pos,&len );
	
	if(!frames || len <= 0 )
	{
		veejay_msg(0, "EDL is empty");
	}
	
	void *prevlist = NULL;
	if( vevo_property_get( srd->info_port, "edl_buffer", 0, &prevlist ) == VEVO_NO_ERROR )
	{
		free(prevlist);	
	}
	
	int error = vevo_property_set( srd->info_port, "edl_buffer", VEVO_ATOM_TYPE_VOIDPTR,1,&frames );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	error = vevo_property_set( srd->info_port, "edl_buffer_len", VEVO_ATOM_TYPE_UINT64,1,&len );
	
	int n = vj_el_edit_del( srd->data, start_pos,end_pos );

	uint64_t	last = vj_el_get_num_frames( srd->data );

	if( sit->start_pos > last )
		sit->start_pos = 0;
	if( sit->end_pos > last )
		sit->end_pos = last;
	
	return 1;	
}



void	sample_increase_frame( void *current_sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	sampleinfo_t *sit = srd->info;

	if(sit->type != VJ_TAG_TYPE_NONE)
	{
		sit->current_pos ++;
		return;
	}
	
	uint64_t cf = sit->current_pos;
	if(sit->repeat_count > 0)
		sit->repeat_count --;
	else	
	{
		cf += sit->speed;
		sit->repeat_count = sit->repeat;
	}
	sit->current_pos = cf;	

	if(sit->speed >= 0 )	
	{
		if( cf > sit->end_pos || cf < sit->start_pos )
		{
			switch( sit->looptype )
			{
				case 2:
					sit->current_pos = sit->end_pos;
					sit->speed *= -1;
					break;
				case 1:
					sit->current_pos = sit->start_pos;
					break;
				case 0:
					sit->current_pos = sit->end_pos;
					sit->speed	 = 0;
					break;
				default:
#ifdef STRICT_CHECKING
					assert(0);
#endif
					break;
			}
		}
	}
	else
	{
		if( cf < sit->start_pos || cf > sit->end_pos || cf <= 0 )
		{
			switch(sit->looptype)
			{
				case 2:
					sit->speed *= -1;
					sit->current_pos = sit->start_pos;
					break;
				case 1:
					sit->current_pos = sit->end_pos;
					break;
				case 0:
					sit->current_pos = sit->start_pos;
					sit->speed	 = 0;
					break;
				default:
#ifdef STRICT_CHECKING
					assert(0);
#endif
					break;
					
			}
		}
	}
#ifdef STRICT_CHECKING
	assert( sit->current_pos >= sit->start_pos );
	assert( sit->current_pos <= sit->end_pos );
#endif	
	void *sender = veejay_get_osc_sender( srd->user_data );
	if(sender)
	{
		veejay_bundle_sample_add( sender, srd->primary_key, "current_pos", "h", sit->current_pos );
	}

}

void	sample_set_itu601( void *current_sample, int status )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
#ifdef STRICT_CHECKING
	assert( srd->info );
#endif
	if(srd->type == VJ_TAG_TYPE_NONE)
	{
		vj_el_set_itu601( srd->data, status );
	}

}
static	void	sample_produce_frame( sample_runtime_data *srd, VJFrame *slot )
{
	double color[4] = {0.0,0.0,0.0,0.0};
	int error = 0;

	error = vevo_property_get( srd->info_port, "red", 0, &color[0]);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_get( srd->info_port, "green",0, &color[1]);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error =  vevo_property_get( srd->info_port, "blue", 0, &color[2]);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	double eY = (0.299 * color[0] ) +
		    (0.587 * color[1] ) +
		    (0.114 * color[2] );
	double eU = (-0.168736 * color[0] ) -
		    (-0.331264 * color[1] ) +
		    (0.500 * color[2] ) + 128.0;
	double eV = (0.500 * color[0]) -
		    (0.418688 * color[1]) -
		    (0.081312 * color[2]) + 128.0;

	uint8_t y = (uint8_t) eY;
	uint8_t u = (uint8_t) eU;
	uint8_t v = (uint8_t) eV;

	memset( slot->data[0], y, slot->len );
	memset( slot->data[1], u, slot->uv_len );
	memset( slot->data[2], v, slot->uv_len );	
}
int	sample_get_audio_frame( void *current_sample, void *buffer, int n_packets )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
	sampleinfo_t *sit = srd->info;
	int tmp = 0;	
	int error = 0;
#ifdef STRICT_CHECKING
	assert( srd->info );
#endif
	long frame_num = srd->info->current_pos;
	void *ptr = NULL;

	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
#ifdef STRICT_CHECKING
			assert( srd->data != NULL );
#endif
			return vj_el_get_audio_frame ( srd->data, frame_num,buffer, n_packets);
			break;
		case VJ_TAG_TYPE_COLOR:
			
			//	error = vevo_property_get( srd->info_port, "audio_spas",0,&tmp);
#ifdef STRICT_CHECKING
			//	assert( error == VEVO_NO_ERROR );
#endif
				return 0; //vj_audio_noise_pack( srd->data, buffer , tmp, sit->bps, n_packets );
			break;

		default:
			
			break;
	}
	return 0;
}
int	sample_get_frame( void *current_sample , void *dslot )
{
	VJFrame *slot = (VJFrame*) dslot;
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
#ifdef STRICT_CHECKING
	assert( srd->info );
#endif
	long frame_num = srd->info->current_pos;
	void *ptr = NULL;

	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
#ifdef STRICT_CHECKING
			assert( srd->data != NULL );
#endif
			vj_el_get_video_frame ( srd->data, frame_num,slot);
			break;
		case VJ_TAG_TYPE_CAPTURE:
			vj_unicap_grab_frame( srd->data, (void*) slot );

			break;
		case VJ_TAG_TYPE_NET:
			break;
		case VJ_TAG_TYPE_YUV4MPEG:
			vj_yuv_get_frame( srd->data, slot->data );
			break;
		case VJ_TAG_TYPE_COLOR:
			sample_produce_frame( srd,slot );
			break;
#ifdef STRICT_CHECKING
		default:
			assert(0);
			break;
#endif
	}
	return VEVO_NO_ERROR;
}

static	void	sample_close( sample_runtime_data *srd )
{
#ifdef STRICT_CHECKING
		assert( srd != NULL );
#endif
	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
			vj_el_free( srd->data );
			break;
		case VJ_TAG_TYPE_CAPTURE:
			vj_unicap_free_device( srd->data );
			break;
		case VJ_TAG_TYPE_YUV4MPEG:
			vj_yuv_stream_stop_read( srd->data );
			break;
#ifdef USE_GDK_PIXBUF
		case VJ_TAG_TYPE_PICTURE:
			break;
#endif
		case VJ_TAG_TYPE_NET:
		case VJ_TAG_TYPE_MCAST:
			vj_client_close( srd->data );
			break;
		case VJ_TAG_TYPE_COLOR:
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
	}	
}


int	sample_delete( int id )
{
	void *info = find_sample(id);
	if(!info)
		return 0;
	sample_delete_ptr( info );	
	return 1;
}
//@ fixme: move / dup function
static char            *clone_str( void *port, const char *key )
{
        size_t len = vevo_property_element_size( port, key, 0 );
        char *ret = NULL;
        if(len<=0) return NULL;
        ret = (char*) vj_malloc(sizeof(char) * len );
        vevo_property_get( port, key, 0, &ret );
        return ret;
}


static	void	sample_write_property( void *port, const char *key, xmlNodePtr node )
{
	int atom_type = vevo_property_atom_type( port, key );
	int int_val = 0;
	double dbl_val = 0.0;
	char *str_val = NULL;
	int error = 0;

	switch( atom_type )
	{
		case VEVO_ATOM_TYPE_INT:
		case VEVO_ATOM_TYPE_BOOL:
			error = vevo_property_get( port, key, 0, &int_val );
			if(error )
				null_as_property( node, key );
			else
				int_as_value( node, int_val, key );
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			error = vevo_property_get( port, key, 0, &dbl_val );
			if( error )
				null_as_property( node, key );
			else
				double_as_value( node, dbl_val, key );
			break;
		case VEVO_ATOM_TYPE_STRING:
			str_val = clone_str( port, key );
			if(str_val == NULL )
				null_as_property( node, key );
			else
				cstr_as_value( node, str_val, key );
			break;
		case VEVO_ATOM_TYPE_PORTPTR:
		case VEVO_ATOM_TYPE_VOIDPTR:
		//	veejay_msg(0, "Not expanding type %d : '%s'", atom_type,key );
			sample_expand_properties( port, key, node );
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;			
			
	}
}
static	void	sample_expand_port( void *port, xmlNodePtr node )
{
	char **props = (char**) vevo_list_properties( port );
	int i;
#ifdef STRICT_CHECKING
	assert( props != NULL );
#endif
	for( i = 0; props[i] != NULL; i ++ )
	{
		sample_write_property( port, props[i], node );
		free(props[i]);
	}
	free(props);
}

static	void	sample_expand_properties( void *sample, const char *key, xmlNodePtr root )
{
	int n = vevo_property_num_elements( sample, key );
	char name[KEY_LEN];
	void *port = NULL;
	int error;
	if( n > 1 )
	{
		int i;
		for( i = 0; i < n ; i ++ )
		{
			error = vevo_property_get( sample, key, i, &port );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			sprintf(name, "%s%x",key,i);
			xmlNodePtr	node = xmlNewChild(root, NULL, (const xmlChar*) name, NULL );
			sample_expand_port( port, node );
		}
	}
	if( n == 1 )
	{
		error = vevo_property_get( sample, key, 0, &port );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
		sample_expand_port( port, root );
	}	
}

static void	sample_write_properties( void *sample, xmlNodePtr node )
{
	char **props = (char**) vevo_list_properties( sample );
	int i;
#ifdef STRICT_CHECKING
	assert( props != NULL );
#endif
	for( i = 0; props[i] != NULL; i ++ )
		sample_write_property( sample, props[i], node );
}

int	samplebank_xml_save( const char *filename )
{
	const char *encoding = "UTF-8";
	int i,error;
	xmlNodePtr rootnode, childnode;

	xmlDocPtr doc = xmlNewDoc( "1.0" );

	rootnode = xmlNewDocNode( doc, NULL, (const xmlChar*) XMLTAG_SAMPLES, NULL );

	xmlDocSetRootElement( doc, rootnode );

	for( i = 1; i < samplebank_size(); i ++ )
	{
		void *sample = find_sample( i );
		if(sample)
		{
			sample_runtime_data *srd = (sample_runtime_data*) sample;
#ifdef STRICT_CHECKING
			assert( srd->info_port != NULL );
#endif
			//@ save editlist!
			childnode = xmlNewChild(rootnode, NULL, (const xmlChar*) XMLTAG_SAMPLE, NULL );
			int_as_property( childnode, srd->type, "type");
			sample_write_properties( srd->info_port, childnode );
		}
	}
	int ret = xmlSaveFormatFileEnc( filename, doc, encoding, 1 );

	xmlFreeDoc( doc );
	
	if( ret < 0 )
		ret = 0;
	else
		ret = 1;
	
	return ret;	
}

static int	sample_identify_xml_token( int sample_type, const unsigned char *name )
{
	int i = 0;
	for( i = 0; common_property_list[i].name != NULL ; i ++ )
	{
		if( strcasecmp( (const char*) name, common_property_list[i].name ) == 0 )
			return common_property_list[i].atom_type;
	}
	if(sample_type == VJ_TAG_TYPE_NONE)
	{
		for( i = 0; sample_property_list[i].name != NULL; i ++ )
		{
			if( strcasecmp( (const char*) name, sample_property_list[i].name ) == 0 )
				return sample_property_list[i].atom_type;
		}
	}
	else
	{
		for( i = 0; stream_property_list[i].name != NULL ; i ++ )
		{
			if( strcasecmp( (const char*) name, stream_property_list[i].name ) == 0 )
				return stream_property_list[i].atom_type;
		}
		if( sample_type == VJ_TAG_TYPE_NET )
		{
			for( i = 0; stream_socket_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_socket_list[i].name ) == 0 )
					return stream_socket_list[i].atom_type;
			}
		}
		if( sample_type == VJ_TAG_TYPE_MCAST )
		{
			for( i = 0; stream_mcast_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_mcast_list[i].name ) == 0 )
					return stream_mcast_list[i].atom_type;
			}
		}
		if( sample_type == VJ_TAG_TYPE_COLOR )
		{
			for( i = 0; stream_color_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_color_list[i].name ) == 0 )
					return stream_color_list[i].atom_type;
			}
		}
		if( sample_type == VJ_TAG_TYPE_YUV4MPEG )
		{
			for( i = 0; stream_file_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_file_list[i].name ) == 0 )
					return stream_color_list[i].atom_type;
			}

		}	
	}
	return 0;
}


static void	sample_from_xml( void *samplebank, int sample_type, xmlDocPtr doc, xmlNodePtr cur )
{
#ifdef STRICT_CHECKING
	assert( sample_type >= 0 && sample_type <= 15 );
#endif	
	void *sample = sample_new( sample_type );
	char key[20];
	while( cur != NULL )
	{
		//@ iterate through properties!
		int atom_type = sample_identify_xml_token( sample_type, cur->name );
		int error = 0;
		char *str = NULL;
		int   int_val = 0;
		double dbl_val = 0.0;

		switch(atom_type)
		{
			case VEVO_ATOM_TYPE_INT:
				int_val = value_as_int( doc, cur, cur->name );
				vevo_property_set( sample, cur->name, atom_type, 1,&int_val );
				break;
			case VEVO_ATOM_TYPE_DOUBLE:
				dbl_val = value_as_double( doc, cur, cur->name );
				vevo_property_set( sample, cur->name, atom_type, 1, &dbl_val );
				break;
			case VEVO_ATOM_TYPE_STRING:
				str = value_as_cstr( doc, cur, cur->name );
				vevo_property_set( sample, cur->name, atom_type, 1, &str );
				break;
			default:
#ifdef STRICT_CHECKING
				assert(0);					
#endif
				break;
		}
		cur = cur->next;		
	}

	samplebank_add_sample( sample );
}

int	sample_xml_load( const char *filename, void *samplebank )
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	doc = xmlParseFile( filename );
	if(!doc)
		return 0;

	cur = xmlDocGetRootElement( doc );
	if(!cur)
	{
		xmlFreeDoc(doc);
		return 0;
	}
	
	if( xmlStrcmp( cur->name, (const xmlChar*)XMLTAG_SAMPLES) )
	{
		xmlFreeDoc( doc );
		return 0;
	}
	
	cur = cur->xmlChildrenNode;
	while( cur != NULL )
	{
		if(! xmlStrcmp( cur->name, (const xmlChar*) XMLTAG_SAMPLE) )
		{
			int type = property_as_int( doc, cur, "type" );
			sample_from_xml(samplebank, type,doc, cur->xmlChildrenNode );
		}
		cur = cur->next;
	}
	xmlFreeDoc( doc );
	return 1;
}

int	sample_set_param_from_str( void *srd, int fx_entry,int p, const char *str )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( srd, fx_entry );

	if(slot->fx_instance)
		return plug_set_param_from_str( slot->fx_instance, p, str,slot->in_values );
	else
		veejay_msg(0, "No FX in slot %d",fx_entry);

	return 0;
}

char	*sample_describe_param( void *srd, int fx_entry, int p )
{
	fx_slot_t *slot = sample_get_fx_port_ptr( srd, fx_entry );
	if(slot->fx_instance)
		return plug_describe_param( slot->fx_instance, p );
	return NULL;
}

static	int	sample_parse_param( void *fx_instance, int num, const char format[], va_list ap )
{
	int n_elems = strlen( format );
	if( n_elems <= 0 )
		return 0;

	double	*g = NULL;
	int32_t *i = NULL;
	char    *s = NULL;
	void	*p = NULL;
	
	switch(format[0])
	{
		case 'd':
		 	i = (int32_t*) vj_malloc( sizeof(int32_t) * n_elems );
			p = &i;
			break;
		case 'g':
			g = (double*) vj_malloc(sizeof(double) * n_elems );
			p = &g;
			break;
		case 's':
			s = NULL;
			p = &s;
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;	
	}

	while(*format)
	{
		switch(*format)
		{
			case 'd':
				*i++ = (int32_t) *(va_arg( ap, int32_t*)); break;
			case 'g':
				*g++ = (double) *(va_arg( ap, double*)); break;
			case 's':
				s    = strdup( (char*) va_arg(ap,char*) ); break;
			default:
				break;
		}
		*format++;
	}
veejay_msg(0, "Change parameter");
	plug_set_parameter( fx_instance, num,n_elems, p );

	if( i ) free( i );
	if( g ) free( g );
	if( s ) free( s );
	
	return n_elems;
}

int	sample_set_param( void *srd, int fx_entry, int p, const char format[] , ... )
{
	va_list ap;
	int n = 0;

	fx_slot_t *slot = sample_get_fx_port_ptr( srd, fx_entry );
	if(!slot->fx_instance)
		return n;

	va_start( ap, format );
	n = sample_parse_param( slot->fx_instance, p , format, ap );
	va_end(ap);
	
	return n;
}

void	sample_process_fx_chain( void *srd )
{
	int k = 0;
	for( k = 0; k < SAMPLE_CHAIN_LEN; k ++ )
	{
		fx_slot_t *slot = sample_get_fx_port_ptr( srd, k );
		plug_process( slot->fx_instance );
	}
}

static void	sample_get_position_info(void *port , uint64_t *start, uint64_t *end, int *loop, int *speed)
{
	vevo_property_get( port, "start_pos", 0,start );
	vevo_property_get( port, "end_pos", 0, end );
	vevo_property_get( port, "looptype",0, loop );
	vevo_property_get( port, "speed",0,speed );
}
void	sample_cache_data( void *info )
{
#ifdef STRICT_CHECKING
	assert( info != NULL );
#endif
	sample_runtime_data *srd = (sample_runtime_data*) info;
	sampleinfo_t *sit = srd->info;

	vevo_property_get( srd->info_port, "start_pos",0, &(sit->start_pos));
	vevo_property_get( srd->info_port, "end_pos",0,&(sit->end_pos));
	vevo_property_get( srd->info_port, "speed",0,&(sit->speed));
	vevo_property_get( srd->info_port, "looptype",0,&(sit->looptype));
	vevo_property_get( srd->info_port, "current_pos",0,&(sit->current_pos));
	vevo_property_get( srd->info_port, "in_point",0,&(sit->in_point));
	vevo_property_get( srd->info_port, "out_point",0,&(sit->out_point));
	vevo_property_get( srd->info_port, "fps",0,&(sit->fps));
	vevo_property_get( srd->info_port, "has_audio",0,&(sit->has_audio));
	vevo_property_get( srd->info_port, "rate",0,&(sit->rate));
	vevo_property_get( srd->info_port, "repeat",0,&(sit->repeat));
}

void		sample_save_cache_data( void *info)
{
#ifdef STRICT_CHECKING
	assert( info != NULL );
#endif

	sample_runtime_data *srd = (sample_runtime_data*) info;
	sampleinfo_t *sit = srd->info;
	vevo_property_set( srd->info_port, "start_pos",VEVO_ATOM_TYPE_UINT64,1,&(sit->start_pos ));
	vevo_property_set( srd->info_port, "end_pos", VEVO_ATOM_TYPE_UINT64,1,&(sit->end_pos));
	vevo_property_set( srd->info_port, "speed", VEVO_ATOM_TYPE_INT,1,&(sit->speed ));
	vevo_property_set( srd->info_port, "looptype", VEVO_ATOM_TYPE_INT,1,&(sit->looptype ));
	vevo_property_set( srd->info_port, "current_pos", VEVO_ATOM_TYPE_UINT64,1,&(sit->current_pos));
	vevo_property_set( srd->info_port, "in_point",VEVO_ATOM_TYPE_UINT64,1,&(sit->in_point));
	vevo_property_set( srd->info_port, "out_point",VEVO_ATOM_TYPE_UINT64,1,&(sit->out_point));
	vevo_property_set( srd->info_port, "fps", VEVO_ATOM_TYPE_DOUBLE,1,&(sit->fps));
	vevo_property_set( srd->info_port, "has_audio", VEVO_ATOM_TYPE_INT,1,&(sit->has_audio));
	vevo_property_set( srd->info_port, "rate", VEVO_ATOM_TYPE_UINT64,1,&(sit->rate));
	vevo_property_set( srd->info_port, "repeat",VEVO_ATOM_TYPE_INT,1,&(sit->repeat));
}

void		sample_format_status( void *sample, char *buf )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;

	sprintf(buf,
		"%04d:%04d:%06d:%06d:%06d:%06d:%06d:%02d:%02d:%d:%03.2g",
		sample_get_key_ptr( sample ),
		samplebank_size(),
		sit->start_pos,
		sit->end_pos,
		sit->current_pos,
		sit->in_point,
		sit->out_point,
		sit->speed,
		sit->repeat,
		sit->looptype,
	        sit->rec );
		
	
}

int	sample_valid_speed(void *sample, int new_speed)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	int sample_len = sit->end_pos - sit->start_pos;
	if(new_speed < sample_len)
		return 1;
	return 0;
}
int	sample_valid_pos(void *sample, uint64_t pos)
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	if(pos >= sit->start_pos && pos <= sit->end_pos )
		return 1;
	return 0;
}

uint64_t	sample_get_audio_rate( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->rate;
}

double		sample_get_fps( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->fps;
}

uint64_t	sample_get_start_pos( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->start_pos;
}
uint64_t	sample_get_end_pos( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->end_pos;
}
int	sample_get_speed( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->speed;
}
int	sample_get_repeat( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->repeat;
}
int	sample_get_repeat_count( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->repeat_count;
}
uint64_t	sample_get_current_pos( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->current_pos;
}
int	sample_get_looptype( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->looptype;
}

int	sample_has_audio( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	return sit->has_audio;
}

void	sample_set_current_pos( void *sample, uint64_t pos )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	sit->current_pos = pos;
}
void	sample_set_end_pos( void *sample, uint64_t pos )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	sit->end_pos = pos;
}
void	sample_set_start_pos( void *sample, uint64_t pos )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	sit->start_pos = pos;
}
void	sample_set_looptype( void *sample, int pos )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	sit->start_pos = pos;
}


int	sample_fx_sscanf_port( void *sample, const char *s, const char *id )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int entry_id = 0;
	int n = sscanf( id, "fx_%02d#", &entry_id);
	if( n <= 0 )
		return 0;

	//@ fx_instance must be valid
	return 1;
}
#define PROPERTY_KEY_SIZE 64
#define MAX_ELEMENTS 16
static 	int	sample_sscanf_property(	sample_runtime_data *srd ,vevo_port_t *port, const char *s)
{
#ifdef STRICT_CHECKING
	assert(0);
#endif
	int done = 0;
	char key[PROPERTY_KEY_SIZE];
	bzero(key, PROPERTY_KEY_SIZE );	
	const char *value = vevo_split_token_(s, '=', key, PROPERTY_KEY_SIZE );
	if(value==NULL)
		return 0;

	char *format = vevo_format_property( port, key );
	int  atom    = vevo_property_atom_type( port, key );

#ifdef STRICT_CHECKING
	int n_elem   = vevo_property_num_elements( port , key);
	assert( n_elem == 1 ); // function not OK for arrays of atoms
#endif
	
	if( format == NULL )
		return done;
	if(atom==-1)
		atom = VEVO_ATOM_TYPE_DOUBLE;
	//@ if a property does not exist, DOUBLE is assumed
	//@ DOUBLE is valid for all sample's of type capture.
	
	uint64_t i64_val[MAX_ELEMENTS];
	int32_t	 i32_val[MAX_ELEMENTS];
	double   dbl_val[MAX_ELEMENTS];
	char     *str_val[MAX_ELEMENTS];
	
	int	 cur_elem = 0;
	int	 n = 0;
	
	const char 	*p = value;
	char	*fmt = format;
	while( *fmt != '\0' )
	{
		char arg[256];
		bzero(arg,256);
		
		if( *fmt == 's' )
			p = vevo_split_token_q( p, ':', arg, 1024 );
		else
			p = vevo_split_token_( p, ':', arg, 1024 );

		if( p == NULL )
			return 0;
		
		if( arg[0] != ':' ) 
		{
			switch(*fmt)
			{
				case 'd':
					n = sscanf( arg, "%d", &(i32_val[cur_elem]));
					break;
				case 'D':
					n = sscanf( arg, "%lld", &(i64_val[cur_elem]));
					break;
				case 'g':
					n = sscanf( arg, "%lf", &(dbl_val[cur_elem]));
					break;
				case 's':
					str_val[cur_elem] = strdup( arg );
					n = 1;
					break;
				default:
					n = 0;
					break;
			}
		}
		else
		{
			n = 0;
		}
		
		*fmt++;
		cur_elem ++;
	}

	void *ptr = NULL;
	if( n > 0 )
	switch( *format )
	{
		case 'd':
			ptr = &(i32_val[0]);
			break;
		case 'D':
			ptr = &(i64_val[0]);
			break;
		case 'g':
			ptr = &(dbl_val[0]);
			break;
		case 's':
			ptr = &(str_val[0]);
			break;
	}	
	
	int error = 0;

	//veejay_msg(0, "Set: '%s' : %d, %g", key,n, dbl_val[0] );
	if( n == 0 )
		error = vevo_property_set( port, key, atom, 0, NULL );
	else
	{
		sample_set_property_ptr( srd, key, atom, ptr );
		error = VEVO_NO_ERROR;
	}
	if( error == VEVO_NO_ERROR )
		done = 1;
	return done;
}

int	sample_sscanf_port( void *sample, const char *s )
{
#ifdef STRICT_CHECKING
	assert(0);
#endif
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	const char *ptr = s;
	int   len = strlen(s);
	int   i = 0;
	while( len > 0 )
	{
		char *token = vevo_scan_token_(ptr);
		int token_len;
		if( token )
		{
			token_len = strlen(token);
			if(sample_sscanf_property(srd,srd->info_port, token ))
				i++;
		}
		else
		{
			token_len = len;
			if(sample_sscanf_property( srd,srd->info_port, ptr ))
				i++;
		}
		len -= token_len;
		ptr += token_len;
	}
	return 1;
	//	return vevo_sscanf_port( srd->info_port, s );
}	
	


char 	*sample_sprintf_port( void *sample )
{
#ifdef STRICT_CHECKING
	assert(0);
#endif
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int k;
	int size = 0;
	char **test = vevo_sprintf_port( srd->info_port );

	if( test == NULL )
		return NULL;
	
	for( k = 0; test[k] != NULL; k ++ )
		size += strlen(test[k]);

	char *buf = (char*) calloc( 1,size+1 );
	
	for( k = 0; test[k] != NULL ; k ++ )
	{
		strcat(buf,test[k]);
		free(test[k]);
	}
	free(test);

	return buf;	
}

static	long	sample_tc_to_frames( const char *tc, float fps )
{
	int h = 0;
	int m = 0;
	int s = 0;
	int f = 0;
	int n = sscanf( tc, "%d:%d:%d:%d", &h,&m,&s,&f);
	long res = 0;
	if( n == 4 )
	{
		res += f;
		res += (long) (fps * s);
		res += (long) (fps * 60 * m );
		res += (long) (fps * 3600 * h );
	}
	return res;
}

int	sample_configure_recorder( void *sample, int format, const char *filename, int nframes) 
{
	char	fmt = 'Y'; //default uncompressed format
	int	max_size = 0;
	char	*codec;
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	sampleinfo_t *sit = srd->info;
	samplerecord_t *rec = srd->record;
	sample_video_info_t *ps = veejay_get_ps( srd->user_data );
	if( !filename )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No filename given");
		return -1;
	}
	if( nframes <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No frames to record");
		return -1;
	}

	
	if( sit->rec )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Please stop the recorder first");
		return 1;
	}
	
	switch( format )
	{
		//@ todo: not all encoders here, mod lav_io.c
		case ENCODER_DVVIDEO:
			fmt = 'd';
			if( ps->w == 720 && (ps->h == 480 || ps->h == 576 ) )
				max_size = ( ps->h == 480 ? 120000: 144000 );
			break;	
		case ENCODER_MJPEG:
			fmt = 'a';
			max_size = 65535 * 4;
			break;		
		case ENCODER_YUV420:
			fmt = 'Y';
			max_size = ( ps->h * ps->w ) + (ps->h * ps->w / 2 );
			break;
		case ENCODER_YUV422:
			fmt = 'P';
			max_size = 2 * (ps->h * ps->w);
			break;
		case ENCODER_YUV444:
			fmt = 'Q';
			max_size = 3 * (ps->h * ps->w);
			break;
		case ENCODER_MPEG4:
			fmt = 'M';
			max_size = 65535 * 4;
			break;
		case ENCODER_DIVX:
			fmt = 'D';
			max_size = 65535 * 4;
			break;
		case ENCODER_LOSSLESS:
			fmt = 'L';
			max_size = 65535 * 4;
			break;
		case ENCODER_MJPEGB:
			fmt = 'A';
			max_size = 65535 * 4;
			break;
		case ENCODER_HUFFYUV:
			fmt = 'H';
			max_size = 65535 * 4;
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unknown recording format");
			return 1;
			break;
	}

	codec = get_codec_name( format );

	rec->format = format;
	rec->aformat = fmt;
	
	if(nframes > 0)
		rec->tf = nframes; //sample_tc_to_frames( timecode, ps->fps);
	else
		rec->tf = (long) (ps->fps * 60);
	
	int error = vevo_property_set( srd->info_port, "filename", VEVO_ATOM_TYPE_STRING,1, &filename );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	rec->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * max_size );

	if(!rec->buf )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate buffer for recorder");
		return 1;
	}	
		
	memset( rec->buf,0, max_size );

	
	rec->con = 1;
	rec->max_size = max_size;

	veejay_msg(VEEJAY_MSG_INFO, "Record to %s (%d frames) in %s", filename, rec->tf, codec );

	return VEVO_NO_ERROR;	
}

int	sample_start_recorder( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	samplerecord_t *rec = srd->record;
	sample_video_info_t *ps = veejay_get_ps( srd->user_data );
	
	if(!rec->con)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "You must configure the recorder first");
		return 1;
	}
	if(rec->rec)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "The recorder is already active");
		return 1;
	}
	
	char *destination = get_str_vevo( srd->info_port,"filename");
#ifdef STRICT_CHECKING
	assert( destination != NULL );
	assert( rec->tf > 0 );
#endif	

	rec->codec = vj_avcodec_new_encoder(
			rec->format, ps->w,ps->h,ps->fmt, (double)ps->fps );

	if(!rec->codec)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize '%s' codec",
				get_codec_name( rec->format ) );
		free(rec->buf);
		memset( rec, 0  , sizeof( samplerecord_t ));	
		return -1;
	}
	
	rec->fd = (void*)
		lav_open_output_file( destination, rec->aformat,
				ps->w, ps->h, ps->inter, ps->fps,
				ps->bps, ps->chans, ps->rate );

	
	if(!rec->fd )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to record to '%s'. Please (re)configure recorder", destination );
		free(rec->buf);
		memset( rec, 0  , sizeof( samplerecord_t ));	
		return 1;
	}
	
	rec->nf = 0;
	rec->rec = 1;

	free(destination);
	
	return VEVO_NO_ERROR;
}

int	sample_is_recording( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	samplerecord_t *rec = srd->record;
	if( rec->rec )
		return 1;
	return 0;
}

char *	sample_get_recorded_file( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	char *destination = get_str_vevo( srd->info_port,"filename");
	return destination;
}

int	sample_stop_recorder( void *sample )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	samplerecord_t *rec = srd->record;
	sampleinfo_t   *sit = srd->info;
	if(!rec->rec)
		return 1;

	vj_avcodec_close_encoder( rec->codec );

	lav_close( (lav_file_t*) rec->fd );
	if( rec->buf )
		free(rec->buf);

	memset( rec, 0, sizeof( samplerecord_t ));
	
	rec->max_size = 0;
	sit->rec      = 0.0;

	return VEVO_NO_ERROR;	
}


int	sample_record_frame( void *sample, void *dframe, uint8_t *audio_buffer, int a_len )
{
	VJFrame *frame = (VJFrame*) dframe;
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	samplerecord_t *rec = srd->record;
	sampleinfo_t   *sit = srd->info;

	
	int compr_len = vj_avcodec_encode_frame(
			rec->codec,
			rec->format,
			(void*)frame,
			rec->buf,
			rec->max_size,
		        (uint64_t) rec->nf 	);

	if( compr_len <= 0 )
	{
		veejay_msg(0, "Cannot encode frame %d", rec->nf );
		return sample_stop_recorder( sample );
	}

	int n = lav_write_frame( (lav_file_t*) rec->fd, rec->buf, compr_len,1 );

	if( n < 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Writing video frame");
		return sample_stop_recorder(sample);
	}
	
	if(sit->has_audio)
	{
		n = lav_write_audio( (lav_file_t*) rec->fd, audio_buffer,a_len );
		if( n <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Writing audio frame");
			return sample_stop_recorder(sample);
		}
	}

	rec->nf ++;
	
	if( rec->nf >= rec->tf )
	{
		veejay_msg(VEEJAY_MSG_INFO, "Done recording");
		sample_stop_recorder(sample);
		return 2;
	}

	sit->rec = (double) (1.0/rec->tf) * rec->nf;

	return 1;
}

int	sample_reset_bind( void *sample, void *src_entry )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = (fx_slot_t*) src_entry;
	
	if(slot->bind)
	{
		int i = 0;
		char **items = vevo_list_properties( slot->bind );
		for( i = 0; items[i] != NULL ; i ++ )
		{
			void *p = NULL;
			if(vevo_property_get(slot->bind, items[i],0,&p) == VEVO_NO_ERROR )
				free(p);
		}
		vevo_port_free( slot->bind );
		slot->bind = NULL;
	}
	
	return 1;	
}
int	sample_del_bind_occ( void *sample, void *src_entry, int n_out_p, int k_entry )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = (fx_slot_t*) src_entry;
	if(!slot->bind)
		return 0;
	char **items = slot->bind;
	if(!items)
		return 0;
	char name[64];
	void *sender = veejay_get_osc_sender( srd->user_data );
	sprintf(name,"SampleBind%dFX%02dOP%d", srd->primary_key, k_entry, n_out_p );
	if(sender)
		veejay_ui_bundle_add( sender, "/destroy/window", "sx", name );
		

	int i;
	for( i = 0; items[i] != NULL ; i ++ )
	{
		int dummy = 0;
		int dd=0;
		int this_p = -1;
		sscanf( items[i], "bp%d_%d_%d", &this_p,&dd,&dummy);	

		if( this_p == n_out_p && vevo_property_get(slot->bind,items[i],0,NULL) == VEVO_NO_ERROR)
		{
			void *bp = NULL;
			int error = vevo_property_get( slot->bind, items[i],0,&bp );
			if( error == VEVO_NO_ERROR )
			{
				free(bp);
				vevo_property_set( slot->bind, items[i], VEVO_ATOM_TYPE_VOIDPTR,0,NULL );
				bp = NULL;
			}
		}
		free(items[i]);
	}
	free(items);
	return VEVO_NO_ERROR;	
}

int	sample_del_bind( void *sample, void *src_entry, int k_entry, int n_out_p, int fx_right, int in_p )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = (fx_slot_t*) src_entry;
	if(!slot->bind)
		return 0;
	char key[64];
	void *bp = NULL;
	sprintf(key, "bp%02d_%02d_%02d", n_out_p, fx_right, in_p );
	char name[64];
	sprintf(name,"SampleBind%dFX%02dOP%d", srd->primary_key, k_entry, n_out_p );

	int error = vevo_property_get( slot->bind, key,0,&bp );
	if( error == VEVO_NO_ERROR )
	{
		free(bp);
		error = vevo_property_set( slot->bind, key, VEVO_ATOM_TYPE_VOIDPTR,0,NULL );
		bp = NULL;
	}
	
	vevosample_ui_get_bind_list( srd, name );

	
	return VEVO_NO_ERROR;	
}

void	*sample_new_bind_parameter( void *fx_a, void *fx_b, int n_out_p, int n_in_p, int n_in_entry )
{
	bind_parameter_t *bt = (bind_parameter_t*) vj_malloc(sizeof( bind_parameter_t));
	memset(bt,0,sizeof(bind_parameter_t));
	int dummy = 0;
#ifdef STRICT_CHECKING
	assert( fx_a != NULL );
	assert( fx_b != NULL );
	assert( n_out_p >= 0 && n_in_p >= 0 && n_in_entry >= 0 && n_in_entry < SAMPLE_CHAIN_LEN );
#endif
	if( plug_parameter_get_range_dbl( fx_a, "out_parameters", n_out_p, &(bt->min[0]), &(bt->max[0]) , &dummy ) != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Can only bind parameters of type INDEX or NUMBER");
		free(bt);
		return NULL;
	}	
	if( plug_parameter_get_range_dbl( fx_b, "in_parameters", n_in_p, &(bt->min[1]), &(bt->max[1]), &(bt->kind) ) != VEVO_NO_ERROR )
	{
		veejay_msg(0, "Can only bind parameters of type INDEX or NUMBER");
		free(bt);
		return NULL;
	}
	bt->p[BIND_OUT_P] = n_out_p;
	bt->p[BIND_IN_P] = n_in_p;
	bt->p[BIND_ENTRY] = n_in_entry;

	veejay_msg(0, "New bind %d %d %d", n_out_p,n_in_p, n_in_entry );
	
	return (void*) bt;

}

int	sample_new_bind( void *sample, void *src_entry,int n_out_p,  int dst_entry, int n_in_p )
{
	void *fx_a = NULL;
	void *fx_b = NULL;
	int error = 0;
	char bind_key[64];
	char param_key[64];
	sample_runtime_data *srd = (sample_runtime_data*) sample;

	veejay_msg(0, "Out = %d, In = %d, Entry = %d", n_out_p, n_in_p, dst_entry );
	fx_slot_t *dst_slot = sample_get_fx_port_ptr( srd, dst_entry );
	fx_slot_t *src_slot = (fx_slot_t*) src_entry;

	if(!src_slot->fx_instance || !dst_slot->fx_instance )
	{
		veejay_msg(0, "There must be a plugin in both FX slots");
		return 1;
	}

	sprintf( param_key, "bp%02d_%02d_%02d",n_out_p, dst_entry, n_in_p);

	if(!src_slot->bind)
		src_slot->bind = vpn( VEVO_ANONYMOUS_PORT );

	if( vevo_property_get( src_slot->bind, param_key, 0, NULL ) == VEVO_NO_ERROR )
	{
		veejay_msg(0, "There is a bind from this output parameter to Entry %d, input %d",
				dst_entry,n_in_p );
		return 1;
	}

	void *inp = sample_new_bind_parameter( 
			src_slot->fx_instance,
			dst_slot->fx_instance,
			n_out_p,
			n_in_p,
			dst_entry
		);
	
	if(!inp)
	{
		veejay_msg(0, "Error while binding parameter");
		return 1;
	}
	
	error = vevo_property_set( src_slot->bind, param_key, VEVO_ATOM_TYPE_VOIDPTR,1,&inp );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return VEVO_NO_ERROR;	
}

int	sample_apply_bind( void *sample, void *current_entry, int k_entry )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	fx_slot_t *slot = (fx_slot_t*) current_entry;
#ifdef STRICT_CHECKING
	assert( slot->bind != NULL );
#endif
	char **items = vevo_list_properties( slot->bind );
	if(! items )
	{
		veejay_msg(0, "BIND empty for sample %d",
				srd->primary_key );
		return 1;
	}
	char key[64];
	char dkey[64];
	char dwin[32];
	sprintf(dwin , "Sample%dFX%d", srd->primary_key, k_entry );
	int i;

	for( i = 0; items[i] != NULL ; i ++ ) 
	{
		void *bp = NULL;
		int error = vevo_property_get( slot->bind, items[i],0,&bp );
		if( error == VEVO_NO_ERROR )
		{

			bind_parameter_t *bpt = (bind_parameter_t*) bp;

			void *fx_b = NULL;
			fx_slot_t *dst_slot = sample_get_fx_port_ptr( srd, bpt->p[BIND_ENTRY] );

			if(dst_slot->active && slot->active)
			{
				double weight = 0.0;
				int    iw = 0;
				double value=0.0;
				char *path = plug_get_osc_path_parameter( dst_slot->fx_instance, bpt->p[BIND_IN_P] );
				char *fmt  = plug_get_osc_format( dst_slot->fx_instance, bpt->p[BIND_IN_P]);
				char pkey[8];
				sprintf(pkey, "p%02d",bpt->p[BIND_OUT_P]);
				void *sender = veejay_get_osc_sender( srd->user_data );

				char *output_pname[128];
				char *output_pval = vevo_sprintf_property_value( slot->out_values, pkey );

				sprintf(output_pname, "o%02d", bpt->p[BIND_OUT_P]);
				if(bpt->kind == HOST_PARAM_INDEX)
				{
					if( vevo_property_get( slot->out_values, pkey,0,&value ) == VEVO_NO_ERROR )
					{
						weight = 1.0 / (bpt->max[1] - bpt->min[1]);
						iw = (int) ( value * weight );
						plug_set_parameter( dst_slot->fx_instance, bpt->p[BIND_IN_P], 1, &iw );
						if(path && sender)
							veejay_bundle_add( sender, path, fmt, iw );
						if(sender)
							veejay_xbundle_add( sender,dwin , output_pname,"s", (output_pval==NULL ? " " : output_pval) );
					}
				}
				else if(bpt->kind == HOST_PARAM_NUMBER)
				{
					int err = vevo_property_get( slot->out_values, pkey,0,&value );
				        if( err	== VEVO_NO_ERROR )
					{
						double norm = (1.0 / (bpt->max[0] - bpt->min[0])) * value;
						double gv = (bpt->max[1] - bpt->min[1]) * norm + bpt->min[1];
						plug_set_parameter( dst_slot->fx_instance, bpt->p[BIND_IN_P], 1, &gv );
	
						if(path && sender)
							veejay_bundle_add( sender, path, fmt, gv );
						if(sender)
							veejay_xbundle_add( sender, dwin, output_pname,"s",(output_pval==NULL ? " " : output_pval));
					}
#ifdef STRICT_CHECKING
					else
					{
						veejay_msg(0, "key is '%s' , error code %d", pkey,err );
						char **items = vevo_list_properties( slot->out_values );
						if(!items) veejay_msg(0,"\tThere are no items!");
						else
						{
							int l;
							for(l =0; items[l] != NULL;l++ )
							{
								veejay_msg(0,"have item '%s'", items[l]);
								free(items[l]);
							}
							free(items[l]);
						}
					}

#endif	
				}
				if(path) free(path);
				if(fmt)  free(fmt);
				if(output_pval) free(output_pval);

			}
			else 
			{
				veejay_msg(0, "Kind is not compatible: %d", bpt->kind );
			}
		}
		
		free( items[i] );
	}
	free(items);
	return 0;
}

void	*sample_clone_from( void *info, void *sample,  sample_video_info_t *project_settings  )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	int type = srd->type;
	char **list = vj_el_get_file_list( srd->data );
	
	void *new_sample = sample_new( type );

	int i = 0;

	if( sample_open( new_sample, list[i], 0, project_settings ) )
	{
		veejay_msg(0, "Opened '%s' ",list[i]);
		int new_id = samplebank_add_sample( new_sample );
		sample_set_user_data( new_sample, info, new_id );
	}

	for( i = 0; list[i] != NULL ; i ++ )
		free(list[i]);
	free(list);
	return new_sample;
}

