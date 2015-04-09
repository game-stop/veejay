/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
 */

/*
 * qrencode - QR Code encoder
 *
 * Copyright (C) 2006-2012 Kentaro Fukuchi <kentaro@fukuchi.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */


/* least effort, take one of kentaro's examples, slightly modify it and write qr code to .png file */

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libel/pixbuf.h>
#include <libqrwrap/vlogo.h>
#include <libqrwrap/bitcoin.h>

static int	qrcode_open(const char *filename, const char *data, const int len);

#ifdef HAVE_QRENCODE
#include <qrencode.h>
#include <png.h>

static int qrwrap_writePNG(const char *outfile, QRcode *qrcode)
{
	static FILE *fp;
	png_structp png_ptr;
	png_infop info_ptr;
	unsigned char *row, *p, *q;
	int x, y, xx, yy, bit;
	int realwidth;
	const int margin = 0;
	const int size = 8;
	const int width = qrcode->width;

	realwidth = (width + margin * 2) * size;
	row = (unsigned char *)vj_malloc((realwidth + 7) / 8);
	if(row == NULL) {
		return 0;
	}

	fp = fopen(outfile, "wb");
	if(fp == NULL) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to write QR code to file:%s", outfile);
		return 0;
	}
	

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if(png_ptr == NULL) {
		fclose(fp);
		return 0;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if(info_ptr == NULL) {
		fclose(fp);
		return 0;
	}

	if(setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(fp);
		return 0;
	}

	png_init_io(png_ptr, fp);
	png_set_IHDR(png_ptr, info_ptr,
			realwidth, realwidth,
			1,
			PNG_COLOR_TYPE_GRAY,
			PNG_INTERLACE_NONE,
			PNG_COMPRESSION_TYPE_DEFAULT,
			PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);

	/* top margin */
	veejay_memset(row, 0xff, (realwidth + 7) / 8);
	for(y=0; y<margin * size; y++) {
		png_write_row(png_ptr, row);
	}

	/* data */
	p = qrcode->data;
	for(y=0; y<width; y++) {
		bit = 7;
		veejay_memset(row, 0xff, (realwidth + 7) / 8);
		q = row;
		q += margin * size / 8;
		bit = 7 - (margin * size % 8);
		for(x=0; x<width; x++) {
			for(xx=0; xx<size; xx++) {
				*q ^= (*p & 1) << bit;
				bit--;
				if(bit < 0) {
					q++;
					bit = 7;
				}
			}
			p++;
		}
		for(yy=0; yy<size; yy++) {
			png_write_row(png_ptr, row);
		}
	}
	/* bottom margin */
	veejay_memset(row, 0xff, (realwidth + 7) / 8);
	for(y=0; y<margin * size; y++) {
		png_write_row(png_ptr, row);
	}

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	fclose(fp);
	free(row);

	return 0;
}

static QRcode *qrwrap_encode(const char *str)
{
	return QRcode_encodeString( str, 0, QR_ECLEVEL_H, QR_MODE_8 ,0 );
}


int	qrwrap_encode_string(const char *outfile, const char *str)
{
	QRcode *qr = qrwrap_encode( str );
	if( qr == NULL ) {
		return 0;
	}
	int ret = qrwrap_writePNG( outfile, qr );
	QRcode_free(qr);

	return ret;
}
#else
int	qrwrap_encode_string(const char *outfile, const char *str )
{
	return qrcode_open( outfile, veejay_logo, veejay_logo_length );
}

#endif


static int	qrcode_open(const char *filename, const char *picture, const int picture_length)
{
	FILE *fp = fopen( filename, "wb" );
	if(fp == NULL) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to write veejay logo to file:%s", filename);
		return 0;
	}

	size_t res = fwrite( picture, 1, picture_length, fp );
	if( res != picture_length ) {
		veejay_msg(VEEJAY_MSG_ERROR,"Failed to write veejay logo to file:%s", filename);
		fclose(fp);
		return 0;
	}

	fclose(fp);

	return 1;
}

static void *pic_ = NULL;
static void *bitcoin_ = NULL;

void	qrwrap_draw( VJFrame *out, int port_num, const char *homedir, int qr_w, int qr_h, int qr_fmt )
{
	if( pic_ == NULL ) {
		char qrfile[1024];
		snprintf(qrfile,sizeof(qrfile), "%s/QR-%d.png", homedir, port_num );
		pic_ = vj_picture_open( qrfile, qr_w, qr_h, qr_fmt );
	}

	VJFrame *qr = vj_picture_get( pic_ );
	if( qr ) {
		int offset_x = out->width - qr->width - 10;
		int offset_y = 10;
		int x,y;
		int w = out->width;
		uint8_t *Y = out->data[0];
		uint8_t *U = out->data[1];
		uint8_t *V = out->data[2];
		const uint8_t *qY = qr->data[0];
		
		if( offset_x < 0 )
			offset_x = 0;

		for(y = 0; y < qr->height; y ++ ) {
			for( x = 0; x < qr->width; x ++ ) {
				Y[ ((y+offset_y) * w + x + offset_x) ] = qY[ (y*qr->width+x) ];
				U[ ((y+offset_y) * w + x + offset_x) ] = 128; 
				V[ ((y+offset_y) * w + x + offset_x) ] = 128; 
			}
		}
	}
	
}

void	qrbitcoin_draw( VJFrame *out, const char *homedir,int qr_w, int qr_h, int qr_fmt )
{
	if( bitcoin_ == NULL ) {
		char qrfile[1024];
		snprintf(qrfile,sizeof(qrfile),"%s/veejay_bitcoin.png", homedir );
		if( qrcode_open( qrfile, veejay_bitcoin, veejay_bitcoin_length ) ) {
			bitcoin_ = vj_picture_open( qrfile, qr_w, qr_h, qr_fmt );
		}
	}

	VJFrame *qr = vj_picture_get( bitcoin_ );
	if( qr ) {
		int offset_x = out->width - qr->width - 10;
		int offset_y = out->height - qr->height - 10;
		int x,y;
		int w = out->width;
		uint8_t *Y = out->data[0];
		uint8_t *U = out->data[1];
		uint8_t *V = out->data[2];
		const uint8_t *qY = qr->data[0];
		
		if( offset_y < 0 )
			offset_y = 0;
		if( offset_x < 0 )
			offset_x = 0;

		for(y = 0; y < qr->height; y ++ ) {
			for( x = 0; x < qr->width; x ++ ) {
				Y[ ((y+offset_y) * w + x + offset_x) ] = qY[ (y*qr->width+x) ];
				U[ ((y+offset_y) * w + x + offset_x) ] = 128; 
				V[ ((y+offset_y) * w + x + offset_x) ] = 128; 
			}
		}
	}

}




void	qrwrap_free()
{
	if( pic_ != NULL ) {
		vj_picture_cleanup( pic_ );
	}
	if( bitcoin_ != NULL ) {
		vj_picture_cleanup( bitcoin_ );
	}
}

