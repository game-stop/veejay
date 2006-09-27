/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <glib.h>
typedef struct
{
	char *buffer;
	char *tmp;
	int   size;
	int   consumed;
} blob_t;

void	*blob_new(int size)
{
	blob_t *blob = (blob_t*) malloc(sizeof(blob_t));
	blob->size = size;
	blob->consumed = 0;
	blob->buffer = (char*) malloc(sizeof(char) * size);
	memset(blob->buffer,0, size );
	blob->tmp    = (char*) malloc( sizeof(char) * 1024 );
	return (void*) blob;
}

int	blob_printf( void *blob, const char format[], ... )
{
	blob_t *b = (blob_t*) blob;
	memset( b->tmp,0,1024 );

	va_list args;
	va_start( args, format );
	vsnprintf( b->tmp,1023, format,args );
	va_end(args);
	int len = strlen( b->tmp );

	if( ((b->size - b->consumed) - len ) > 0 )
	{
		gsize br = 0;
		gsize bw = 0;
		GError *error = NULL;
		//strncat( b->buffer, b->tmp, len );
		char *utf8_str = g_locale_to_utf8(
				b->tmp,
				len,
				&br,
				&bw,
				&error );
		strncat( b->buffer, utf8_str, bw );
		g_free(utf8_str);
		b->consumed += bw;
		return 1;
	}	
	return 0;
}

void	blob_wipe(void *data)
{
	blob_t *b = (blob_t*) data;
	memset( b->buffer, 0, b->size );
	b->consumed = 0;	
}

int	blob_get_buffer_size( void *blob )
{
	blob_t *b = (blob_t*) blob;
	return b->consumed;
}

char	*blob_get_buffer( void *blob )
{
	blob_t *b = (blob_t*) blob;
	return b->buffer;
}

void	blob_free( void *blob )
{
	blob_t *b = (blob_t*) blob;
	free(b->tmp);
	free(b->buffer);
	free(b);
	b = NULL;
}

