/* veejay - Linux VeeJay - libplugger utility
 *           (C) 2010      Niels Elburg <nwelburg@gmail.com> ported from veejay-ng
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <libhash/hash.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvevo/libvevo.h>
#include <libvje/vje.h>
#include <libplugger/defs.h>
#include <libplugger/ldefs.h>
#include <libplugger/specs/livido.h>
#include <libyuv/yuvconv.h>
#include <ffmpeg/avcodec.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

char		*get_str_vevo( void *port, const char *key )
{
	size_t len = vevo_property_element_size( port, key, 0 );
	char *ret = NULL;
	if(len<=0) return NULL;
	ret = (char*) vj_malloc(sizeof(char) * len );
	vevo_property_get( port, key, 0, &ret );
	return ret;
}
char		*alloc_str_vevo( void *port, const char *key )
{
	size_t len = vevo_property_element_size( port, key, 0 );
	char *ret = NULL;
	if(len<=0) return NULL;
	ret = (char*) vj_malloc(sizeof(char) * len );
	return ret;
}


double		*get_dbl_arr_vevo( void *port, const char *key )
{
	double *res = NULL;

	int	num = vevo_property_num_elements( port, key );
	if( num <= 0 )
		return NULL;

	res = (double*) vj_malloc(sizeof(double) * num );

	int 	i;
	for( i = 0; i < num ; i++ )
		vevo_property_get( port, key, i, &(res[i]) );

	return res;
}

int		auto_scale_int( void *port, const char *key, int n, int *dst)
{
	double tmpd = 0.0;
	int tmpb = 0;
	int ikind = -1;
 	int error = vevo_property_get( port, "HOST_kind", 0, &ikind );
	if( error != 0 )
		return 1;
		
	int	transition = 0;
	vevo_property_get( port, "transition", 0, &transition );
	
	switch(ikind)
	{
		case	HOST_PARAM_INDEX:
		case	HOST_PARAM_SWITCH:
			vevo_property_get( port, key,n , &tmpb );
			*dst = tmpb;
			break;
		case  	HOST_PARAM_NUMBER:
			vevo_property_get( port, key,n, &tmpd );
			if( transition )
				*dst = (int) (tmpd * 100.0);
			else
				*dst = (int) (tmpd * 255.0);
		//	vevo_property_get( port, "HOST_width", 0, &w );
		//	vevo_property_get( port, "HOST_height",0, &w );
			break;
		default:
			veejay_msg(VEEJAY_MSG_ERROR, "Unsupported parameter: %d for key %s", ikind,key);
			break;
	}
	return LIVIDO_NO_ERROR;
}

void clone_prop_vevo( void *port, void *to_port, const char *key, const char *as_key )
{
	int	num = vevo_property_num_elements( port, key );
	int	type= vevo_property_atom_type( port, key );
	int 	i;
	
	if( num <= 0 )
		return;
	
	int itmp[8];
	double dtemp[8];
	char *stmp[8];
	
	int error;
	void *ptr = NULL;
	
	switch( type )
	{
		case VEVO_ATOM_TYPE_INT:
		case VEVO_ATOM_TYPE_BOOL:
			for( i= 0; i < num; i ++ )
			{
				error = vevo_property_get( port,key,i, &(itmp[i]) );
#ifdef STRICT_CHECKING
				assert( error == VEVO_NO_ERROR );
#endif
			}
			error = vevo_property_set( to_port, as_key, type, num, &itmp );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			for( i = 0; i < num ; i++ )
			{
				error = vevo_property_get( port, key, i, &(dtemp[i]));
#ifdef STRICT_CHECKING
				assert( error == VEVO_NO_ERROR );
#endif
			}
		//@ FIXME: scale parameter and treat as TYPE_INT

			for( i = 0; i < num; i ++ ) {
				itmp[i] = (int)( dtemp[i] * 100.0);
			}

			error = vevo_property_set( to_port, as_key, VEVO_ATOM_TYPE_INT, num, &itmp );
		//		error = vevo_property_set( to_port, as_key, type, num, &dtemp );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
		//	veejay_msg(0, "\t\tValue is %g ", dtemp[0] );
			break;
		//@ FIXME: not supported yet
	/*	case VEVO_ATOM_TYPE_STRING:
			for( i = 0; i < num; i ++)
			{
				size_t len = vevo_property_element_size( port,key,i);
				stmp[i] = NULL;
				if( len > 0 ) continue;
				stmp[i] = (char*) vj_malloc(sizeof(char) * len );
				error = vevo_property_get( port, key, i, &(stmp[i]) );
#ifdef STRICT_CHECKING
				assert( error == VEVO_NO_ERROR );
#endif
			}
			error = vevo_property_set( to_port, as_key, type, num, &stmp );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			break;*/
		default:
			veejay_msg(0, "Internal error. Cannot clone this type of atom");
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;

	}

}


void		clone_prop_vevo2( void *port, void *to_port, const char *key, const char *as_key )
{
	int n = vevo_property_atom_type( port ,key);
#ifdef STRICT_CHECKING
	assert( n >= 0 );
#endif
	int itmp = 0;
	double dtmp = 0.0;
	int error = 0;

	switch(n)
	{
		case VEVO_ATOM_TYPE_INT:
			error = vevo_property_get( port, key, 0, &itmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR);
#endif
			error = vevo_property_set( to_port, as_key, n, 1, &itmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR);
#endif
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			error = vevo_property_get( port, key, 0, &dtmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR);
			assert( to_port != NULL );
			assert( key != NULL );
#endif	
			error = vevo_property_set( to_port, as_key, n, 1, &dtmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR);
#endif

			break;
		case VEVO_ATOM_TYPE_BOOL:
			error = vevo_property_get( port, key, 0, &itmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR);
#endif
			error = vevo_property_set( to_port, as_key, n, 1, &itmp );
#ifdef STRICT_CHECKING
			assert( error == LIVIDO_NO_ERROR );
#endif

			break;
		default:
#ifdef STRICT_CHECKING
			veejay_msg(0, "Type %d cannot be cloned",n);
			assert(  0 );
#endif
			break;
	}
}


