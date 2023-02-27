;
;	Compute HL = TOS & HL
;
		.export __andc
		.export __anduc

		.setcpu 8080
		.code
__andc:
__anduc:
		mov	a,l		; working register into A
		pop	h		; return address
		shld	__retaddr
		ana	e
		mov	l,a
		jmp	__ret
