	.65c816
	.a16
	.i16

	.export __switchl
	.export __switchlu

__switchlu:
__switchl:
	; X holds the switch table hireg:A the value
	lda 0,x
	sta @tmp	; length
next:
	cmp 2,x
	bne nomatch
	pha
	lda 4,x
	cmp @hireg
	beq gotswitch
	pla
nomatch:
	inx
	inx
	inx
	inx
	inx
	inx
	dec @tmp
	bne next
	; Fall through for default
	lda 2,x
	dec a
	pha
	rts
gotswitch:
	pla		; discard
	lda 6,x
	dec a
	pha
	rts
