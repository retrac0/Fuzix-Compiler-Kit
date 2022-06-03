;
;		(TOS) /= HL
;

			.export __remeq
			.setcpu 8080
			.code
__remeq:
	xchg
	pop	h
	xthl
	; Now we are doing (HL) * DE
	push	d
	mov	e,m
	inx	h
	mov	d,m
	xthl	; swap address with stacked value
	xchg	; swap them back as we divide by DE
	; We are now doing HL / DE and the address we want is TOS
	call __remde
	; Return is in HL
	xchg
	pop	h
	mov	m,d
	dcx	h
	mov	m,e
	xchg
	ret
