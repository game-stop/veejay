/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "softblur.h"
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif
#ifdef HAVE_ASM_SSE2
#include <emmintrin.h>
#endif

vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1; /* 3*/
    ve->description = "Soft Blur (1x3) and (3x3)";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Kernel size");
    return ve;
}

static void softblur3_apply(VJFrame *frame )
{
	int r,c;
	uint8_t *Y = frame->data[0];
	const int len = frame->len;
	const int width = frame->width;

	for(c = 1; c < width - 1; c ++ )
	   Y[c ] = (Y[c - 1] +
	      Y[c ] +
	      Y[c + 1]
		) / 3;

	for(r=width; r < (len-width); r+=width) {
		for(c=1; c < (width-1); c++) {
			Y[r+c] = ( 	Y[r - width + c - 1] +
				   	Y[r - width + c ] +
					Y[r + c + 1] +
					Y[r - width + c] +
					Y[r + c] + 
					Y[r + c + 1] +
					Y[r + width + c - 1] +
					Y[r + width + c] +
					Y[r + width + c + 1]  ) / 9;

		}
	}

	for( c = (len-width) ; c < len; c ++ )
	  Y[c ] = (Y[c - 1] +
	      Y[c] +
	      Y[c + 1]
		) / 3;


}
    	
#ifdef HAVE_ASM_MMX
/* mmx_blur() taken from libvisual plugins
 *
 * Libvisual-plugins - Standard plugins for libvisual
 *
 * Copyright (C) 2002, 2003, 2004, 2005 Dennis Smit <ds@nerds-incorporated.org>
 *
 * Authors: Dennis Smit <ds@nerds-incorporated.org>
 */

static	void	mmx_blur(VJFrame *frame)
{
	__asm __volatile
		("\n\t pxor %%mm6, %%mm6"
		 ::);

	const int len = frame->len;
	const int width = frame->width;
	int scrsh = (len) >> 1;
	int i;

	uint8_t *buf = frame->data[0];
	/* Prepare substraction register */
	for (i = 0; i < scrsh; i += 4) {
		__asm __volatile
			("\n\t movd %[buf], %%mm0"
			 "\n\t movd %[add1], %%mm1"
			 "\n\t punpcklbw %%mm6, %%mm0"
			 "\n\t movd %[add2], %%mm2"
			 "\n\t punpcklbw %%mm6, %%mm1"
			 "\n\t movd %[add3], %%mm3"
			 "\n\t punpcklbw %%mm6, %%mm2"
			 "\n\t paddw %%mm1, %%mm0"
			 "\n\t punpcklbw %%mm6, %%mm3"
			 "\n\t paddw %%mm2, %%mm0"
			 "\n\t paddw %%mm3, %%mm0"
			 "\n\t psrlw $2, %%mm0"
			 "\n\t packuswb %%mm6, %%mm0"
			 "\n\t movd %%mm0, %[buf]"
			 :: [buf] "m" (*(buf + i))
			 , [add1] "m" (*(buf + i + width))
			 , [add2] "m" (*(buf + i + width + 1))
			 , [add3] "m" (*(buf + i + width - 1))
		  );
		//	 : "mm0", "mm1", "mm2", "mm3", "mm6");
	}

	for (i = len; i > scrsh; i -= 4) {
		__asm __volatile
			("\n\t movd %[buf], %%mm0"
			 "\n\t movd %[add1], %%mm1"
			 "\n\t punpcklbw %%mm6, %%mm0"
			 "\n\t movd %[add2], %%mm2"
			 "\n\t punpcklbw %%mm6, %%mm1"
			 "\n\t movd %[add3], %%mm3"
			 "\n\t punpcklbw %%mm6, %%mm2"
			 "\n\t paddw %%mm1, %%mm0"
			 "\n\t punpcklbw %%mm6, %%mm3"
			 "\n\t paddw %%mm2, %%mm0"
			 "\n\t paddw %%mm3, %%mm0"
			 "\n\t psrlw $2, %%mm0"
			 "\n\t packuswb %%mm6, %%mm0"
			 "\n\t movd %%mm0, %[buf]"
			 :: [buf] "m" (*(buf + i))
			 , [add1] "m" (*(buf + i + width))
			 , [add2] "m" (*(buf + i + 1))
			 , [add3] "m" (*(buf + i + width - 1))
		);//	 : "mm0", "mm1", "mm2", "mm3", "mm6");
	}

	do_emms;
}
#endif
#if !defined(HAVE_ASM_MMX) && !defined(HAVE_ARM)
static void softblur1_apply( VJFrame *frame)
{
    int r, c;
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	const int width = frame->width;

    for (r = 0; r < len; r += width) {
		for (c = 1; c < width-1; c++) {
			Y[c + r] = (Y[r + c - 1] +
					  Y[r + c] +
					  Y[r + c + 1]
					) / 3;
		}
    }

}
#endif

#ifdef HAVE_ARM
static void softblur1_apply(VJFrame *frame) {
    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    const int width = frame->width;

    const int aligned_width = (width / 16) * 16;

    for (int r = 0; r < len; r += width) {
        for (int c = 1; c < aligned_width - 1; c += 16) {
            uint8x16_t prev = vld1q_u8(&Y[r + c - 1]);
            uint8x16_t current = vld1q_u8(&Y[r + c]);
            uint8x16_t next = vld1q_u8(&Y[r + c + 1]);

            uint16x8_t sum_low = vaddl_u8(vget_low_u8(prev), vget_low_u8(current));
            uint16x8_t sum_high = vaddl_u8(vget_high_u8(prev), vget_high_u8(current));
            sum_low = vaddw_u8(sum_low, vget_low_u8(next));
            sum_high = vaddw_u8(sum_high, vget_high_u8(next));

            uint8x16_t result = vcombine_u8(vshrn_n_u16(sum_low, 2), vshrn_n_u16(sum_high, 2));
            vst1q_u8(&Y[r + c], result);
        }

        for (int c = aligned_width; c < width - 1; c++) {
            Y[r + c] = (Y[r + c - 1] + Y[r + c] + Y[r + c + 1]) / 3;
        }
    }
}
#endif

#ifdef HAVE_ASM_SSE2
static void sse2_blur(VJFrame *frame) {
    const int len = frame->len;
    const int width = frame->width;
    int scrsh = len >> 1;

    uint8_t *buf = frame->data[0];

    for (int i = 0; i < scrsh; i += 16) {
        __m128i mm0 = _mm_load_si128((__m128i *)(buf + i));
        __m128i mm1 = _mm_load_si128((__m128i *)(buf + i + width));
        __m128i mm2 = _mm_load_si128((__m128i *)(buf + i + width + 1));
        __m128i mm3 = _mm_load_si128((__m128i *)(buf + i + width - 1));

        mm0 = _mm_unpacklo_epi8(mm0, _mm_setzero_si128());
        mm1 = _mm_unpacklo_epi8(mm1, _mm_setzero_si128());
        mm2 = _mm_unpacklo_epi8(mm2, _mm_setzero_si128());
        mm3 = _mm_unpacklo_epi8(mm3, _mm_setzero_si128());

        mm0 = _mm_add_epi16(mm0, mm1);
        mm0 = _mm_add_epi16(mm0, mm2);
        mm0 = _mm_add_epi16(mm0, mm3);

        mm0 = _mm_srli_epi16(mm0, 2);

        mm0 = _mm_packus_epi16(mm0, mm0);

        _mm_storel_epi64((__m128i *)(buf + i), mm0);
    }

    for (int i = len - 16; i > scrsh; i -= 16) {
        __m128i mm0 = _mm_load_si128((__m128i *)(buf + i));
        __m128i mm1 = _mm_load_si128((__m128i *)(buf + i + width));
        __m128i mm2 = _mm_load_si128((__m128i *)(buf + i + 1));
        __m128i mm3 = _mm_load_si128((__m128i *)(buf + i + width - 1));

        mm0 = _mm_unpacklo_epi8(mm0, _mm_setzero_si128());
        mm1 = _mm_unpacklo_epi8(mm1, _mm_setzero_si128());
        mm2 = _mm_unpacklo_epi8(mm2, _mm_setzero_si128());
        mm3 = _mm_unpacklo_epi8(mm3, _mm_setzero_si128());

        mm0 = _mm_add_epi16(mm0, mm1);
        mm0 = _mm_add_epi16(mm0, mm2);
        mm0 = _mm_add_epi16(mm0, mm3);

        mm0 = _mm_srli_epi16(mm0, 2);

        mm0 = _mm_packus_epi16(mm0, mm0);

        _mm_storel_epi64((__m128i *)(buf + i), mm0);
    }

    _mm_empty();
}
#endif


void softblur_apply(void *ptr, VJFrame *frame, int *args)
{
   
    int type = args[0];

    switch (type) {
 	   case 0:
#ifdef HAVE_ASM_SSE2
		   	sse2_blur(frame);
#else
#ifdef HAVE_ASM_MMX
			mmx_blur(frame);
#else
			softblur1_apply(frame);
#endif
#endif
		break;
	    case 1:
			softblur3_apply(frame);
		break;
    }
}

void softblur_apply_internal(VJFrame *frame, int type)
{
    switch (type) {
 	   case 0:
#ifdef HAVE_ASM_SSE2
		   	sse2_blur(frame);
#else
#ifdef HAVE_ASM_MMX
			mmx_blur(frame);
#else
			softblur1_apply(frame);
#endif
#endif
		break;
	    case 1:
			softblur3_apply(frame);
		break;
    }
}
