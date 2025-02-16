	.65c816
	.a16
	.i16

	.export __divl
	.export __reml
	.export __diveqxl
	.export __remeqxl

;
;	We handle divide specially to get the stack layout
;	convenient. The backend pushes the left to the data stack
;	instead.
;
divsetup:
	dey
	dey
	dey
	dey
	sta 0,y		; save the working register
	lda @hireg
	sta 2,y
	rts

__divl:
	stz @sign		; clear sign flag
	ldx @hireg		; is the working register negative ?
	bpl signfixed
	jsr __negatel		; negate it if so
	inc @sign		; and count the negation
signfixed:
	phy
	jsr divsetup
	lda 6,y			; get the stacked high word
	bpl nocarry		; are we negative
	inc @sign		; remember negation
	eor #0xffff		; negate
	sta 6,y
	lda 4,y
	eor #0xffff
	inc a
	sta 4,y
	bne nocarry
	tyx			; inc doesnt have ,y form so use x
	inc 6,x
nocarry:
	jsr div32x32		; unsigned divide
	lda @sign
	and #1			; one negation -> negatvie
	beq nosignfix3
	lda 6,y			; negate into hireg:a
	eor #0xffff
	sta @hireg
	lda 4,y
	eor #0xffff
	inc a
	bne popout		; and done
	inc @hireg		; carried so an extra inc needed
popout:
	ply
	dey			; clean up passed stack argument
	dey
	dey
	dey
	rts			; home time

nosignfix3:
	lda 6,y
	sta @hireg
	lda 4,y
	bra popout


;
;	Same basic idea but the sign is determined solely by hireg
;
__reml:
	ldx @hireg
	stx @sign		; save word that determines sign
	bpl msignfixed
	jsr __negatel
msignfixed:
	phy
	jsr divsetup
	lda 6,y
	bpl mnocarry
	eor #0xffff
	sta 6,y
	lda 4,y
	eor #0xffff
	inc a
	sta 4,y
	bne mnocarry
	tyx
	inc 6,x
mnocarry:
	jsr div32x32
	lda @tmp3
	sta @hireg
	lda @tmp2
	ldx @sign
	bpl popout
	jsr __negatel
	bra popout

;
;	X is the pointer. Build the stack and call the main operation
;
;	(X) / hireg:A
;
__diveqxl:
	dey
	dey
	dey
	dey
	pha
	lda 2,x
	sta 2,y
	lda 0,x
	sta 0,y
	pla
	phx
	jsr __divl
	plx
	; it did all our cleanup on the data stack
	sta 0,x
	pha
	lda @hireg
	sta 2,x
	pla
	rts

__remeqxl:
	dey
	dey
	dey
	dey
	pha
	lda 2,x
	sta 2,y
	lda 0,x
	sta 0,y
	pla
	phx
	jsr __reml
	plx
	; it did all our cleanup on the data stack
	sta 0,x
	pha
	lda @hireg
	sta 2,x
	pla
	rts
