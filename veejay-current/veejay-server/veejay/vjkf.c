/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg < nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 *
 *
 */


#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <string.h>
#include <libvjmsg/vj-msg.h>
#include <libvevo/vevo.h>
#include <veejay/vevo.h>
#include <libvevo/libvevo.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-tag.h>
#include <assert.h>
#ifdef HAVE_XML2
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#endif
#include <libvjxml/vj-xml.h>
/* veejay server stores keyframes 
 *
 *
 *
 * keyframe format:
 *       ( [frame_num1][value1][frame_num2][value2][status] ... [frame_numN][valueN] )
 *
 *
 */

#include <veejay/vjkf.h>

static	char	*extract_( const char *prefix , int p_id )
{
	char tmp[100];
	snprintf(tmp,sizeof(tmp), "%s_p%d",prefix,p_id);
	return vj_strdup(tmp);
}

unsigned char *keyframe_pack( void *port, int parameter_id, int entry_id, int *rlen )
{
	int i,k=0;
	unsigned char *result = NULL;
	int start = 0, end = 0, type =0, status = 0, values_len = 0;
	int *values = NULL;

	char *k_s = extract_( "start", parameter_id );
	char *k_e = extract_( "end",   parameter_id );
	char *k_t = extract_( "type",  parameter_id );
	char *k_x = extract_( "status", parameter_id );
	char *k_d = extract_( "data", parameter_id );
	char *k_dn= extract_( "datalen", parameter_id );

	if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}
	if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}
	if( vevo_property_get( port, k_x,   0, &status ) != VEVO_NO_ERROR )
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}
	if( vevo_property_get( port, k_d, 0, &values ) != VEVO_NO_ERROR ) 
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}	
	if( vevo_property_get( port, k_dn, 0, &values_len ) != VEVO_NO_ERROR ) 
	{
		free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
		return NULL;
	}	
	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
	free(k_dn);

	int len = end - start;

	result = vj_calloc( (len*4) + 64 );

	sprintf( (char*) result,"key%02d%02d%08d%08d%02d%02d", entry_id,parameter_id,start, end, type, status );

	unsigned char *out = result + 27;

	for( i = 0; i < values_len; i ++ ) 
	{
		int value = values[i];
		unsigned char *buf = out + (4 * k);
		
		buf[0] = ( value ) & 0xff;
		buf[1] = (value >> 8) & 0xff;
		buf[2] = (value >> 16) & 0xff;
		buf[3] = (value >> 24) & 0xff;
		k++;
	}
	
	*rlen = 27 + (4 *  k);

	veejay_msg(VEEJAY_MSG_DEBUG, "KF %p pack %2.2fKb: range=%d-%d, FX entry %d, P%d, type %d status %d",
		port,(*rlen/1024.0f),start,end, entry_id,parameter_id, type, status );

	return result;
}

int	keyframe_get_param_status( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	void *port = NULL;

	if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return 0;
	}

	int status = 0;
	char *k_x = extract_ ( "status", parameter_id );
	
	vevo_property_get( port, k_x,0, &status ); 

	free(k_x);

	return status;
}


void	keyframe_set_param_status( int lookup, int fx_entry, int parameter_id, int status, int is_sample )
{
	void *port = NULL;
	int kf_status = status;

	if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return;
	}

	char *k_x = extract_( "status", parameter_id );
	
	vevo_property_set( port, k_x, VEVO_ATOM_TYPE_INT,1, &kf_status );

	free(k_x);
}

void	keyframe_clear_entry( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	int start = 0;
	int end = 0;
	int type = 0;
	int status = 0;
	int values_len = 0;
	int *values = NULL;
	void *port = NULL;

	if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	if( port == NULL ) {
		veejay_msg(0, "FX Entry %d does not have animated parameters", fx_entry );
		return;
	}

	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );
	char *k_d = extract_ ( "data", parameter_id );
	char *k_dn= extract_ ( "datalen", parameter_id );
	
	vevo_property_get( port, k_s,0, &start );
	vevo_property_get( port, k_e,0, &end );
	vevo_property_get( port, k_t,0, &type );
	vevo_property_get( port, k_x,0, &status );   
	vevo_property_get( port, k_d,0, &values );
	vevo_property_get( port, k_dn,0,&values_len);

	if( values != NULL ) {
		free(values);
	}

	vevo_property_del( port, k_s );
	vevo_property_del( port, k_e );
	vevo_property_del( port, k_t );
	vevo_property_del( port, k_x );
	vevo_property_del( port, k_d );
	vevo_property_del( port, k_dn);

	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
	free(k_dn);
}

int		keyframe_unpack( unsigned char *in, int len, int *entry, int lookup, int is_sample )
{
	int i;
	int parameter_id = 0;
	int start = 0, end = 0, type = 0;
	int fx_entry = 0;
	int status = 0;
	int n = sscanf( (char*) in, "key%2d%2d%8d%8d%2d%2d", &fx_entry,&parameter_id, &start, &end,&type,&status );
	int idx = 0;

	if(n != 6 )
	{
		veejay_msg(0, "Unable to unpack keyframe data");
		return 0;
	}

    if( fx_entry < 0 || fx_entry > SAMPLE_MAX_EFFECTS ) {
        veejay_msg(0, "Invalid fx entry [%d] in '%s'", fx_entry, in );
        return 0;
    }
    if( parameter_id < 0 || parameter_id > SAMPLE_MAX_PARAMETERS ) {
        veejay_msg(0, "Invalid parameter id [%d] in '%s'", parameter_id, in );
        return 0;
    }
    if( start < 0 || start > end ) {
        veejay_msg(0, "Invalid starting position [%d] in '%s'", start, in );
        return 0;
    }
    if( end < 0 || end < start ) {
        veejay_msg(0, "Invalid ending position [%d] in '%s'", end, in );
        return 0;
    }
    if( status < 0 || status > 1 ) {
        veejay_msg(0, "Invalid status value [%d] in '%s'", status, in );
        return 0;
    }

	int values_len = (end-start+1);
	int *values = (int*) vj_calloc( sizeof(int) * values_len);
	if(values == NULL) {
		veejay_msg(0, "Unable to allocate space for keyframe data");
		return 0;
	}

	void *port = NULL;
	if( is_sample ) {
		port = sample_get_kf_port( lookup, fx_entry );
		if( port == NULL ) {
			sample_chain_alloc_kf( lookup, fx_entry );
			port = sample_get_kf_port(lookup,fx_entry);
		}
	} else {
		port = vj_tag_get_kf_port( lookup, fx_entry );
	}

	in += (27);

	unsigned char *ptr = in;
	for(i = start ; i <= end; i ++ )
	{
		int value = 
		  ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );
		//char *key = keyframe_id( parameter_id, i );
		//vevo_property_set( port, key, VEVO_ATOM_TYPE_INT, 1, &value );
		ptr += 4;
		//free(key);
		values[idx] = value;
		idx ++;
	}

	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );
	char *k_d = extract_ ( "data" , parameter_id );
	char *k_dn = extract_( "datalen", parameter_id );

	vevo_property_set( port, k_s, VEVO_ATOM_TYPE_INT,1, &start );
	vevo_property_set( port, k_e, VEVO_ATOM_TYPE_INT,1, &end );
	vevo_property_set( port, k_t, VEVO_ATOM_TYPE_INT,1, &type );
	vevo_property_set( port, k_x, VEVO_ATOM_TYPE_INT,1, &status );
	vevo_property_set( port, k_d, VEVO_ATOM_TYPE_VOIDPTR, 1, &values);
	vevo_property_set( port, k_dn, VEVO_ATOM_TYPE_INT,1, &values_len);

	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
		
	*entry = fx_entry;

    veejay_msg(VEEJAY_MSG_DEBUG, "Stored FX anim data %d - %d (status %d, %d values)",
            start,end,status, values_len );

	return 1;
}

int		keyframe_get_tokens( void *port, int parameter_id, int *start, int *end, int *type, int *status )
{
	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );

	if( vevo_property_get( port, k_s, 0, start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x);
			return 0;
	}
	if( vevo_property_get( port, k_e,   0, end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x);
			return 0;
	}
	if( vevo_property_get( port, k_t,   0, type ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x);
			return 0;
	}
	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);

	return 1;
}

int keyframe_xml_pack( xmlNodePtr node, void *port, int parameter_id  )
{
	int i;
	int start = 0, end = 0, type = 0, status = 0, values_len = 0;
	int *values = NULL;
	
	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );
	char *k_x = extract_ ( "status", parameter_id );
	char *k_d = extract_ ( "data", parameter_id );
	char *k_dn = extract_ ( "datalen", parameter_id );

	if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_x,   0, &status ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_d,   0, &values ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}
	if( vevo_property_get( port, k_dn,   0, &values_len ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t); free(k_x); free(k_d); free(k_dn);
			return 0;
	}

	put_xml_int( node, k_s, start );
	put_xml_int( node, k_e, end );
	put_xml_int( node, k_t, type );
	put_xml_int( node, k_x, status );
	put_xml_int( node, k_dn, values_len );

	for( i = 0; i < values_len ; i ++ ) 
	{
		char xmlbuf[128];
		snprintf((char*)xmlbuf,sizeof(xmlbuf), "%d %d", parameter_id,values[i] );
		put_xml_str( node, "value", xmlbuf );
	}

	free(k_s);
	free(k_e);
	free(k_t);
	free(k_x);
	free(k_d);
	free(k_dn);

	return 1;
}

int		keyframe_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *port )
{
	int start = 0 , end = 0, type = 0, status = 0, values_len = 0;
	int frame = 0;
	int nodes = 0;
	int *values = NULL;
	if(!node)
		return 0;

	xmlNodePtr n = node;
	while( n != NULL )
	{
 		if ( !xmlStrncmp(n->name, (const xmlChar*) "datalen", 7 ))
		{
			values_len = get_xml_int(doc,n);
			vevo_property_set(port, (char*)n->name, VEVO_ATOM_TYPE_INT,1,&values_len);

			if(values != NULL ) {
				free(values);
			}

			values = (int*) vj_calloc(sizeof(int) * values_len );
		}
		n = n->next;
	}

	while( node != NULL )
	{
		if( !xmlStrncmp(node->name, (const xmlChar*) "start",4 ))
		{
			start = get_xml_int( doc, node );
			vevo_property_set( port, (char*) node->name, VEVO_ATOM_TYPE_INT,1,&start);
			nodes ++;
		}
		else if ( !xmlStrncmp(node->name, (const xmlChar*) "end",3 ))
		{
			end = get_xml_int( doc, node );
			vevo_property_set(port, (char*)node->name, VEVO_ATOM_TYPE_INT,1,&end);
		}
		else if ( !xmlStrncmp(node->name, (const xmlChar*) "type",4 ))
		{	
			type = get_xml_int(doc,node);
			vevo_property_set(port,(char*)node->name, VEVO_ATOM_TYPE_INT,1,&type);
		}
		else if ( !xmlStrncmp(node->name, (const xmlChar*) "status", 6))
		{
			status = get_xml_int(doc,node);
			vevo_property_set(port, (char*)node->name, VEVO_ATOM_TYPE_INT,1,&status);
		}
		else if ( !xmlStrcmp(node->name, (const xmlChar*) "value" ))
		{
			int val = 0;
			int pid = get_xml_2int( doc, node, &val);

			if( values == NULL ) {
				veejay_msg(0,"Invalid KF tree, datalen is not (yet) found");
			}
			else {
				// There is no node for data
				char *k_d = extract_ ( "data", pid );
				int *tmp = NULL;
				if( vevo_property_get( port, k_d, 0, &tmp ) != VEVO_NO_ERROR ) {
					vevo_property_set(port, k_d, VEVO_ATOM_TYPE_VOIDPTR,1, &values );
				}
				    

				values[ frame ] = val;
				frame ++;
				if( frame > end )
					end = frame;
			}
		}	

		node = node->next;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "KF loaded: %d-%d with %d values",start,end, frame );

	return nodes;
}

int	get_keyframe_value(void *port, int n_frame, int parameter_id, int *result )
{
	char *k_x = extract_ ( "status", parameter_id );
	char *k_s = extract_ ( "start", parameter_id );
    char *k_e = extract_ ( "end", parameter_id );
	char *k_d = extract_ ( "data", parameter_id );
	int status= 0,start=0,end=0;
	if( vevo_property_get( port, k_x,   0, &status ) != VEVO_NO_ERROR )
	{
		free(k_x); free(k_s); free(k_d); free(k_e);
		return 0;
	}
	if( vevo_property_get( port, k_s,   0, &start ) != VEVO_NO_ERROR )
	{
		free(k_x); free(k_s); free(k_d); free(k_e);
		return 0;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
		free(k_x); free(k_s); free(k_d); free(k_e);
		return 0;
	}

	if( status == 0 ) {
		free(k_x);
		free(k_s);
		free(k_d);
        free(k_e);
		return 0;
	}
	

    if( n_frame < start || n_frame > end ) {
        free(k_x); free(k_s); free(k_d); free(k_e);
        return 0;
    }

	int *values = NULL;
	if( vevo_property_get( port, k_d, 0, &values ) != VEVO_NO_ERROR ) 
	{
		free(k_x); free(k_s); free(k_d); free(k_e);
		return 0;
	}

    int idx = n_frame - start;
    int max = end - start;
    if( idx < 0||idx > max ) {
        free(k_x); free(k_s); free(k_d); free(k_e);
        veejay_msg(VEEJAY_MSG_DEBUG, "KF position %d is not within bounds %d-%d", n_frame, start,end );
        return 0;
    } 

	*result = values[ idx ];

	free(k_x);
	free(k_s);
	free(k_d);

	return 1;
}

