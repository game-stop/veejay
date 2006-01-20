;
;  bdist2_mmx.s:  MMX optimized bidirectional squared distance sum
;
;  Original believed to be Copyright (C) 2000 Brent Byeler
;
;  This program is free software; you can reaxstribute it and/or
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

;/*
; * squared error between a (16*h) block and a bidirectional
; * prediction
; *
; * p2: address of top left pel of block
; * pf,hxf,hyf: address and half pel flags of forward ref. block
; * pb,hxb,hyb: address and half pel flags of backward ref. block
; * h: height of block
; * lx: distance (in bytes) of vertically adjacent pels in p2,pf,pb
; * mmX version
; */

;int bsumsq_mmx(
;unsigned char *pf, unsigned char *pb, unsigned char *p2,
;int lx, int hxf, int hyf, int hxb, int hyb, int h)
;{
;  unsigned char *pfa,*pfb,*pfc,*pba,*pbb,*pbc;
;  int s;

; Handy macros for readbility

%define pf [ebp+8]
%define pb [ebp+12]
%define p2 [ebp+16]
%define lx [ebp+20]
%define hxf [ebp+24]
%define hyf [ebp+28]
%define hxb [ebp+32]
%define hyb [ebp+36]
%define h   [ebp+40]


%define pfa [esp+4]
%define pfb [esp+8]
%define pfc [esp+12]
%define pba [esp+16]
%define pbb [esp+20]
%define pbc [esp+24]

SECTION .text
global bsumsq_mmx

align 32
bsumsq_mmx:
	push ebp			; save frame pointer
	mov ebp, esp		; link
	push ebx
	push ecx
	push edx
	push esi     
	push edi

	;;
	;; Make space for local variables on stack
	sub       esp, 32
	
	mov       edx, hxb
	mov       eax, hxf
	mov       esi, lx

	mov       ecx, pf
	add       ecx, eax
	mov       pfa, ecx
	mov       ecx, esi
	imul      ecx, hyf
	mov       ebx, pf
	add       ecx, ebx
	mov       pfb, ecx
	add       eax, ecx
	mov       pfc, eax
	mov       eax, pb
	add       eax, edx
	mov       pba, eax
	mov       eax, esi
	imul      eax, hyb
	mov       ecx, pb
	add       eax, ecx
	mov       pbb, eax
	add       edx, eax
	mov       pbc, edx
	xor       esi, esi	; esi = s (accumulated sym)
	mov       eax, esi

	mov       edi, h
	test      edi, edi  ; h = 0?
	jle       near bsumsqexit

	pxor	  mm7, mm7
	pxor	  mm6, mm6
	pcmpeqw	  mm5, mm5
	psubw	  mm6, mm5
	psllw	  mm6, 1

bsumsqtop:
	mov	  eax, pf
	mov	  ebx, pfa
	mov	  ecx, pfb
	mov	  edx, pfc
	movq	  mm0, [eax]
	movq	  mm1, mm0
	punpcklbw mm0, mm7
	punpckhbw mm1, mm7
	movq	  mm2, [ebx]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	movq	  mm2, [ecx]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	movq	  mm2, [edx]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	paddw	  mm0, mm6
	paddw	  mm1, mm6
	psrlw	  mm0, 2
	psrlw	  mm1, 2

	mov	  eax, pb
	mov	  ebx, pba
	mov	  ecx, pbb
	mov	  edx, pbc
	movq	  mm2, [eax]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	movq	  mm4, [ebx]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5
	movq	  mm4, [ecx]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5
	movq	  mm4, [edx]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5

	paddw	  mm2, mm6
	paddw	  mm3, mm6
	psrlw	  mm2, 2
	psrlw	  mm3, 2

	paddw	  mm0, mm2
	paddw	  mm1, mm3
	psrlw	  mm6, 1
	paddw	  mm0, mm6
	paddw	  mm1, mm6
	psllw	  mm6, 1
	psrlw	  mm0, 1
	psrlw	  mm1, 1

	mov	  eax, p2
	movq	  mm2, [eax]
	movq	  mm3, mm2
        punpcklbw mm2, mm7
        punpckhbw mm3, mm7

        psubw     mm0, mm2
        psubw     mm1, mm3
        pmaddwd   mm0, mm0
        pmaddwd   mm1, mm1
        paddd     mm0, mm1

	movd	  eax, mm0
	psrlq	  mm0, 32
	movd	  ebx, mm0
	add	  esi, eax
	add	  esi, ebx

	mov	  eax, pf
	mov	  ebx, pfa
	mov	  ecx, pfb
	mov	  edx, pfc
	movq	  mm0, [eax+8]
	movq	  mm1, mm0
	punpcklbw mm0, mm7
	punpckhbw mm1, mm7
	movq	  mm2, [ebx+8]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	movq	  mm2, [ecx+8]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	movq	  mm2, [edx+8]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	paddw	  mm0, mm2
	paddw	  mm1, mm3
	paddw	  mm0, mm6
	paddw	  mm1, mm6
	psrlw	  mm0, 2
	psrlw	  mm1, 2

	mov	  eax, pb
	mov	  ebx, pba
	mov	  ecx, pbb
	mov	  edx, pbc
	movq	  mm2, [eax+8]
	movq	  mm3, mm2
	punpcklbw mm2, mm7
	punpckhbw mm3, mm7
	movq	  mm4, [ebx+8]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5
	movq	  mm4, [ecx+8]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5
	movq	  mm4, [edx+8]
	movq	  mm5, mm4
	punpcklbw mm4, mm7
	punpckhbw mm5, mm7
	paddw	  mm2, mm4
	paddw	  mm3, mm5
	paddw	  mm2, mm6
	paddw	  mm3, mm6
	psrlw	  mm2, 2
	psrlw	  mm3, 2

	paddw	  mm0, mm2
	paddw	  mm1, mm3
	psrlw	  mm6, 1
	paddW	  mm0, mm6
	paddw	  mm1, mm6
	psllw	  mm6, 1
	psrlw	  mm0, 1
	psrlw	  mm1, 1

	mov	  eax, p2
	movq	  mm2, [eax+8]
	movq	  mm3, mm2
        punpcklbw mm2, mm7
        punpckhbw mm3, mm7

        psubw     mm0, mm2
        psubw     mm1, mm3
        pmaddwd   mm0, mm0
        pmaddwd   mm1, mm1
        paddd     mm0, mm1

	movd	  eax, mm0
	psrlq	  mm0, 32
	movd	  ebx, mm0
	add	  esi, eax
	add	  esi, ebx

    mov       eax, lx
	add       p2, eax
	add       pf, eax
	add       pfa, eax
	add       pfb, eax
	add       pfc, eax
	add       pb, eax
	add       pba, eax
	add       pbb, eax
	add       pbc, eax

	dec       edi
	jg        near bsumsqtop
    mov       eax, esi

bsumsqexit:
	
	;;
	;; Get rid of local variables
	add esp, 32
	
	;; Retore (callee saves convention...)
	;;
	pop edi
	pop esi
	pop edx
	pop ecx
	pop ebx

	pop ebp			; restore stack pointer

	emms			; clear mmx registers
	ret	
	
