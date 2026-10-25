/*
 * umi CPU0 bring-up only. Append to the already initialized Linux ramoops
 * console at 0xb0000000..0xb0200000. Never touch its adjacent pmsg region.
 * No stack, allocation, calls, UART or firmware services. x9..x17/NZCV scratch;
 * use only at the reviewed startup boundaries where those registers are dead.
 * The caller supplies a physical or explicitly mapped, uncached address.
 */
.macro UMI_PUTC
	strb	w16, [x11, x12]
	add	x12, x12, #1
	cmp	x12, x17
	csel	x12, xzr, x12, eq
	add	x13, x13, #1
	cmp	x13, x17
	csel	x13, x17, x13, hi
.endmacro

.macro UMI_HEX
	and	w16, w16, #15
	add	w16, w16, #48
	add	w14, w16, #39
	cmp	w16, #57
	csel	w16, w14, w16, hi
	UMI_PUTC
.endmacro

/* x9=phase, x10=value, x11=console header. Preserve x0..x8, x18..x30. */
.macro THYME_BAND
	/* thyme: paint a 512 KiB band at 0x9C400000 + phase*0x80000 on the
	   inherited splash framebuffer. Pre-MMU callers store physically
	   (dcache off); post-MMU callers rely on the uncached framebuffer
	   block mapped next to the console mapping in start.s. */
	movz	x14, #0x9C70, lsl #16
	add	x14, x14, x9, lsl #19
	orr	x15, xzr, x9
	orr	x15, x15, x15, lsl #8
	orr	x15, x15, x15, lsl #16
	orr	x15, x15, x15, lsl #32
	mov	x16, #0x8000
91:
	stp	x15, x15, [x14], #16
	subs	x16, x16, #1
	b.ne	91b
.endm

.macro UMI_TRACE_BODY
	THYME_BAND
	// thyme: re-arm the APSS watchdog with a phase-scaled bite, SKIPPING
	// phase 1 (its MMIO store was implicated by the x8 experiment).
	cmp	x9, #2
	b.lo	98f
	mov	x14, #0x10
	movk	x14, #0x17C1, lsl #16
	orr	x15, xzr, x9, lsl #23
	str	x15, [x14]
	str	x15, [x14, #4]
	mov	w16, #1
	str	w16, [x14, #-8]
	dsb	sy
	98:
	cbz	x11, 9f
	ldr	w16, [x11]
	mov	w17, #0x4244
	movk	w17, #0x4347, lsl #16
	cmp	w16, w17
	b.ne	9f
	mov	x17, #0xfff4
	movk	x17, #0x1f, lsl #16
	ldr	w12, [x11, #4]
	ldr	w13, [x11, #8]
	cmp	x13, x17
	b.hi	9f
	cmp	x12, x13
	b.hi	9f
	cmp	x12, x17
	csel	x12, xzr, x12, eq
	add	x11, x11, #12
	adr	x14, Lumi_trace_prefix
1:
	ldrb	w16, [x14], #1
	cbz	w16, 2f
	UMI_PUTC
	b	1b
2:
	lsr	w16, w9, #4
	UMI_HEX
	mov	w16, w9
	UMI_HEX
	mov	w16, #32
	UMI_PUTC
	mov	x15, #60
3:
	lsr	x16, x10, x15
	UMI_HEX
	subs	x15, x15, #4
	b.ge	3b
	mov	w16, #10
	UMI_PUTC
	dsb	sy
	stur	w13, [x11, #-4]
	stur	w12, [x11, #-8]
	dsb	sy
9:
.endmacro

.macro UMI_TRACE
	mov	x9, $0
	mov	x10, $1
	mov	x11, #0xb0000000
	UMI_TRACE_BODY
.endmacro

/* C ABI wrapper: phase, value, console address. Leaf, no stack protector.
 * x3 is caller-saved. Mask IRQ/FIQ across each record so a timer observer
 * cannot interrupt another writer on CPU0. Restore the exact incoming DAIF.
 * The early assembly-only UMI_TRACE macro remains unchanged.
 */
.text
.align 2
.globl EXT(umi_early_trace)
LEXT(umi_early_trace)
	mrs	x3, daif
	msr	daifset, #3
	mov	x9, x0
	mov	x10, x1
	mov	x11, x2
	UMI_TRACE_BODY
	msr	daif, x3
	ret
Lumi_trace_prefix:
	.asciz	"\nUMI XNU "
.align 2
