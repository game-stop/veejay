/*
 *  jpegutils.c: Some Utility programs for dealing with
 *               JPEG encoded images
 *
 *  Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *  Copyright (C) 2001 pHilipp Zabel  <pzabel@gmx.de>
 *
 *  based on jdatasrc.c and jdatadst.c from the Independent
 *  JPEG Group's software by Thomas G. Lane
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <config.h>
#ifdef HAVE_JPEG
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

#include <jerror.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <stdint.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>

#include "jpegutils.h"
#include <libel/lav_io.h>

 /*
  * jpeg_data:       buffer with input / output jpeg
  * len:             Length of jpeg buffer
  * itype:           0: Not interlaced
  *                  1: Interlaced, Top field first
  *                  2: Interlaced, Bottom field first
  * ctype            Chroma format for decompression.
  *                  Currently always 420 and hence ignored.
  * raw0             buffer with input / output raw Y channel
  * raw1             buffer with input / output raw U/Cb channel
  * raw2             buffer with input / output raw V/Cr channel
  * width            width of Y channel (width of U/V is width/2)
  * height           height of Y channel (height of U/V is height/2)
  */


static void jpeg_buffer_src(j_decompress_ptr cinfo, unsigned char *buffer,
			    long num);
static void jpeg_buffer_dest(j_compress_ptr cinfo, unsigned char *buffer,
			     long len);
static void jpeg_skip_ff(j_decompress_ptr cinfo);

/*******************************************************************
 *                                                                 *
 *    The following routines define a JPEG Source manager which    *
 *    just reads from a given buffer (instead of a file as in      *
 *    the jpeg library)                                            *
 *                                                                 *
 *******************************************************************/


/*
 * Initialize source --- called by jpeg_read_header
 * before any data is actually read.
 */

static void init_source(j_decompress_ptr cinfo)
{
    /* no work necessary here */
}


/*
 * Fill the input buffer --- called whenever buffer is emptied.
 *
 * Should never be called since all data should be allready provided.
 * Is nevertheless sometimes called - sets the input buffer to data
 * which is the JPEG EOI marker;
 *
 */

static uint8_t EOI_data[2] = { 0xFF, 0xD9 };

static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
    cinfo->src->next_input_byte = EOI_data;
    cinfo->src->bytes_in_buffer = 2;
    return TRUE;
}


/*
 * Skip data --- used to skip over a potentially large amount of
 * uninteresting data (such as an APPn marker).
 *
 */

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    if (num_bytes > 0) {
	if (num_bytes > (long) cinfo->src->bytes_in_buffer)
	    num_bytes = (long) cinfo->src->bytes_in_buffer;
	cinfo->src->next_input_byte += (size_t) num_bytes;
	cinfo->src->bytes_in_buffer -= (size_t) num_bytes;
    }
}


/*
 * Terminate source --- called by jpeg_finish_decompress
 * after all data has been read.  Often a no-op.
 */

static void term_source(j_decompress_ptr cinfo)
{
    /* no work necessary here */
}


/*
 * Prepare for input from a data buffer.
 */

static void
jpeg_buffer_src(j_decompress_ptr cinfo, unsigned char *buffer, long num)
{
    /* The source object and input buffer are made permanent so that a series
     * of JPEG images can be read from the same buffer by calling jpeg_buffer_src
     * only before the first one.  (If we discarded the buffer at the end of
     * one image, we'd likely lose the start of the next one.)
     * This makes it unsafe to use this manager and a different source
     * manager serially with the same JPEG object.  Caveat programmer.
     */
    if (cinfo->src == NULL) {	/* first time for this JPEG object? */
	cinfo->src = (struct jpeg_source_mgr *)
	    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo,
					JPOOL_PERMANENT,
					sizeof(struct jpeg_source_mgr));
    }

    cinfo->src->init_source = init_source;
    cinfo->src->fill_input_buffer = fill_input_buffer;
    cinfo->src->skip_input_data = skip_input_data;
    cinfo->src->resync_to_restart = jpeg_resync_to_restart;	/* use default method */
    cinfo->src->term_source = term_source;
    cinfo->src->bytes_in_buffer = num;
    cinfo->src->next_input_byte = (JOCTET *) buffer;
}


/*
 * jpeg_skip_ff is not a part of the source manager but it is
 * particularly useful when reading several images from the same buffer:
 * It should be called to skip padding 0xff bytes beetween images.
 */

static void jpeg_skip_ff(j_decompress_ptr cinfo)
{
    while (cinfo->src->bytes_in_buffer > 1
	   && cinfo->src->next_input_byte[0] == 0xff
	   && cinfo->src->next_input_byte[1] == 0xff) {
	cinfo->src->bytes_in_buffer--;
	cinfo->src->next_input_byte++;
    }
}


/*******************************************************************
 *                                                                 *
 *    The following routines define a JPEG Destination manager     *
 *    which just reads from a given buffer (instead of a file      *
 *    as in the jpeg library)                                      *
 *                                                                 *
 *******************************************************************/


/*
 * Initialize destination --- called by jpeg_start_compress
 * before any data is actually written.
 */

static void init_destination(j_compress_ptr cinfo)
{
    /* No work necessary here */
}


/*
 * Empty the output buffer --- called whenever buffer fills up.
 *
 * Should never be called since all data should be written to the buffer.
 * If it gets called, the given jpeg buffer was too small.
 *
 */

static boolean empty_output_buffer(j_compress_ptr cinfo)
{
    veejay_msg(0,"Given jpeg buffer was too small!");
    ERREXIT(cinfo, JERR_BUFFER_SIZE);	/* shouldn't be FILE_WRITE but BUFFER_OVERRUN! */
    return TRUE;
}


/*
 * Terminate destination --- called by jpeg_finish_compress
 * after all data has been written.  Usually needs to flush buffer.
 *
 * NB: *not* called by jpeg_abort or jpeg_destroy; surrounding
 * application must deal with any cleanup that should happen even
 * for error exit.
 */

static void term_destination(j_compress_ptr cinfo)
{
    /* no work necessary here */
}


/*
 * Prepare for output to a stdio stream.
 * The caller must have already opened the stream, and is responsible
 * for closing it after finishing compression.
 */

static void
jpeg_buffer_dest(j_compress_ptr cinfo, unsigned char *buf, long len)
{

    /* The destination object is made permanent so that multiple JPEG images
     * can be written to the same file without re-executing jpeg_stdio_dest.
     * This makes it dangerous to use this manager and a different destination
     * manager serially with the same JPEG object, because their private object
     * sizes may be different.  Caveat programmer.
     */
    if (cinfo->dest == NULL) {	/* first time for this JPEG object? */
	cinfo->dest = (struct jpeg_destination_mgr *)
	    (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo,
					JPOOL_PERMANENT,
					sizeof(struct
					       jpeg_destination_mgr));
    }

    cinfo->dest->init_destination = init_destination;
    cinfo->dest->empty_output_buffer = empty_output_buffer;
    cinfo->dest->term_destination = term_destination;
    cinfo->dest->free_in_buffer = len;
    cinfo->dest->next_output_byte = (JOCTET *) buf;
}


/*******************************************************************
 *                                                                 *
 *    decode_jpeg_data: Decode a (possibly interlaced) JPEG frame  *
 *                                                                 *
 *******************************************************************/

/*
 * ERROR HANDLING:
 *
 *    We want in all cases to return to the user.
 *    The following kind of error handling is from the
 *    example.c file in the Independent JPEG Group's JPEG software
 */

struct my_error_mgr {
    struct jpeg_error_mgr pub;	/* "public" fields */
    jmp_buf setjmp_buffer;	/* for return to caller */
};

static void my_error_exit(j_common_ptr cinfo)
{
    /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
    struct my_error_mgr *myerr = (struct my_error_mgr *) cinfo->err;

    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    (*cinfo->err->output_message) (cinfo);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}

#define MAX_LUMA_WIDTH   4096
#define MAX_CHROMA_WIDTH 2048

static unsigned char buf0[16][MAX_LUMA_WIDTH];
static unsigned char buf1[8][MAX_CHROMA_WIDTH];
static unsigned char buf2[8][MAX_CHROMA_WIDTH];
static unsigned char chr1[8][MAX_CHROMA_WIDTH];
static unsigned char chr2[8][MAX_CHROMA_WIDTH];



#if 1				/* generation of 'std' Huffman tables... */

static void add_huff_table(j_decompress_ptr dinfo,
			   JHUFF_TBL ** htblptr,
			   const UINT8 * bits, const UINT8 * val)
/* Define a Huffman table */
{
    int nsymbols, len;

    if (*htblptr == NULL)
	*htblptr = jpeg_alloc_huff_table((j_common_ptr) dinfo);

    /* Copy the number-of-symbols-of-each-code-length counts */
    memcpy((*htblptr)->bits, bits, sizeof((*htblptr)->bits));

    /* Validate the counts.  We do this here mainly so we can copy the right
     * number of symbols from the val[] array, without risking marching off
     * the end of memory.  jchuff.c will do a more thorough test later.
     */
    nsymbols = 0;
    for (len = 1; len <= 16; len++)
	nsymbols += bits[len];
    if (nsymbols < 1 || nsymbols > 256)
    {
      veejay_msg(0, "(jpegutils) HUFFMAN table failed badly");
      return;
    }

    memcpy((*htblptr)->huffval, val, nsymbols * sizeof(UINT8));
}



static void std_huff_tables(j_decompress_ptr dinfo)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
    static const UINT8 bits_dc_luminance[17] =
	{ /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
    static const UINT8 val_dc_luminance[] =
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    static const UINT8 bits_dc_chrominance[17] =
	{ /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
    static const UINT8 val_dc_chrominance[] =
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    static const UINT8 bits_ac_luminance[17] =
	{ /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1,
	0x7d
    };
    static const UINT8 val_ac_luminance[] =
	{ 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
    };

    static const UINT8 bits_ac_chrominance[17] =
	{ /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2,
	0x77
    };
    static const UINT8 val_ac_chrominance[] =
	{ 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
    };

    add_huff_table(dinfo, &dinfo->dc_huff_tbl_ptrs[0],
		   bits_dc_luminance, val_dc_luminance);
    add_huff_table(dinfo, &dinfo->ac_huff_tbl_ptrs[0],
		   bits_ac_luminance, val_ac_luminance);
    add_huff_table(dinfo, &dinfo->dc_huff_tbl_ptrs[1],
		   bits_dc_chrominance, val_dc_chrominance);
    add_huff_table(dinfo, &dinfo->ac_huff_tbl_ptrs[1],
		   bits_ac_chrominance, val_ac_chrominance);
}



static void guarantee_huff_tables(j_decompress_ptr dinfo)
{
    if ((dinfo->dc_huff_tbl_ptrs[0] == NULL) &&
	(dinfo->dc_huff_tbl_ptrs[1] == NULL) &&
	(dinfo->ac_huff_tbl_ptrs[0] == NULL) &&
	(dinfo->ac_huff_tbl_ptrs[1] == NULL)) {
	mjpeg_debug("Generating standard Huffman tables for this frame.");
	std_huff_tables(dinfo);
    }
}


#endif				/* ...'std' Huffman table generation */


jpeghdr_t *decode_jpeg_raw_hdr(unsigned char *jpeg_data, int len)
{
    struct jpeg_decompress_struct dinfo;
    jpeg_create_decompress(&dinfo);

    jpeg_buffer_src(&dinfo, jpeg_data, len);

    jpeg_read_header(&dinfo, TRUE);
   
	jpeghdr_t *j = (jpeghdr_t*) malloc(sizeof(jpeghdr_t));
	j->jpeg_color_space= dinfo.jpeg_color_space;
	j->width = dinfo.image_width;
	j->height= dinfo.image_height;
	j->num_components = dinfo.num_components;
	j->ccir601 = dinfo.CCIR601_sampling;
	j->version[0] = dinfo.JFIF_major_version;
	j->version[1] = dinfo.JFIF_minor_version;

    jpeg_destroy_decompress(&dinfo);

    return j;
}
/*
 * jpeg_data:       Buffer with jpeg data to decode
 * len:             Length of buffer
 * itype:           0: Not interlaced
 *                  1: Interlaced, Top field first
 *                  2: Interlaced, Bottom field first
 * ctype            Chroma format for decompression.
 *                  Currently always 420 and hence ignored.
 */

int decode_jpeg_raw(unsigned char *jpeg_data, int len,
		    int itype, int ctype, int width, int height,
		    unsigned char *raw0, unsigned char *raw1,
		    unsigned char *raw2)           
{
    int numfields, hsf[3], vsf[3], field, yl, yc, x, y =
	0, i, xsl, xsc, xs, xd, hdown;

    JSAMPROW row0[16] = { buf0[0], buf0[1], buf0[2], buf0[3],
	buf0[4], buf0[5], buf0[6], buf0[7],
	buf0[8], buf0[9], buf0[10], buf0[11],
	buf0[12], buf0[13], buf0[14], buf0[15]
    };
    JSAMPROW row1[8] = { buf1[0], buf1[1], buf1[2], buf1[3],
	buf1[4], buf1[5], buf1[6], buf1[7]
    };
    JSAMPROW row2[16] = { buf2[0], buf2[1], buf2[2], buf2[3],
	buf2[4], buf2[5], buf2[6], buf2[7]
    };
    JSAMPROW row1_444[16], row2_444[16];
    JSAMPARRAY scanarray[3] = { row0, row1, row2 };
    struct jpeg_decompress_struct dinfo;
    struct my_error_mgr jerr;

    /* We set up the normal JPEG error routines, then override error_exit. */
    dinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerr.setjmp_buffer)) {
	/* If we get here, the JPEG code has signaled an error. */
	jpeg_destroy_decompress(&dinfo);
	return -1;
    }

    jpeg_create_decompress(&dinfo);

    jpeg_buffer_src(&dinfo, jpeg_data, len);

    /* Read header, make some checks and try to figure out what the
       user really wants */

    jpeg_read_header(&dinfo, TRUE);
    dinfo.raw_data_out = TRUE;
    dinfo.out_color_space = JCS_YCbCr;
//   dinfo.dct_method = (_dct_method == 0 ? JDCT_DEFAULT : JDCT_FLOAT);
    dinfo.dct_method = JDCT_DEFAULT;
    guarantee_huff_tables(&dinfo);
    jpeg_start_decompress(&dinfo);

    if (dinfo.output_components != 3) {
	veejay_msg(0,"Output components of JPEG image = %d, must be 3",
		    dinfo.output_components);
	goto ERR_EXIT;
    }

    for (i = 0; i < 3; i++) {
	hsf[i] = dinfo.comp_info[i].h_samp_factor;
	vsf[i] = dinfo.comp_info[i].v_samp_factor;
    }

    if ((hsf[0] != 2 && hsf[0] != 1) || hsf[1] != 1 || hsf[2] != 1 ||
	(vsf[0] != 1 && vsf[0] != 2) || vsf[1] != 1 || vsf[2] != 1) {
	veejay_msg
	    (0,"Unsupported sampling factors, hsf=(%d, %d, %d) vsf=(%d, %d, %d) !",
	     hsf[0], hsf[1], hsf[2], vsf[0], vsf[1], vsf[2]);
	goto ERR_EXIT;
    }

    if (hsf[0] == 1) {
	if (height % 8 != 0) {
	    veejay_msg
		(0,"YUV 4:4:4 sampling, but image height %d not dividable by 8 !\n",
		 height);
	    goto ERR_EXIT;
	}

	mjpeg_info
	    ("YUV 4:4:4 sampling encountered ! Allocating special row buffer\n");
	for (y = 0; y < 16; y++)	// allocate a special buffer for the extra sampling depth
	{
	    //mjpeg_info("YUV 4:4:4 %d.\n",y);
	    row1_444[y] =
		(unsigned char *) malloc(dinfo.output_width *
					 sizeof(unsigned char));
	    row2_444[y] =
		(unsigned char *) malloc(dinfo.output_width *
					 sizeof(unsigned char));
	}
	//mjpeg_info("YUV 4:4:4 sampling encountered ! Allocating done.\n");
	scanarray[1] = row1_444;
	scanarray[2] = row2_444;
    }

    /* Height match image height or be exact twice the image height */

    if (dinfo.output_height == height) {
	numfields = 1;
    } else if (2 * dinfo.output_height == height) {
	numfields = 2;
    } else {
	veejay_msg
	    (0,"Read JPEG: requested height = %d, height of image = %d",
	     height, dinfo.output_height);
	goto ERR_EXIT;
    }

    /* Width is more flexible */

    if (dinfo.output_width > MAX_LUMA_WIDTH) {
	veejay_msg(0,"Image width of %d exceeds max", dinfo.output_width);
	goto ERR_EXIT;
    }
    if (width < 2 * dinfo.output_width / 3) {
	/* Downclip 2:1 */

	hdown = 1;
	if (2 * width < dinfo.output_width)
	    xsl = (dinfo.output_width - 2 * width) / 2;
	else
	    xsl = 0;
    } else if (width == 2 * dinfo.output_width / 3) {
	/* special case of 3:2 downsampling */

	hdown = 2;
	xsl = 0;
    } else {
	/* No downsampling */

	hdown = 0;
	if (width < dinfo.output_width)
	    xsl = (dinfo.output_width - width) / 2;
	else
	    xsl = 0;
    }

    /* Make xsl even, calculate xsc */

    xsl = xsl & ~1;
    xsc = xsl / 2;

    yl = yc = 0;

    for (field = 0; field < numfields; field++) {
	if (field > 0) {
	    jpeg_read_header(&dinfo, TRUE);
	    dinfo.raw_data_out = TRUE;
	    dinfo.out_color_space = JCS_YCbCr;
	    dinfo.dct_method = JDCT_FLOAT; //JDCT_DEFAULT;            
	    jpeg_start_decompress(&dinfo);
	}

	if (numfields == 2) {
	    switch (itype) {
	    case LAV_INTER_TOP_FIRST:
		yl = yc = field;
		break;
	    case LAV_INTER_BOTTOM_FIRST:
		yl = yc = (1 - field);
		break;
	    default:
		veejay_msg(0,"Input is interlaced but no interlacing set");
		goto ERR_EXIT;
	    }
	} else
	    yl = yc = 0;

	while (dinfo.output_scanline < dinfo.output_height) {
	    /* read raw data */
	    jpeg_read_raw_data(&dinfo, scanarray, 8 * vsf[0]);

	    for (y = 0; y < 8 * vsf[0]; yl += numfields, y++) {
		xd = yl * width;
		xs = xsl;

		if (hdown == 0)
#pragma omp simd
		    for (x = 0; x < width; x++)
			raw0[xd++] = row0[y][xs++];
		else if (hdown == 1)
		    for (x = 0; x < width; x++, xs += 2)
			raw0[xd++] = (row0[y][xs] + row0[y][xs + 1]) >> 1;
		else
		    for (x = 0; x < width / 2; x++, xd += 2, xs += 3) {
			raw0[xd] = (2 * row0[y][xs] + row0[y][xs + 1]) / 3;
			raw0[xd + 1] =
			    (2 * row0[y][xs + 2] + row0[y][xs + 1]) / 3;
		    }
	    }

	    /* Horizontal downsampling of chroma */

	    for (y = 0; y < 8; y++) {
		xs = xsc;

		if (hsf[0] == 1)
		    for (x = 0; x < width / 2; x++, xs++) {
			row1[y][xs] =
			    (row1_444[y][2 * x] +
			     row1_444[y][2 * x + 1]) >> 1;
			row2[y][xs] =
			    (row2_444[y][2 * x] +
			     row2_444[y][2 * x + 1]) >> 1;
		    }

		xs = xsc;
		if (hdown == 0)
		    for (x = 0; x < width / 2; x++, xs++) {
			chr1[y][x] = row1[y][xs];
			chr2[y][x] = row2[y][xs];
		} else if (hdown == 1)
		    for (x = 0; x < width / 2; x++, xs += 2) {
			chr1[y][x] = (row1[y][xs] + row1[y][xs + 1]) >> 1;
			chr2[y][x] = (row2[y][xs] + row2[y][xs + 1]) >> 1;
		} else
		    for (x = 0; x < width / 2; x += 2, xs += 3) {
			chr1[y][x] =
			    (2 * row1[y][xs] + row1[y][xs + 1]) / 3;
			chr1[y][x + 1] =
			    (2 * row1[y][xs + 2] + row1[y][xs + 1]) / 3;
			chr2[y][x] =
			    (2 * row2[y][xs] + row2[y][xs + 1]) / 3;
			chr2[y][x + 1] =
			    (2 * row2[y][xs + 2] + row2[y][xs + 1]) / 3;
		    }
	    }

	    /* Vertical downsampling of chroma */

	    if (vsf[0] == 1) {
		for (y = 0; y < 8 /*&& yc < height/2 */ ;
		    y += 2, yc += numfields) {
		    xd = yc * width / 2;
		    for (x = 0; x < width / 2; x++, xd++) {
#ifdef STRICT_CHECKING
            assert(xd < (width * height / 4));
#endif
			raw1[xd] = (chr1[y][x] + chr1[y + 1][x]) >> 1;
			raw2[xd] = (chr2[y][x] + chr2[y + 1][x]) >> 1;
		    }
		}

	    } else {
		/* Just copy */
		for (y = 0; y < 8 /*&& yc < height/2 */ ;
		     y++, yc += numfields) {
		    xd = yc * width / 2;
		    for (x = 0; x < width / 2; x++, xd++) {
			raw1[xd] = chr1[y][x];
			raw2[xd] = chr2[y][x];
		    }
		}
	    }
	}

	(void) jpeg_finish_decompress(&dinfo);
	if (field == 0 && numfields > 1)
	    jpeg_skip_ff(&dinfo);
    }

    if (hsf[0] == 1) {
	//mjpeg_info("YUV 4:4:4 sampling encountered ! Deallocating special row buffer\n");
	for (y = 0; y < 16; y++)	// allocate a special buffer for the extra sampling depth
	{
	    free(row1_444[y]);
	    free(row2_444[y]);
	}
    }

    jpeg_destroy_decompress(&dinfo);
    return 0;

  ERR_EXIT:
    jpeg_destroy_decompress(&dinfo);
    return -1;
}

/*
 * jpeg_data:       Buffer with jpeg data to decode, must be grayscale mode
 * len:             Length of buffer
 * itype:           0: Not interlaced
 *                  1: Interlaced, Top field first
 *                  2: Interlaced, Bottom field first
 * ctype            Chroma format for decompression.
 *                  Currently always 420 and hence ignored.
 */


int decode_jpeg_gray_raw(unsigned char *jpeg_data, int len,
			 int itype, int ctype, int width, int height,
			 unsigned char *raw0, unsigned char *raw1,
			 unsigned char *raw2)
{
    int numfields, vsf[3], field, yl, yc, x, y, xsl, xsc, xs, xd,
	hdown;

    JSAMPROW row0[16] = { buf0[0], buf0[1], buf0[2], buf0[3],
	buf0[4], buf0[5], buf0[6], buf0[7],
	buf0[8], buf0[9], buf0[10], buf0[11],
	buf0[12], buf0[13], buf0[14], buf0[15]
    };
    JSAMPARRAY scanarray[3] = { row0 };
    struct jpeg_decompress_struct dinfo;
    struct my_error_mgr jerr;

    mjpeg_info("decoding jpeg gray\n");

    /* We set up the normal JPEG error routines, then override error_exit. */
    dinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerr.setjmp_buffer)) {
	/* If we get here, the JPEG code has signaled an error. */
	jpeg_destroy_decompress(&dinfo);
	return -1;
    }

    jpeg_create_decompress(&dinfo);

    jpeg_buffer_src(&dinfo, jpeg_data, len);

    /* Read header, make some checks and try to figure out what the
       user really wants */

    jpeg_read_header(&dinfo, TRUE);
    dinfo.raw_data_out = TRUE;
    dinfo.out_color_space = JCS_GRAYSCALE;
//  dinfo.dct_method = ( _dct_method == 0 ? JDCT_DEFAULT: JDCT_FLOAT);
    dinfo.dct_method = JDCT_DEFAULT;	

    if (dinfo.jpeg_color_space != JCS_GRAYSCALE) {
	veejay_msg
	    (0,"FATAL: Expected grayscale colorspace for JPEG raw decoding");
	goto ERR_EXIT;
    }

    guarantee_huff_tables(&dinfo);
    jpeg_start_decompress(&dinfo);

    vsf[0] = 1;
    vsf[1] = 1;
    vsf[2] = 1;

    /* Height match image height or be exact twice the image height */

    if (dinfo.output_height == height) {
	numfields = 1;
    } else if (2 * dinfo.output_height == height) {
	numfields = 2;
    } else {
	veejay_msg
	    (0,"Read JPEG: requested height = %d, height of image = %d",
	     height, dinfo.output_height);
	goto ERR_EXIT;
    }

    /* Width is more flexible */

    if (dinfo.output_width > MAX_LUMA_WIDTH) {
	veejay_msg(0,"Image width of %d exceeds max", dinfo.output_width);
	goto ERR_EXIT;
    }
    if (width < 2 * dinfo.output_width / 3) {
	/* Downclip 2:1 */

	hdown = 1;
	if (2 * width < dinfo.output_width)
	    xsl = (dinfo.output_width - 2 * width) / 2;
	else
	    xsl = 0;
    } else if (width == 2 * dinfo.output_width / 3) {
	/* special case of 3:2 downsampling */

	hdown = 2;
	xsl = 0;
    } else {
	/* No downsampling */

	hdown = 0;
	if (width < dinfo.output_width)
	    xsl = (dinfo.output_width - width) / 2;
	else
	    xsl = 0;
    }

    /* Make xsl even, calculate xsc */

    xsl = xsl & ~1;
    xsc = xsl / 2;

    yl = yc = 0;

    for (field = 0; field < numfields; field++) {
	if (field > 0) {
	    jpeg_read_header(&dinfo, TRUE);
	    dinfo.raw_data_out = TRUE;
	    dinfo.out_color_space = JCS_GRAYSCALE;
	    dinfo.dct_method = JDCT_FLOAT;//DCT_DEFAULT;
	    jpeg_start_decompress(&dinfo);
	}

	if (numfields == 2) {
	    switch (itype) {
	    case LAV_INTER_TOP_FIRST:
		yl = yc = field;
		break;
	    case LAV_INTER_BOTTOM_FIRST:
		yl = yc = (1 - field);
		break;
	    default:
		veejay_msg(0,"Input is interlaced but no interlacing set");
		goto ERR_EXIT;
	    }
	} else
	    yl = yc = 0;

	while (dinfo.output_scanline < dinfo.output_height) {
	    jpeg_read_raw_data(&dinfo, scanarray, 16);

	    for (y = 0; y < 8 * vsf[0]; yl += numfields, y++) {
		xd = yl * width;
		xs = xsl;

		if (hdown == 0)	// no horiz downsampling
#pragma omp simd
    		for (x = 0; x < width; x++)
			raw0[xd++] = row0[y][xs++];
		else if (hdown == 1)	// half the res
		for (x = 0; x < width; x++, xs += 2)
			raw0[xd++] = (row0[y][xs] + row0[y][xs + 1]) >> 1;
		else		// 2:3 downsampling
		for (x = 0; x < width / 2; x++, xd += 2, xs += 3) {
			raw0[xd] = (2 * row0[y][xs] + row0[y][xs + 1]) / 3;
			raw0[xd + 1] =
			    (2 * row0[y][xs + 2] + row0[y][xs + 1]) / 3;
		    }
	    }

	    //mjpeg_info("/* Horizontal downsampling of chroma - in Grayscale, all this is ZERO ! */");

	    for (y = 0; y < 8; y++) {
		xs = xsc;

		if (hdown == 0)
		    for (x = 0; x < width / 2; x++, xs++) {
			chr1[y][x] = 0;	//row1[y][xs];
			chr2[y][x] = 0;	//row2[y][xs];
		} else if (hdown == 1)
		    for (x = 0; x < width / 2; x++, xs += 2) {
			chr1[y][x] = 0;	//(row1[y][xs] + row1[y][xs + 1]) >> 1;
			chr2[y][x] = 0;	//(row2[y][xs] + row2[y][xs + 1]) >> 1;
		} else
		    for (x = 0; x < width / 2; x += 2, xs += 3) {
			chr1[y][x] = 0;	//(2 * row1[y][xs] + row1[y][xs + 1]) / 3;
			chr1[y][x + 1] = 0;
			//(2 * row1[y][xs + 2] + row1[y][xs + 1]) / 3;
			chr2[y][x] = 0;	// (2 * row2[y][xs] + row2[y][xs + 1]) / 3;
			chr2[y][x + 1] = 0;
			//(2 * row2[y][xs + 2] + row2[y][xs + 1]) / 3;
		    }
	    }

	    //mjpeg_info("/* Vertical downsampling of chroma, line %d, max %d */", dinfo.output_scanline, dinfo.output_height);

	    if (vsf[0] == 1) {
		/* Really downclip */
		for (y = 0; y < 8; y += 2, yc += numfields) {
		    xd = yc * width / 2;
		    for (x = 0; x < width / 2; x++, xd++) {
			raw1[xd] = 127;	//(chr1[y][x] + chr1[y + 1][x]) >> 1;
			raw2[xd] = 127;	//(chr2[y][x] + chr2[y + 1][x]) >> 1;
		    }
		}
	    } else {
		/* Just copy */

		for (y = 0; y < 8; y++, yc += numfields) {
		    xd = yc * width / 2;
		    for (x = 0; x < width / 2; x++, xd++) {
			raw1[xd] = 127;	//chr1[y][x];
			raw2[xd] = 127;	//chr2[y][x];
		    }
		}
	    }
	}

	(void) jpeg_finish_decompress(&dinfo);
	if (field == 0 && numfields > 1)
	    jpeg_skip_ff(&dinfo);
    }

    jpeg_destroy_decompress(&dinfo);
    return 0;

  ERR_EXIT:
    jpeg_destroy_decompress(&dinfo);
    return -1;
}


/*******************************************************************
 *                                                                 *
 *    encode_jpeg_data: Compress raw YCbCr data (output JPEG       *
 *                      may be interlaced                          *
 *                                                                 *
 *******************************************************************/

 /*
  * jpeg_data:       Buffer to hold output jpeg
  * len:             Length of buffer
  * itype:           0: Not interlaced
  *                  1: Interlaced, Top field first
  *                  2: Interlaced, Bottom field first
  * ctype            Chroma format for decompression.
  *                  Currently always 420 and hence ignored.
  */

int encode_jpeg_raw(unsigned char *jpeg_data, int len, int quality,int dct_method,
		    int itype, int ctype, int width, int height,
		    unsigned char *raw0, unsigned char *raw1,
		    unsigned char *raw2)
{
    int numfields, field, yl, yc, y, i;

    JSAMPROW row0[16] = { buf0[0], buf0[1], buf0[2], buf0[3],
	buf0[4], buf0[5], buf0[6], buf0[7],
	buf0[8], buf0[9], buf0[10], buf0[11],
	buf0[12], buf0[13], buf0[14], buf0[15]
    };
    JSAMPROW row1[8] = { buf1[0], buf1[1], buf1[2], buf1[3],
	buf1[4], buf1[5], buf1[6], buf1[7]
    };
    JSAMPROW row2[8] = { buf2[0], buf2[1], buf2[2], buf2[3],
	buf2[4], buf2[5], buf2[6], buf2[7]
    };
    JSAMPARRAY scanarray[3] = { row0, row1, row2 };

    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;

    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    /* Establish the setjmp return context for my_error_exit to use. */
    if (setjmp(jerr.setjmp_buffer)) {
	/* If we get here, the JPEG code has signaled an error. */
	jpeg_destroy_compress(&cinfo);
	return -1;
    }

    jpeg_create_compress(&cinfo);

    jpeg_buffer_dest(&cinfo, jpeg_data, len);

    /* Set some jpeg header fields */

    cinfo.input_components = 3;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, FALSE);

    cinfo.raw_data_in = TRUE;
    cinfo.in_color_space = JCS_YCbCr;
    cinfo.dct_method = JDCT_FLOAT;//  CT_DEFAULT;

    cinfo.input_gamma = 1.0;

    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 1;	/*1||2 */
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;	/*1||2 */
    cinfo.comp_info[2].v_samp_factor = 1;


    if ((width > 4096) || (height > 4096)) {
	veejay_msg
	    (0,"Image dimensions (%dx%d) exceed veejay' max (4096x4096)",
	     width, height);
	goto ERR_EXIT;
    }
    if ((width % 16) || (height % 16)) {
	veejay_msg(0,"Image dimensions (%dx%d) not multiples of 16", width,
		    height);
	goto ERR_EXIT;
    }
    cinfo.image_width = width;
    switch (itype) {
    case LAV_INTER_TOP_FIRST:
    case LAV_INTER_BOTTOM_FIRST:	/* interlaced */
	numfields = 2;
	break;
    default:
	numfields = 1;
	if (height > 2048) {
	    veejay_msg
		(0,"Image height (%d) exceeds lavtools max for non-interlaced frames",
		 height);
	    goto ERR_EXIT;
	}
    }
    cinfo.image_height = height / numfields;

    yl = yc = 0;		/* y luma, chroma */

    for (field = 0; field < numfields; field++) {

	jpeg_start_compress(&cinfo, FALSE);

	if (numfields == 2) {
	    static const JOCTET marker0[40];

	    jpeg_write_marker(&cinfo, JPEG_APP0, marker0, 14);
	    jpeg_write_marker(&cinfo, JPEG_APP0 + 1, marker0, 40);

	    switch (itype) {
	    case LAV_INTER_TOP_FIRST:	/* top field first */
		yl = yc = field;
		break;
	    case LAV_INTER_BOTTOM_FIRST:	/* bottom field first */
		yl = yc = (1 - field);
		break;
	    default:
		veejay_msg(0,"Input is interlaced but no interlacing set");
		goto ERR_EXIT;
	    }
	} else
	    yl = yc = 0;

	while (cinfo.next_scanline < cinfo.image_height) {

	    for (y = 0; y < 8 * cinfo.comp_info[0].v_samp_factor;
		 yl += numfields, y++) {
		row0[y] = &raw0[yl * width];
	    }
	    for (y = 0; y < 8; y++) {
		row1[y] = &raw1[yc * width / 2];
		row2[y] = &raw2[yc * width / 2];
		if (y % 2)
		    yc += numfields;
	    }

	    jpeg_write_raw_data(&cinfo, scanarray,
				8 * cinfo.comp_info[0].v_samp_factor);

	}

	(void) jpeg_finish_compress(&cinfo);
    }

    i = len - cinfo.dest->free_in_buffer;

    jpeg_destroy_compress(&cinfo);

    return i;			/* size of jpeg */

  ERR_EXIT:
    jpeg_destroy_compress(&cinfo);
    return -1;
}
#endif
