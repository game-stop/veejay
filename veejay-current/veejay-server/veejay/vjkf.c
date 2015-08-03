/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2008 Niels Elburg < elburg@hio.hen.nl>
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
/* veejay server stores keyframes 
 *
 *
 *
 * keyframe format:
 *       ( [frame_num1][value1][frame_num2][value2] ... [frame_numN][valueN] )
 *
 *
 */

#include <veejay/vjkf.h>

static	char	*keyframe_id( int p_id, int n_frame )
{
	char tmp[32];
	snprintf(tmp,sizeof(tmp), "FX%d_%d",p_id,n_frame );
	return vj_strdup(tmp);
}

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


	int start = 0, end = 0, type =0;

	char *k_s = extract_( "start", parameter_id );
	char *k_e = extract_( "end",   parameter_id );
	char *k_t = extract_( "type",  parameter_id );

	if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return NULL;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return NULL;
	}
	if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return NULL;
	}

	free(k_s);
	free(k_e);
	free(k_t);

	int len = end - start;

	result = vj_calloc( (len*4) + 64 );

	sprintf( (char*) result,"key%02d%02d%08d%08d%02d", entry_id,parameter_id,start, end, type );

	unsigned char *out = result + 25;

	for( i = start; i < end; i ++ )
	{
		char *key = keyframe_id( parameter_id, i );
		int value = 0;

		if(vevo_property_get(port, key, 0, &value )==VEVO_NO_ERROR)
		{
			unsigned char *buf = out + (4 * k);
		
			buf[0] = ( value ) & 0xff;
			buf[1] = (value >> 8) & 0xff;
			buf[2] = (value >> 16) & 0xff;
			buf[3] = (value >> 24) & 0xff;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_WARNING, "No keyframe at position %d", i );
		}
		k++;
		
	/*	else
		{
			unsigned char *buf = out + (4 * k);
			buf[0] = 0;
			buf[1] = 0;
			buf[2] = 0;
			buf[3] = 0;
			k++;
		}*/

		free(key);
	}

	*rlen = 25 + (4 *  k);

	veejay_msg(VEEJAY_MSG_DEBUG, "KF %p pack: range=%d-%d, FX entry %d, P%d, type %d",
		port,start,end, entry_id,parameter_id, type );

	return result;
}

void	keyframe_clear_entry( int lookup, int fx_entry, int parameter_id, int is_sample )
{
	int start = 0;
	int end = 0;
	int type = 0;
	int i;

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
	
	vevo_property_get( port, k_s, 0, &start );
	vevo_property_get( port, k_e,   0, &end );
	vevo_property_get( port, k_t,   0,&type );

	for(i = start ; i <= end; i ++ )
	{
		char *key = keyframe_id( parameter_id, i );
		vevo_property_del( port, key );
		free(key);
	}

	vevo_property_del( port, k_s );
	vevo_property_del( port, k_e );
	vevo_property_del( port, k_t );

	free(k_s);
	free(k_e);
	free(k_t);
}

int		keyframe_unpack( unsigned char *in, int len, int *entry, int lookup, int is_sample )
{
	int i;
	int parameter_id = 0;
	int start = 0, end = 0, type = 0;
	int fx_entry = 0;
	int n = sscanf( (char*) in, "key%2d%2d%8d%8d%2d", &fx_entry,&parameter_id, &start, &end,&type );
	
	if(n != 5 )
	{
		veejay_msg(0, "Unable to unpack parameter_id,start,end");
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

	in += (25);

	unsigned char *ptr = in;
	for(i = start ; i <= end; i ++ )
	{
		int value = 
		  ( ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24) );
		char *key = keyframe_id( parameter_id, i );
		vevo_property_set( port, key, VEVO_ATOM_TYPE_INT, 1, &value );
		ptr += 4;
		free(key);
	}

	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );

	vevo_property_set( port, k_s, VEVO_ATOM_TYPE_INT,1, &start );
	vevo_property_set( port, k_e, VEVO_ATOM_TYPE_INT,1, &end );
	vevo_property_set( port, k_t, VEVO_ATOM_TYPE_INT,1, &type );

	free(k_s);
	free(k_e);
	free(k_t);

	*entry = fx_entry;

	return 1;
}

int		keyframe_get_tokens( void *port, int parameter_id, int *start, int *end, int *type )
{
	char *k_s = extract_ ( "start", parameter_id );
	char *k_e = extract_ ( "end", parameter_id );
	char *k_t = extract_ ( "type", parameter_id );

	if( vevo_property_get( port, k_s, 0, start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return 0;
	}
	if( vevo_property_get( port, k_e,   0, end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return 0;
	}
	if( vevo_property_get( port, k_t,   0, type ) != VEVO_NO_ERROR )
	{
		free(k_s); free(k_e); free(k_t);
		return 0;
	}
	free(k_s);
	free(k_e);
	free(k_t);


	return 1;
}




int keyframe_xml_pack( xmlNodePtr node, void *port, int parameter_id  )
{
	int i;

	int start = 0, end = 0, type = 0;

        char *k_s = extract_ ( "start", parameter_id );
        char *k_e = extract_ ( "end", parameter_id );
        char *k_t = extract_ ( "type", parameter_id );


	if( vevo_property_get( port, k_s, 0, &start ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return 0;
	}
	if( vevo_property_get( port, k_e,   0, &end ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e); free(k_t);
			return 0;
	}
	if( vevo_property_get( port, k_t,   0, &type ) != VEVO_NO_ERROR )
	{
			free(k_s); free(k_e);free(k_t);
			return 0;
	}

	unsigned char xmlbuf[100];

	snprintf((char*)xmlbuf, 100,"%d", start );
	xmlNewChild(node, NULL, (const xmlChar*) k_s, xmlbuf );
	snprintf((char*)xmlbuf, 100,"%d", end );
	xmlNewChild(node, NULL, (const xmlChar*) k_e, xmlbuf );
	snprintf((char*)xmlbuf, 100,"%d", type );
	xmlNewChild(node, NULL, (const xmlChar*) k_t, xmlbuf );

	for( i = start; i < end; i ++ )
	{
		char *key = keyframe_id( parameter_id, i );
		int value = 0;

		if(vevo_property_get(port, key, 0, &value )==VEVO_NO_ERROR)
		{
			sprintf((char*)xmlbuf, "%d %d", parameter_id,value );
			xmlNewChild(node, NULL, (const xmlChar*) "value", xmlbuf);	
		}
		free(key);
	}

	free( k_s);
	free( k_e);
	free( k_t);

	return 1;
}
static	 int	get_xml_int( xmlDocPtr doc, xmlNodePtr node )
{
	xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
	char *ch = UTF8toLAT1( tmp );
	int res = 0;
	if( ch )
	{
		res = atoi( ch );
		free(ch);
	}
	if(tmp)
		free(tmp);
	return res;
}
static	 int	get_xml_2int( xmlDocPtr doc, xmlNodePtr node, int *second )
{
	xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
	char *ch = UTF8toLAT1( tmp );
	int res = 0;
	if( ch )
	{
		sscanf( ch, "%d %d", &res, second );
		free(ch);
	}
	if(tmp)
		free(tmp);
	return res;
}

int		keyframe_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *port )
{
	int start = 0 , end = 0, type = 0;
	int frame = 0;
	int nodes = 0;
	if(!node)
		return 0;

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
		else if ( !xmlStrcmp(node->name, (const xmlChar*) "value" ))
		{
			int val = 0;
			int pid = get_xml_2int( doc, node, &val);
			char *key = keyframe_id( pid, start + frame );


			vevo_property_set( port, key, VEVO_ATOM_TYPE_INT, 1, &val );
			free(key);

			frame ++;
			if( frame > end )
				end = frame;
		}

		node = node->next;
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "KF loaded: %d-%d with %d values",start,end, frame );

	return nodes;
}


int	get_keyframe_value(void *port, int n_frame, int parameter_id, int *result )
{
	char *key = keyframe_id( parameter_id, n_frame );
	
	int error = vevo_property_get( port, key, 0, result );
	if( error != VEVO_NO_ERROR ) {
		free(key);
		return 0;
	}
	
	free(key);

	return 1;
}

