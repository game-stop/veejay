/*
 * subsample.c:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *
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
 *
 */



#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mjpeg_types.h>
#include <veejay/vj-common.h>
#include <veejay/subsample.h>
extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;


const char *ssm_id[SSM_COUNT] = {
  "unknown",
  "420_jpeg",
  "420_mpeg2",
#if 0
  "420_dv_pal",
  "411_dv_ntsc"
#endif
};


const char *ssm_description[SSM_COUNT] = {
  "unknown/illegal",
  "4:2:0, JPEG/MPEG-1, interstitial siting",
  "4:2:0, MPEG-2, horizontal cositing",
#if 0
  "4:2:0, DV-PAL, cosited, Cb/Cr line alternating",
  "4:1:1, DV-NTSC"
  "4:2:2",
#endif
};


/*************************************************************************
 * Chroma Subsampling
 *************************************************************************/


/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 */

/*
static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;
  in1 = buffer + width;
  out = buffer;
  for (y = 0; y < height; y += 2) {
    for (x = 0; x < width; x += 2) {
      *out = (in0[0] + in0[1] + in1[0] + in1[1]) >> 2;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width;
    in1 += width;
  }
}
*/
/*

        using weighted averaging for subsampling 2x2 -> 1x1
	here, 4 pixels are filled in each inner loop, (weighting
        16 source pixels)
*/

static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
  const uint8_t *in0, *in1;
  uint8_t *out;
  int x, y = height;
  in0 = buffer;
  in1 = buffer + width;
  out = buffer;
  for (y = 0; y < height; y += 4) {
    for (x = 0; x < width; x += 4) {
     out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
     out[1] = (in0[2] + 3 * (in0[3] + in1[2]) + (9 * in1[3]) + 8) >> 4;
     out[2] = (in0[4] + 3 * (in0[5] + in1[4]) + (9 * in1[5]) + 8) >> 4;
     out[3] = (in0[6] + 3 * (in0[7] + in1[6]) + (9 * in1[7]) + 8) >> 4;

      in0 += 8;
      in1 += 8;
      out += 4;
    }
    for (  ; x < width; x +=2 )
    {
 	out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
        in0 += 2;
        in1 += 2;
	out++;
    }
    in0 += width*2;
    in1 += width*2;
  }
}


/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C       C      inm
 *    Y   Y   Y   Y           
 *                  
 *    Y   Y   Y - Y           out0
 *      C     | C |     C      in0
 *    Y   Y   Y - Y           out1
 *
 *
 *      C       C       C      inp
 *
 *
 *  Each iteration through the loop reconstitutes one 2x2 block of 
 *   pixels from the "surrounding" 3x3 block of samples...
 *  Boundary conditions are handled by cheap reflection; i.e. the
 *   center sample is simply reused.
 *              
 */             


#define BLANK_CRB in0[1]
#define BLANK_CRB_2 (in0[1] << 1)

static void tr_420jpeg_to_444(uint8_t *buffer, int width, int height)
{
  uint8_t *inm, *in0, *inp, *out0, *out1;
  uint8_t cmm, cm0, cmp, c0m, c00, c0p, cpm, cp0, cpp;
  int x, y;
  static uint8_t *saveme = NULL;
  static int saveme_size = 0;
  if (width > saveme_size) {
    free(saveme);
    saveme_size = width;
    saveme = vj_malloc(saveme_size * sizeof(saveme[0]));
    assert(saveme != NULL);
  }
  veejay_memcpy(saveme, buffer, width);

  in0 = buffer + ( width * height /4) - 2;
  inm = in0 - width/2;
  inp = in0 + width/2;
  out1 = buffer + (width * height) - 1;
  out0 = out1 - width;

  for (y = height; y > 0; y -= 2) {
    if (y == 2) {
      in0 = saveme + width/2 - 2;
      inp = in0 + width/2;
    }
    for (x = width; x > 0; x -= 2) {
#if 0
      if ((x == 2) && (y == 2)) {
	cmm = in0[1];
	cm0 = in0[1];
	cmp = in0[2];
	c0m = in0[1];
	c0p = in0[2];
	cpm = inp[1];
	cp0 = inp[1];
	cpp = inp[2];
      } else if ((x == 2) && (y == height)) {
	cmm = inm[1];
	cm0 = inm[1];
	cmp = inm[2];
	c0m = in0[1];
	c0p = in0[2];
	cpm = in0[1];
	cp0 = in0[1];
	cpp = in0[2];
      } else if ((x == width) && (y == height)) {
	cmm = inm[0];
	cm0 = inm[1];
	cmp = inm[1];
	c0m = in0[0];
	c0p = in0[1];
	cpm = in0[0];
	cp0 = in0[1];
	cpp = in0[1];
      } else if ((x == width) && (y == 2)) {
	cmm = in0[0];
	cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (x == 2) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (y == 2) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (x == width) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (y == height) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      }
      c00 = in0[1];

      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
#else
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c00 = in0[1];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
#endif
      inm--;
      in0--;
      inp--;

      *(out1--) = (1*cpp + 3*(cp0+c0p) + 9*c00 + 8) >> 4;
      *(out1--) = (1*cpm + 3*(cp0+c0m) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmp + 3*(cm0+c0p) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmm + 3*(cm0+c0m) + 9*c00 + 8) >> 4;
    }
    out1 -= width;
    out0 -= width;
  }
}

// lame box filter
// the dampening of high frequencies depend
// on the directions these frequencies occur in the
// image, resulting in clear edges between certain
// group of pixels.

static void ss_420jpeg_to_444(uint8_t *buffer, int width, int height)
{
  uint8_t *in, *out0, *out1;
  int x, y;
  in = buffer + (width * height / 4) - 1;
  out1 = buffer + (width * height) - 1;
  out0 = out1 - width;
  for (y = height - 1; y >= 0; y -= 2) {
    for (x = width - 1; x >= 0; x -=2) {
      uint8_t val = *(in--);
      *(out1--) = val;
      *(out1--) = val;
      *(out0--) = val;
      *(out0--) = val;
    }
    out0 -= width;
    out1 -= width;
  }
}


/* vertical intersitial siting; horizontal cositing
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 * [1,2,1] kernel for horizontal subsampling:
 *
 *    inX[0] [1] [2]
 *        |   |   |
 *    C   C   C   C
 *         \  |  /
 *          \ | /
 *            C
 */

static void ss_444_to_420mpeg2(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;          /* points to */
  in1 = buffer + width;  /* second of pair of lines */
  out = buffer;
  for (y = 0; y < height; y += 2) {
    /* first column boundary condition -- just repeat it to right */
    *out = (in0[0] + (2 * in0[0]) + in0[1] +
	    in1[0] + (2 * in1[0]) + in1[1]) >> 3;
    out++;
    in0++;
    in1++;
    /* rest of columns just loop */
    for (x = 2; x < width; x += 2) {
      *out = (in0[0] + (2 * in0[1]) + in0[2] +
	      in1[0] + (2 * in1[1]) + in1[2]) >> 3;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width + 1;
    in1 += width + 1;
  }
}
      




void chroma_subsample(subsample_mode_t mode, uint8_t *ycbcr[],
		      int width, int height)
{
  switch (mode) {
  case SSM_420_JPEG_BOX:
  case SSM_420_JPEG_TR: 
    ss_444_to_420jpeg(ycbcr[1], width, height);
    ss_444_to_420jpeg(ycbcr[2], width, height);
    break;
  case SSM_420_MPEG2:
    ss_444_to_420mpeg2(ycbcr[1], width, height);
    ss_444_to_420mpeg2(ycbcr[2], width, height);
    break;
  default:
    break;
  }
}


void chroma_supersample(subsample_mode_t mode, uint8_t *ycbcr[],
			int width, int height)
{
  switch (mode) {
  case SSM_420_JPEG_BOX:
    ss_420jpeg_to_444(ycbcr[1], width, height);
    ss_420jpeg_to_444(ycbcr[2], width, height);
    break;
  case SSM_420_JPEG_TR:
    tr_420jpeg_to_444(ycbcr[1], width, height);
    tr_420jpeg_to_444(ycbcr[2], width, height);
    break;

  case SSM_420_MPEG2:
    //    ss_420mpeg2_to_444(ycbcr[1], width, height);
    //    ss_420mpeg2_to_444(ycbcr[2], width, height);
    exit(4);
    break;
  default:
    break;
  }
}


