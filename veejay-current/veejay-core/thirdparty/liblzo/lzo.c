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
#include <veejaycore/defs.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <liblzo/lzo.h>
#include <libyuv/yuvconv.h>
#include <liblzo/minilzo.h>

#ifdef HAVE_ARM_ASIMD
#include <arm_neon.h>
#include <arm_sve.h>
#endif
#if defined (__SSE2__) || defined(__SSE4_2__) || defined(_SSE4_1__)
#include <immintrin.h>
#endif
#include <string.h>

typedef struct
{
	lzo_align_t *wrkmem;
	int      pixfmt;
	void *scaler;
	VJFrame *out_frame;
	VJFrame *in_frame;
	uint8_t *compr_buff[3];
} lzot;

void	lzo_free( void *lzo )
{
	lzot *l = (lzot*) lzo;
	if(l)
	{
		if( l->compr_buff[0] ) {
			free(l->compr_buff[0]);
		}
		if( l->in_frame ) {
			if( l->in_frame->data[0] ) 
				free(l->in_frame->data[0] );
			free(l->in_frame);
		}
		if( l->out_frame ) {
			free(l->out_frame);
		}
		if( l->scaler ) {
			yuv_free_swscaler(l->scaler);
		}
		free(l);
	}
	l = NULL;
}

static int lzo_verify_compression(uint8_t *in, int in_len, uint8_t *out , lzo_uint *out_lenptr, lzo_align_t *wrkmem)
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
		veejay_msg(VEEJAY_MSG_ERROR, "LZO (test) Block contains incompressible data");
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

#define LZO_ALIGN_SIZE(size) \
	 ( ((size) + (sizeof(lzo_align_t) - 1)) / sizeof(lzo_align_t) ) * sizeof(lzo_align_t)

void	*lzo_new( int pixfmt, int width, int height, int is_decoder )
{
	lzot *l = (lzot*) vj_calloc(sizeof(lzot));	
	if (lzo_init() != LZO_E_OK)
   	{
		veejay_msg(VEEJAY_MSG_ERROR, "LZO: Failed to initialize. Buggy compiler?");
		free( l );
		return NULL;
	}

	l->out_frame = yuv_yuv_template( NULL,NULL,NULL, width, height, pixfmt );
	l->pixfmt = pixfmt;

	veejay_msg(VEEJAY_MSG_DEBUG, "LZO: alignment size is %zu", LZO_ALIGN_SIZE(1) );

	if(!is_decoder) {

		l->wrkmem = malloc(sizeof(long)*LZO_AL(LZO1X_1_MEM_COMPRESS));
		if(l->wrkmem == NULL )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "LZO Cannot allocate work memory for LZO1X_1");
			free(l->out_frame);
			free(l);
			return NULL;
		}

		l->compr_buff[0] = malloc(LZO_ALIGN_SIZE(l->out_frame->len + l->out_frame->uv_len + l->out_frame->uv_len)) ;
		l->compr_buff[1] = l->compr_buff[0] + l->out_frame->len;
		l->compr_buff[2] = l->compr_buff[1] + l->out_frame->uv_len;

		uint8_t in[16384];
		uint8_t out[16834];

		veejay_memset( in, 128, sizeof(in) );
		veejay_memset( out, 0, sizeof(out));
		
		lzo_bytep inp = (lzo_bytep) &in[0];
		lzo_bytep outp = (lzo_bytep) &out[0];

		lzo_uint out_len = 0;
		int lzo_verify_compression_result = lzo_verify_compression( inp, sizeof(in), outp, &out_len, l->wrkmem );
		if( lzo_verify_compression_result == 1 ) {
			int i;
			for( i = 0; i < sizeof(in); i ++ ) { 
				if( in[i] != 128 )  //decompression back into 'in'
				{
					veejay_msg(VEEJAY_MSG_ERROR, "LZO: Failed to verify compression algorithm");
					free(l->compr_buff[0]);
					free(l->out_frame);
					free(l->wrkmem);
					free(l);
					return NULL;
				}
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "LZO: Verified compression algorithm successfully");
		}
	
		veejay_msg(VEEJAY_MSG_DEBUG,"LZO real-time data compression library (v%s, %s) enabled",
            lzo_version_string(), lzo_version_date());

		free(l->wrkmem);
	}

	
	return (void*) l;	
}

static void long2str(unsigned char *dst, uint32_t n)
{
   dst[0] = (n    )&0xff;
   dst[1] = (n>> 8)&0xff;
   dst[2] = (n>>16)&0xff;
   dst[3] = (n>>24)&0xff;
}

static uint32_t str2ulong(unsigned char *str)
{
   return ( str[0] | (str[1]<<8) | (str[2]<<16) | (str[3]<<24) );
}

static lzo_uint	lzo_compress( void *lzo, uint8_t *src, uint8_t *plane, lzo_uint *size, int ilen )
{
	lzo_uint src_len = ilen;
	lzot *l = (lzot*) lzo;
	lzo_bytep dst = plane;

	l->wrkmem = malloc(sizeof(long)*LZO_AL(LZO1X_1_MEM_COMPRESS));
	if(l->wrkmem == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "LZO Cannot allocate work memory for LZO1X_1");
		return 0;
	}

	lzo_voidp wrkmem = l->wrkmem;
	int r = lzo1x_1_compress( src, src_len, dst, size, wrkmem );

	if( *size > src_len ) {
		veejay_msg(VEEJAY_MSG_ERROR, "LZO plane is not compressible");
		free(l->wrkmem);
		return 0;
	}

	if( r != LZO_E_OK || r < 0 )
	{
		veejay_msg(0, "LZO compression error: %d", r );
		free(l->wrkmem);
		return 0;
	}

	lzo_uint res = (*size);

	free(l->wrkmem);
		
	return res;
}

static	void lzo_frame_copy( lzot *l, uint8_t *data[4], const int len, const int uv_len) {
	
	memcpy( l->compr_buff[0], data[0], len );
	memcpy( l->compr_buff[1], data[1], uv_len); 
	memcpy( l->compr_buff[2], data[2], uv_len );
	
}


static lzo_uint LZO_HEADER_LEN = 16;
long		lzo_compress_frame( void *lzo, VJFrame *frame, uint8_t *alt_data[4], uint8_t *dst ) {
	lzot *l = (lzot*) lzo;

	lzo_uint s1=0,s2=0,s3=0,pf= frame->format;
	lzo_uint *size1 = &s1, *size2=&s2,*size3=&s3;
	lzo_uint res;
	lzo_uint pos = LZO_HEADER_LEN;

	lzo_frame_copy( l, alt_data, frame->len, frame->uv_len );

	uint8_t *compressed_data = dst + pos;
	
/*	res = lzo_compress( l, data[0], compressed_data, size1, frame->len);
	if( res == 0 )
	{
		veejay_msg(0,"LZO: unable to compress Y plane");
		return 0;
	}
	pos += res;
	
	res = lzo_compress( l, data[1], compressed_data + pos, size2 , frame->uv_len );
	if( res == 0 )
	{
		veejay_msg(0,"LZO: unable to compress U plane");
		return 0;
	}
	pos += res;

	res = lzo_compress( l, data[2], compressed_data + pos, size3 , frame->uv_len );
	if( res == 0 )
	{
		veejay_msg(0,"LZO: unable to compress V plane");
		return 0;
	}

	pos += res;
*/
	res = lzo_compress( l, l->compr_buff[0], compressed_data, size1, frame->len + frame->uv_len + frame->uv_len);
	pos += res;

	long2str( dst, pf ); // store pixfmt
	long2str( dst + 4, s1 ); // store compressed length
	long2str( dst + 8, s2 );
	long2str( dst + 12, s3 );
	
	/*veejay_msg(VEEJAY_MSG_DEBUG, "LZO block size: %zu, data size: %zu,%zu,%zu = %zu, offsets=%zu,%zu,%zu ",
		pos, s1, s2, s3, s1 + s2 + s3,
		 LZO_HEADER_LEN,
		 LZO_HEADER_LEN + s1,
		LZO_HEADER_LEN + s1 + s2  );*/

	return pos;
}

static lzo_uint lzo_decompress_plane(lzot *l, uint8_t *linbuf, lzo_uint linbuf_len, int is_direct,uint8_t *dest, lzo_uint expected_length) {
	lzo_uint result_len;
	
	const lzo_bytep src = (lzo_bytep) (linbuf);
	int r = -1;

	if(is_direct)
	{	
		r = lzo1x_decompress( src, linbuf_len, dest, &result_len, l->wrkmem );
	}
	else
	{
		r = lzo1x_decompress( src, linbuf_len, l->in_frame->data[0], &result_len, l->wrkmem );
	}

	if( r != LZO_E_OK || r < 0 ) {
		veejay_msg( VEEJAY_MSG_ERROR, "LZO decompression error %d (%d expanded into %d , missing %d)", r, linbuf_len, result_len,
		 expected_length - result_len);
		return 0;
	}

	return result_len;
}

long		lzo_decompress_el( void *lzo, uint8_t *linbuf, int linbuf_len, uint8_t *dst[4], int width, int height, int pixfmt)
{
	lzo_uint len[4] = { 0,0,0,0};
	lzo_uint expected_length = 0;
	unsigned int sum = 0;
	lzot *l = (lzot*) lzo;
	lzo_uint result_len = 0;
	lzo_uint offset = LZO_HEADER_LEN;
	unsigned int is_direct = 0;
	unsigned int old_format = 0;
	
	len[0] = str2ulong( linbuf );   //pixel format
	len[1] = str2ulong( linbuf+4 ); // length of first plane
	len[2] = str2ulong( linbuf+8 ); // length of second plane
	len[3] = str2ulong( linbuf+12 );// length of third plane

	
	if( len[1] + len[2] + len[3] > linbuf_len || len[1] + len[2] + len[3] <= 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "LZO: Invalid length data in MLZO header");
		return -1;
	}

	if( len[2] > 0 && len[3] > 0 ) {
		old_format = 1;
	}
	
	if( 1 == 1 && width == l->out_frame->width && height == l->out_frame->height &&
	    pixfmt == l->out_frame->format ) {
		is_direct = 1;
	}

	if( l->in_frame == NULL ) {
		l->in_frame = yuv_yuv_template( NULL,NULL,NULL, width, height, pixfmt );
		if(l->in_frame == NULL) {
			return 0;
		}
	}
	
	expected_length = l->in_frame->len + l->in_frame->uv_len + l->in_frame->uv_len;

	if(!is_direct) {	
		if(!l->in_frame->data[0]) {
			l->in_frame->data[0] = vj_malloc(expected_length);
			if(l->in_frame->data[0] == NULL) {
				return 0;
			}
			l->in_frame->data[1] = l->in_frame->data[0] + l->in_frame->len;
			l->in_frame->data[2] = l->in_frame->data[1] + l->in_frame->uv_len;
		}
		l->in_frame->format = len[0];

		if(l->scaler == NULL) {
			sws_template tmpl;
			tmpl.flags = 1;
			l->scaler = yuv_init_swscaler( l->in_frame,l->out_frame, &tmpl, yuv_sws_get_cpu_flags());
			if(l->scaler == NULL) {
				return 0;
			}
			veejay_msg(VEEJAY_MSG_DEBUG, "[MLZO] Using libswscale for target resolution/pixelformat" );
		}

		l->out_frame->data[0] = dst[0];
		l->out_frame->data[1] = dst[1];
		l->out_frame->data[2] = dst[2];
	}

	l->wrkmem = malloc(sizeof(long)*LZO_AL(LZO1X_1_MEM_COMPRESS));
	if(l->wrkmem == NULL )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "LZO Cannot allocate work memory for LZO1X_1");
		return 0;
	}

	if( old_format ) {
		result_len = lzo_decompress_plane(l, linbuf + offset, len[1], is_direct, dst[0], l->in_frame->len);
		if( result_len == 0 )
			goto mlzo_decode_exit;
    	sum += result_len;
		offset += len[1];
		result_len = lzo_decompress_plane(l, linbuf + offset, len[2], is_direct, dst[1], l->in_frame->uv_len);
		if( result_len == 0 )
			goto mlzo_decode_exit;
    	sum += result_len;
		offset += len[2];
		result_len = lzo_decompress_plane(l, linbuf + offset, len[3], is_direct, dst[2], l->in_frame->uv_len);
		if( result_len == 0 )
			goto mlzo_decode_exit;
    	sum += result_len;
	}
	else {
		result_len = lzo_decompress_plane(l, linbuf + offset, len[1] + len[2] + len[3], is_direct, dst[0], expected_length);
    	sum += result_len;
	}

mlzo_decode_exit:

	if(!is_direct) {
		yuv_convert_and_scale( l->scaler, l->in_frame, l->out_frame);
	}

	/*veejay_msg(VEEJAY_MSG_DEBUG, "LZO block size: %zu, data size: %d, hdr: pixfmt=%zu Y=%zu, U=%zu, V=%zu offset=%zu,%zu,%zu",
		result_len, linbuf_len, len[0],len[1],len[2],len[3],
		 offset,
		 offset + len[1] ,
		 offset + len[1] + len[2]  );*/

	free(l->wrkmem);
		
	return (long) result_len;
}


