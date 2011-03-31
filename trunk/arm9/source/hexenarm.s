@hexenarm.s
@rww - custom arm routines for hexends

	.section .rodata

	.align	4
	.arm

@==========================================================================
@Math Functions
@==========================================================================
	.global HexenDivInt
	.global	FixedMul
	.global	FixedDiv
	.global	FixedDiv2

@=====================================
@HexenDivInt/HexenDivIntU
@regular int divide
@=====================================
.pool
HexenDivInt:
	@set r12 to address of DIV_NUMERATOR32
	ldr		r12, =67109520
	
	@set DIV_CR DIV_32_32 bit
	mov		r2, #0
	strh	r2, [r12, #-16]

	@move a to DIV_NUMERATOR32
	str		r0, [r12]
	@move b to DIV_DENOMINATOR32
	str		r1, [r12, #8]
	
	@hang around until busy bit is clear
.regWaitA:
	ldrh	r0, [r12, #-16]
	ands	r0, r0, #32768
	bne		.regWaitA
			
	@put result from DIV_RESULT32 into r0
	ldr		r0, [r12, #16]
		
	bx		lr

@=====================================
@FixedMul
@fast fixed multiply
@=====================================
FixedMul:
	smull	r2, r3, r0, r1
	
	@shift by FRACBITS
	mov		r1, r2, lsr #16
	mov		r2, r3, lsl #16
	orr		r0, r1, r2

	bx		lr

@=====================================
@FixedDiv
@bounds checking prefrace to FixedDiv2
@=====================================
.pool
FixedDiv:
	mov		r2, r0
	mov		r3, r1
	
	@abs a, b	
	cmp		r2, #0
	rsblt	r2, r2, #0
	cmp		r3, #0
	rsblt	r3, r3, #0

	mov		r2, r2, lsr #14

	@if abs(a)>>14 >= abs(b)
	cmp		r2, r3

	blt		FixedDiv2

	@then return (a^b)<0 ? MININT : MAXINT
	eors	r2, r0, r1
	movpl	r0, #2147483647
	movmi	r0, #2147483648
	
	bx		lr

@=====================================
@FixedDiv2
@fast fixed divide
@=====================================
FixedDiv2:
	@store low 32 bits in r2
	mov		r2, r0
	
	@extend to r3
	ands	r3, r0, #2147483648
	movmi	r3, #-1
	
	@shift over 2 registers	
	mov		r3, r3, lsl #16
	mov		r0, r2, lsr #16
	orr		r3, r3, r0
	mov		r2, r2, asl #16
	
	@set r12 to address of DIV_NUMERATOR64
	ldr		r12, =67109520
	
	@set DIV_CR DIV_64_32 bit
	mov		r0, #1
	strh	r0, [r12, #-16]

	@move expanded a into DIV_NUMERATOR64
	stmia	r12, {r2-r3}

	@move b into DIV_DENOMINATOR32
	str		r1, [r12, #8]

	@hang around until busy bit is clear
.regWaitB:
	ldrh	r0, [r12, #-16]
	ands	r0, r0, #32768
	bne		.regWaitB
	
	@put result from DIV_RESULT32 into r0
	ldr		r0, [r12, #16]

	bx		lr

	.align
	.end
