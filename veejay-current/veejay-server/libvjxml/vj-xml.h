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
#ifndef VJ_XML_H
#define VJ_XML_H
int	get_xml_int( xmlDocPtr doc, xmlNodePtr node );
char 	*get_xml_str( xmlDocPtr doc, xmlNodePtr node );
void	get_xml_str_n( xmlDocPtr doc, xmlNodePtr node, char *val, size_t len );
float	get_xml_float( xmlDocPtr doc, xmlNodePtr node );
void 	get_xml_3int( xmlDocPtr doc, xmlNodePtr node, int *first , int *second, int *third );
int	get_xml_2int( xmlDocPtr doc, xmlNodePtr node, int *second );
void	put_xml_int( xmlNodePtr node, char *name, int value );
void	put_xml_str( xmlNodePtr node, char *name, char *value );
void	put_xml_float( xmlNodePtr node, char *name, float value );
#endif
