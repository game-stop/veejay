/*
 * subsample.c:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *                2004 Niels Elburg <nwelburg@gmail.com>
 *                2014 added mmx routines
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
#include <stdint.h>
#include <veejaycore/defs.h>
#ifdef HAVE_ASM_MMX
#include <veejaycore/mmx.h>
#include <veejaycore/mmx_macros.h>
#include "subsample-mmx.h"
#endif
#ifdef HAVE_ASM_SSE2
#include <emmintrin.h>
#endif
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <veejaycore/mjpeg_types.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>

#define BLANK_CRB in0[1]
#define BLANK_CRB_2 (in0[1] << 1)

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

#if !defined(HAVE_ARM) && !defined(HAVE_ASM_SSE2)
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
#endif
#ifdef HAVE_ARM
void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
    const uint8_t *in0, *in1;
    uint8_t *out;
    int x, y;

    const uint8_t is_width_even = (width & 2) == 0;

    in0 = buffer;
    in1 = buffer + width;
    out = buffer;

    for (y = 0; y < height; y += 4)
    {
        for (x = 0; x < width; x += 4)
        {
            uint8x16_t vin0 = vld1q_u8(in0);
            uint8x16_t vin1 = vld1q_u8(in1);

            uint8x16_t vresult = vrhaddq_u8(vin0, vin1);
            vst1q_u8(out, vresult);

            in0 += 16;
            in1 += 16;
            out += 4;
        }

        if (!is_width_even)
        {
            uint8x8_t vin0 = vld1_u8(in0);
            uint8x8_t vin1 = vld1_u8(in1);

            uint8x8_t vresult = vrhadd_u8(vin0, vin1);
            vst1_u8(out, vresult);

            in0 += 8;
            in1 += 8;
            out += 1;
        }

        in0 += width * 2;
        in1 += width * 2;
    }
}
#endif

#if !defined(HAVE_ARM) && !defined(HAVE_ASM_SSE2)
static void ss_444_to_420jpeg_cp(uint8_t *buffer,uint8_t *dest, int width, int height)
{
  const uint8_t *in0, *in1;
  uint8_t *out;
  int x, y = height;
  in0 = buffer;
  in1 = buffer + width;
  out = dest;
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
#endif
#ifdef HAVE_ARM
void ss_444_to_420jpeg_cp(uint8_t *buffer, uint8_t *dest, int width, int height)
{
    const uint8_t *in0, *in1;
    uint8_t *out;
    int x, y;

    const uint8_t is_width_even = (width & 2) == 0;

    in0 = buffer;
    in1 = buffer + width;
    out = dest;

    for (y = 0; y < height; y += 4)
    {
        for (x = 0; x < width; x += 4)
        {
            uint8x16_t vin0 = vld1q_u8(in0);
            uint8x16_t vin1 = vld1q_u8(in1);

            uint8x16_t vresult = vrhaddq_u8(vin0, vin1);

            vst1q_u8(out, vresult);

            in0 += 16;
            in1 += 16;
            out += 4;
        }

        if (!is_width_even)
        {
            uint8x8_t vin0 = vld1_u8(in0);
            uint8x8_t vin1 = vld1_u8(in1);

            uint8x8_t vresult = vrhadd_u8(vin0, vin1);

            vst1_u8(out, vresult);

            in0 += 8;
            in1 += 8;
            out += 1;
        }

        in0 += width * 2;
        in1 += width * 2;
    }
}
#endif
#ifdef HAVE_ASM_SSE2
static void ss_444_to_420jpeg_cp(uint8_t* buffer, uint8_t* dest, int width, int height) {
    const uint8_t* in0, * in1;
    uint8_t* out;
    int x, y = height;
    in0 = buffer;
    in1 = buffer + width;
    out = dest;

    for (y = 0; y < height; y += 4) {
        for (x = 0; x < width; x += 4) {
            __m128i vin0 = _mm_unpacklo_epi8(_mm_loadu_si128((__m128i*)in0), _mm_setzero_si128());
            __m128i vin1 = _mm_unpacklo_epi8(_mm_loadu_si128((__m128i*)in1), _mm_setzero_si128());

            __m128i result0 = _mm_sra_epi16(_mm_adds_epu16(vin0, _mm_slli_epi16(vin1, 1)), _mm_set1_epi16(4));
            __m128i result1 = _mm_sra_epi16(_mm_adds_epu16(vin1, _mm_slli_epi16(vin0, 1)), _mm_set1_epi16(4));

            __m128i packed_result = _mm_packus_epi16(result0, result1);

            _mm_storeu_si128((__m128i*)out, packed_result);

            in0 += 16;
            in1 += 16;
            out += 16;
        }

        for (; x < width; x += 2) {
            __m128i vin0 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i*)in0), _mm_setzero_si128());
            __m128i vin1 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i*)in1), _mm_setzero_si128());

            __m128i result = _mm_sra_epi16(_mm_adds_epu16(vin0, _mm_slli_epi16(vin1, 1)), _mm_set1_epi16(4));

            __m128i packed_result = _mm_packus_epi16(result, result);

            _mm_storel_epi64((__m128i*)out, packed_result);

            in0 += 8;
            in1 += 8;
            out += 8;
        }

        in0 += width * 2;
        in1 += width * 2;
    }
}
#endif

#ifdef HAVE_ASM_SSE2
static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
    const uint8_t *in0, *in1;
    uint8_t *out;
    int x, y;

    in0 = buffer;
    in1 = buffer + width;
    out = buffer;

    for (y = 0; y < height; y += 4)
    {
        for (x = 0; x < width; x += 4)
        {
            __m128i vin0 = _mm_loadu_si128((__m128i*)in0);
            __m128i vin1 = _mm_loadu_si128((__m128i*)in1);

            __m128i vsum0 = _mm_adds_epu8(vin0, _mm_slli_epi16(vin1, 1));
            __m128i vsum1 = _mm_adds_epu8(_mm_srli_epi16(vin0, 1), _mm_slli_epi16(vin1, 3));
            __m128i vsum2 = _mm_adds_epu8(_mm_srli_epi16(vin0, 3), _mm_slli_epi16(vin1, 4));
            __m128i vsum3 = _mm_adds_epu8(_mm_srli_epi16(vin0, 4), _mm_slli_epi16(vin1, 3));

            vsum0 = _mm_srli_epi16(_mm_adds_epu8(vsum0, _mm_set1_epi8(8)), 4);
            vsum1 = _mm_srli_epi16(_mm_adds_epu8(vsum1, _mm_set1_epi8(8)), 4);
            vsum2 = _mm_srli_epi16(_mm_adds_epu8(vsum2, _mm_set1_epi8(8)), 4);
            vsum3 = _mm_srli_epi16(_mm_adds_epu8(vsum3, _mm_set1_epi8(8)), 4);

            __m128i vout = _mm_packus_epi16(_mm_packus_epi16(vsum0, vsum1), _mm_packus_epi16(vsum2, vsum3));
            _mm_storeu_si128((__m128i*)out, vout);

            in0 += 8;
            in1 += 8;
            out += 4;
        }

        for (; x < width; x += 2)
        {
            __m128i vin0 = _mm_loadl_epi64((__m128i*)in0);
            __m128i vin1 = _mm_loadl_epi64((__m128i*)in1);

            __m128i vsum = _mm_adds_epu8(vin0, _mm_slli_epi16(vin1, 1));
            vsum = _mm_srli_epi16(_mm_adds_epu8(vsum, _mm_set1_epi8(8)), 4);

            _mm_storel_epi64((__m128i*)out, vsum);

            in0 += 2;
            in1 += 2;
            out++;
        }

        in0 += width * 2;
        in1 += width * 2;
    }
}
#endif
 

/* horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *    C   C   C   C     in0 
 *    Y   Y   Y   Y         
 *    C   C   C   C      
 *           
 *    Y   Y   Y   Y       
 *    C       C     	out0  
 *    Y   Y   Y   Y       
 *    C       C  
 *
 *
 */             



 
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
#if !defined(HAVE_ARM) && !defined(HAVE_ASM_SSE2)
static void tr_420jpeg_to_444(uint8_t *data, uint8_t *buffer, int width, int height)
{
  uint8_t *inm, *in0, *inp, *out0, *out1;
  uint8_t cmm, cm0, cmp, c0m, c00, c0p, cpm, cp0, cpp;
  int x, y;

  uint8_t *saveme = data;

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

      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c00 = in0[1];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];

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
#endif
#ifdef HAVE_ASM_SSE2
static void tr_420jpeg_to_444(uint8_t *data, uint8_t *buffer, int width, int height)
{
    uint8_t *inm, *in0, *inp, *out0, *out1;
    int x, y;

    uint8_t *saveme = data;

    veejay_memcpy(saveme, buffer, width);

    in0 = buffer + (width * height / 4) - 2;
    inm = in0 - width / 2;
    inp = in0 + width / 2;
    out1 = buffer + (width * height) - 1;
    out0 = out1 - width;

    __m128i zero = _mm_setzero_si128();
    __m128i eight = _mm_set1_epi8(8);

    for (y = height; y > 0; y -= 2) {
        if (y == 2) {
            in0 = saveme + width / 2 - 2;
            inp = in0 + width / 2;
        }
        for (x = width; x > 0; x -= 2) {
            __m128i vin0 = _mm_loadu_si128((__m128i*)in0);
            __m128i vinm = _mm_loadu_si128((__m128i*)inm);
            __m128i vinp = _mm_loadu_si128((__m128i*)inp);

            __m128i vsum1 = _mm_adds_epu8(_mm_adds_epu8(_mm_adds_epu8(_mm_adds_epu8(vin0, vinp), _mm_adds_epu8(vinm, vin0)), _mm_adds_epu8(vinm, vinp)), eight);
            __m128i vsum2 = _mm_adds_epu8(_mm_adds_epu8(_mm_adds_epu8(vinm, vinp), _mm_adds_epu8(vin0, vin0)), eight);
            __m128i vsum3 = _mm_adds_epu8(_mm_adds_epu8(_mm_adds_epu8(vinm, vinm), _mm_adds_epu8(vin0, vin0)), eight);

            __m128i vout0 = _mm_srli_epi16(vsum1, 4);
            __m128i vout1 = _mm_srli_epi16(vsum2, 4);
            __m128i vout2 = _mm_srli_epi16(vsum3, 4);

            _mm_storeu_si128((__m128i*)out1, vout0);
            _mm_storeu_si128((__m128i*)out0, vout1);
            _mm_storeu_si128((__m128i*)(out1 - width), vout2);

            inm--;
            in0--;
            inp--;

            out1 -= 2;
            out0 -= 2;
        }
        out1 -= width;
        out0 -= width;
    }
}
#endif
#ifdef HAVE_ARM
static void tr_420jpeg_to_444(uint8_t *data, uint8_t *buffer, int width, int height)
{
    uint8_t *inm, *in0, *inp, *out0, *out1;
    int x, y;

    uint8_t *saveme = data;
    veejay_memcpy(saveme, buffer, width);

    in0 = buffer + (width * height / 4) - 2;
    inm = in0 - width / 2;
    inp = in0 + width / 2;
    out1 = buffer + (width * height) - 1;
    out0 = out1 - width;

    uint8x16_t zero = vdupq_n_u8(0);
    uint8x16_t eight = vdupq_n_u8(8);

    const uint8_t is_width_multiple_of_16 = (width & 14) == 0;

    for (y = height; y > 0; y -= 2) {
        if (y == 2) {
            in0 = saveme + width / 2 - 2;
            inp = in0 + width / 2;
        }

        if (is_width_multiple_of_16) {
            for (x = width; x > 0; x -= 16) {
                uint8x16_t vin0 = vld1q_u8(in0);
                uint8x16_t vinm = vld1q_u8(inm);
                uint8x16_t vinp = vld1q_u8(inp);

                uint8x16_t vsum1 = vqaddq_u8(vqaddq_u8(vqaddq_u8(vqaddq_u8(vin0, vinp), vinm), vin0), vinp);
                uint8x16_t vsum2 = vqaddq_u8(vqaddq_u8(vqaddq_u8(vinm, vinp), vin0), vin0);
                uint8x16_t vsum3 = vqaddq_u8(vqaddq_u8(vqaddq_u8(vinm, vinm), vin0), vin0);

                uint8x16_t vout0 = vshrq_n_u8(vsum1, 4);
                uint8x16_t vout1 = vshrq_n_u8(vsum2, 4);
                uint8x16_t vout2 = vshrq_n_u8(vsum3, 4);

                vst1q_u8(out1, vout0);
                vst1q_u8(out0, vout1);
                vst1q_u8(out1 - width, vout2);

                inm -= 16;
                in0 -= 16;
                inp -= 16;

                out1 -= 16;
                out0 -= 16;
            }
        } else {
            for (x = width; x > 0; x -= 2) {

                if (x & 14) {
                    uint8x8_t vin0 = vld1_u8(in0);
                    uint8x8_t vinm = vld1_u8(inm);
                    uint8x8_t vinp = vld1_u8(inp);

                    uint8x8_t vsum1 = vqadd_u8(vqadd_u8(vqadd_u8(vqadd_u8(vin0, vinp), vinm), vin0), vinp);
                    uint8x8_t vsum2 = vqadd_u8(vqadd_u8(vqadd_u8(vinm, vinp), vin0), vin0);
                    uint8x8_t vsum3 = vqadd_u8(vqadd_u8(vqadd_u8(vinm, vinm), vin0), vin0);

                    uint8x8_t vout0 = vshr_n_u8(vsum1, 4);
                    uint8x8_t vout1 = vshr_n_u8(vsum2, 4);
                    uint8x8_t vout2 = vshr_n_u8(vsum3, 4);

                    vst1_u8(out1, vout0);
                    vst1_u8(out0, vout1);
                    vst1_u8(out1 - width, vout2);

                    inm -= 8;
                    in0 -= 8;
                    inp -= 8;

                    out1 -= 8;
                    out0 -= 8;
                }
            }
        }
    }
}
#endif

// lame box filter
// the dampening of high frequencies depend
// on the directions these frequencies occur in the
// image, resulting in clear edges between certain
// group of pixels.

static void ss_420jpeg_to_444(uint8_t *buffer, int width, int height)
{
#if !defined(HAVE_ASM_SSE2) && !defined(HAVE_ARM) && !defined(HAVE_ASM_MMX)
    uint8_t *in, *out0, *out1;
	unsigned int x, y;
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
#endif

#ifdef HAVE_ARM_NEON
    uint8_t *in, *out0, *out1;
    int x, y;

    in = buffer + (width * height / 4) - 1;
    out1 = buffer + (width * height) - 1;
    out0 = out1 - width;

    int optimized_pixels = width - (width & 7);

    for (y = height - 1; y >= 0; y -= 2) {
        for (x = optimized_pixels - 1; x >= 0; x -= 8) {
            uint8x8_t val = vld1_u8(in);

            uint8x8x2_t duplicated_val;
            duplicated_val.val[0] = val;
            duplicated_val.val[1] = val;

            vst1q_u8(out1 - 8, vreinterpretq_u8_u16(vzip_u16(
                vreinterpret_u16_u8(duplicated_val.val[0]),
                vreinterpret_u16_u8(duplicated_val.val[1])
            )));

            vst1q_u8(out0 - 8, vreinterpretq_u8_u16(vzip_u16(
                vreinterpret_u16_u8(duplicated_val.val[0]),
                vreinterpret_u16_u8(duplicated_val.val[1])
            )));

            in -= 8;
            out1 -= 8;
            out0 -= 8;
        }

        for (x = width - 1; x >= optimized_pixels; x -= 2) {
            uint8_t val = *(in--);
            *(out1--) = val;
            *(out1--) = val;
            *(out0--) = val;
            *(out0--) = val;
        }

        out0 -= width;
        out1 -= width;
    }
#endif

#ifdef HAVE_ARM_ASIMD
    uint8_t *in, *out0, *out1;
    unsigned int x, y;
    in = buffer + (width * height / 4) - 1;
    out1 = buffer + (width * height) - 1;
    out0 = out1 - width;
    uint8x16_t val, val_dup;

    int optimized_pixels = width - (width & 15);

    for (y = height - 1; y >= 0; y -= 2) {
        for (x = optimized_pixels - 1; x >= 0; x -= 16) {
            val = vld1q_u8(in);
            val_dup = vdupq_n_u8(vgetq_lane_u8(val, 0));

            vst1q_u8(out1 - 15, val_dup);
            vst1q_u8(out0 - 15, val_dup);

            in -= 16;
            out1 -= 16;
            out0 -= 16;
        }

        for (x = width - 1; x >= optimized_pixels; x -= 2) {
            uint8_t val = *(in--);
            *(out1--) = val;
            *(out1--) = val;
            *(out0--) = val;
            *(out0--) = val;
        }

        out0 -= width;
        out1 -= width;
    }
#endif

#ifdef HAVE_ASM_SSE2
    uint8_t *in, *out0, *out1;
    unsigned int x, y;

    in = buffer + (width * height / 4) - 1;
    out1 = buffer + (width * height) - 1;
    out0 = out1 - width;

    for (y = height - 1; y >= 0; y -= 2) {
        for (x = width - 1; x >= 0; x -= 2) {
            uint8_t val = *(in--);

            __m128i val128 = _mm_set1_epi8(val);

            _mm_storel_epi64((__m128i*)(out1--), val128);
            _mm_storel_epi64((__m128i*)(out1--), val128);
            _mm_storel_epi64((__m128i*)(out0--), val128);
            _mm_storel_epi64((__m128i*)(out0--), val128);
        }
        out0 -= width;
        out1 -= width;
    }
#else
#ifdef HAVE_ASM_MMX
	int x,y;
	const int mmx_stride = width >> 3;
	uint8_t *src = buffer + ((width * height) >> 2)-1;
	uint8_t *dst = buffer + (width * height) -1;
	uint8_t *dst2 = dst - width;

	for( y = height-1; y >= 0; y -= 2)
	{
		for( x = 0; x < mmx_stride; x ++ )
		{
			movq_m2r( *src,mm0 );
			movq_m2r( *src,mm1 );
			movq_r2m(mm0, *dst );
			movq_r2m(mm1, *(dst+8) );
			movq_r2m(mm0, *dst2 );
			movq_r2m(mm1, *(dst2+8) );
			dst += 16;
			dst2 += 16;
			src += 8;
		}
		dst -= width;	
		dst2 -= width;
	}

	__asm__(_EMMS"       \n\t"
            	SFENCE"     \n\t"
            	:::"memory");
#endif
#endif

}

#if !defined(HAVE_ASM_SSE) && !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
static inline void downsample2x1( const uint8_t *src, uint8_t *dst, const int width )
{
	unsigned int x;
	unsigned int x1=0;	
	for(x=0; x < width; x+=2 , x1++)
	{
		dst[x1] = ( src[x] + src[x+1] + 1 ) >> 1;
	}
}
#endif

#ifndef HAVE_ASM_SSE2
#ifdef HAVE_ASM_MMX
static inline void downsample16x8( const uint8_t *src, uint8_t *dst, const int width, const int left )
{
	unsigned int x;
	unsigned int x1 = 0;
	unsigned int i;

	for( x= 0; x < width; x += 16, x1 += 8 ) {
		subsample_down_1x16to1x8( &dst[x1], &src[x] );
	}
	
	for(i=0; i < left; i+=2, x1++) {
		dst[x1] = ( src[x + i] + src[x + i + 1] + 1 ) >> 1;
	}
}

static inline void downsample32x16( const uint8_t *src, uint8_t *dst, const int width, const int left )
{
	unsigned int x;
	unsigned int x1 = 0;
	unsigned int i;

	for( x= 0; x < width; x += 32, x1 += 16 ) {
		subsample_down_1x32to1x16( &dst[x1], &src[x] );
	}
	
	for(i=0; i < left; i+=2, x1++) {
		dst[x1] = ( src[x + i] + src[x + i + 1] + 1 ) >> 1;
	}

}
#endif
#endif

#ifdef HAVE_ASM_SSE2
static inline void downsample2x1(const uint8_t *src, uint8_t *dst, const int width) {
    unsigned int x;
    unsigned int x1 = 0;

    for (x = 0; x < width; x += 16, x1 += 8) {
        __m128i src128 = _mm_loadu_si128((__m128i*)&src[x]);

        __m128i srcLow = _mm_unpacklo_epi8(src128, _mm_setzero_si128());
        __m128i srcHigh = _mm_unpackhi_epi8(src128, _mm_setzero_si128());

        __m128i sum = _mm_add_epi16(srcLow, srcHigh);
        sum = _mm_add_epi16(sum, _mm_set1_epi16(1));

        __m128i result = _mm_srli_epi16(sum, 1);

        __m128i result8 = _mm_packus_epi16(result, result);
        _mm_storeu_si128((__m128i*)&dst[x1], result8);
    }
}

#endif

#ifdef HAVE_ARM
static inline void downsample32x16(const uint8_t *src, uint8_t *dst, const int width, const int left)
{
    unsigned int x;
    unsigned int x1 = 0;
    unsigned int i;

    int optimized_pixels = width - left;
    if (optimized_pixels & 31) {
        optimized_pixels -= 31;
    }

    for (x = 0; x < optimized_pixels; x += 32, x1 += 16)
    {
        uint8x16x2_t vsrc = vld2q_u8(&src[x]);
        uint8x16_t vsum = vrhaddq_u8(vsrc.val[0], vsrc.val[1]);
        vst1q_u8(&dst[x1], vsum);
    }

    for (i = 0; i < left; i += 2, x1++)
    {
        dst[x1] = (src[x + i] + src[x + i + 1] + 1) >> 1;
    }
}
static inline void downsample16x8(const uint8_t *src, uint8_t *dst, const int width)
{
    unsigned int x;
    unsigned int x1 = 0;

    for (x = 0; x < width - 16; x += 16, x1 += 8) {
        uint8x16_t vsrc = vld1q_u8(&src[x]);
        uint8x8_t vsum = vpadd_u8(vget_low_u8(vsrc), vget_high_u8(vsrc));
        vsum = vrshr_n_u8(vsum, 1);
        vst1_u8(&dst[x1], vsum);
    }

    for (; x < width; x += 2, x1++) {
        dst[x1] = (src[x] + src[x + 1] + 1) >> 1;
    }
}
#endif 

static void ss_444_to_422_cp(uint8_t *buffer, uint8_t *dest, int width, int height)
{
	const unsigned int dst_stride = width >> 1;
	int y;
	const unsigned int left = dst_stride % 8;
#ifndef HAVE_ASM_SSE2
#ifdef HAVE_ASM_MMX
	subsample_load_mask16to8();
#endif
#endif

	for( y = 0; y < height; y ++ ) {
		uint8_t *src = buffer + (y*width);
		uint8_t *dst = dest + (y*dst_stride);

#ifndef HAVE_ASM_SSE2
#if defined(HAVE_ASM_MMX) || defined(HAVE_ARM)
		downsample32x16( src, dst, width,left );
#endif
#endif
#ifdef HAVE_ASM_SSE2
		downsample2x1( src, dst, width  );
#endif
	}

#ifndef HAVE_ASM_SSE2
#ifdef HAVE_ASM_MMX
	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif
#endif
}

#ifdef HAVE_ARM
static inline void subsample_up_1x16to1x32(uint8_t *in, uint8_t *out)
{
    uint8x16_t vzero = vdupq_n_u8(0);
    uint8x16_t vin = vld1q_u8(in);

    uint8x8_t vin_low = vget_low_u8(vin);
    uint8x8_t vin_high = vget_high_u8(vin);

    vin_low = vshr_n_u8(vin_low, 1); 
    vin_high = vshr_n_u8(vin_high, 1); 

    uint8x16_t vout = vcombine_u8(vin_low, vin_high);

    vst1q_u8(out, vout);
}
#endif


static void tr_422_to_444( uint8_t *buffer, int width, int height)
{
	int x,y;
	const int stride = width >> 1;
#ifndef HAVE_ASM_SSE2
#if !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
	for( y = height-1; y > 0 ; y -- ) {
		uint8_t *dst = buffer + (y * width);
		uint8_t *src = buffer + (y * stride);
		for(x=0; x < stride; x++) // for 1 row
		{
			dst[0] = src[x]; //put to dst
			dst[1] = src[x];
			dst+=2; // increment dst
		}
	}
#endif
#endif
#ifdef HAVE_ASM_SSE2
    for (y = height - 1; y > 0; y--) {
        uint8_t* dst = buffer + (y * width);
        uint8_t* src = buffer + (y * stride);
        for (x = 0; x < stride; x++) {
            __m128i srcValue = _mm_set1_epi8(src[x]);
            __m128i duplicatedValue = _mm_unpacklo_epi8(srcValue, srcValue);
            _mm_storeu_si128((__m128i*)dst, duplicatedValue);
            dst += 2;
        }
    }
#endif

#ifndef HAVE_ASM_SSE2
#if defined(HAVE_ASM_MMX)
	for( y = height -1 ; y > 0; y -- ) {
		uint8_t *src = buffer + (y* stride);
		uint8_t *dst = buffer + (y* width);
		unsigned int x1 = 0;	
		for( x = 0; x < stride; x += 16, x1 += 32 ) {
			subsample_up_1x16to1x32( &src[x], &dst[x1] );
		}
	}
	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif
#endif

#ifdef HAVE_ARM
    int optimized_pixels = width - (width & 15);

    for (y = height - 1; y >= 0; y--) { 
        uint8_t *dst = buffer + (y * width);
        uint8_t *src = buffer + (y * width / 2);

        for (x = 0; x < optimized_pixels; x += 16) { 
            uint8x8_t vin = vld1_u8(src);
            uint8x16_t vout = vcombine_u8(vin, vin);
            vst1q_u8(dst, vout);
            src += 8;
            dst += 16;
        }

        for (; x < width; x += 2) {
            dst[0] = src[x];
            dst[1] = src[x];
            dst += 2;
        }
    }
#endif

}

static void tr_422_to_444t(uint8_t *out, uint8_t *in, int width, int height)
{
	int x,y;
	const int stride = width >> 1;
#ifndef HAVE_ASM_SSE2
#if !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
	for( y = height; y > 0 ; y -- ) {
		uint8_t *d = out + (y * width);
		uint8_t *s = in + (y * stride);
		for(x=0; x < stride; x++) // for 1 row
		{
			d[0] = s[x]; //put to dst
			d[1] = s[x];
			d+=2; // increment dst
		}
	}
#endif
#endif

#ifdef HAVE_ASM_SSE2
    for (y = height; y > 0; y--) {
        uint8_t* d = out + (y * width);
        uint8_t* s = in + (y * stride);
        for (x = 0; x < stride; x++) {
            __m128i srcValue = _mm_set1_epi8(s[x]);

            __m128i duplicatedValue = _mm_unpacklo_epi8(srcValue, srcValue);

            _mm_storeu_si128((__m128i*)d, duplicatedValue);

            d += 2;
        }
    }
#endif

#ifndef HAVE_ASM_SSE2
#if defined(HAVE_ASM_MMX)
	int x1 = 0;
	for( y = height -1 ; y > 0; y -- ) {
		uint8_t *src = in + (y* stride);
		uint8_t *dst = out + (y* width);
		for( x = 0; x < stride; x += 16, x1 += 32 ) {
			subsample_up_1x16to1x32(&src[x], &dst[x1] );
		}
	}
	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif
#endif

#ifdef HAVE_ARM
    for (y = height; y > 0; y--) {
            uint8_t *d = out + ((y - 1) * width);
            uint8_t *s = in + ((y - 1) * stride);

            for (x = 0; x < stride; x += 8) {
                uint8x8_t vin = vld1_u8(s);
                uint8x16_t vout = vcombine_u8(vin, vin); 
                vst1q_u8(d, vout); 

                s += 8;
                d += 16;
            }

            for (; x < stride; x += 2) {
                d[0] = s[x];
                d[1] = s[x];
                d += 2;
            }
    }
#endif

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

#if !defined(HAVE_ASM_SSE2) 
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
#endif
#ifdef HAVE_ASM_SSE2
static void ss_444_to_420mpeg2(uint8_t *buffer, int width, int height)
{
    uint8_t *in0, *in1, *out;
    int x, y;

    in0 = buffer;
    in1 = buffer + width;
    out = buffer;

    for (y = 0; y < height; y += 2)
    {
        __m128i v0 = _mm_loadu_si128((__m128i*)in0);
        __m128i v1 = _mm_loadu_si128((__m128i*)in1);
        __m128i vsum = _mm_adds_epu8(v0, v1);
        vsum = _mm_srli_epi16(vsum, 1);
        __m128i vout = _mm_packus_epi16(vsum, _mm_setzero_si128());
        _mm_storel_epi64((__m128i*)out, vout);
        out++;
        in0++;
        in1++;

        for (x = 2; x < width; x += 2)
        {
            v0 = _mm_loadu_si128((__m128i*)in0);
            v1 = _mm_loadu_si128((__m128i*)in1);
            vsum = _mm_adds_epu8(v0, v1);
            vsum = _mm_srli_epi16(vsum, 1);
            vout = _mm_packus_epi16(vsum, _mm_setzero_si128());
            _mm_storel_epi64((__m128i*)out, vout);
            in0 += 2;
            in1 += 2;
            out++;
        }

        in0 += width + 1;
        in1 += width + 1;
    }
}
#endif
    
void chroma_subsample_cp(subsample_mode_t mode,VJFrame *frame, uint8_t *ycbcr[], uint8_t *dcbcr[])
{
	switch (mode) {
		case SSM_420_JPEG_BOX:
		case SSM_420_JPEG_TR:
			ss_444_to_420jpeg_cp(ycbcr[1],dcbcr[1], frame->width, frame->height);
			ss_444_to_420jpeg_cp(ycbcr[2],dcbcr[2], frame->width, frame->height);
			break;
		case SSM_420_MPEG2:
 			break;
		case SSM_422_444:
  			ss_444_to_422_cp(ycbcr[1],dcbcr[1],frame->width,frame->height);
			ss_444_to_422_cp(ycbcr[2],dcbcr[2],frame->width,frame->height);
		    break;
  		default:
   		break;
  	}
}

void chroma_subsample(subsample_mode_t mode, VJFrame *frame, uint8_t *ycbcr[] )
{
	switch (mode) {
		case SSM_420_JPEG_BOX:
		case SSM_420_JPEG_TR: 
			ss_444_to_420jpeg(ycbcr[1], frame->width, frame->height);
 			ss_444_to_420jpeg(ycbcr[2], frame->width, frame->height);
			break;
		case SSM_420_MPEG2:
			ss_444_to_420mpeg2(ycbcr[1], frame->width, frame->height);
			ss_444_to_420mpeg2(ycbcr[2], frame->width, frame->height);
			break;
		case SSM_422_444:
		    ss_444_to_422_cp(ycbcr[1],ycbcr[1],frame->width,frame->height);
		    ss_444_to_422_cp(ycbcr[2],ycbcr[2],frame->width,frame->height);
    		break;
		default:
		break;
  	}
}


void chroma_supersample(subsample_mode_t mode,VJFrame *frame, uint8_t *ycbcr[] )
{
    uint8_t *_chroma_supersample_data = NULL;


	if( mode == SSM_420_JPEG_TR ) {
 		_chroma_supersample_data = (uint8_t*) vj_calloc( sizeof(uint8_t) * (frame->width * 2) );
	}

	switch (mode) {
		case SSM_420_JPEG_BOX:
	      	ss_420jpeg_to_444(ycbcr[1], frame->width, frame->height);
	    	ss_420jpeg_to_444(ycbcr[2], frame->width, frame->height);
		break;
		case SSM_420_JPEG_TR:
			tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[1], frame->width, frame->height);
			tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[2], frame->width, frame->height);
		break;
		case SSM_422_444:
 			tr_422_to_444(ycbcr[1],frame->width,frame->height);
 			tr_422_to_444(ycbcr[2],frame->width,frame->height);
    		break;
  		default:
   		break;
 	 }

     if( _chroma_supersample_data != NULL )
        free( _chroma_supersample_data );
}
