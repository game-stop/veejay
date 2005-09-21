#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <stdlib.h>
#include "curve.h"

key_chain_t	*new_chain(void)
{
	key_chain_t *chain = (key_chain_t*) vj_malloc(sizeof(key_chain_t));
	if(!chain)
		return NULL;
	chain->effects = (key_effect_t**) vj_malloc(sizeof(key_effect_t*) * MAX_CHAIN_LEN );
	memset(chain->effects, 0 , sizeof( key_effect_t*) * MAX_CHAIN_LEN );
	gint i;
	for( i = 0; i < MAX_CHAIN_LEN ;i ++ )
	{
		chain->effects[i] = new_chain_entry();
	}
	return chain;	
}

void		del_chain( key_chain_t *chain )
{
	if(chain)
	{
		gint i;
		for( i = 0; i < MAX_CHAIN_LEN ;  i++ )
		{
			if(chain->effects[i])
				del_chain_entry( chain->effects[i] );
		}
		free(chain->effects);
		free(chain);
	}
}

key_effect_t	*new_chain_entry(void)
{
	gint i;
	key_effect_t *ke = (key_effect_t*) vj_malloc(sizeof(key_effect_t));
	if(!ke) 
		return NULL;
	ke->parameters = (key_parameter_t**) vj_malloc(sizeof(key_parameter_t*) * MAX_PARAMETERS );
	memset( ke->parameters, 0, sizeof(key_parameter_t*) * MAX_PARAMETERS);
	ke->enabled = 1;
	for( i = 0; i < MAX_PARAMETERS; i ++ )
	{
		ke->parameters[i] = new_parameter_key();
		if(!ke->parameters[i])
			return NULL;
	}
	return ke;
}

key_parameter_t	*new_parameter_key()
{
	gint i = 0;
	key_parameter_t *key = vj_malloc(sizeof(key_parameter_t));
	if(!key)
		return NULL;
	key->parameter_id = 0;
	key->end_pos	  = 0;
	key->start_pos	  = 0;
	key->vector	  = NULL;
	key->running	  = 0;
	key->min	  = 0;
	key->max	  = 0;
	key->type = GTK_CURVE_TYPE_LINEAR;
	return key;
}
void	free_parameter_key( key_parameter_t *key )
{
	if(key)
	{
		if(key->vector) free(key->vector);
		free(key);
	}
	key = NULL;
}

void		del_chain_entry( key_effect_t *ke )
{	if(ke)
	{
		if(ke->parameters)
		{
			gint i;
			for( i = 0; i < MAX_PARAMETERS; i ++ )
			{
				if(ke->parameters[i])
					free_parameter_key( ke->parameters[i] );
			}
			free( ke->parameters );
		}		
		free(ke );
	}
}

void	clear_parameter_values( key_parameter_t *key )
{
	if(key)
	{
		if(key->vector)
			free(key->vector);
		key->vector = NULL;
		key->running = 0;
		key->parameter_id =0;
		key->min = 0;
		key->max = 0;	
		key->end_pos = 0;
		key->start_pos = 0;
		key->type = GTK_CURVE_TYPE_LINEAR;
	}
}

int	parameter_for_frame( key_parameter_t *key, gint frame_pos )
{
	if( frame_pos >= key->start_pos && frame_pos <= key->end_pos )
		return 1;
	return 0;
}

/* Get a value for a frame position */
int	get_parameter_key_value( key_parameter_t *key, gint frame_pos, float *result )
{
	if( frame_pos < key->start_pos || frame_pos > key->end_pos )
		return 0;
	if(!key->vector)
	{
		fprintf(stderr, "%s : no vector\n", __FUNCTION__ );
		exit(0);
	}
	*result = key->vector[ frame_pos ];	
	return 1;
}

void	get_points_from_curve( key_parameter_t *key, GtkWidget *curve )
{
	gtk_curve_get_vector( GTK_CURVE(curve), key->end_pos - key->start_pos, key->vector );
}

void	curve_timeline_changed( key_parameter_t *key, GtkWidget *curve)
{
	int len = key->end_pos - key->start_pos;
	if(key->vector)
		free(key->vector);
	key->vector = NULL;

	key->vector = (float*) vj_malloc(sizeof(float) * len );
}

void	reset_curve( key_parameter_t *key, GtkWidget *curve )
{
	gtk_curve_reset(GTK_CURVE(curve));
	gtk_curve_set_range( GTK_CURVE(curve), 0.0, 1.0, 0.0, 1.0 );
	gtk_curve_set_curve_type( GTK_CURVE(curve), key->type );
}

void	set_points_in_curve( key_parameter_t *key, GtkWidget *curve)
{
	gtk_curve_reset(GTK_CURVE(curve));
	gtk_curve_set_range( GTK_CURVE(curve), 0.0, 1.0, 0.0, 1.0 );
	gtk_curve_set_curve_type( GTK_CURVE(curve), key->type );

	if(key->vector)
		gtk_curve_set_vector( GTK_CURVE( curve ), key->end_pos - key->start_pos, key->vector );
}


