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
#ifndef VJKF_H
#define VJKF_H
unsigned char *keyframe_pack( void *port, int parameter_id, int entry_id, int *rlen );

int		keyframe_unpack( unsigned char *in, int len, int *entry, int lookup, int tag );

int		keyframe_get_tokens( void *port, int parameter_id, int *start, int *end, int *type );

void	keyframe_clear_entry( int lookup, int fx_entry, int parameter_id, int is_sample );

int 	keyframe_xml_pack( xmlNodePtr node, void *port, int parameter_id  );

int		keyframe_xml_unpack( xmlDocPtr doc, xmlNodePtr node, void *port );

int	get_keyframe_value(void *port, int n_frame, int parameter_id, int *result );
#endif
