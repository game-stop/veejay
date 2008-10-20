/* lav_common - some general utility functionality used by multiple
	lavtool utilities. */

/* Copyright (C) 2000, Rainer Johanni, Andrew Stevens */
/* - added scene change detection code 2001, pHilipp Zabel */
/* - broke some code out to lav_common.h and lav_common.c 
 *   July 2001, Shawn Sulma <lavtools@athos.cx>.  In doing this,
 *   I replaced the large number of globals with a handful of structs
 *   that are passed into the appropriate methods.  Check lav_common.h
 *   for the structs.  I'm sure some of what I've done is inefficient,
 *   subtly incorrect or just plain Wrong.  Feedback is welcome.
 */
/* - removed a lot of subsumed functionality and unnecessary cruft
 *   March 2002, Matthew Marjanovic <maddog@mir.com>.
 */


/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                
*/

#include <config.h>
#include <stdio.h>
#include <string.h>

#include <veejay/lav_common.h>
#include <veejay/jpegutils.h>
#include <mjpegtools/mpegconsts.h>

static uint8_t jpeg_data[MAX_JPEG_LEN];


#ifdef SUPPORT_READ_DV2

dv_decoder_t *decoder;
#ifdef LIBDV_PRE_0_9_5
gint pitches[3];
#else
int pitches[3];
#endif
uint8_t *dv_frame[3] = { NULL, NULL, NULL };

/*
 * As far as I (maddog) can tell, this is what is going on with libdv-0.9
 *  and the unpacking routine... 
 *    o Ft/Fb refer to top/bottom scanlines (interleaved) --- each sample
 *       is implicitly tagged by 't' or 'b' (samples are not mixed between
 *       fields)
 *    o Indices on Cb and Cr samples indicate the Y sample with which
 *       they are co-sited.
 *    o '^' indicates which samples are preserved by the unpacking
 *
 * libdv packs both NTSC 4:1:1 and PAL 4:2:0 into a common frame format of
 *  packed 4:2:2 pixels as follows:
 *
 *
 ***** NTSC 4:1:1 *****
 *
 *   libdv's 4:2:2-packed representation  (chroma repeated horizontally)
 *
 *Ft Y00.Cb00.Y01.Cr00.Y02.Cb00.Y03.Cr00 Y04.Cb04.Y05.Cr04.Y06.Cb04.Y07.Cr04
 *    ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^
 *Fb Y00.Cb00.Y01.Cr00.Y02.Cb00.Y03.Cr00 Y04.Cb04.Y05.Cr04.Y06.Cb04.Y07.Cr04
 *    ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^
 *Ft Y10.Cb10.Y11.Cr10.Y12.Cb10.Y13.Cr10 Y14.Cb14.Y15.Cr14.Y16.Cb14.Y17.Cr14
 *    ^        ^        ^        ^        ^        ^        ^        ^    
 *Fb Y10.Cb10.Y11.Cr10.Y12.Cb10.Y13.Cr10 Y14.Cb14.Y15.Cr14.Y16.Cb14.Y17.Cr14
 *    ^        ^        ^        ^        ^        ^        ^        ^    
 *
 *    lavtools unpacking into 4:2:0-planar  (note lossiness)
 *
 *Ft  Y00.Y01.Y02.Y03.Y04.Y05.Y06.Y07...
 *Fb  Y00.Y01.Y02.Y03.Y04.Y05.Y06.Y07...
 *Ft  Y10.Y11.Y12.Y13.Y14.Y15.Y16.Y17...
 *Fb  Y10.Y11.Y12.Y13.Y14.Y15.Y16.Y17...
 *
 *Ft  Cb00.Cb00.Cb04.Cb04...    Cb00,Cb04... are doubled
 *Fb  Cb00.Cb00.Cb04.Cb04...    Cb10,Cb14... are ignored
 *
 *Ft  Cr00.Cr00.Cr04.Cr04...
 *Fb  Cr00.Cr00.Cr04.Cr04...
 *
 * 
 ***** PAL 4:2:0 *****
 *
 *   libdv's 4:2:2-packed representation  (chroma repeated vertically)
 *
 *Ft Y00.Cb00.Y01.Cr10.Y02.Cb02.Y03.Cr12 Y04.Cb04.Y05.Cr14.Y06.Cb06.Y07.Cr16
 *    ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^
 *Fb Y00.Cb00.Y01.Cr10.Y02.Cb02.Y03.Cr12 Y04.Cb04.Y05.Cr14.Y06.Cb06.Y07.Cr16
 *    ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^   ^    ^
 *Ft Y10.Cb00.Y11.Cr10.Y12.Cb02.Y13.Cr12 Y14.Cb04.Y15.Cr14.Y16.Cb06.Y17.Cr16
 *    ^        ^        ^        ^        ^        ^        ^        ^    
 *Fb Y10.Cb00.Y11.Cr10.Y12.Cb02.Y13.Cr12 Y14.Cb04.Y15.Cr14.Y16.Cb06.Y17.Cr16
 *    ^        ^        ^        ^        ^        ^        ^        ^    
 *
 *    lavtools unpacking into 4:2:0-planar
 *
 *Ft  Y00.Y01.Y02.Y03.Y04.Y05.Y06.Y07...
 *Fb  Y00.Y01.Y02.Y03.Y04.Y05.Y06.Y07...
 *Ft  Y10.Y11.Y12.Y13.Y14.Y15.Y16.Y17...
 *Fb  Y10.Y11.Y12.Y13.Y14.Y15.Y16.Y17...
 *
 *Ft  Cb00.Cb02.Cb04.Cb06...
 *Fb  Cb00.Cb02.Cb04.Cb06...
 *
 *Ft  Cr10.Cr12.Cr14.Cr16...
 *Fb  Cr10.Cr12.Cr14.Cr16...
 *
 */

/*
 * Unpack libdv's 4:2:2-packed into our 4:2:0-planar
 *
 */
void frame_YUV422_to_YUV420P(uint8_t ** output, uint8_t * input,
			     int width, int height)
{
    int i, j, w2;
    uint8_t *y, *cb, *cr;

    w2 = width / 2;
    y = output[0];
    cb = output[1];
    cr = output[2];

    for (i = 0; i < height; i += 2) {
	/* process two scanlines (one from each field, interleaved) */
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(cb++) = *(input++);
	    *(y++) = *(input++);
	    *(cr++) = *(input++);
	}
	/* process next two scanlines (one from each field, interleaved) */
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}
    }
}

#endif				/* SUPPORT_READ_DV2 */



/***********************
 *
 * Take a random(ish) sampled mean of a frame luma and chroma
 * Its intended as a rough and ready hash of frame content.
 * The idea is that changes above a certain threshold are treated as
 * scene changes.
 *
 **********************/

int luminance_mean(uint8_t * frame[], int w, int h)
{
    uint8_t *p;
    uint8_t *lim;
    int sum = 0;
    int count = 0;
    p = frame[0];
    lim = frame[0] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }

    w = w / 2;
    h = h / 2;

    p = frame[1];
    lim = frame[1] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }
    p = frame[2];
    lim = frame[2] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }
    return sum / count;
}
