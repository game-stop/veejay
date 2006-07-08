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
 *     -# DV1394 Digital Camera
 *     -# V4L Video4Linux 
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
#include	<veejay/defs.h>
#include	<vevosample/defs.h>
#include	<vevosample/vevosample.h>
#include	<libplugger/plugload.h>
#include	<libvjnet/vj-client.h>
#include	<vevosample/v4lutils.h>
#ifdef HAVE_DV1394
#include	<vevosample/vj-dv1394.h>
#endif
#include	<vevosample/vj-v4lvideo.h>
#include	<vevosample/vj-vloopback.h>
#include	<vevosample/vj-yuv4mpeg.h>
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


//! \typedef sampleinfo_t Sample A/V Information structure
typedef struct
{
	uint64_t	start_pos;	//!< Starting position 
	uint64_t	end_pos;	//!< Ending position 
	int		looptype;	//!< Looptype
	int		speed;		//!< Playback speed
	uint64_t	in_point;	//!< In point (overrides start_pos)
	uint64_t	out_point;	//!< Out point (overrides end_pos)
	uint64_t	current_pos;	//!< Current position
	int		marker_lock;	//!< Keep in-out point length constant
	int		rel_pos;	//!< Relative position
	int		has_audio;	//!< Audio available
	int		type;		//!< Type of Sample
} sampleinfo_t;

//! \typedef sample_runtime_data Sample Runtime Data structure
typedef	struct
{
	void	*data;						/* private data, depends on stream type */
	void	*info_port;					/* collection of sample properties */
	int	width;						/* processing information */
	int	height;
	int	format;
	int	palette;
	int	type;						/* type of sample */
	sampleinfo_t *info;
} sample_runtime_data;

/* forward */
static	void	sample_fx_clean_up( void *port );
static	void	*sample_get_fx_port_values_ptr( int id, int fx_entry );
static	void	sample_expand_properties( void *sample, const char *key, xmlNodePtr root );
static	void	sample_expand_port( void *port, xmlNodePtr node );
static  void	sample_close( sample_runtime_data *srd );
static	void	sample_new_fx_chain(void *sample);
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

static	struct
{
	const char *name;
	int	atom_type;
} fx_entry_list_[] =
{
	{	"fx_status",	VEVO_ATOM_TYPE_INT	},	/* fx status */
	{	"fx_alpha",	VEVO_ATOM_TYPE_INT	},	/* alpha */
	{	"fx_instance",	VEVO_ATOM_TYPE_VOIDPTR	},	/* plugin instance point */
	{	"fx_values",	VEVO_ATOM_TYPE_PORTPTR	},	/* port of p0 .. pN, containing copy of fx parameter values */
	{	"fx_channels",	VEVO_ATOM_TYPE_PORTPTR	},	/* contains list of sample pointers to use as input channels */
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
	{	"fx_status",	VEVO_ATOM_TYPE_INT	},	/* FX chain enabled/disabled */
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
	{	"width",	VEVO_ATOM_TYPE_STRING	},
	{	"height",	VEVO_ATOM_TYPE_INT	},
	{	"palette",	VEVO_ATOM_TYPE_INT	},
	{	"active",	VEVO_ATOM_TYPE_INT	},
	{	"format",	VEVO_ATOM_TYPE_INT	},
	{	"data",		VEVO_ATOM_TYPE_VOIDPTR	},
	{	NULL,		0			}
};

static	struct
{
	const char *name;
	int atom_type;
} stream_v4l_list[] = {
	{	"device",	VEVO_ATOM_TYPE_STRING	},
	{	"channel",	VEVO_ATOM_TYPE_INT	},
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
} stream_dv1394_list[] = {
	{	"device",	VEVO_ATOM_TYPE_STRING	},
	{	"channel",	VEVO_ATOM_TYPE_INT	},
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
	char key[20];
	void *info = NULL;
	int error;
#ifdef STRICT_CHECKING
	assert( id > 0 && id < SAMPLE_LIMIT );
#endif
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

int		sample_get_key_ptr( void *info )
{
	int pk = 0;
	sample_runtime_data *srd = (sample_runtime_data*) info;
	int error = vevo_property_get( srd->info_port, "primary_key", 0, &pk );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif	
	return pk;
}

void	sample_delete_ptr( void *info )
{
	sample_runtime_data *srd = (sample_runtime_data*) info;
	//@ FIXME: get default channel configuratioe_data *srd = (sample_runtime_data*) info;
#ifdef STRICT_CHECKING
	if( info == NULL )
		trap_vevo_sample();
	assert( info != NULL );
	assert( srd->info_port != NULL );
#endif
	int pk = 0;
	int error = vevo_property_get( srd->info_port, "primary_key", 0, &pk );

	sample_fx_clean_up( srd->info_port );
	
	sample_close( srd );
	
	vevo_port_recursive_free( srd->info_port );
	
	free_slot( pk );
	free(srd->info);
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

	sample_fx_clean_up( srd->info_port );

	sample_new_fx_chain ( srd->info_port );
}

void	sample_fx_set_parameter( int id, int fx_entry, int param_id,int n_elem, void *value )
{
	void *port = sample_get_fx_port( id, fx_entry );
	int fx_id = 0;
	void *fx_instance = NULL;
	int error = vevo_property_get( port, "fx_instance", 0, &fx_instance );
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	plug_set_parameter( fx_instance, param_id, n_elem,value );	
}

void	sample_fx_get_parameter( int id, int fx_entry, int param_id, int idx, void *dst)
{
	void *fx_values = sample_get_fx_port_values_ptr( id, fx_entry );
	char pkey[KEY_LEN];
	sprintf(pkey, "p%02d", param_id );	
	vevo_property_get( fx_values, pkey, idx, dst );	
}

int	sample_fx_set( void *info, int fx_entry, const int new_fx )
{
 	void *port = sample_get_fx_port_ptr( info,fx_entry );
	void *fx_values = NULL;
	void *fx_channels = NULL;
	//int cur_fx_id  = 0;
	int error;
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif
	void *fxi = NULL;	
	error = vevo_property_get( port, "fx_instance", 0, &fxi );
	if( error == VEVO_NO_ERROR )
	{
		veejay_msg(0, "FX entry %d is used. Please select another",
				fx_entry);
		return 0;
	}

	error = vevo_property_set( port, "fx_id", VEVO_ATOM_TYPE_INT,1, &new_fx );	
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	void *fx_instance = plug_activate( new_fx );
	if(!fx_instance)
	{
		veejay_msg(0, "Unable to initialize plugin %d", new_fx );
		return 0;
	}
	error = vevo_property_set( port, "fx_instance", VEVO_ATOM_TYPE_PORTPTR,
			1, &fx_instance );
#ifdef STRICT_CHECKING
	if(error != VEVO_NO_ERROR)
		veejay_msg(0, "Error code %d",error);
	assert( error == VEVO_NO_ERROR );
#endif

	error = vevo_property_get( port, "fx_values", 0, &fx_values );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif	
	error = vevo_property_get( port, "fx_channels", 0, &fx_channels );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	int n_channels = vevo_property_num_elements( fx_instance, "in_channels" );

	error = vevo_property_set( fx_channels, "n_in_channels", VEVO_ATOM_TYPE_INT,1, &n_channels );

	int o_channels = vevo_property_num_elements( fx_instance, "out_channels" );

	error = vevo_property_set( fx_channels, "n_out_channels",VEVO_ATOM_TYPE_INT,1, &o_channels );
	
	plug_get_defaults( fx_instance, fx_values );

	plug_set_defaults( fx_instance, fx_values );

	int i;
	for(i=0; i < n_channels; i ++ )
		sample_fx_set_in_channel(info,fx_entry,i, sample_get_key_ptr(info));

	
	veejay_msg(0, "Entry %d Plug %d has %d Inputs, %d Outputs",fx_entry, new_fx,n_channels,o_channels );
	
//@ FIXME: get default channel configuration	
//	plug_get_channels( fx_instance, fx_channels );	
	return 1;
}

int	sample_fx_set_in_channel( void *info, int fx_entry, int seq_num, const int sample_id )
{
	void *port = sample_get_fx_port_ptr( info, fx_entry );
	void *fx_channels = NULL;
	void *fx_instance = NULL;
	char key[64];
	int error = vevo_property_get( port, "fx_channels",0, &fx_channels );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	int max_in = 0;
	error = vevo_property_get( fx_channels, "n_in_channels", 0, &max_in );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
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
	error = vevo_property_set( fx_channels, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &sample );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return 1;
}

int	sample_fx_push_in_channel( void *info, int fx_entry, int seq_num, void *frame_info )
{
	void *port = sample_get_fx_port_ptr( info,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif
	void *fx_instance = NULL;

	int error = vevo_property_get( port, "fx_instance", 0, &fx_instance );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	plug_push_frame( fx_instance, 0,seq_num, frame_info );
	return 1;
}

int	sample_fx_push_out_channel( void *info, int fx_entry, int seq_num, void *frame_info )
{
	void *port = sample_get_fx_port_ptr( info,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	void *fx_instance = NULL;

	int error = vevo_property_get( port, "fx_instance", 0, &fx_instance );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	plug_push_frame( fx_instance, 1,seq_num, frame_info );

	return 1;
}
int	sample_process_entry( void *data, int fx_entry )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	int fx_status;
	int error = vevo_property_get( port, "fx_status",0,&fx_status);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	return fx_status;
}
int	sample_get_fx_alpha( void *data, int fx_entry )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	int fx_alpha=0;
	int error = vevo_property_get( port, "fx_alpha",0,&fx_alpha);

	if( error != VEVO_NO_ERROR )
		return 256;

	return fx_alpha;
}
void	sample_set_fx_alpha( void *data, int fx_entry, int v )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	int fx_alpha = v;
	int error = vevo_property_set( port, "fx_alpha",VEVO_ATOM_TYPE_INT,1,&fx_alpha);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
}


void	sample_toggle_process_entry( void *data, int fx_entry, int v )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	int fx_status = v;
	int error = 0;
	void *fxi = NULL;
	error = vevo_property_get( port, "fx_instance",
			0, &fxi );
	if( error != VEVO_NO_ERROR )
	{
		veejay_msg(0,"Nothing to enable on entry %d",fx_entry);
		return;
	}

	error = vevo_property_set( port, "fx_status",VEVO_ATOM_TYPE_INT,1,&fx_status);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
}
void	*sample_scan_in_channels( void *data, int fx_entry )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	int n = sample_fx_num_in_channels( port );
	if( n <= 0 )
		return NULL;
//FIXME: fx_channels contains slot[d] with value sample ptr,
////     quque list expects sample[d] with value sample ptr	
	void *fx_channels = NULL;
	int error = vevo_property_get( port, "fx_channels",0,  &fx_channels);
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	return fx_channels;
}
/*void	*sample_scan_in_channels( void *data, int fx_entry )
{
#ifdef STRICT_CHECKING
	assert( data != NULL);
#endif
	sample_runtime_data *info = (sample_runtime_data*) data;
#ifdef STRICT_CHECKING
	assert( info != NULL );
	assert( info->info_port != NULL );
#endif
	void *port = sample_get_fx_port_ptr( info,fx_entry );

	int n = sample_fx_num_in_channels( port );

	if( n <= 0 )
		return NULL;
	
	void *fx_channels = NULL;
	int error = vevo_property_get( port, "fx_channels",0,  &fx_channels);
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	veejay_msg(0, "Entry %d has port fx_channels: %p", fx_entry,fx_channels );
	char **list = vevo_list_properties( fx_channels );
	int k = 0;
	for ( k = 0; list[k] != NULL ; k ++ )
	{
		veejay_msg(0, "\t'%s'", list[k]);
		free(list[k]);
	}
	free(list);

	
	return fx_channels;
}*/

int	sample_scan_out_channels( void *data, int fx_entry )
{
	void *port = sample_get_fx_port_ptr( data,fx_entry );
	if(!port)
		return NULL;
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif	
	return sample_fx_num_out_channels( port );
}

// frame buffer passed by performer
int	sample_process_fx( void *sample, int fx_entry )
{
	unsigned int i,k;
	void *fx_instance = NULL;
	void *fx_values = NULL;

	void *port = sample_get_fx_port_ptr( sample,fx_entry );
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif		
	int error = vevo_property_get( port, "fx_instance", 0, &fx_instance );
#ifdef STRICT_CHECKING
	assert( error == 0 );
#endif
	plug_process( fx_instance );

	error = vevo_property_get( port, "fx_values", 0, &fx_values );
#ifdef STRICT_CHECKING
	assert( error == 0 );			
#endif
	//update internal parameter values
	plug_clone_from_parameters( fx_instance, fx_values );

	return VEVO_NO_ERROR;
}

int	sample_fx_num_in_channels( void *port )
{
	void *fx_channels = NULL;
	int error = vevo_property_get( port, "fx_channels",0, &fx_channels );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int n_channels = 0;
	error = vevo_property_get( fx_channels, "n_in_channels", 0, &n_channels );
	if( error != VEVO_NO_ERROR )
		n_channels = 0;

	return n_channels;
}
int	sample_fx_num_out_channels( void *port )
{
	void *fx_channels = NULL;
	int error = vevo_property_get( port, "fx_channels",0, &fx_channels );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	int n_channels = 0;
	vevo_property_get( fx_channels, "n_out_channels", 0, &n_channels );
	return n_channels;
}

int	sample_fx_set_by_name( int id, int fx_entry, const char *name )
{
	return 0;
}

void	*sample_get_fx_port_ptr( void *data, int fx_entry )
{
	sample_runtime_data *info = (sample_runtime_data*) data;
	void *port = NULL;
	char fx[KEY_LEN];
	sprintf(fx, "fx_%x", fx_entry );
#ifdef STRICT_CHECKING
	assert( info->info_port != NULL );
#endif
	int error = vevo_property_get( info->info_port, fx, 0, &port );

#ifdef STRICT_CHECKING
	assert( error == 0 );
	assert( port != NULL );
#endif
	return port;
}

int	sample_get_fx_id( void *data , int fx_entry )
{
	void *port = sample_get_fx_port_ptr( data, fx_entry );
	int fx_id = -1;
	int error = vevo_property_get( port, "fx_id",0, &fx_id );	
	return fx_id;
}

static	void	*sample_get_fx_port_values_ptr( int id, int fx_entry )
{
	void *port = sample_get_fx_port( id, fx_entry );
	void *fx_values = NULL;
	int error = vevo_property_get( port, "fx_values", 0, &fx_values );
	return fx_values;	
}

void	*sample_get_fx_port_channels_ptr( int id, int fx_entry )
{
	void *port = sample_get_fx_port( id, fx_entry );
	void *fx_values = NULL;
	int error = vevo_property_get( port, "fx_channels", 0, &fx_values );
	return fx_values;	
}	

void	*sample_get_fx_port( int id, int fx_entry )
{
	return sample_get_fx_port_ptr( find_sample(id),fx_entry );
}
void	 sample_set_property_ptr( void *ptr, const char *key, int atom_type, void *value )
{
	sample_runtime_data *info = (sample_runtime_data*) ptr;
	if(info)
		vevo_property_set( info->info_port, key, atom_type,1, value );	
}

void	 sample_set_property( int id, const char *key, int atom_type, void *value )
{
	sample_runtime_data *info = (sample_runtime_data*) find_sample( id );
	if(info)
		vevo_property_set( info->info_port, key, atom_type,1, value );	
}

void	 sample_get_property( int id, const char *key, void *dst )
{
	sample_runtime_data *info = (sample_runtime_data*) find_sample( id );
	if(info)
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
		case	VJ_TAG_TYPE_V4L:
			for( i = 0 ; stream_v4l_list[i].name != NULL; i ++ )
				vevo_property_set( info, stream_v4l_list[i].name, stream_v4l_list[i].atom_type, 0, NULL );
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
#ifdef HAVE_DV1394
		case	VJ_TAG_TYPE_DV1394:
			for( i = 0; stream_dv1394_list[i].name != NULL; i ++ )
				vevo_property_set( info, stream_dv1394_list[i].name, stream_dv1394_list[i].atom_type,0,NULL);
			break;
#endif
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
		case	VJ_TAG_TYPE_V4L:
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
#ifdef HAVE_DV1394
		case	VJ_TAG_TYPE_DV1394:
			break;
#endif
		default:
			break;	
	}

	
	return NULL;
}

static	void	sample_fx_entry_clean_up( void *sample, int id )
{
	char entry_key[KEY_LEN];
	void *entry_port = NULL;

	sprintf(entry_key, "fx_%x", id );
	int error = vevo_property_get( sample, entry_key,0, &entry_port );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	void *instance = NULL;
	error = vevo_property_get( entry_port, "fx_instance",0,&instance );
	if(error == VEVO_NO_ERROR )
		plug_deactivate( instance );

	void *fxv = NULL;
	error = vevo_property_get( entry_port, "fx_values",0,&fxv );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	void *fxc = NULL;
	error = vevo_property_get(entry_port, "fx_channels",0,&fxc );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	vevo_port_free( fxv );
	vevo_port_free( fxc );
	vevo_port_free( entry_port );

	
	error = vevo_property_set( sample, entry_key, VEVO_ATOM_TYPE_PORTPTR, 0, NULL );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

}

static	void	sample_fx_clean_up( void *sample )
{
	int i;
	int error;
	for( i = 0 ; i < SAMPLE_CHAIN_LEN; i ++ )
	{
		sample_fx_entry_clean_up( sample,i );
	}
}

static	void	sample_new_fx_chain_entry( void *sample, int id )
{
	void *port = vevo_port_new( VEVO_FX_ENTRY_PORT );
	char entry_key[KEY_LEN];

	sprintf(entry_key, "fx_%x", id );
	int dstatus = 0;	
	int error = vevo_property_set( port, "fx_status", VEVO_ATOM_TYPE_INT,1,&dstatus );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	void *fx_values = vevo_port_new( VEVO_FX_VALUES_PORT );
	void *fx_channels = vevo_port_new( VEVO_ANONYMOUS_PORT );

	error = vevo_property_set( port, "fx_values", VEVO_ATOM_TYPE_PORTPTR,1,&fx_values);
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error = vevo_property_set( port, "fx_channels",VEVO_ATOM_TYPE_PORTPTR,1,&fx_channels );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif

	error = vevo_property_set( port, "fx_instance", VEVO_ATOM_TYPE_PORTPTR,0,NULL );
#ifdef STRICT_CHECKING
	assert( error == VEVO_NO_ERROR );
#endif
	error =	vevo_property_set( sample, entry_key, VEVO_ATOM_TYPE_PORTPTR,1, &port );
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
		sample_fx_entry_clean_up( sample->info_port, id );
		sample_new_fx_chain_entry( sample->info_port, id );
		return 1;
	}
	return 0;
}

void	*sample_new( int type )
{
	int i;
	sample_runtime_data *rtdata = (sample_runtime_data*) malloc(sizeof( sample_runtime_data ) );
	memset( rtdata,0,sizeof(sample_runtime_data));
	sampleinfo_t *sit = (sampleinfo_t*) vj_malloc(sizeof(sampleinfo_t));
	memset( sit,0,sizeof(sampleinfo_t));
	rtdata->info = sit;
	rtdata->type	  = type;
	rtdata->info_port = (void*) vevo_port_new( VEVO_SAMPLE_PORT );

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

	
	sample_new_fx_chain( rtdata->info_port );

	return res;
}

void	samplebank_init()
{
	sample_bank_ = (void*) vevo_port_new( VEVO_SAMPLE_BANK_PORT );
#ifdef STRICT_CHECKING
	veejay_msg(2,"VEVO Sampler initialized. (max=%d)", SAMPLE_LIMIT );
#endif
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
	veejay_msg(2, "Shutting down VEVO Sampler");
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
	vevo_property_set( port, "primary_key", VEVO_ATOM_TYPE_INT, 1, &pk );
	
	sprintf(pri_key, "sample%04x", pk );	
	vevo_property_set( sample_bank_, pri_key, VEVO_ATOM_TYPE_VOIDPTR, 1, &sample );
	num_samples_ += 1;
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

//@ what pixelformat are we supposed to play,
/// what pixel format is file openened in ?
int	sample_open( void *sample, const char *token, int extra_token , sample_video_info_t *project_settings )
{
//	void *info = find_sample(id);
	int	res = 0;
	int 	my_palette = 0;
	if(!sample)
		return 0;

	sample_runtime_data *srd = (sample_runtime_data*) sample;

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
			sampleinfo_t *sit = srd->info;
			sit->end_pos = vj_el_get_num_frames(srd->data);
			sit->looptype = 1;
			sit->speed = 1;
			sample_set_property_ptr( sample, "end_pos", VEVO_ATOM_TYPE_INT, &(sit->end_pos));
			sample_set_property_ptr( sample, "looptype",VEVO_ATOM_TYPE_INT, &(sit->looptype));
			sample_set_property_ptr( sample, "speed", VEVO_ATOM_TYPE_INT,&(sit->speed));
			break;

		case VJ_TAG_TYPE_V4L:
			srd->data = (void*) vj_v4lvideo_alloc();
			res = vj_v4lvideo_init(
				(v4l_video*) srd->data,
				token,
				extra_token,
				project_settings->norm, // NOTE: THIS IS PROBABLY CHAR
				0,
				project_settings->w,
				project_settings->h,
				project_settings->fmt ); // NOTE THIS SHOULD BE PALETTE, convert to V4L palette in v4l componenet!
			//	my_palette );
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
#ifdef HAVE_DV1394
		case VJ_TAG_TYPE_DV1394:
	//		srd->data = (void*) vj_dv1394_init( (void*)el, extra_token,1 ); // DV1394 componenet like YUV and V4L comp.
			break;
#endif
#ifdef USE_GDK_PIXBUF
		case VJ_TAG_TYPE_PICTURE:
			if( vj_picture_probe ( token )  )
				srd->data = vj_picture_open( token,project_settings->w,project_settings->h,0 );
//@ fixme: project!
			break;
#endif
		case VJ_TAG_TYPE_COLOR:
			break;
	}
	return res;
}

int	sample_append_file( const char *filename, long n1, long n2, long n3 )
{
	/*end = el->video_frames;

 *
 *	// create initial edit list for sample (is currently playing)
	if(!sample_edl) 
		sample_edl = vj_el_init_with_args( files,1,info->preserve_pathnames,info->auto_deinterlace,0,
				info->edit_list->video_norm , info->pixel_format);
	// i
 	el->frame_list = (uint64_t *) realloc(el->frame_list, (end + el->num_frames[n])*sizeof(uint64_t));
	if (el->frame_list==NULL)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Insufficient memory to allocate frame_list");
		vj_el_free(el);
		return 0;
	}

	for (i = 0; i < el->num_frames[n]; i++)
	{
		el->frame_list[c] = EL_ENTRY(n, i);
		c++;
	}
 
	el->video_frames = c;
//@ set min and max frame num anew
//@ update start/end positions
	
	*/
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

	uint64_t cf = sit->current_pos + sit->speed;

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

int	sample_get_frame( void *current_sample , VJFrame *slot )
{
	sample_runtime_data *srd = (sample_runtime_data*) current_sample;
#ifdef STRICT_CHECKING
	assert( srd->info );
#endif
	long frame_num = srd->info->current_pos;
	void *ptr = NULL;


	//srd->type !!
	//@ assign function pointer at sample_open to kill this switch statement
	switch(srd->type)
	{
		case VJ_TAG_TYPE_NONE:
#ifdef STRICT_CHECKING
			assert( srd->data != NULL );
#endif
			vj_el_get_video_frame ( srd->data, frame_num,slot);
			break;
		case VJ_TAG_TYPE_V4L:
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
		case VJ_TAG_TYPE_V4L:
			vj_v4l_video_grab_stop( srd->data );
			break;
		case VJ_TAG_TYPE_YUV4MPEG:
			vj_yuv_stream_stop_read( srd->data );
			break;
#ifdef HAVE_DV1394
		case VJ_TAG_TYPE_DV1394:
			vj_dv1394_close( srd->data );
			break;
#endif
#ifdef USE_GDK_PIXBUF
		case VJ_TAG_TYPE_PICTURE:
			//vj_picture_cleanup( 
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
        ret = (char*) malloc(sizeof(char) * len );
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

printf("%s: %p, %s type = %d\n", __FUNCTION__, port, key, atom_type );
	
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
		if( sample_type == VJ_TAG_TYPE_V4L )
		{
			for( i = 0; stream_v4l_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_v4l_list[i].name ) == 0 )
					return stream_v4l_list[i].atom_type;
			}
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
#ifdef HAVE_DV1394
		if( sample_type == VJ_TAG_TYPE_DV1394 )
		{
			for( i = 0; stream_dv1394_list[i].name != NULL ; i ++ )
			{
				if( strcasecmp( (const char*) name, stream_dv1394_list[i].name ) == 0 )
					return stream_dv1394_list[i].atom_type;
			}
		}
#endif
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
	void *port = sample_get_fx_port_ptr( srd, fx_entry );
	if(port)
	{
		void *fx_instance = NULL;
		int error = vevo_property_get( port, "fx_instance",0,&fx_instance );
		void *fx_values = NULL;
		
veejay_msg(0, "Entry %d, Param %d, '%s'",fx_entry,p,str);
		
		if(error == VEVO_NO_ERROR)
		{
			void *fx_values = NULL;
			error = vevo_property_get( port, "fx_values",0,&fx_values );
			return plug_set_param_from_str( fx_instance, p, str,fx_values );
		}
		else
			veejay_msg(0 ,"No fx instance on entry %d", fx_entry );
	}
	else
		veejay_msg(0, "Entry %d is invalid",fx_entry);
	return 0;
}

void	sample_process_fx_chain( void *srd )
{
	int k = 0;
	
	for( k = 0; k < SAMPLE_CHAIN_LEN; k ++ )
	{
		void *port = sample_get_fx_port_ptr( srd, k );
		void *fx_instance = NULL;
		int fx_status = 0;

		int error = vevo_property_get( port, "fx_status", 0, &fx_status);		
#ifdef STRICT_CHECKING
		assert( error == 0 );
#endif

//@TODO: Alpha fading

		if( fx_status )
		{
			error = vevo_property_get( port, "fx_instance", 0, &fx_instance );
#ifdef STRICT_CHECKING
			assert( error == 0 );
#endif
			plug_process( fx_instance );

			void *fx_values = NULL;
			error = vevo_property_get( port, "fx_values", 0, &fx_values );
#ifdef STRICT_CHECKING
			assert( error == 0 );			
#endif
			plug_clone_from_parameters( fx_instance, fx_values );
		}
	}
}

static void		edl_copy( void *sample, uint64_t start, uint64_t end )
{

}


static void	sample_get_position_info(void *port , uint64_t *start, uint64_t *end, int *loop, int *speed)
{
	vevo_property_get( port, "start_pos", 0,start );
	vevo_property_get( port, "end_pos", 0, end );
	vevo_property_get( port, "looptype",0, loop );
	vevo_property_get( port, "speed",0,speed );
}
static	void	sample_get_position_loop(void *port, int start, int end)
{
	double fps = 0.0;
	vevo_property_get( port, "fps",0,&fps );
	
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

//	veejay_msg(0, "start %d - end %d - speed %d - loop %d - position %d -  in %d - out %d",
//			sit->start_pos,sit->end_pos,sit->speed,sit->looptype,sit->current_pos,
//			sit->in_point, sit->out_point );
}

void		sample_save_cache_data( void *info)
{
#ifdef STRICT_CHECKING
	assert( info != NULL );
#endif

	sample_runtime_data *srd = (sample_runtime_data*) info;
	sampleinfo_t *sit = srd->info;
//	veejay_msg(0, "SAVE: start %d - end %d - speed %d - loop -  %d position %d -in - %d -out %d",
///			sit->start_pos,sit->end_pos,sit->speed,sit->looptype,sit->current_pos,
//			sit->in_point, sit->out_point );
	vevo_property_set( srd->info_port, "start_pos",VEVO_ATOM_TYPE_UINT64,1,&(sit->start_pos ));
	vevo_property_set( srd->info_port, "end_pos", VEVO_ATOM_TYPE_UINT64,1,&(sit->end_pos));
	vevo_property_set( srd->info_port, "speed", VEVO_ATOM_TYPE_INT,1,&(sit->speed ));
	vevo_property_set( srd->info_port, "looptype", VEVO_ATOM_TYPE_INT,1,&(sit->looptype ));
	vevo_property_set( srd->info_port, "current_pos", VEVO_ATOM_TYPE_UINT64,1,&(sit->current_pos));
	vevo_property_set( srd->info_port, "in_point",VEVO_ATOM_TYPE_UINT64,1,&(sit->in_point));
	vevo_property_set( srd->info_port, "out_point",VEVO_ATOM_TYPE_UINT64,1,&(sit->out_point));


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


int	sample_sscanf_port( void *sample, const char *s )
{
	sample_runtime_data *srd = (sample_runtime_data*) sample;
	return vevo_sscanf_port( srd->info_port, s );
}	
	


char 	*sample_sprintf_port( void *sample )
{
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
