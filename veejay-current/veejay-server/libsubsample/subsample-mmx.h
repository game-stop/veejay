/*
 * subsample-mmx.c:  Routines to resample UV planes using MMX
 *
 *  Copyright (C) 2014 Niels Elburg <nwelburg@gmail.com>
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
#ifndef SUBSAMPLE_MMX
#define SUBSAMPLE_MMX

/*
 
 down sampling is done by dropping every 2nd pixel:

 in:   [A] [B] [C] [D] [E] [F] [G] [H] [I] ..
 
 out:   [A] [C] [E] [G] ...


 up sampling is done by duplicating every pixel:

 in:    [A] [B] [C] [D] [E] [F] [G] [H] [I] 

 out:   [A] [A] [B] [B] [C] [C] [D] [D] [E] [E] [F] [F] [G] [G] [H] [H] [I] [I]

 */


static	inline	void	subsample_load_mask16to8()
{
	const uint64_t mask = 0x00ff00ff00ff00ffLL;
	const uint8_t *m    = (uint8_t*)&mask;

	__asm __volatile(
		"movq		(%0), %%mm4\n\t"
		:: "r" (m)
	);

}

static	inline	void	subsample_down_1x16to1x8( uint8_t *out, const uint8_t *in )
{
	__asm __volatile(
		"movq		(%0), %%mm1\n\t" 
		"movq		8(%0),%%mm3\n\t"
		"pxor		%%mm5,%%mm5\n\t"
		"pand		%%mm4,%%mm1\n\t"
		"pand		%%mm4,%%mm3\n\t"
		"packuswb	%%mm1,%%mm2\n\t"
		"packuswb	%%mm3,%%mm5\n\t"
		"psrlq		$32, %%mm2\n\t"
		"por		%%mm5,%%mm2\n\t"
		"movq		%%mm2, (%1)\n\t"
		:: "r" (in), "r" (out)
	);
}

static inline void 	subsample_down_1x32to1x16( uint8_t *out, const uint8_t *in )
{
	__asm __volatile(
		"movq		(%0), %%mm1\n\t"
		"movq		8(%0),%%mm2\n\t" 
		"movq          16(%0),%%mm6\n\t"
		"movq	       24(%0),%%mm7\n\t"

		"pxor		%%mm5,%%mm5\n\t"
		"pand		%%mm4,%%mm1\n\t"
		"pand		%%mm4,%%mm2\n\t"
		"pand		%%mm4,%%mm6\n\t"
		"pand		%%mm4,%%mm7\n\t"

		"packuswb	%%mm1,%%mm3\n\t"
		"packuswb	%%mm2,%%mm5\n\t"
		"psrlq		$32,%%mm3\n\t"
		"por		%%mm5,%%mm3\n\t"
		"movq		%%mm3, (%1)\n\t"

		"pxor		%%mm5,%%mm5\n\t"
		"packuswb	%%mm6,%%mm3\n\t"
		"packuswb	%%mm7,%%mm5\n\t"
		"psrlq		$32, %%mm3\n\t"
		"por		%%mm5,%%mm3\n\t"
		"movq		%%mm3,8(%1)\n\t"

		:: "r" (in), "r" (out)
	);
}

static	inline	void	subsample_up_1x8to1x16( uint8_t *in, uint8_t *out )
{
	//@ super sample by duplicating pixels
	__asm__ __volatile__ (
		"\n\tpxor	%%mm2,%%mm2"
		"\n\tpxor	%%mm4,%%mm4"
		"\n\tmovq	(%0), %%mm1"  
		"\n\tpunpcklbw	%%mm1,%%mm2" 
		"\n\tpunpckhbw	%%mm1,%%mm4"   
		"\n\tmovq	%%mm2,%%mm5"
		"\n\tmovq	%%mm4,%%mm6"
		"\n\tpsrlq	$8, %%mm5"    
		"\n\tpsrlq	$8, %%mm6"  
		"\n\tpor	%%mm5,%%mm2"
		"\n\tpor	%%mm6,%%mm4"	
		"\n\tmovq	%%mm2, (%1)"
		"\n\tmovq	%%mm4, 8(%1)"
		:: "r" (in), "r" (out)
	);
}

static inline void	subsample_up_1x16to1x32( uint8_t *in, uint8_t *out )
{
	__asm__ __volatile__ (
		"\n\tpxor	%%mm2,%%mm2"
		"\n\tpxor	%%mm4,%%mm4"
		"\n\tpxor	%%mm3,%%mm3"
		"\n\tpxor	%%mm7,%%mm7"
		"\n\tmovq	(%0), %%mm1"  
		"\n\tpunpcklbw	%%mm1,%%mm2" 
		"\n\tpunpckhbw	%%mm1,%%mm4"   
		"\n\tmovq	%%mm2,%%mm5"
		"\n\tmovq	%%mm4,%%mm6"
		"\n\tpsrlq	$8, %%mm5"    
		"\n\tpsrlq	$8, %%mm6"  
		"\n\tpor	%%mm5,%%mm2"
		"\n\tpor	%%mm6,%%mm4"

		"\n\tmovq	8(%0), %%mm1"
		"\n\tpunpcklbw %%mm1, %%mm3" 
		"\n\tpunpckhbw %%mm1, %%mm7" 
		"\n\tmovq	%%mm3,%%mm5"
		"\n\tmovq	%%mm7,%%mm6"
		"\n\tpsrlq	$8, %%mm5"    
		"\n\tpsrlq	$8, %%mm6"  
		"\n\tpor	%%mm5,%%mm3"
		"\n\tpor	%%mm6,%%mm7"
	
		"\n\tmovq	%%mm2, (%1)"
		"\n\tmovq	%%mm4, 8(%1)"
		"\n\tmovq	%%mm3,16(%1)"
		"\n\tmovq	%%mm7,24(%1)"
		:: "r" (in), "r" (out)
	);
}
#endif

