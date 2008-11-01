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
#ifndef VJXMLUTIL
#define VJXMLUTIL

static	double	property_as_double( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	char *str = NULL;
	double value = 0.0;
	str = (char*) xmlGetProp( cur, key );
	if(!str)
		return 0.0;
	value = strtod( str, NULL );
	xmlFree(str);
	return value;
}

static	int	property_as_int( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	char *str = NULL;
	int value = 0;
	str = (char*) xmlGetProp( cur, key );
	if(!str)
		return 0;
	value = atoi(str);
	xmlFree(str);
	return value;
}

static	char	*property_as_cstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	char *str = NULL;
	char *ret = NULL;
	str = (char*) xmlGetProp( cur, key );
	if(!str)
		return NULL;
	ret = strdup( str );
	xmlFree(str);
	return ret;
}

static	char	*value_as_cstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key)
{
	xmlChar *tmp = xmlNodeListGetString( doc, cur, 1 );
	char *ret = NULL;
	if(!tmp)
		return NULL;
	tmp = strdup( (char*)tmp) ;
	xmlFree(tmp);
	return ret;
}

static	double	value_as_double( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key)
{
	xmlChar *tmp = xmlNodeListGetString( doc,cur,1 );
	double ret = 0.0;
	if(tmp)
	{
		sscanf( (char*)tmp, "%g", &ret );
		xmlFree(tmp);
	}
	return ret;
}

static	int	value_as_int( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *key )
{
	xmlChar *tmp = xmlNodeListGetString( doc,cur,1 );
	int ret = 0.0;
	if(tmp)
	{
		sscanf( (char*)tmp, "%d", &ret ); 
		xmlFree(tmp);
	}
	return ret;
}


static	void	int_as_value( xmlNodePtr node, int value, const char *xml_key )
{
	char buffer[100];
	sprintf(buffer, "%d", value );
	xmlNewChild( node, NULL, (const xmlChar*) xml_key, (const xmlChar*) buffer );
}

static	void	double_as_value( xmlNodePtr node, double value, const char *xml_key )
{
	char buffer[100];
	sprintf( buffer, "%g", value );
	xmlNewChild( node, NULL, (const xmlChar*) xml_key, (const xmlChar*) buffer );
}

static	void	cstr_as_value( xmlNodePtr node, char *str, const char *xml_key )
{
	xmlNewChild( node, NULL, (const xmlChar*) xml_key, (const xmlChar*) str );
}

static	void	int_as_property( xmlNodePtr node, int value, const char *xml_key )
{
	char buffer[100];
	sprintf(buffer, "%d", value );
	xmlSetProp( node, (const xmlChar*) xml_key, (const xmlChar*) buffer );
}

static	void	null_as_property( xmlNodePtr node, const char *xml_key )
{
	xmlNewChild( node, NULL, (const xmlChar*) xml_key, (const xmlChar*) NULL );
}

static	void	double_as_property( xmlNodePtr node, double value, const char *xml_key )
{
	char buffer[100];
	sprintf(buffer, "%g", value );
	xmlSetProp( node, (const xmlChar*) xml_key, (const xmlChar*) buffer );
}

static	void	cstr_as_property( xmlNodePtr node, char *str, const char *xml_key )
{
	xmlSetProp( node, (const xmlChar*) xml_key, (const xmlChar*) str );
}
#endif
