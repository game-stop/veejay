/*
 * colorspace.c:  Routines to perform colorspace conversions.
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
#include <mjpeg_types.h>

#include "colorspace.h"

#define FP_BITS 18

/* precomputed tables */

static int Y_R[256];
static int Y_G[256];
static int Y_B[256];
static int Cb_R[256];
static int Cb_G[256];
static int Cb_B[256];
static int Cr_R[256];
static int Cr_G[256];
static int Cr_B[256];
static int conv_RY_inited = 0;

static int RGB_Y[256];
static int R_Cr[256];
static int G_Cb[256];
static int G_Cr[256];
static int B_Cb[256];
static int conv_YR_inited = 0;


static int myround(double n)
{
  if (n >= 0) 
    return (int)(n + 0.5);
  else
    return (int)(n - 0.5);
}



static void init_RGB_to_YCbCr_tables(void)
{
  int i;

  /*
   * Q_Z[i] =   (coefficient * i
   *             * (Q-excursion) / (Z-excursion) * fixed-point-factor)
   *
   * to one of each, add the following:
   *             + (fixed-point-factor / 2)         --- for rounding later
   *             + (Q-offset * fixed-point-factor)  --- to add the offset
   *             
   */
  for (i = 0; i < 256; i++) {
    Y_R[i] = myround(0.299 * (double)i 
		     * 219.0 / 255.0 * (double)(1<<FP_BITS));
    Y_G[i] = myround(0.587 * (double)i 
		     * 219.0 / 255.0 * (double)(1<<FP_BITS));
    Y_B[i] = myround((0.114 * (double)i 
		      * 219.0 / 255.0 * (double)(1<<FP_BITS))
		     + (double)(1<<(FP_BITS-1))
		     + (16.0 * (double)(1<<FP_BITS)));

    Cb_R[i] = myround(-0.168736 * (double)i 
		      * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cb_G[i] = myround(-0.331264 * (double)i 
		      * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cb_B[i] = myround((0.500 * (double)i 
		       * 224.0 / 255.0 * (double)(1<<FP_BITS))
		      + (double)(1<<(FP_BITS-1))
		      + (128.0 * (double)(1<<FP_BITS)));

    Cr_R[i] = myround(0.500 * (double)i 
		      * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cr_G[i] = myround(-0.418688 * (double)i 
		      * 224.0 / 255.0 * (double)(1<<FP_BITS));
    Cr_B[i] = myround((-0.081312 * (double)i 
		       * 224.0 / 255.0 * (double)(1<<FP_BITS))
		      + (double)(1<<(FP_BITS-1))
		      + (128.0 * (double)(1<<FP_BITS)));
  }
  conv_RY_inited = 1;
}




static void init_YCbCr_to_RGB_tables(void)
{
  int i;

  /*
   * Q_Z[i] =   (coefficient * i
   *             * (Q-excursion) / (Z-excursion) * fixed-point-factor)
   *
   * to one of each, add the following:
   *             + (fixed-point-factor / 2)         --- for rounding later
   *             + (Q-offset * fixed-point-factor)  --- to add the offset
   *             
   */

  /* clip Y values under 16 */
  for (i = 0; i < 16; i++) {
    RGB_Y[i] = myround((1.0 * (double)(16 - 16) 
		     * 255.0 / 219.0 * (double)(1<<FP_BITS))
		    + (double)(1<<(FP_BITS-1)));
  }
  for (i = 16; i < 236; i++) {
    RGB_Y[i] = myround((1.0 * (double)(i - 16) 
		     * 255.0 / 219.0 * (double)(1<<FP_BITS))
		    + (double)(1<<(FP_BITS-1)));
  }
  /* clip Y values above 235 */
  for (i = 236; i < 256; i++) {
    RGB_Y[i] = myround((1.0 * (double)(235 - 16) 
		     * 255.0 / 219.0 * (double)(1<<FP_BITS))
		    + (double)(1<<(FP_BITS-1)));
  }
    
  /* clip Cb/Cr values below 16 */	 
  for (i = 0; i < 16; i++) {
    R_Cr[i] = myround(1.402 * (double)(-112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(-112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(-112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(-112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  for (i = 16; i < 241; i++) {
    R_Cr[i] = myround(1.402 * (double)(i - 128)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(i - 128)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(i - 128)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(i - 128)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  /* clip Cb/Cr values above 240 */	 
  for (i = 241; i < 256; i++) {
    R_Cr[i] = myround(1.402 * (double)(112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cr[i] = myround(-0.714136 * (double)(112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    G_Cb[i] = myround(-0.344136 * (double)(i - 128)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
    B_Cb[i] = myround(1.772 * (double)(112)
		   * 255.0 / 224.0 * (double)(1<<FP_BITS));
  }
  conv_YR_inited = 1;
}





/* 
 * in-place conversion [R', G', B'] --> [Y', Cb, Cr]
 *
 */

void convert_RGB_to_YCbCr(uint8_t *planes[], int length)
{
  uint8_t *Y, *Cb, *Cr;
  int i;

  if (!conv_RY_inited) init_RGB_to_YCbCr_tables();

  for ( i = 0, Y = planes[0], Cb = planes[1], Cr = planes[2];
	i < length;
	i++, Y++, Cb++, Cr++ ) {
    int r = *Y;
    int g = *Cb;
    int b = *Cr;

    *Y = (Y_R[r] + Y_G[g]+ Y_B[b]) >> FP_BITS;
    *Cb = (Cb_R[r] + Cb_G[g]+ Cb_B[b]) >> FP_BITS;
    *Cr = (Cr_R[r] + Cr_G[g]+ Cr_B[b]) >> FP_BITS;
  }
}



/* 
 * in-place conversion [Y', Cb, Cr] --> [R', G', B']
 *
 */

void convert_YCbCr_to_RGB(uint8_t *planes[], int length)
{
  uint8_t *R, *G, *B;
  int i;

  if (!conv_YR_inited) init_YCbCr_to_RGB_tables();

  for ( i = 0, R = planes[0], G = planes[1], B = planes[2];
	i < length;
	i++, R++, G++, B++ ) {
    int y = *R;
    int cb = *G;
    int cr = *B;

    int r = (RGB_Y[y] + R_Cr[cr]) >> FP_BITS;
    int g = (RGB_Y[y] + G_Cb[cb]+ G_Cr[cr]) >> FP_BITS;
    int b = (RGB_Y[y] + B_Cb[cb]) >> FP_BITS;

    *R = (r < 0) ? 0 : (r > 255) ? 255 : r ;
    *G = (g < 0) ? 0 : (g > 255) ? 255 : g ;
    *B = (b < 0) ? 0 : (b > 255) ? 255 : b ;
  }
  
}


#if 0

/* This stuff ain't ready for prime-time yet,
   mostly because I haven't figured out if JPEG Y' is scaled to 255,
   or clipped to 255...
     - Matto
*/

static uint8_t JPEG_TO_R601_Y[256];
static uint8_t JPEG_TO_R601_C[256];
static uint8_t R601_TO_JPEG_Y[256];
static uint8_t R601_TO_JPEG_C[256];
static int conv_YJR_inited = 0;
static int conv_YRJ_inited = 0;

#define CONVERT_Y  1
#define CONVERT_CB 2
#define CONVERT_CR 3

static void init_YCbCr_JPEG_to_Rec601_tables()
{
  int i;
  for (i = 0; i < 255; i++) {

    JPEG_TO_R601_Y[i] =   i * 219.0 / 256.0 + 16;
    JPEG_TO_R601_C[i] =   (i - 128) * 224.0 / 256.0 + 128;

  }
}

static void init_YCbCr_Rec601_to_JPEG_tables()
{
  int i;
  for (i = 0; i < 255; i++) {

    R601_TO_JPEG_Y[i] =   (i - 16) / 219.0 * 256.0;
    R601_TO_JPEG_C[i] =   (i - 128) / 224.0 * 256.0 + 128;

  }
}

/* 
 * in-place conversion JPEG [Y', Cb, Cr] --> CCIR-601 [Y', Cb, Cr]
 *
 */

void convert_YCbCr_JPEG_to_Rec601(int plane_type,
				  uint8_t data[], int length)
{
  int i;

  if (!conv_YJR_inited) init_YCbCr_JPEG_to_Rec601_tables();

  switch (plane_type) {
  case CONVERT_Y:
    for (i = 0; i < length; i++, data++) {
      *data = JPEG_TO_R601_Y[*data];
    }
    break;
  case CONVERT_CB:
  case CONVERT_CR:
    for (i = 0; i < length; i++, data++) {
      *data = JPEG_TO_R601_C[*data];
    }
    break;
  }
}



/* 
 * in-place conversion JPEG [Y', Cb, Cr] --> CCIR-601 [Y', Cb, Cr]
 *
 */

void convert_YCbCr_Rec601_to_JPEG(int plane_type,
				  uint8_t data[], int length)
{
  int i;

  if (!conv_YRJ_inited) init_YCbCr_Rec601_to_JPEG_tables();

  switch (plane_type) {
  case CONVERT_Y:
    for (i = 0; i < length; i++, data++) {
      *data = R601_TO_JPEG_Y[*data];
    }
    break;
  case CONVERT_CB:
  case CONVERT_CR:
    for (i = 0; i < length; i++, data++) {
      *data = R601_TO_JPEG_C[*data];
    }
    break;
  }
}

#endif
