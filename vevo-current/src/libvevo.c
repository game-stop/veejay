/*
Copyright (c) 2004-2005 N.Elburg <nelburg@looze.net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string.h>
#include <stdlib.h>
#include <include/libvevo.h>

/***********************
  PRIVATE FUNCTIONS
*************************/

#define	__IS_CHANNEL_PROPERTY(a)  ( (a >= VEVO_PROPERTY_CHANNEL && a < VEVO_PROPERTY_INSTANCE)? 1: 0) 
#define __IS_INSTANCE_PROPERTY(a)  ( (a >= VEVO_PROPERTY_INSTANCE ) ? 1: 0)
#define __IS_PARAM_PROPERTY(a)  ( (a < VEVO_PROPERTY_CHANNEL ) ? 1: 0)


static void		parse_gt( vevo_atom_type_t i, atom_t *t, void *dst );

static void		parse_pd( atom_t *t , double *dst )
{
		*dst = *( (double*) t->value);
}

static void		parse_pv( atom_t *t, void *dst )
{
	memcpy( dst, t->value, sizeof(void*) );
}

static void		parse_pu8(atom_t *t, uint8_t *dst)
{
	memcpy( dst, t->value, sizeof(uint8_t*));	
}
static void		parse_pu16(atom_t *t, uint16_t *dst)
{
	memcpy( dst, t->value, sizeof(uint16_t*));
}

static void		parse_pu32(atom_t *t, uint32_t *dst)
{
	memcpy( dst, t->value, sizeof(uint32_t*));
}

static void		parse_ps8(atom_t *t, int8_t *dst)
{
	memcpy( dst, t->value, sizeof(int8_t*));
}
static void 		parse_ps16(atom_t *t, int16_t *dst)
{
	memcpy( dst, t->value, sizeof(int16_t*));
}
static void		parse_ps32(atom_t *t, int32_t *dst)
{
	memcpy( dst, t->value, sizeof(int32_t*));
}

// convert an atom value to double
static void		parse_d ( atom_t *t, double *dst )
{
		*dst = *( (double*) t->value);
}

// convert an atom value to int 
static void		parse_i ( atom_t *t, int *dst )
{
		*dst = *( (int*) t->value);
}

// convert an atom value into a string ( char array )
static	void	parse_s	( atom_t *t, unsigned char *dst )
{
	unsigned char *s = *( (unsigned char**) t->value );
	memcpy( dst,s, t->size);
	dst[t->size] = '\0';	// null terminate the string!
}

// convert an atom to a boolean
static	void	parse_b( atom_t *t, vevo_boolean_t *dst)
{
	*dst = *( (vevo_boolean_t*) t->value );
}

// generic parsing function
static	void	parse_gt ( vevo_atom_type_t i, atom_t *t, void *dst )
{
	if( i == VEVO_INT )
		parse_i( t, dst );
	if( i == VEVO_STRING )
		parse_s( t, dst );
	if( i == VEVO_DOUBLE )
		parse_d( t, dst );
	if( i == VEVO_BOOLEAN )
		parse_b( t, dst );
	if( i == VEVO_PTR_DBL)
		parse_pd( t, dst );
	if( i == VEVO_PTR_VOID)
		parse_pv( t, dst );
	if( i == VEVO_PTR_U8 )
		parse_pu8( t, dst );
	if( i == VEVO_PTR_U16 )
		parse_pu16( t,dst );
	if( i == VEVO_PTR_U32 )
		parse_pu32( t, dst );
	if( i == VEVO_PTR_S8 )
		parse_ps8( t, dst );
	if( i == VEVO_PTR_S16 )
		parse_ps16( t, dst );
	if( i == VEVO_PTR_S32 )
		parse_ps32( t, dst );
}

static void	vevo_cast_int_to_double_arr( atom_t **val, vevo_atom_type_t type, double *dst, size_t len )
{
	int cur;
	int i;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur );
		dst[i] = (double) cur;
	}
}

static  void vevo_scale_int_to_double_arr( atom_t **val, vevo_atom_type_t type, double *dst, size_t len, double m)
{
	int cur;
	int i;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur);
		dst[i] = (  (double)cur) / m;
	}
}

static void	vevo_cast_double_to_int_arr( atom_t **val, vevo_atom_type_t type, int *dst, size_t len )
{
	double cur;
	int i;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur);
		dst[i] = (int) cur;
	}
}

static	void	vevo_scale_double_to_int_arr( atom_t **val, vevo_atom_type_t type, int *dst, size_t len, double m)
{
	double cur;  
	int i;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur);
		dst[i] = (int) ( cur * m );
	}
} 

static void		vevo_scale_int_to_string_arr( atom_t **val, vevo_atom_type_t type, unsigned char *dst, size_t len, double m)
{
	double cur;
	int i,value;
	unsigned char tmp[3];
	dst[0] = '#';
	dst++;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur );
		value = (int) ( cur );
		if( value < 0 ) value = 0;
		if( value > 255 ) value = 255;
		snprintf(tmp,3, "%x", value );
		strncpy( dst, tmp, 2 );
		dst+=2;
	}
}

static void		vevo_scale_double_to_string_arr( atom_t **val, vevo_atom_type_t type, unsigned char *dst, size_t len, double m )
{
	double cur;
	int i,value;
	unsigned char tmp[3];
	dst[0] = '#';
	dst++;
	for( i = 0; i < len; i ++ )
	{
		parse_gt( type, val[i], &cur );
		value = (int) ( cur * m );
		if( value < 0 ) value = 0;
		if( value > 255 ) value = 255;
		snprintf(tmp,3, "%x", value );
		strncpy( dst, tmp, 2 );
		dst+=2;
	}
}

static	void	vevo_scale_string_to_int ( atom_t *val, vevo_atom_type_t type, int *dst )
{
	unsigned char str[val->size];
	unsigned char tmp[5];
	int i;
	parse_gt( type, val, &str );
	tmp[0] = '0';
	tmp[1] = 'x';
	tmp[4] = '\0';
	for ( i = 0; i < 4; i ++ )
	{
		tmp[2] = str[i*2+1];
		tmp[3] = str[i*2+2];
		dst[i] = (int) strtod( tmp, NULL );
	}
}

static	void	vevo_scale_string_to_double( atom_t *val, vevo_atom_type_t type, double *dst, double m )
{
	unsigned char tmp[5];
	unsigned char str[val->size];
	int i;
	parse_gt( type, val, &str );
	tmp[0] = '0';
	tmp[1] = 'x';
	tmp[4] = '\0';
	for ( i = 0; i < 4; i ++ )
	{
		tmp[2] = str[i*2+1];
		tmp[3] = str[i*2+2];
		dst[i] = ( (double) ((int) strtod(tmp,NULL)) / m );
	}
}

static void	vevo_cast_int_to_double( atom_t *val , vevo_atom_type_t type, double *dst )
{
	int cur;
	parse_gt( type, val, &cur );
	*dst = (double) cur;
}

static	void	vevo_scale_int_to_double( atom_t *val, vevo_atom_type_t type, double *dst , double scale )
{
	int cur;
	parse_gt( type, val, &cur );
	*dst = (double) cur / scale;
}

static void	vevo_cast_double_to_int( atom_t *val, vevo_atom_type_t type, int *dst )
{
	double cur;
	parse_gt( type, val, &cur );
	*dst = (int) cur;
}

static	void vevo_scale_double_to_int( atom_t *val , vevo_atom_type_t type, int *dst, double scale )
{
	double cur;
	parse_gt( type, val, &cur );
	*dst = (int) ( cur * scale );
}

static int	vevo_get_storage_type( vevo_datatype *t )
{
	return t->type;
}

// parse an atom or an array of atoms and put the result to *dst
static void	parse_g( vevo_datatype *t, void *dst )
{
	if( t->type == VEVOP_ATOM )
	{
		parse_gt( t->ident, t->atom, dst );
	}
	if( t->type == VEVOP_ARRAY )
	{
		int i;
		if( t->ident == VEVO_INT )
		{
			int *arr = dst;
			for( i = 0; i < t->length; i ++)
				parse_gt( t->ident, t->array[i], &arr[i]);
		}
		if( t->ident == VEVO_DOUBLE)
		{
			double *arr = dst;
			for( i = 0; i < t->length; i ++)
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_STRING )
		{
			char **arr = dst;
			for( i = 0; i < t->length; i ++)
			{
				parse_gt(t->ident, t->array[i], arr[i] );
			}
		}
		if( t->ident == VEVO_BOOLEAN )
		{
			vevo_boolean_t *arr = dst;
			for( i = 0; i < t->length; i ++ )
				parse_gt(t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_PTR_DBL )
		{
			double **arr = (double**)dst;
			for( i = 0; i < t->length; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_PTR_U8 )
		{
			uint8_t **arr = (uint8_t**)dst;
			for( i = 0; i < t->length; i++ )
				parse_gt( t->ident, t->array[i], &arr[i] ); 
		}
		if( t->ident == VEVO_PTR_U16 )
		{
			uint16_t **arr = (uint16_t**)dst;
			for( i = 0; i < t->length ; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_PTR_U32 )
		{
			uint32_t **arr = (uint32_t**)dst;
			for( i = 0; i < t->length; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_PTR_S8 )
		{
			int8_t **arr = (int8_t**) dst;
			for( i = 0; i < t->length; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i]);
		}
		if( t->ident == VEVO_PTR_S16 )
		{
			int16_t **arr = (int16_t**) dst;	
			for( i = 0; i < t->length; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
		if( t->ident == VEVO_PTR_S32 )
		{
			int32_t **arr = (int32_t**) dst;
			for( i = 0; i < t->length; i ++ )
				parse_gt( t->ident, t->array[i], &arr[i] );
		}
	}
}

static	atom_t *vevo_alloca_atom(size_t len)
{
	atom_t *atom = (atom_t*) malloc(sizeof(atom_t));
	atom->value = (void*) malloc( len );
	atom->size = len ;
	return atom;
}

static	void	vevo_free_atom(atom_t *atom)
{
	if(atom)
	{
		free(atom->value);
		free(atom);
	}
	atom = NULL;
}

static atom_t *vevo_put_atom( void *dst, vevo_atom_type_t ident )
{
	atom_t *atom; 
	size_t atom_size = sizeof(int);
	if( ident == VEVO_DOUBLE ) atom_size = sizeof(double);
	if( ident == VEVO_BOOLEAN ) atom_size = sizeof(vevo_boolean_t);

	if( ident == VEVO_PTR_VOID ) atom_size = sizeof(void*);
	if( ident == VEVO_PTR_DBL) atom_size = sizeof(double*);
	if( ident == VEVO_PTR_U8) atom_size = sizeof(uint8_t*);
	if( ident == VEVO_PTR_U16) atom_size = sizeof(uint16_t*);
	if( ident == VEVO_PTR_U32) atom_size = sizeof(uint32_t*);
	if( ident == VEVO_PTR_S8) atom_size = sizeof(int8_t*);
	if( ident == VEVO_PTR_S16) atom_size = sizeof(int16_t*);
	if( ident == VEVO_PTR_S32) atom_size = sizeof(int32_t*);

	if( ident == VEVO_STRING ) 
	{  // special case
		unsigned char *s = *( (unsigned char**) dst );
		atom_size = sizeof(unsigned char) * strlen(s) + 1;
		// + 1 for explicit null termination
		atom = vevo_alloca_atom( atom_size );
		memcpy( atom->value, dst, atom_size - 1);
	}
	else
	{
		atom = vevo_alloca_atom( atom_size );
		memcpy( atom->value, dst, atom_size );
	}

	return atom;
}

// put a value *src into an atom 
static void	put_g( void *src, int n, vevo_datatype *d, vevo_storage_t i , vevo_atom_type_t v )
{
	d->ident = v;
	d->type = i;
	d->length = n;

	if( i == VEVOP_ATOM )
	{
			if(d->atom) vevo_free_atom(d->atom);
			d->atom = vevo_put_atom( src, v );
	}
	if( i == VEVOP_ARRAY )
	{
			int i;
			if(d->array)
			{
				for( i =0; i < d->length; i ++ )
					vevo_free_atom(d->array[i]);
				free(d->array);
			}	

			d->array = (atom_t**) malloc(sizeof(atom_t*) * n );

			if( v == VEVO_DOUBLE )
			{
				double *arr = src;
				for( i = 0; i < d->length ; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if( v == VEVO_STRING )
			{
				char **arr = src;
				for( i = 0; i < d->length ; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if( v == VEVO_INT )
			{
				int *arr = src;
				for( i = 0; i < d->length; i ++)
					d->array[i] = vevo_put_atom( &arr[i], v );
			}			
			if( v == VEVO_BOOLEAN )
			{
				vevo_boolean_t *arr = src;
				for( i = 0; i < d->length; i ++)
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if ( v == VEVO_PTR_DBL )
			{
				double **arr = src;
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if ( v == VEVO_PTR_U8 )	
			{
				uint8_t **arr = src;
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v ); 
			}
			if ( v == VEVO_PTR_U16 )
			{
				int16_t **arr = src;
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( arr[i], v ); 
			}
			if ( v == VEVO_PTR_U32 )
			{
				uint32_t **arr = src;
				for ( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( arr[i], v );
			}
			if ( v == VEVO_PTR_S8 )
			{
				int8_t **arr = src; 
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if( v == VEVO_PTR_S16 )
			{
				int16_t **arr = src;
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( &arr[i], v );
			}
			if( v == VEVO_PTR_S32 )
			{
				int32_t **arr = src;
				for( i = 0; i < d->length; i ++ )
					d->array[i] = vevo_put_atom( &arr[i]  , v );
			}
	}

}

static	vevo_datatype *vevo_alloca_datatype(vevo_storage_t type)
{
	vevo_datatype *d =(vevo_datatype*) malloc(sizeof(vevo_datatype));
	d->type = type;
	if( d->type == VEVOP_ARRAY )
		d->array = NULL;
	else
		d->atom = NULL;
	d->cmd = VEVO_PROPERTY_WRITEABLE;
	return d;
}



static  void	vevo_free_datatype(vevo_datatype *t)
{
	if(t)
	{
		if(t->type == VEVOP_ARRAY)
		{
			int i;
			for( i = 0; i < t->length; i ++)
			{
				vevo_free_atom( t->array[i] );
			}
			free( t->array );
		}
		if(t->type == VEVOP_ATOM)
		{
			vevo_free_atom( t->atom );
		}
		free(t);
	}
	t = NULL; 
}

static vevo_property_t *vevo_alloca_property( void *value, vevo_atom_type_t ident, vevo_property_type_t type, size_t num_atoms )
{
	vevo_property_t *p = (vevo_property_t*) malloc(sizeof(vevo_property_t));
	p->type = type;
	p->data = vevo_alloca_datatype( ident );
	put_g( value, num_atoms,p->data, (num_atoms == 1 ? VEVOP_ATOM : VEVOP_ARRAY), ident );
	return p;
}

static void	vevo_free_property( vevo_property_t *p )
{
	if(p)
	{
		if(p->data) vevo_free_datatype(p->data);
		free(p);
		p = NULL;
	}
}

static	int	vevo_property_writeable( vevo_port *p, vevo_property_type_t t )
{
	vevo_property_t *curr = p->properties;
	while ( curr != NULL )
	{
		if( curr->type == t )
		{
			if(curr->data->cmd == VEVO_PROPERTY_WRITEABLE)
			{
				return VEVO_ERR_SUCCESS;
			}
			else
			{
				return VEVO_ERR_READONLY;
			}
		}
		curr = curr->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

/*****************************
  PUBLIC FUNCTIONS
*******************************/

int			vevo_find_property( vevo_port *p, vevo_property_type_t t )
{
	vevo_property_t *curr = p->properties;
	while( curr != NULL )
	{
		if( curr->type == t )
			return VEVO_ERR_SUCCESS;
		curr = curr->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

int			vevo_sort_property( vevo_port *p, vevo_property_type_t t )
{
	vevo_property_t *first = p->properties;
	vevo_property_t *curr = first;
	vevo_property_t *found = NULL;
	vevo_property_t *prev = NULL;

	while( curr != NULL )
	{
		if( curr->type == t )
		{
			found = curr;
			break;
		}
		prev = curr;
		curr = curr->next;
	}

	if( found == NULL ) 
		return VEVO_ERR_NO_SUCH_PROPERTY; 
	
	if( first == found )
	{
		return VEVO_ERR_SUCCESS;  
	}

	if(found->next == NULL )
	{
		vevo_property_t *tmp = p->properties;
		p->properties = found;
		p->properties->next = tmp;
		prev->next = NULL;
	}
	else
	{ 
		vevo_property_t *tmp = p->properties;
		prev->next = found->next;
		p->properties = found;
		p->properties->next = tmp;
	}
	return VEVO_ERR_SUCCESS;
}

// manipulate property datatype descriptor
int		vevo_cntl_property( vevo_port *p, vevo_property_type_t type, vevo_cntl_flags_t ro_flag )
{
	vevo_property_t *curr = p->properties;
	while( curr != NULL )
	{
		if( curr->type == type )
		{
			curr->data->cmd = ro_flag;
			return VEVO_ERR_SUCCESS;
		}
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}


void	vevo_del_property( vevo_port *p, vevo_property_type_t t ) 
{
	vevo_property_t *next;

	// first, put property as first element in the list
	if(vevo_sort_property( p, t ) == 0 )
	{
 		next = p->properties->next;
		vevo_free_property( p->properties );  
		p->properties = next;
	}
}

int	vevo_verify_runnable( vevo_instance_t *inst )
{
	/* check all instance flags */
	int i_flags; 
	int tmp;
	int err = vevo_get_property( inst->self, VEVOI_FLAGS, &i_flags );
	
	if( err != VEVO_ERR_SUCCESS )
		return err;

	/* fps need to be set ? */
	if( i_flags & VEVOI_FLAG_REQUIRE_FPS )
	{
		err = vevo_get_property( inst->self, VEVOI_FPS, &tmp );
		if( err != VEVO_ERR_SUCCESS )
			return err;
	}

	/* can do scaled requires scale_x, scale_y properties */
	if( i_flags & VEVOI_FLAG_CAN_DO_SCALED )
	{
		err = vevo_get_property( inst->self, VEVOI_SCALE_X, &tmp );   
		if( err != VEVO_ERR_SUCCESS )
			return err;
		err = vevo_get_property( inst->self, VEVOI_SCALE_Y, &tmp );
		if( err != VEVO_ERR_SUCCESS )
			return err;
	}

	/* window flag requires viewport properties */
	if( i_flags & VEVOI_FLAG_CAN_DO_WINDOW )
	{
		int n;
		int i[4];
		err = vevo_get_property( inst->self, VEVOI_VIEWPORT, i );
		if( err != VEVO_ERR_SUCCESS )
			return err;
		err = vevo_get_num_items( inst->self, VEVOI_VIEWPORT, &n );
		if( err != VEVO_ERR_SUCCESS )
			return err;
		// need x,y,w,h
		if( n != 4 )
			return VEVO_ERR_INVALID_PROPERTY;
	}

 	/* check if width,height,format,pixeldata,pixelinfo are set in all input channels
	 */
	err = vevo_get_property( inst->self, VEVOI_N_IN_CHANNELS , &tmp );
	if( err == VEVO_ERR_SUCCESS )
	{
		int i;
		for( i = 0; i < tmp; i ++ )
		{
			err = vevo_find_property( inst->in_channels[i], VEVOC_WIDTH );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->in_channels[i], VEVOC_HEIGHT );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->in_channels[i], VEVOC_FORMAT );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->in_channels[i], VEVOC_ENABLED );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->in_channels[i], VEVOC_PIXELDATA );
				return err;
			err = vevo_find_property( inst->in_channels[i], VEVOC_PIXELINFO );
		}
	}

	/* idem, but for output channels */
	err = vevo_get_property( inst->self, VEVOI_N_OUT_CHANNELS, &tmp );	
	if( err == VEVO_ERR_SUCCESS )
	{
		int i;
		for( i = 0; i < tmp; i ++ )
		{
			err = vevo_find_property( inst->out_channels[i], VEVOC_WIDTH );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->out_channels[i], VEVOC_HEIGHT );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->out_channels[i], VEVOC_FORMAT );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->out_channels[i], VEVOC_ENABLED );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->out_channels[i], VEVOC_PIXELDATA );
			if( err != VEVO_ERR_SUCCESS )
				return err;
			err = vevo_find_property( inst->out_channels[i], VEVOC_PIXELINFO );
			if( err != VEVO_ERR_SUCCESS ) 
				return err;	
		}

	}
	
	/* See if at least value is set for a parameter */
	err = vevo_get_property( inst->self, VEVOI_N_IN_PARAMETERS, &tmp );
	if( err == VEVO_ERR_SUCCESS )
	{	
		int i;
		for( i = 0; i < tmp; i ++ )
		{
			err = vevo_find_property( inst->in_params[i], VEVOP_VALUE );
			if( err != VEVO_ERR_SUCCESS )
				return err; 
		} 
	}

	return VEVO_ERR_SUCCESS;	
}

void	vevo_set_property( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t ident, size_t arglen, void *value )
{
	int err = vevo_property_writeable(p,type);

	if( err == VEVO_ERR_NO_SUCH_PROPERTY )
	{
		vevo_property_t *tmp = vevo_alloca_property( value,ident, type, arglen );
		tmp->next = p->properties;
		p->properties = tmp;
	}
	else
	{
	    if(err == VEVO_ERR_SUCCESS )
		{
			vevo_property_t *tmp;
			vevo_del_property( p, type ); 
			tmp = vevo_alloca_property( value, ident, type, arglen ); 
			tmp->next = p->properties;
			p->properties = tmp;
		}
	}
}

int		vevo_get_num_items( vevo_port *p, vevo_property_type_t type, int *size )
{
	vevo_property_t *property = p->properties;
	while( property != NULL )
	{
		if( property->type == type )
		{
			*size = property->data->length;
			return VEVO_ERR_SUCCESS;
		}
		property = property->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

int		vevo_get_item_size( vevo_port *p, vevo_property_type_t type,int index, int *size )
{
	vevo_property_t *property = p->properties;
	while(property != NULL)
	{
		if( property->type == type )
		{
			vevo_datatype *d = property->data;
			if(d->type == VEVOP_ARRAY )
			{
					if( index < d->length )
						*size = d->array[index]->size;
					else
						return VEVO_ERR_OOB;
			}
			else
			{
					*size = d->atom->size;
			}
		}
		property = property->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

int		vevo_get_data_type( vevo_port *p, vevo_property_type_t type, int *dt )
{
	vevo_property_t *property = p->properties;
	while( property != NULL )
	{
		if( property->type == type )
		{
			*dt = property->data->ident;
			return VEVO_ERR_SUCCESS;
		}
		property = property->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

int		vevo_get_property( vevo_port *p, vevo_property_type_t type, void *dst )
{
	vevo_property_t *property = p->properties;
	while( property != NULL )
	{
		if( property->type == type )
		{
			parse_g( property->data, dst );
			return VEVO_ERR_SUCCESS;
		}
		property = property->next;
	}
	return VEVO_ERR_NO_SUCH_PROPERTY;
}


static int		vevo_cast_atom_t_ ( vevo_datatype *val, vevo_atom_type_t dsti, void *dst )
{
	if( val->ident == VEVO_INT )
	{
		if( dsti == VEVO_DOUBLE )
		{
			vevo_cast_int_to_double( val->atom, val->ident, dst );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_INT )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == VEVO_DOUBLE )
	{
		if( dsti == VEVO_DOUBLE )
		{
			parse_g(val, dst ); 
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_INT )
		{
			vevo_cast_double_to_int( val->atom, val->ident, dst );
			return VEVO_ERR_SUCCESS;
		}
	}
	return VEVO_ERR_INVALID_CAST;
}

static int		vevo_scale_atom_t_ ( vevo_datatype *val, vevo_atom_type_t dsti, void *dst, double scale )
{
	if( val->ident == VEVO_INT )
	{
		if( dsti == VEVO_DOUBLE )
		{
			vevo_scale_int_to_double( val->atom, val->ident, dst, scale );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_INT )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == VEVO_DOUBLE )
	{
		if( dsti == VEVO_INT )
		{
			vevo_scale_double_to_int( val->atom, val->ident, dst, scale ); 
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_DOUBLE )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == VEVO_STRING )
	{
		if( dsti == VEVO_INT )
		{
			if(val->atom->size < 9) return VEVO_ERR_OOB;
			vevo_scale_string_to_int( val->atom, val->ident, dst );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_DOUBLE )
		{
			if(val->atom->size < 9) return VEVO_ERR_OOB;
			vevo_scale_string_to_double( val->atom, val->ident, dst, scale );
			return VEVO_ERR_SUCCESS;
		}
	}
	return VEVO_ERR_INVALID_CAST;
}


static int		vevo_cast_array_t_ (vevo_datatype *val, vevo_atom_type_t dsti, void *dst )
{
	if( val->ident == VEVO_INT )
	{
		if( dsti == VEVO_DOUBLE )
		{
			vevo_cast_int_to_double_arr( val->array, val->ident, dst, val->length );		
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_INT )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;	
		}
	}
	if( val->ident == VEVO_DOUBLE )
	{
		if( dsti == VEVO_INT )
		{
			vevo_cast_double_to_int_arr( val->array, val->ident, dst, val->length );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_DOUBLE )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == dsti)
	{
		parse_g( val, dst );
		return VEVO_ERR_SUCCESS;
	}	
	return VEVO_ERR_INVALID_CAST;
}

static int		vevo_scale_array_t_ ( vevo_datatype *val, vevo_atom_type_t dsti, void *dst , double scale)
{
	if( val->ident == VEVO_INT )
	{
		if( dsti == VEVO_DOUBLE )
		{
			vevo_scale_int_to_double_arr( val->array, val->ident, dst, val->length, scale );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_INT )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_STRING )
		{
			vevo_scale_int_to_string_arr( val->array, val->ident, dst, val->length, scale );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == VEVO_DOUBLE )
	{
		if( dsti == VEVO_INT )
		{
			vevo_scale_double_to_int_arr( val->array, val->ident, dst, val->length, scale );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_DOUBLE )
		{
			parse_g( val, dst );
			return VEVO_ERR_SUCCESS;
		}
		if( dsti == VEVO_STRING)
		{
			vevo_scale_double_to_string_arr( val->array, val->ident, dst, val->length, scale );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( val->ident == dsti )
	{	parse_g( val, dst );
		return VEVO_ERR_SUCCESS;
	}
	return VEVO_ERR_INVALID_CAST;
}


static int 	vevo_get_value_as_ ( vevo_property_t *prop, vevo_atom_type_t dst_ident, void *dst )
{
	int st = vevo_get_storage_type(prop->data);

	if( st == VEVOP_ATOM )
		vevo_cast_atom_t_ ( prop->data, dst_ident, dst );
	if( st == VEVOP_ARRAY )
		vevo_cast_array_t_ ( prop->data, dst_ident, dst );

	return VEVO_ERR_INVALID_CAST;
}

static int		vevo_get_parameter_as_( vevo_property_t *prop, vevo_param_hint_t hint, vevo_atom_type_t dst_ident, void *dst )
{
	int st = vevo_get_storage_type(prop->data);
	if( hint == VEVOP_HINT_RGBA )
	{
		if( st == VEVOP_ARRAY )	
			return vevo_scale_array_t_( prop->data, dst_ident, dst, 255.0 );
		if( st == VEVOP_ATOM )
			return vevo_scale_atom_t_(prop->data,dst_ident,dst,255.0);
	}
	if( hint == VEVOP_HINT_TRANSITION )
	{
		if( dst_ident == VEVO_STRING )
			return VEVO_ERR_INVALID_CONVERSION;
		if( st == VEVOP_ATOM )
			return vevo_scale_atom_t_ (prop->data,dst_ident, dst , 255.0);
		if( st == VEVOP_ARRAY )
			return vevo_scale_array_t_ ( prop->data, dst_ident, dst, 255.0 );

	}

	if( hint == VEVOP_HINT_NORMAL || hint == VEVOP_HINT_GROUP )
	{
		if( st == VEVOP_ATOM )
			return vevo_cast_atom_t_ (prop->data, dst_ident, dst );
		if( st == VEVOP_ARRAY )
			return vevo_cast_array_t_ ( prop->data, dst_ident, dst );
	}

	if( hint == VEVOP_HINT_COORD2D )
		if( dst_ident == VEVO_STRING )
			return VEVO_ERR_INVALID_CONVERSION;

	return VEVO_ERR_INVALID_CAST;	
}

static	int		vevo_scale_to_atom_t( vevo_datatype *data, vevo_atom_type_t src_ident, size_t arglen, void *value, double scale )
{
	if( src_ident == VEVO_INT )
	{
		int val = *( (int*) value );
		if(data->ident == VEVO_INT )
		{
			put_g(&val, 1, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}
		if(data->ident == VEVO_DOUBLE )
		{
			double res = (double) val / scale; 
			put_g( &res, 1, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( src_ident == VEVO_DOUBLE )
	{
		double val = *( (double*) value );
		if( data->ident == VEVO_INT )
		{
			int res = (int) ( val * scale );
			put_g( &res, 1, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}
		if( data->ident == VEVO_DOUBLE )
		{
			put_g( &val, 1, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}

	}
	return VEVO_ERR_INVALID_CONVERSION;
}
static	int		vevo_scale_to_array_t( vevo_datatype *data, vevo_atom_type_t src_ident, size_t arglen, void *value, double scale )
{
	if( src_ident == VEVO_INT )
	{
		int *src = (int*) value;
		vevo_datatype *tmp = vevo_alloca_datatype( VEVO_INT );
		put_g( value, arglen, tmp, VEVOP_ARRAY, VEVO_INT );
		
		if( data->ident == VEVO_DOUBLE )
		{
			double result[arglen];
			vevo_scale_int_to_double_arr( tmp->array, VEVO_INT, result, arglen, scale );
			put_g( result, arglen, data, VEVOP_ARRAY, VEVO_DOUBLE );
			vevo_free_datatype( tmp );
			return VEVO_ERR_SUCCESS;
		}
		if( data->ident == VEVO_INT )
		{
			put_g( src, arglen, data, VEVOP_ARRAY, VEVO_INT );
			vevo_free_datatype(tmp);
			return VEVO_ERR_SUCCESS;
		}
		if( data->ident == VEVO_STRING )
		{
			char result[10];
			vevo_scale_int_to_string_arr( tmp->array, VEVO_INT, result, arglen, scale );  
			put_g( result, 10, data, VEVOP_ATOM, VEVO_STRING );
			vevo_free_datatype( tmp);
			return VEVO_ERR_SUCCESS;    
		}
		if(tmp) vevo_free_datatype(tmp);
	}
	if( src_ident == VEVO_STRING )
	{
		char *src =(char*) value;
		if( data->ident == VEVO_DOUBLE )
		{
			double result[4];
			vevo_datatype *tmp = vevo_alloca_datatype( VEVO_STRING );
			put_g( &src, arglen, tmp, VEVOP_ATOM, VEVO_STRING );
			vevo_scale_string_to_double( tmp->atom, VEVO_STRING, result, scale );
			put_g( result, 4, data, VEVOP_ARRAY, VEVO_DOUBLE ); 
			vevo_free_datatype( tmp );
			return VEVO_ERR_SUCCESS;
		}
		if( data->ident == VEVO_INT )
		{
			int result[4];
			vevo_datatype *tmp = vevo_alloca_datatype( VEVO_STRING );
			put_g(value, arglen, tmp, VEVOP_ATOM, VEVO_STRING );
			vevo_scale_string_to_int( tmp->atom, VEVO_STRING, result );
			put_g( result, 4, data, VEVOP_ARRAY, VEVO_INT );
			vevo_free_datatype( tmp );
			return VEVO_ERR_SUCCESS;
		}	
		if( data->ident == VEVO_STRING )
		{
			put_g( src, arglen, data, VEVOP_ATOM, VEVO_STRING );
		}

	}
	if( src_ident == VEVO_DOUBLE )
	{
		double *src =  (double*)value;
		
		if( data->ident == VEVO_DOUBLE )
		{
			put_g( src, arglen, data, VEVOP_ARRAY, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}	
		if( data->ident == VEVO_INT )
		{
			int result[arglen];
			vevo_datatype *tmp = vevo_alloca_datatype( VEVO_DOUBLE );
			put_g( src, arglen, tmp, VEVOP_ARRAY, VEVO_DOUBLE );
			vevo_scale_double_to_int_arr( tmp->array, VEVO_DOUBLE, result,arglen, scale );
			put_g( result, arglen, data,VEVOP_ARRAY, VEVO_INT );
			vevo_free_datatype( tmp );
			return VEVO_ERR_SUCCESS;
		}
 		if( data->ident == VEVO_STRING )
		{
			char result[arglen*2+1];
			vevo_datatype *tmp = vevo_alloca_datatype( VEVO_DOUBLE );
			put_g( src, arglen, tmp, VEVOP_ARRAY, VEVO_DOUBLE );
			vevo_scale_double_to_string_arr( tmp->array, VEVO_DOUBLE, result, arglen*2+1, scale );
			put_g( result, arglen*2+1, data, VEVOP_ATOM, VEVO_STRING );
			vevo_free_datatype( tmp );
			return VEVO_ERR_SUCCESS;
		}
	}
	return VEVO_ERR_INVALID_CONVERSION;
}
static 	int		vevo_cast_to_atom_t( vevo_datatype *data, vevo_atom_type_t src_ident, size_t arglen, void *value )
{
	if( src_ident == VEVO_INT )
	{
		int src = *( (int*) value );
		if(data->ident == VEVO_DOUBLE )
		{
			double val = (double) src;
			put_g( &val, 1, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}
		if(data->ident == VEVO_INT )
		{
			put_g( &src, 1, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}
	}
	
	if( src_ident == VEVO_DOUBLE )
	{
		double src = *( (double*) value );
		if(data->ident == VEVO_INT )
		{
			int val = (int) src;
			put_g(&val, 1, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}	
		if(data->ident == VEVO_DOUBLE )
		{
			put_g(&src, 1, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}
	}
	
	return VEVO_ERR_INVALID_CAST;
}
static	int		vevo_cast_to_array_t( vevo_datatype *data, vevo_atom_type_t src_ident, size_t arglen, void *value)
{
	if( src_ident == VEVO_INT )
	{
		int *src = value;
		if(data->ident == VEVO_DOUBLE )
		{
			double *val = (double*) src;
			put_g(&val, arglen, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}
		if(data->ident == VEVO_INT )
		{
			put_g(&src, arglen, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}
	}
	if( src_ident == VEVO_DOUBLE )	
	{
		double *src = value;
		if( data->ident == VEVO_INT )
		{
			int *val = (int*) src;
			put_g(&val, arglen, data, VEVOP_ATOM, VEVO_INT );
			return VEVO_ERR_SUCCESS;
		}
		if( data->ident == VEVO_DOUBLE )
		{
			put_g( &src, arglen, data, VEVOP_ATOM, VEVO_DOUBLE );
			return VEVO_ERR_SUCCESS;
		}
	}
	return VEVO_ERR_INVALID_CAST;
}

static int		vevo_set_property_by_( vevo_property_t *prop, vevo_param_hint_t hint, vevo_atom_type_t src_ident, size_t arglen, void *value )
{
	int st = vevo_get_storage_type(prop->data);
	if( hint == VEVOP_HINT_RGBA )
	{
		if( st == VEVOP_ATOM )
			return	vevo_scale_to_atom_t( prop->data, src_ident, arglen, value, 255.0 );
		if( st == VEVOP_ARRAY )
			return  vevo_scale_to_array_t( prop->data, src_ident, arglen, value, 255.0 );
	}

	if( hint == VEVOP_HINT_TRANSITION )
	{
		if( src_ident == VEVO_STRING )
			return VEVO_ERR_INVALID_CONVERSION;
		if( st == VEVOP_ATOM )
			return	vevo_scale_to_atom_t( prop->data, src_ident, arglen, value, 255.0 );
		if( st == VEVOP_ARRAY )
			return  vevo_scale_to_array_t( prop->data, src_ident, arglen, value, 255.0 );
	}

	if( hint == VEVOP_HINT_NORMAL  || hint == VEVOP_HINT_GROUP )
	{
		if( src_ident == VEVO_STRING )
			return VEVO_ERR_INVALID_CONVERSION;
		if( st == VEVOP_ATOM )
			return vevo_cast_to_atom_t( prop->data, src_ident, arglen, value );
		if( st == VEVOP_ARRAY )
			return vevo_cast_to_array_t( prop->data, src_ident, arglen, value );
	}

	if( hint == VEVOP_HINT_COORD2D )
		if ( src_ident == VEVO_STRING )
			return VEVO_ERR_INVALID_CONVERSION;

	return VEVO_ERR_INVALID_CAST;
}

static int		vevo_set_property_value( vevo_property_t *prop, vevo_atom_type_t src_ident, size_t arglen, void *value )
{
	int st = vevo_get_storage_type( prop->data );
	if( st == VEVOP_ATOM )
		vevo_cast_to_atom_t( prop->data, src_ident, arglen, value );
	if( st == VEVOP_ARRAY )
		vevo_cast_to_array_t( prop->data, src_ident, arglen, value );

	return VEVO_ERR_SUCCESS;
}

int		vevo_set_property_by( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t src_ident, size_t arglen, void *value )
{
	int err = vevo_property_writeable(p,type);

	if( err != VEVO_ERR_SUCCESS )
		return err;

	vevo_property_t *prop = p->properties;
	while( prop != NULL )
	{
		if( prop->type == type )
		{
			if( p->type->id == VEVO_CONTROL )
				return vevo_set_property_by_( prop, p->type->hint, src_ident, arglen, value );
			else
				return vevo_set_property_value( prop, src_ident,arglen, value );
		}
	}
	return VEVO_ERR_SUCCESS;	
}
int		vevo_get_property_as( vevo_port *p, vevo_property_type_t type, vevo_atom_type_t dest_ident, void *dst )
{
	vevo_property_t *property = p->properties;
	while (property != NULL )
	{
		if(property->type == type )
		{
				if( p->type->id == VEVO_CONTROL ) 
					return vevo_get_parameter_as_( property, p->type->hint, dest_ident, dst );
				else 
					vevo_get_value_as_( property, dest_ident, dst );
				return VEVO_ERR_SUCCESS;
		}
		property = property->next;
	}	
	return VEVO_ERR_NO_SUCH_PROPERTY;
}

int		vevo_collect_frame_data( vevo_port *p, vevo_frame_t *dst )
{
	int err = vevo_get_property_as( p, VEVOC_WIDTH,VEVO_INT, &(dst->width) );

	if( err != VEVO_ERR_SUCCESS )
		return err;

	err 	= vevo_get_property_as( p, VEVOC_HEIGHT,VEVO_INT, &(dst->height) );
	if( err != VEVO_ERR_SUCCESS )
		return err;

	err		= vevo_get_property_as( p, VEVOC_FORMAT,VEVO_INT, &(dst->fmt) );
	if( err != VEVO_ERR_SUCCESS )
		return err;

	if( dst->fmt <= 0 || dst->fmt > 19 )
		return VEVO_ERR_INVALID_FORMAT;

	if( dst->fmt == VEVO_YUV422P || dst->fmt == VEVO_YUV420P || dst->fmt == VEVO_YUV444P )
	{
		err		= vevo_get_property_as( p, VEVOC_SHIFT_H, VEVO_INT, &(dst->shift_h) );
		if( err != VEVO_ERR_SUCCESS )
			return err;

		err		= vevo_get_property_as( p, VEVOC_SHIFT_V, VEVO_INT, &(dst->shift_v) );
		if( err != VEVO_ERR_SUCCESS )
			return err;	
	}
	else
	{
		dst->shift_h = 0;
		dst->shift_v = 0;
	}

	err		= vevo_get_property( p, VEVOC_PIXELINFO, &(dst->type) );

	if( err != VEVO_ERR_SUCCESS )
		return err;


	if( dst->type == VEVO_FRAME_U8 )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_u8 );

	if( dst->type == VEVO_FRAME_U16 )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_u16 );

	if( dst->type == VEVO_FRAME_U32 )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_u32 );

	if( dst->type == VEVO_FRAME_S8)
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_s8 );

	if( dst->type == VEVO_FRAME_S16 )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_s16 );

	if( dst->type == VEVO_FRAME_S32 )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_s32 );

	if( dst->type == VEVO_FRAME_FLOAT )
		err		= vevo_get_property( p, VEVOC_PIXELDATA, dst->data_float );

	if( err != VEVO_ERR_SUCCESS )
		return err;

	dst->row_strides[0] = dst->width * dst->height;
	dst->row_strides[1] = dst->width >> dst->shift_h;
	dst->row_strides[2] = dst->width >> dst->shift_h;
	dst->row_strides[3] = 0; // alpha channel untested

	return VEVO_ERR_SUCCESS;
}

vevo_port *vevo_allocate_parameter( vevo_parameter_templ_t *info )
{
	vevo_port *p = (vevo_port*) malloc(sizeof(vevo_port));
	vevo_port_type_t *t = (vevo_port_type_t*) malloc(sizeof(vevo_port_type_t));
	if(!p || !t) return NULL;
	t->id = VEVO_CONTROL;
	t->hint = info->hint;
	p->type = t;
	p->properties = NULL;
	return p;	
}

vevo_port	*vevo_allocate_channel( vevo_channel_templ_t *info )
{
	int tmp = 1;
	vevo_port *p = (vevo_port*) malloc(sizeof(vevo_port));
	vevo_port_type_t *t = (vevo_port_type_t*) malloc(sizeof(vevo_port_type_t));
	t->id = VEVO_DATA;
	t->format = info->format[0];
	p->type = t;
	p->properties = NULL;


	if(info->flags != 0)
	{
		vevo_set_property( p, VEVOC_FLAGS, VEVO_INT, 1, &(info->flags) );
	}

	vevo_set_property( p, VEVOC_FORMAT, VEVO_INT,1, &(t->format) );
	vevo_set_property( p, VEVOC_ENABLED, VEVO_INT,1, &tmp );

	if( t->format == VEVO_YUV420P )
	{
		tmp = 1;
		vevo_set_property( p, VEVOC_SHIFT_V, VEVO_INT, 1,&tmp );
		vevo_set_property( p, VEVOC_SHIFT_H, VEVO_INT, 1,&tmp );
	}

	if( t->format == VEVO_YUV422P )
	{
		tmp = 0;
		vevo_set_property( p, VEVOC_SHIFT_H, VEVO_INT, 1,&tmp);
		tmp = 1;
		vevo_set_property( p, VEVOC_SHIFT_V, VEVO_INT, 1,&tmp);
	}

	if( t->format == VEVO_YUV444P )
	{
		tmp = 0;
		vevo_set_property( p, VEVOC_SHIFT_V, VEVO_INT, 1,&tmp );
		vevo_set_property( p, VEVOC_SHIFT_H, VEVO_INT, 1,&tmp );
	}

	return p;
}

void		vevo_free_instance( vevo_instance_t *p )
{
	int ni=0,no=0,pi=0,po=0;
	int i;
	vevo_get_property( p->self, VEVOI_N_IN_PARAMETERS, &pi );
	if( pi > 0 )
	{
		for(i = 0; i < pi; i ++ )
			vevo_free_port( p->in_params[i] );
		free( p->in_params );
	}

	vevo_get_property( p->self, VEVOI_N_OUT_PARAMETERS, &po );
	if( po > 0 )
	{
		for( i = 0; i < po; i ++ )
			vevo_free_port( p->out_params[i] );
		free( p->out_params );
	}

	vevo_get_property( p->self, VEVOI_N_IN_CHANNELS, &ni );
	if( ni > 0 )
	{
		for( i = 0; i < ni; i ++ )
			vevo_free_port( p->in_channels[i] );
		free( p->in_channels );
	}

	vevo_get_property( p->self, VEVOI_N_OUT_CHANNELS, &no );
	if( no > 0 )
	{
		for( i = 0; i < no; i ++ )
			vevo_free_port( p->out_channels[i] );
		free( p->out_channels );
	}	


	vevo_free_port(p->self);
	free(p);
}

#define __num_items(n, l) \
{ while( l != NULL ) { n++; l = l->next; } }


#define __allocate_port(top,port, property, datatype,size)\
{ if(size > 0 ) port = (vevo_port**) malloc(sizeof(vevo_port*) * size );\
  else port = NULL;\
  vevo_set_property( top, property, datatype, 1, &size );\
  vevo_cntl_property( top,property, VEVO_PROPERTY_READONLY );\
  size = 0;\
}

#define __alloca_channels( tmpl,port )\
{ n=0; while( tmpl != NULL ) { port[n] = vevo_allocate_channel( tmpl ); tmpl = tmpl->next; n++; }}

#define __alloca_parameters( tmpl,port )\
{ n=0; while( tmpl != NULL ) { port[n] = vevo_allocate_parameter( tmpl ); tmpl = tmpl->next; n++; }}




vevo_instance_t	*vevo_allocate_instance( vevo_instance_templ_t * info )
{
	vevo_instance_t *p = (vevo_instance_t*) malloc(sizeof(vevo_instance_t));
	vevo_port	   *self = (vevo_port*) malloc(sizeof(vevo_port));
	vevo_port_type_t *type = (vevo_port_type_t*) malloc(sizeof(vevo_port_type_t));
	int n = 0;

	type->id = VEVO_INSTANCE;
	self->type = type;
	self->properties = NULL;

	vevo_parameter_templ_t *inp = info->in_params;
	__num_items( n, inp );
	__allocate_port( self, p->in_params, VEVOI_N_IN_PARAMETERS, VEVO_INT, n );

	vevo_parameter_templ_t *outp = info->out_params;
	__num_items(n, outp );
	__allocate_port( self, p->out_params, VEVOI_N_OUT_PARAMETERS, VEVO_INT, n );

	vevo_channel_templ_t *inc = info->in_channels;
	__num_items( n, inc );
	__allocate_port( self, p->in_channels, VEVOI_N_IN_CHANNELS, VEVO_INT, n );

	vevo_channel_templ_t *outc = info->out_channels;
	__num_items(n, outc);
	__allocate_port( self, p->out_channels , VEVOI_N_OUT_CHANNELS, VEVO_INT, n );

	p->self = self;

	vevo_channel_templ_t *inc_ = info->in_channels;
	__alloca_channels( inc_ , p->in_channels );

	vevo_channel_templ_t *outc_ = info->out_channels;
	__alloca_channels( outc_, p->out_channels );
	
	vevo_parameter_templ_t *inp_ = info->in_params;
	__alloca_parameters( inp_, p->in_params );

	vevo_parameter_templ_t *outp_ = info->out_params;
	__alloca_parameters( outp_, p->out_params );
	
	return p;
}

void			vevo_free_port( vevo_port *p )
{
	if(p)
	{
		while(p->properties)
		{
			vevo_del_property( p, p->properties->type );
		}
		if(p->type) free(p->type);
		free(p);
	}
}



