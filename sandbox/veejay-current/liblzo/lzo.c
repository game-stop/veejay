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
#include <liblzo/lzoconf.h>
#include <liblzo/minilzo.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <liblzo/lzo.h>

typedef struct
{
	lzo_byte *wrkmem;
} lzot;

void	lzo_free( void *lzo )
{
	lzot *l = (lzot*) lzo;
	if(l)
	{
		if(l->wrkmem)
			free(l->wrkmem);
		free(l);
	}
	l = NULL;
}

void	*lzo_new( )
{
	lzot *l = (lzot*) vj_calloc(sizeof(lzot));	
	if (lzo_init() != LZO_E_OK)
   	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize LZO. Could be buggy compiler");
		free( l );
		return NULL;
	}

	l->wrkmem = (lzo_bytep)
		vj_malloc( LZO1X_1_MEM_COMPRESS );

	if(l->wrkmem == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Cannot allocate work memory for LZO1X_1");
		if(l) free(l);
		return NULL;
	}
	veejay_msg(VEEJAY_MSG_INFO,"LZO real-time data compression library (v%s, %s).",
            lzo_version_string(), lzo_version_date());

	return (void*) l;	
}

static uint32_t str2ulong(unsigned char *str)
{
   return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

int		lzo_compress( void *lzo, uint8_t *src, uint8_t *plane, unsigned int *size, int ilen )
{
	lzo_uint out_len =0;
	lzo_uint len = ilen;
	lzot *l = (lzot*) lzo;
	lzo_bytep dst = plane;
	lzo_uintp dst_len = (lzo_uintp) size;
	lzo_voidp wrkmem = l->wrkmem;
	int r = lzo1x_1_compress( src, len, dst, dst_len, l->wrkmem );
	if( r != LZO_E_OK )
		return 0;
	//@ dont care about incompressible blocks
	return (*size);	
}

long		lzo_decompress( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[3] )
{
	int i;
	lzo_uint len[3] = { 0,0,0};
	int sum = 0;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;
	lzo_uint offset = 0;
	
	len[0] = str2ulong( linbuf );
	len[1] = str2ulong( linbuf+4 );
	len[2] = str2ulong( linbuf+8 );

	for( i = 0; i <= 2; i ++ )
	{
		const lzo_bytep src = (lzo_bytep) (linbuf+12+offset);
		int r = lzo1x_decompress( src, len[i], dst[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK )
			return 0;
		sum += result_len;
		offset += len[i];
	}
	return (long)sum;
}



long		lzo_decompress2( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst )
{
	lzo_uint len = linbuf_len;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;

	const lzo_bytep src = (lzo_bytep) linbuf;
	int r = lzo1x_decompress( src, len, dst, &result_len, l->wrkmem );
	
	if( r != LZO_E_OK )
		return 0;
	return (long)result_len;
}

