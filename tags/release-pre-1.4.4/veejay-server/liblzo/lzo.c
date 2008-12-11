/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdint.h>
#include <libvje/vje.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <liblzo/lzo.h>

#include <libyuv/yuvconv.h>
#include <liblzo/lzoconf.h>
#include <liblzo/minilzo.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
typedef struct
{
	lzo_byte *wrkmem;
	uint8_t  *tmp[3];
} lzot;

void	lzo_free( void *lzo )
{
	lzot *l = (lzot*) lzo;
	if(l)
	{
		if(l->wrkmem)
			free(l->wrkmem);
		if(l->tmp[0])
			free(l->tmp[0]);
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
	veejay_msg(VEEJAY_MSG_DEBUG,"LZO real-time data compression library (v%s, %s).",
            lzo_version_string(), lzo_version_date());

	l->tmp[0] = NULL;
	l->tmp[1] = NULL;
	l->tmp[2] = NULL;

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
#ifdef STRICT_CHECKING
	assert( len[0] > 0 && len[1] > 0 && len[2] > 0 );
#endif

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

long		lzo_decompress422into420( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[3], int w, int h )
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
veejay_msg(0,"%s",__FUNCTION__);

	if( l->tmp[0] == NULL ) {
		l->tmp[0] = vj_malloc(sizeof(uint8_t) * w * h * 3); // will do
		l->tmp[1] = l->tmp[0] + w * h;
		l->tmp[2] = l->tmp[1] + ( (w>>1)*h);
	}

#ifdef STRICT_CHECKING
	assert( len[0] > 0 && len[1] > 0 && len[2] > 0 );
#endif

	for( i = 0; i <= 2; i ++ )
	{
		const lzo_bytep src = (lzo_bytep) (linbuf+12+offset);
		int r = lzo1x_decompress( src, len[i], l->tmp[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK )
			return 0;
		sum += result_len;
		offset += len[i];
	}

	veejay_memcpy( dst[0], l->tmp[0], w*h);
	yuv422to420planar( l->tmp, dst, w, h );

	return (long)sum;
}
long		lzo_decompress420into422( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[3], int w, int h )
{
	int i;
	lzo_uint len[3] = { 0,0,0};
	int sum = 0;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;
	lzo_uint offset = 0;
	veejay_msg(0,"%s",__FUNCTION__);

	len[0] = str2ulong( linbuf );
	len[1] = str2ulong( linbuf+4 );
	len[2] = str2ulong( linbuf+8 );

	if( l->tmp[0] == NULL ) {
		l->tmp[0] = vj_malloc(sizeof(uint8_t) * w * h * 3); // will do
		l->tmp[1] = l->tmp[0] + ( w * h );
		l->tmp[2] = l->tmp[1] + ( (w>>1) * (h>>1));
	}

#ifdef STRICT_CHECKING
	assert( len[0] > 0 && len[1] > 0 && len[2] > 0 );
#endif

	for( i = 0; i <= 2; i ++ )
	{
		const lzo_bytep src = (lzo_bytep) (linbuf+12+offset);
		int r = lzo1x_decompress( src, len[i], l->tmp[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK )
			return 0;
		sum += result_len;
		offset += len[i];
	}
	veejay_memcpy( dst[0], l->tmp[0], w*h);

	yuv420to422planar( l->tmp, dst, w, h );

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

