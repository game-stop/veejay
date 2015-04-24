/* veejay - Linux VeeJay
 *           (C) 2002-2013 Niels Elburg <nwelburg@gmail.com> 
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
#include <liblzo/minilzo.h>
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
/*
void lzo_print_buf( uint8_t *buf, int len )
{
	int i;
	printf("------ %p, %d\n", buf, len );
	printf("line     : ");
	for( i = 1; i < 32; i ++ ) {
		printf(". ");
	}
	printf("\n");
	for(i = 0; i < len ; i ++ ) {
		if( (i%32) == 0 )
			printf("\nline %04d: ",i);
		printf("%02x", buf[i] );
	}
	printf("\n");
}
static int lzo_verify_compression(uint8_t *in, int in_len, uint8_t *out , lzo_uint *out_lenptr, uint8_t *wrkmem)
{
	lzo_uint out_len = 0;
	int r = lzo1x_1_compress( in, in_len, out, &out_len, wrkmem );
	if( r == LZO_E_OK ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "LZO (test) Compressed %lu bytes into %lu bytes\n",
				(unsigned long) in_len, (unsigned long) out_len );
		*out_lenptr = out_len;
	} else {
		veejay_msg(VEEJAY_MSG_ERROR, "LZO (test) Compression error: %d", r );
		return 0;
	}

	if( out_len >= in_len ) {
		veejay_msg(VEEJAY_MSG_ERROR, "LZO (test) Block contains incompressible data.");
		return 0;
	}

	lzo_uint new_len = in_len;
	r = lzo1x_decompress( out, out_len, in, &new_len, NULL );
	if( r == LZO_E_OK && new_len == in_len ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "LZO (test) Decompressed %lu (%lu) bytes back into %lu bytes\n",
				(unsigned long) *out_lenptr,out_len, (unsigned long) in_len );
		return 1;
	}

	veejay_msg(VEEJAY_MSG_ERROR, "LZO (test) Decompression error: %d", r );

	return 0;

}
*/
#define LZO_ALIGN_SIZE(size) \
	 ( ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ) * sizeof(lzo_align_t)

void	*lzo_new( )
{
	lzot *l = (lzot*) vj_calloc(sizeof(lzot));	
	if (lzo_init() != LZO_E_OK)
   	{
		veejay_msg(VEEJAY_MSG_ERROR, "LZO Unable to initialize. Could be buggy compiler?");
		free( l );
		return NULL;
	}

	l->wrkmem = (lzo_bytep)
		vj_calloc( LZO_ALIGN_SIZE( LZO1X_1_MEM_COMPRESS ) );

	if(l->wrkmem == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "LZO Cannot allocate work memory for LZO1X_1");
		if(l) free(l);
		return NULL;
	}


/*
	uint8_t in[16384];
	uint8_t out[16834];

	veejay_memset( in, 1, sizeof(in) );
	veejay_memset( out, 0, sizeof(out));

	lzo_bytep inp = (lzo_bytep) &in[0];
	lzo_bytep outp = (lzo_bytep) &out[0];

	lzo_uint out_len = 0;
	int lzo_verify_compression_result = lzo_verify_compression( inp, sizeof(in), outp, &out_len, l->wrkmem );
       
	if( lzo_verify_compression_result == 1 ) {
		int i;
		for( i = 0; i < sizeof(in); i ++ ) { 
			if( in[i] != 1 )  //decompression back into 'in'
				veejay_msg(VEEJAY_MSG_ERROR, "LZO verify error at byte pos %d.", i );
	
		}

		veejay_msg(VEEJAY_MSG_DEBUG, "LZO verified compression algorithms successfully.");
	}
	*/

	veejay_msg(VEEJAY_MSG_DEBUG,"LZO real-time data compression library (v%s, %s) enabled.",
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

long		lzo_compress( void *lzo, uint8_t *src, uint8_t *plane, unsigned int *size, int ilen )
{
	lzo_uint src_len = ilen;
	lzot *l = (lzot*) lzo;
	lzo_bytep dst = plane;
	lzo_voidp wrkmem = l->wrkmem;
	int r = lzo1x_1_compress( src, src_len, dst, (lzo_uint*) size, wrkmem );
	if( r != LZO_E_OK || r < 0 )
	{
		veejay_msg(0, "LZO Compression error: %d", r );
		return 0;
	}

	long res = (long) (*size);

	return res;
}

long		lzo_decompress_el( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[3], int uv_len)
{
	unsigned int i;
	lzo_uint len[3] = { 0,0,0};
	unsigned int sum = 0;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;
	lzo_uint offset = 12;

	len[0] = str2ulong( linbuf );
	len[1] = str2ulong( linbuf+4 );
	len[2] = str2ulong( linbuf+8 );

	if(len[0] == 0 && len[1] == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error in MLZO header", linbuf );
		return -1;
	}

	for( i = 0; i < 3; i ++ )
	{
		if( len[i] <= 0 ) { 
			continue;
		}
		
		const lzo_bytep src = (lzo_bytep) (linbuf+offset);
		
		int r = lzo1x_decompress( src, len[i], dst[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK || r < 0 )
			return 0;
		sum += result_len;
		offset += len[i];

		result_len = 0;
	}

	return (long)sum;
}

long		lzo_decompress( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[3], int uv_len, 
	       		uint32_t stride1, uint32_t stride2, uint32_t stride3	)
{
	unsigned int i;
	lzo_uint len[3] = { 0,0,0};
	unsigned int mode = 0;
	unsigned int sum = 0;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;
	lzo_uint offset = 16;
	
	len[0] = str2ulong( linbuf );
	len[1] = str2ulong( linbuf+4 );
	len[2] = str2ulong( linbuf+8 );
	mode   = str2ulong( linbuf+12 );

	if( len[0] != stride1 || len[1] != stride2 || len[2] != stride3 ) {
		veejay_msg(0, "LZO received corrupted packet.");
		return 0;
	}

	len[0] = stride1;
	len[1] = stride2;
	len[2] = stride3;

	if(len[1] ==0 && len[2] == 0 )
		mode = 1;

	for( i = 0; i < 3; i ++ )
	{
		if( len[i] <= 0 ) 
			continue;

	//	const lzo_bytep src = (lzo_bytep) (linbuf+offset);

		int r = lzo1x_decompress( linbuf+offset, len[i], dst[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK )
			return 0;
		sum += result_len;
		offset += len[i];

		result_len = 0;
	}

	if(mode == 1) {
		veejay_memset( dst[1], 128, uv_len );
		veejay_memset( dst[2], 128, uv_len );
	}

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

	len[0] = str2ulong( linbuf );
	len[1] = str2ulong( linbuf+4 );
	len[2] = str2ulong( linbuf+8 );
	
	if( l->tmp[0] == NULL ) {
		l->tmp[0] = vj_malloc(sizeof(uint8_t) * w * h * 2); // will do
		l->tmp[1] = l->tmp[0] + ( w * h );
		l->tmp[2] = l->tmp[1] + ( (w>>1) * (h>>1));
	}

	for( i = 0; i < 3; i ++ )
	{
		if( len[i] <= 0 )
			continue;
		const lzo_bytep src = (lzo_bytep) (linbuf+offset);
		int r = lzo1x_decompress( src, len[i], l->tmp[i], &result_len, l->wrkmem );
		if( r != LZO_E_OK )
			return 0;
		sum += result_len;
		offset += len[i];
	}
	
	vj_frame_copy1( l->tmp[0], dst[0], w*h);
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

