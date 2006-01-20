;;; 
;;;  mblock_sad_mmxe.s:  
;;; 
;;; Enhanced MMX optimized Sum Absolute Differences routines for macroblocks
;;; (interpolated, 1-pel, 2*2 sub-sampled pel and 4*4 sub-sampled pel)
;
;  sad_* Original Copyright (C) 2000 Chris Atenasio <chris@crud.net>
;  Enhancements and rest Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
;
;
;

SECTION .text

global sad_00_mmx

; int sad_mmx(unsigned char *blk1,unsigned char *blk2,int lx,int h, int distlim);
; N.b. distlim is *ignored* as testing for it is more expensive than the
; occasional saving by aborting the computionation early...
; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ebx = distlim  
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = temp 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = 0
; mm7 = temp


align 32
sad_00_mmx:
	push ebp		; save frame pointer
	mov ebp, esp

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 
	push esi
	push edi
		
	pxor mm0, mm0				; zero acculumators
	pxor mm6, mm6
	mov esi, [ebp+8]			; get p1
	mov edi, [ebp+12]			; get p2
	mov edx, [ebp+16]			; get lx
	mov  ecx, [ebp+20]			; get rowsleft
	;mov ebx, [ebp+24]	        ; distlim
	jmp nextrowmm00
align 32
nextrowmm00:
	movq mm4, [esi]		; load first 8 bytes of p1 row 
	movq mm5, [edi]		; load first 8 bytes of p2 row
		
	movq mm7, mm4       ; mm5 = abs(mm4-mm5)
	psubusb mm7, mm5
	psubusb mm5, mm4
	paddb   mm5, mm7

	;;  Add the abs(mm4-mm5) bytes to the accumulators
	movq mm2, [esi+8]		; load second 8 bytes of p1 row (interleaved)
	movq  mm7,mm5				; mm7 := [i :	B0..3, mm1]W
	punpcklbw mm7,mm6
	movq mm3, [edi+8]		; load second 8 bytes of p2 row (interleaved)
	paddw mm0, mm7
	punpckhbw mm5,mm6
	paddw mm0, mm5

		;; This is logically where the mm2, mm3 loads would go...
		
	movq mm7, mm2       ; mm3 = abs(mm2-mm3)
	psubusb mm7, mm3
	psubusb mm3, mm2
	paddb   mm3, mm7

	;;  Add the abs(mm4-mm5) bytes to the accumulators
	movq  mm7,mm3				; mm7 := [i :	B0..3, mm1]W
	punpcklbw mm7,mm6
	punpckhbw mm3,mm6
	paddw mm0, mm7
	
	add esi, edx		; update pointer to next row
	add edi, edx		; ditto	

	paddw mm0, mm3



	sub  ecx,1
	jnz near nextrowmm00
		
returnmm00:	

		;; Sum the Accumulators
	movq  mm5, mm0				; mm5 := [W0+W2,W1+W3, mm0
	psrlq mm5, 32
	movq  mm4, mm0
	paddw mm4, mm5

	movq  mm7, mm4              ; mm6 := [W0+W2+W1+W3, mm0]
	psrlq mm7, 16
	paddw mm4, mm7
	movd eax, mm4		; store return value
	and  eax, 0xffff

	pop edi
	pop esi	
	pop edx	
	pop ecx	
	pop ebx	

	pop ebp	

	emms			; clear mmx registers
	ret	
;
;;;  sad_01_mmx.s:  mmx1 optimised 7bit*8 word absolute difference sum
;;;     We're reduce to seven bits as otherwise we also have to mess
;;;     horribly with carries and signed only comparisons make the code
;;;     simply enormous (and probably barely faster than a simple loop).
;;;     Since signals with a bona-fide 8bit res will be rare we simply
;;;     take the precision hit...
;;;     Actually we don't worry about carries from the low-order bits
;;;     either so 1/4 of the time we'll be 1 too low...
;;; 
;  Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
;
;
;


global sad_01_mmx

; int sad_01_mmx(unsigned char *p1,unsigned char *p2,int lx,int h);

; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = bytes p2
; mm2 = bytes p1
; mm3 = bytes p1+1
; mm4 = temp 4 bytes in words interpolating p1, p1+1
; mm5 = temp 4 bytes in words from p2
; mm6 = temp comparison bit mask p1,p2
; mm7 = temp comparison bit mask p2,p1


align 32
sad_01_mmx:
	push ebp		; save stack pointer
	mov ebp, esp	; so that we can do this

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 
	push esi
	push edi
		
	pxor mm0, mm0				; zero acculumators

	mov esi, [ebp+8]			; get p1
	mov edi, [ebp+12]			; get p2
	mov edx, [ebp+16]			; get lx
	mov ecx, [ebp+20]			; rowsleft := h
	jmp nextrowmm01					; snap to it
align 32
nextrowmm01:

		;; 
		;; First 8 bytes of row
		;; 
		
		;; First 4 bytes of 8

	movq mm4, [esi]             ; mm4 := first 4 bytes p1
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [esi+1]			;  mm6 := first 4 bytes p1+1
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              ;  mm4 := First 4 bytes interpolated in words
	psrlw mm4, 1
		
	movq mm5, [edi]				; mm5:=first 4 bytes of p2 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
		
	paddw mm4, mm6          ;  mm4 := First 4 Interpolated bytes in words
	psrlw mm4, 1

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


 		;;
		;; Second 8 bytes of row
		;; 
		;; First 4 bytes of 8

	movq mm4, [esi+8]             ; mm4 := first 4 bytes p1+8
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [esi+9]			;  mm6 := first 4 bytes p1+9
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              ;  mm4 := First 4 bytes interpolated in words
	psrlw mm4, 1
		
	movq mm5, [edi+8]				; mm5:=first 4 bytes of p2+8 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
		
	paddw mm4, mm6          ;  mm4 := First 4 Interpolated bytes in words
	psrlw mm4, 1

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


		;;
		;;	Loop termination condition... and stepping
		;;		

	add esi, edx		; update pointer to next row
	add edi, edx		; ditto
			
	sub  ecx,1
	test ecx, ecx		; check rowsleft
	jnz near nextrowmm01
		

		;; Sum the Accumulators
	movq  mm4, mm0
	psrlq mm4, 32
	paddw mm0, mm4
	movq  mm6, mm0
	psrlq mm6, 16
	paddw mm0, mm6
	movd eax, mm0		; store return value
	and  eax, 0xffff

	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming
;
;;;  sad_01_mmx.s:  mmx1 optimised 7bit*8 word absolute difference sum
;;;     We're reduce to seven bits as otherwise we also have to mess
;;;     horribly with carries and signed only comparisons make the code
;;;     simply enormous (and probably barely faster than a simple loop).
;;;     Since signals with a bona-fide 8bit res will be rare we simply
;;;     take the precision hit...
;;;     Actually we don't worry about carries from the low-order bits
;;;     either so 1/4 of the time we'll be 1 too low...
;;; 
;  Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
;
;
;


global sad_10_mmx

; int sad_10_mmx(unsigned char *p1,unsigned char *p2,int lx,int h);

; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ebx = p1+lx
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = bytes p2
; mm2 = bytes p1
; mm3 = bytes p1+1
; mm4 = temp 4 bytes in words interpolating p1, p1+1
; mm5 = temp 4 bytes in words from p2
; mm6 = temp comparison bit mask p1,p2
; mm7 = temp comparison bit mask p2,p1


align 32
sad_10_mmx:
	push ebp		; save stack pointer
	mov ebp, esp	; so that we can do this

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 
	push esi
	push edi
		
	pxor mm0, mm0				; zero acculumators

	mov esi, [ebp+8]			; get p1
	mov edi, [ebp+12]			; get p2
	mov edx, [ebp+16]			; get lx
	mov ecx, [ebp+20]			; rowsleft := h
	mov ebx, esi
    add ebx, edx		
	jmp nextrowmm10					; snap to it
align 32
nextrowmm10:

		;; 
		;; First 8 bytes of row
		;; 
		
		;; First 4 bytes of 8

	movq mm4, [esi]             ; mm4 := first 4 bytes p1
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx]			    ;  mm6 := first 4 bytes p1+lx
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              ;  mm4 := First 4 bytes interpolated in words
	psrlw mm4, 1
		
	movq mm5, [edi]				; mm5:=first 4 bytes of p2 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
		
	paddw mm4, mm6          ;  mm4 := First 4 Interpolated bytes in words
	psrlw mm4, 1

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


 		;;
		;; Second 8 bytes of row
		;; 
		;; First 4 bytes of 8

	movq mm4, [esi+8]             ; mm4 := first 4 bytes p1+8
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx+8]			;  mm6 := first 4 bytes p1+lx+8
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              ;  mm4 := First 4 bytes interpolated in words
	psrlw mm4, 1
		
	movq mm5, [edi+8]				; mm5:=first 4 bytes of p2+8 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
		
	paddw mm4, mm6          ;  mm4 := First 4 Interpolated bytes in words
	psrlw mm4, 1

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


		;;
		;;	Loop termination condition... and stepping
		;;		

	add esi, edx		; update pointer to next row
	add edi, edx		; ditto
	add ebx, edx

	sub  ecx,1
	test ecx, ecx		; check rowsleft
	jnz near nextrowmm10
		
		;; Sum the Accumulators
	movq  mm4, mm0
	psrlq mm4, 32
	paddw mm0, mm4
	movq  mm6, mm0
	psrlq mm6, 16
	paddw mm0, mm6
	movd eax, mm0		; store return value
	and  eax, 0xffff

		
	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming
;
;;;  sad_01_mmx.s:  
;;; 
;  Copyright (C) 2000 Andrew Stevens <as@comlab.ox.ac.uk>

;
;  This program is free software; you can redistribute it and/or
;  modify it under the terms of the GNU General Public License
;  as published by the Free Software Foundation; either version 2
;  of the License, or (at your option) any later version.
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
;
;  You should have received a copy of the GNU General Public License
;  along with this program; if not, write to the Free Software
;  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
;
;
;


global sad_11_mmx

; int sad_11_mmx(unsigned char *p1,unsigned char *p2,int lx,int h);

; esi = p1 (init:		blk1)
; edi = p2 (init:		blk2)
; ebx = p1+lx
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = bytes p2
; mm2 = bytes p1
; mm3 = bytes p1+lx
; I'd love to find someplace to stash p1+1 and p1+lx+1's bytes
; but I don't think thats going to happen in iA32-land...
; mm4 = temp 4 bytes in words interpolating p1, p1+1
; mm5 = temp 4 bytes in words from p2
; mm6 = temp comparison bit mask p1,p2
; mm7 = temp comparison bit mask p2,p1


align 32
sad_11_mmx:
	push ebp		; save stack pointer
	mov ebp, esp	; so that we can do this

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 
	push esi
	push edi
		
	pxor mm0, mm0				; zero acculumators

	mov esi, [ebp+8]			; get p1
	mov edi, [ebp+12]			; get p2
	mov edx, [ebp+16]			; get lx
	mov ecx, [ebp+20]			; rowsleft := h
	mov ebx, esi
    add ebx, edx		
	jmp nextrowmm11					; snap to it
align 32
nextrowmm11:

		;; 
		;; First 8 bytes of row
		;; 
		
		;; First 4 bytes of 8

	movq mm4, [esi]             ; mm4 := first 4 bytes p1
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx]			    ;  mm6 := first 4 bytes p1+lx
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              


	movq mm5, [esi+1]			; mm5 := first 4 bytes p1+1
	punpcklbw mm5, mm7            ;  First 4 bytes p1 in Words...
	paddw mm4, mm5		
	movq mm6, [ebx+1]           ;  mm6 := first 4 bytes p1+lx+1
	punpcklbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, [edi]				; mm5:=first 4 bytes of p2 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
	paddw mm4, mm6          

	movq mm5, [esi+1]			; mm5 := first 4 bytes p1+1
	punpckhbw mm5, mm7          ;  First 4 bytes p1 in Words...
	paddw mm4, mm5
	movq mm6, [ebx+1]           ;  mm6 := first 4 bytes p1+lx+1
	punpckhbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


 		;;
		;; Second 8 bytes of row
		;; 
		;; First 4 bytes of 8

	movq mm4, [esi+8]             ; mm4 := first 4 bytes p1+8
	pxor mm7, mm7
	movq mm2, mm4				;  mm2 records all 8 bytes
	punpcklbw mm4, mm7            ;  First 4 bytes p1 in Words...
	
	movq mm6, [ebx+8]			    ;  mm6 := first 4 bytes p1+lx+8
	movq mm3, mm6               ;  mm3 records all 8 bytes
	punpcklbw mm6, mm7
	paddw mm4, mm6              


	movq mm5, [esi+9]			; mm5 := first 4 bytes p1+9
	punpcklbw mm5, mm7            ;  First 4 bytes p1 in Words...
	paddw mm4, mm5
	movq mm6, [ebx+9]           ;  mm6 := first 4 bytes p1+lx+9
	punpcklbw mm6, mm7
	paddw mm4, mm6

	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words
		
	movq mm5, [edi+8]				; mm5:=first 4 bytes of p2+8 in words
	movq mm1, mm5
	punpcklbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator

		;; Second 4 bytes of 8
	
	movq mm4, mm2		    ; mm4 := Second 4 bytes p1 in words
	pxor  mm7, mm7
	punpckhbw mm4, mm7			
	movq mm6, mm3			; mm6 := Second 4 bytes p1+1 in words  
	punpckhbw mm6, mm7
	paddw mm4, mm6          

	movq mm5, [esi+9]			; mm5 := first 4 bytes p1+1
	punpckhbw mm5, mm7          ;  First 4 bytes p1 in Words...
	paddw mm4, mm5	
	movq mm6, [ebx+9]           ;  mm6 := first 4 bytes p1+lx+1
	punpckhbw mm6, mm7
	paddw mm4, mm6
		
	psrlw mm4, 2	            ; mm4 := First 4 bytes interpolated in words

	movq mm5, mm1			; mm5:= second 4 bytes of p2 in words
	punpckhbw mm5, mm7
			
	movq  mm7,mm4
	pcmpgtw mm7,mm5		; mm7 := [i : W0..3,mm4>mm5]

	movq  mm6,mm4		; mm6 := [i : W0..3, (mm4-mm5)*(mm4-mm5 > 0)]
 	psubw mm6,mm5
	pand  mm6, mm7

	paddw mm0, mm6				; Add to accumulator

	movq  mm6,mm5       ; mm6 := [i : W0..3,mm5>mm4]
	pcmpgtw mm6,mm4	    
 	psubw mm5,mm4		; mm5 := [i : B0..7, (mm5-mm4)*(mm5-mm4 > 0)]
	pand  mm5, mm6		

	paddw mm0, mm5				; Add to accumulator


		;;
		;;	Loop termination condition... and stepping
		;;		

	add esi, edx		; update pointer to next row
	add edi, edx		; ditto
	add ebx, edx

	sub  ecx,1
	test ecx, ecx		; check rowsleft
	jnz near nextrowmm11
		
		;; Sum the Accumulators
	movq  mm4, mm0
	psrlq mm4, 32
	paddw mm0, mm4
	movq  mm6, mm0
	psrlq mm6, 16
	paddw mm0, mm6
	movd eax, mm0		; store return value
	and  eax, 0xffff
		
	pop edi
	pop esi	
	pop edx			
	pop ecx			
	pop ebx			

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming


global sad_sub22_mmx

; int sad_sub22_mmx(unsigned char *blk1,unsigned char *blk2,int lx,int h);

; eax = p1 (init:		blk1)
; ebx = p2 (init:		 blk2)
; ecx = rowsleft (init:	 h)
; edx = lx;

; mm0 = distance accumulators (4 words)
; mm1 = temp 
; mm2 = temp 
; mm3 = temp
; mm4 = temp
; mm5 = temp 
; mm6 = 0
; mm7 = temp


align 32
sad_sub22_mmx:
	push ebp		; save stack pointer
	mov ebp, esp	; so that we can do this

	push ebx		; Saves registers (called saves convention in
	push ecx		; x86 GCC it seems)
	push edx		; 

	pxor mm0, mm0				; zero acculumators
	pxor mm6, mm6
	mov eax, [ebp+8]			; get p1
	mov ebx, [ebp+12]			; get p2
	mov edx, [ebp+16]			; get lx

	mov  ecx, [ebp+20]			; get rowsleft

	jmp nextrow					; snap to it
align 32
nextrow:
	movq mm4, [eax]		; load 8 bytes of p1 
	movq mm5, [ebx]		; load 8 bytes of p2
		
	movq mm7, mm4       ; mm5 = abs(*p1-*p2)
	psubusb mm7, mm5
	psubusb mm5, mm4
	add eax, edx		; update pointer to next row
	paddb   mm5,mm7	

		;;  Add the mm5 bytes to the accumulatores
	movq  mm7,mm5			
	punpcklbw mm7,mm6
	paddw mm0, mm7
	punpckhbw mm5,mm6
	add ebx, edx		; update pointer to next row
	paddw mm0, mm5
	
	movq mm4, [eax]		; load 8 bytes of p1 (next row) 
	movq mm5, [ebx]		; load 8 bytes of p2 (next row)
		
	movq mm7, mm4       ; mm5 = abs(*p1-*p2)
	psubusb mm7, mm5
	psubusb mm5, mm4
	add eax, edx		; update pointer to next row
	paddb   mm5,mm7	

		;;  Add the mm5 bytes to the accumulatores
	movq  mm7,mm5				
	punpcklbw mm7,mm6
	add ebx, edx		; update pointer to next row
	paddw mm0, mm7              
	punpckhbw mm5,mm6
	sub  ecx,2
	paddw mm0, mm5


	jnz nextrow

		;; Sum the Accumulators
	movq  mm1, mm0
	psrlq mm1, 16
	movq  mm2, mm0
	psrlq mm2, 32
	movq  mm3, mm0
	psrlq mm3, 48
	paddw mm0, mm1
	paddw mm2, mm3
	paddw mm0, mm2
		
	movd eax, mm0		; store return value
	and  eax, 0xffff
	
	pop edx			; pop pop
	pop ecx			; fizz fizz
	pop ebx			; ia86 needs a fizz instruction

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming




global sad_sub44_mmx

; int sad_sub44_mmx(unsigned char *blk1,unsigned char *blk2,int qlx,int qh);

; eax = p1
; ebx = p2
; ecx = temp
; edx = qlx;
; esi = rowsleft

; mm0 = distance accumulator left block p1
; mm1 = distance accumulator right block p1
; mm2 = 0
; mm3 = right block of p1
; mm4 = left block of p1
; mm5 = p2
; mm6 = temp
; mm7 = temp

align 32
sad_sub44_mmx:
	push ebp		; save stack pointer
	mov ebp, esp		; so that we can do this

	push ebx
	push ecx
	push edx
	push esi     

	pxor mm0, mm0		; zero acculumator
	pxor mm1, mm1
	pxor mm2, mm2
	mov eax, [ebp+8]	; get p1
	mov ebx, [ebp+12]	; get p2
	mov edx, [ebp+16]	; get qlx
	mov esi, [ebp+20]	; get rowsleft
	jmp nextrowqd		; snap to it
align 32
nextrowqd:

		;;
		;; Beware loop obfuscated by interleaving to try to
		;; hide latencies...
		;; 
	movq mm4, [eax]				; mm4 =  first 4 bytes of p1 in words
	movq mm5, [ebx]             ; mm5 = 4 bytes of p2 in words
	movq mm3, mm4
	punpcklbw mm4, mm2			
	punpcklbw mm5, mm2
	
	movq mm7, mm4
	movq mm6, mm5
	psubusw mm7, mm5
	psubusw mm6, mm4
	
	add eax, edx		        ; update a pointer to next row
;	punpckhbw mm3, mm2			; mm3 = 2nd 4 bytes of p1 in words

	paddw   mm7, mm6
	paddw mm0, mm7				; Add absolute differences to left block accumulators
		
;	movq mm7,mm3
;	psubusw mm7, mm5
;	psubusw mm5, mm3

	add ebx, edx		; update a pointer to next row
	sub   esi, 1

;	paddw   mm7, mm5
;	paddw mm1, mm7				; Add absolute differences to right block accumulators
	

		
	jnz nextrowqd		

		;;		Sum the accumulators

	movq  mm4, mm0
	psrlq mm4, 32
	paddw mm0, mm4
	movq  mm6, mm0
	psrlq mm6, 16
	paddw mm0, mm6
	movd eax, mm0		; store return value

;	movq  mm4, mm1
;	psrlq mm4, 32
;	paddw mm1, mm4
;	movq  mm6, mm1
;	psrlq mm6, 16
;	paddw mm1, mm6
;	movd ebx, mm1

	and  eax, 0xffff		
;	sal  ebx, 16
;	or   eax, ebx
		
	pop esi
	pop edx
	pop ecx
	pop ebx

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret			; we now return you to your regular programming
