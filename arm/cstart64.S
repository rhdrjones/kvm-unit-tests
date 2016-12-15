/*
 * Boot entry point and assembler functions for aarch64 tests.
 *
 * Copyright (C) 2017, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#define __ASSEMBLY__
#include <auxinfo.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>

.section .init

.globl start
start:
	/*
	 * bootloader params are in x0-x3
	 * The physical address of the dtb is in x0, x1-x3 are reserved
	 * See the kernel doc Documentation/arm64/booting.txt
	 */
	mov	x20, x0

	/* We might have been started in EL2 */
	mrs	x19, CurrentEL
	and	x19, x19, #CurrentEL_EL2

	/* set the stack */
	mov	x0, #1
	msr	spsel, x0
	isb
	adrp    x0, stackptr
	add     sp, x0, :lo12:stackptr

	/* Enable FP/ASIMD */
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* set up exception handling */
	bl	exceptions_init_el1
	bl	exceptions_init_el2

	/* complete setup */
	mov	x0, x20
	bl	setup
	bl	get_mmu_off
	cbnz	x0, 1f
	bl	setup_vm

1:
	/* run the test */
	adrp	x0, __argc
	ldr	x0, [x0, :lo12:__argc]
	adrp	x1, __argv
	add	x1, x1, :lo12:__argv
	adrp	x2, __environ
	add	x2, x2, :lo12:__environ
	bl	main
	bl	exit
	b	halt

exceptions_init_el1:
	adrp	x0, el1_vector_table
	add	x0, x0, :lo12:el1_vector_table
	msr	vbar_el1, x0
	isb
	ret

exceptions_init_el2:
	cbz	x19, 1f
	adrp	x0, el2_vector_table
	add	x0, x0, :lo12:el2_vector_table
	msr	vbar_el2, x0
	isb
1:	ret

.text

.globl get_mmu_off
get_mmu_off:
	adrp	x0, auxinfo
	ldr	x0, [x0, :lo12:auxinfo + 8]
	and	x0, x0, #AUXINFO_MMU_OFF
	ret

.globl secondary_entry
secondary_entry:
	/* We might have been started in EL2 */
	mrs	x19, CurrentEL
	and	x19, x19, #CurrentEL_EL2

	/* Enable FP/ASIMD */
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0

	/* set up exception handling */
	bl	exceptions_init_el1
	bl	exceptions_init_el2

	/* enable the MMU unless requested off */
	bl	get_mmu_off
	cbnz	x0, 1f
	adrp	x0, mmu_idmap
	ldr	x0, [x0, :lo12:mmu_idmap]
	bl	asm_mmu_enable

1:
	/* set the stack */
	mov	x0, #1
	msr	spsel, x0
	isb
	adrp	x0, secondary_data
	ldr	x0, [x0, :lo12:secondary_data]
	mov	sp, x0

	/* finish init in C code */
	bl	secondary_cinit

	/* x0 is now the entry function, run it */
	blr	x0
	b	do_idle

.globl halt
halt:
1:	wfi
	b	1b

/*
 * asm_mmu_enable
 *   Inputs:
 *     x0 is the base address of the translation table
 *   Outputs: none
 *
 * Adapted from
 *   arch/arm64/kernel/head.S
 *   arch/arm64/mm/proc.S
 */

/*
 * Memory region attributes for LPAE:
 *
 *   n = AttrIndx[2:0]
 *                      n       MAIR
 *   DEVICE_nGnRnE      000     00000000
 *   DEVICE_nGnRE       001     00000100
 *   DEVICE_GRE         010     00001100
 *   NORMAL_NC          011     01000100
 *   NORMAL             100     11111111
 */
#define MAIR(attr, mt) ((attr) << ((mt) * 8))

.globl asm_mmu_enable
asm_mmu_enable:
	ic	iallu			// I+BTB cache invalidate
	tlbi	vmalle1is		// invalidate I + D TLBs
	dsb	ish

	/* TCR */
	ldr	x3, =TCR_TxSZ(VA_BITS) |		\
		     TCR_TG0_64K | TCR_TG1_64K |	\
		     TCR_IRGN_WBWA | TCR_ORGN_WBWA |	\
		     TCR_SHARED
	mrs	x2, id_aa64mmfr0_el1
	bfi	x3, x2, #32, #3

	/* MAIR */
	ldr	x2, =MAIR(0x00, MT_DEVICE_nGnRnE) |	\
		     MAIR(0x04, MT_DEVICE_nGnRE) |	\
		     MAIR(0x0c, MT_DEVICE_GRE) |	\
		     MAIR(0x44, MT_NORMAL_NC) |		\
		     MAIR(0xff, MT_NORMAL)

	mrs	x1, CurrentEL
	and	x1, x1, #CurrentEL_EL2
	cbnz	x1, 1f

	msr	tcr_el1, x3
	msr	mair_el1, x2
	msr	ttbr0_el1, x0
	isb
	mrs	x1, sctlr_el1
	orr	x1, x1, SCTLR_ELx_C
	orr	x1, x1, SCTLR_ELx_I
	orr	x1, x1, SCTLR_ELx_M
	msr	sctlr_el1, x1
	isb
	ret

1:
	and	x3, x3, #((1 << 19) - 1)	// upper TCR_EL2 bits
						// don't match TCR_EL1
	msr	tcr_el2, x3
	msr	mair_el2, x2
	msr	ttbr0_el2, x0
	isb
	mrs	x1, sctlr_el2
	orr	x1, x1, SCTLR_ELx_C
	orr	x1, x1, SCTLR_ELx_I
	orr	x1, x1, SCTLR_ELx_M
	msr	sctlr_el2, x1
	isb
	ret

.globl asm_mmu_disable
asm_mmu_disable:
	mrs	x1, CurrentEL
	and	x1, x1, #CurrentEL_EL2
	cbnz	x1, 1f

	mrs	x0, sctlr_el1
	bic	x0, x0, SCTLR_ELx_M
	msr	sctlr_el1, x0
	isb
	ret
1:
	mrs	x0, sctlr_el2
	bic	x0, x0, SCTLR_ELx_M
	msr	sctlr_el2, x0
	isb
	ret

/*
 * Vectors
 * Adapted from arch/arm64/kernel/entry.S
 */
.macro vector_stub, name, vec
\name:
	stp	 x0,  x1, [sp, #-S_FRAME_SIZE]!
	stp	 x2,  x3, [sp,  #16]
	stp	 x4,  x5, [sp,  #32]
	stp	 x6,  x7, [sp,  #48]
	stp	 x8,  x9, [sp,  #64]
	stp	x10, x11, [sp,  #80]
	stp	x12, x13, [sp,  #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x29, [sp, #224]

	str	x30, [sp, #S_LR]

	.if \vec >= 8 && \vec < 16
	mrs	x1, sp_el0
	.else
	add	x1, sp, #S_FRAME_SIZE
	.endif
	str	x1, [sp, #S_SP]

	.if \vec < 16
	mrs	x1, elr_el1
	mrs	x2, spsr_el1
	.else
	mrs	x1, elr_el2
	mrs	x2, spsr_el2
	.endif
	stp	x1, x2, [sp, #S_PC]

	mov	x0, \vec
	mov	x1, sp
	.if \vec < 16
	mrs	x2, esr_el1
	.else
	mrs	x2, esr_el2
	.endif
	bl	do_handle_exception

	ldp	x1, x2, [sp, #S_PC]
	.if \vec < 16
	msr	spsr_el1, x2
	msr	elr_el1, x1
	.else
	msr	spsr_el2, x2
	msr	elr_el2, x1
	.endif

	.if \vec >= 8 && \vec < 16
	ldr	x1, [sp, #S_SP]
	msr	sp_el0, x1
	.endif

	ldr	x30, [sp, #S_LR]

	ldp	x28, x29, [sp, #224]
	ldp	x26, x27, [sp, #208]
	ldp	x24, x25, [sp, #192]
	ldp	x22, x23, [sp, #176]
	ldp	x20, x21, [sp, #160]
	ldp	x18, x19, [sp, #144]
	ldp	x16, x17, [sp, #128]
	ldp	x14, x15, [sp, #112]
	ldp	x12, x13, [sp,  #96]
	ldp	x10, x11, [sp,  #80]
	ldp	 x8,  x9, [sp,  #64]
	ldp	 x6,  x7, [sp,  #48]
	ldp	 x4,  x5, [sp,  #32]
	ldp	 x2,  x3, [sp,  #16]
	ldp	 x0,  x1, [sp], #S_FRAME_SIZE

	eret
.endm

vector_stub	el1t_sync,     0
vector_stub	el1t_irq,      1
vector_stub	el1t_fiq,      2
vector_stub	el1t_error,    3

vector_stub	el1h_sync,     4
vector_stub	el1h_irq,      5
vector_stub	el1h_fiq,      6
vector_stub	el1h_error,    7

vector_stub	el0_sync_64,   8
vector_stub	el0_irq_64,    9
vector_stub	el0_fiq_64,   10
vector_stub	el0_error_64, 11

vector_stub	el0_sync_32,  12
vector_stub	el0_irq_32,   13
vector_stub	el0_fiq_32,   14
vector_stub	el0_error_32, 15

vector_stub	el2t_sync,    16
vector_stub	el2t_irq,     17
vector_stub	el2t_fiq,     18
vector_stub	el2t_error,   19

vector_stub	el2h_sync,    20
vector_stub	el2h_irq,     21
vector_stub	el2h_fiq,     22
vector_stub	el2h_error,   23

vector_stub	el1_sync_64,  24
vector_stub	el1_irq_64,   25
vector_stub	el1_fiq_64,   26
vector_stub	el1_error_64, 27

vector_stub	el1_sync_32,  28
vector_stub	el1_irq_32,   29
vector_stub	el1_fiq_32,   30
vector_stub	el1_error_32, 31

.section .text.ex

.macro ventry, label
.align 7
	b	\label
.endm

.align 11
el1_vector_table:
	ventry	el1t_sync			// Synchronous EL1t
	ventry	el1t_irq			// IRQ EL1t
	ventry	el1t_fiq			// FIQ EL1t
	ventry	el1t_error			// Error EL1t

	ventry	el1h_sync			// Synchronous EL1h
	ventry	el1h_irq			// IRQ EL1h
	ventry	el1h_fiq			// FIQ EL1h
	ventry	el1h_error			// Error EL1h

	ventry	el0_sync_64			// Synchronous 64-bit EL0
	ventry	el0_irq_64			// IRQ 64-bit EL0
	ventry	el0_fiq_64			// FIQ 64-bit EL0
	ventry	el0_error_64			// Error 64-bit EL0

	ventry	el0_sync_32			// Synchronous 32-bit EL0
	ventry	el0_irq_32			// IRQ 32-bit EL0
	ventry	el0_fiq_32			// FIQ 32-bit EL0
	ventry	el0_error_32			// Error 32-bit EL0

.align 11
el2_vector_table:
	ventry	el2t_sync			// Synchronous EL2t
	ventry	el2t_irq			// IRQ EL2t
	ventry	el2t_fiq			// FIQ EL2t
	ventry	el2t_error			// Error EL2t

	ventry	el2h_sync			// Synchronous EL2h
	ventry	el2h_irq			// IRQ EL2h
	ventry	el2h_fiq			// FIQ EL2h
	ventry	el2h_error			// Error EL2h

	ventry	el1_sync_64			// Synchronous 64-bit EL1
	ventry	el1_irq_64			// IRQ 64-bit EL1
	ventry	el1_fiq_64			// FIQ 64-bit EL1
	ventry	el1_error_64			// Error 64-bit EL1

	ventry	el1_sync_32			// Synchronous 32-bit EL1
	ventry	el1_irq_32			// IRQ 32-bit EL1
	ventry	el1_fiq_32			// FIQ 32-bit EL1
	ventry	el1_error_32			// Error 32-bit EL1
