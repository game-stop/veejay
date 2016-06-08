/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libvjxml/vj-xml.h>
#include <libvjmem/vjmem.h>
static char *UTF8toLAT1(unsigned char *in)
{
	if (in == NULL)
		return NULL;

	int in_size = strlen( (char*) in ) + 1;
	int out_size = in_size;
	unsigned char *out = malloc((size_t) out_size);

	if (out == NULL) {
		return NULL;
	}

	if (UTF8Toisolat1(out, &out_size, in, &in_size) != 0)
		veejay_memcpy( out, in, out_size );
	out = realloc(out, out_size + 1);
	out[out_size] = 0;	

	return (char*) out;
}


int    get_xml_int( xmlDocPtr doc, xmlNodePtr node )
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

char 	*get_xml_str( xmlDocPtr doc, xmlNodePtr node )
{
	xmlChar *xmlTemp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	char *chTemp = UTF8toLAT1(xmlTemp);
	return chTemp;
}

void	get_xml_str_n( xmlDocPtr doc, xmlNodePtr node, char *val, size_t len )
{
	xmlChar *xmlTemp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	char *chTemp = UTF8toLAT1(xmlTemp);
	strncpy( val, chTemp, len );
}

float	get_xml_float( xmlDocPtr doc, xmlNodePtr node )
{
	xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
        char *ch = UTF8toLAT1( tmp );
        float val = 0.0f;
        if( ch )
        {
		sscanf( ch, "%f", &val );
                free(ch);
        }
        if(tmp)
                free(tmp);
        return val;
}

void 	get_xml_3int( xmlDocPtr doc, xmlNodePtr node, int *first , int *second, int *third )
{
        xmlChar *tmp = xmlNodeListGetString( doc, node->xmlChildrenNode, 1 );
        char *ch = UTF8toLAT1( tmp );
        if( ch )
        {
                sscanf( ch, "%d %d %d" , first, second, third );
                free(ch);
        }
        if(tmp)
                free(tmp);
}

int	get_xml_2int( xmlDocPtr doc, xmlNodePtr node, int *second )
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

void	put_xml_int( xmlNodePtr node, char *key, int value )
{
	const xmlChar *name = (const xmlChar*) key;
	char buffer[64];
	snprintf(buffer,sizeof(buffer),"%d", value );
	xmlNewChild( node, NULL, name, (const xmlChar*) buffer );
}

void	put_xml_str( xmlNodePtr node, char *key, char *value )
{
	const xmlChar *name = (const xmlChar*) key;
	xmlNewChild( node, NULL, name, (const xmlChar*) value );
}

void	put_xml_float( xmlNodePtr node, char *key, float value )
{
	const xmlChar *name = (const xmlChar*) key;
	char buffer[64];
	snprintf(buffer,sizeof(buffer),"%f", value );
	xmlNewChild( node, NULL, name, (const xmlChar*) buffer );
}

