		.export __ccne

		.setcpu 8085
		.code

__ccne:		xchg
		pop	h
		xthl
		mov	a,l
		cmp	e
		jnz	__true
		mov	a,h
		cmp	d
		jnz	__false
		jmp	__true
