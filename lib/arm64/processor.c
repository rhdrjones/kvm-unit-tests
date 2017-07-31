/*
 * processor control and status functions
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <libcflat.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

static const char *vector_names[] = {
	"el1t_sync",
	"el1t_irq",
	"el1t_fiq",
	"el1t_error",
	"el1h_sync",
	"el1h_irq",
	"el1h_fiq",
	"el1h_error",
	"el0_sync_64",
	"el0_irq_64",
	"el0_fiq_64",
	"el0_error_64",
	"el0_sync_32",
	"el0_irq_32",
	"el0_fiq_32",
	"el0_error_32",
	"el2t_sync",
	"el2t_irq",
	"el2t_fiq",
	"el2t_error",
	"el2h_sync",
	"el2h_irq",
	"el2h_fiq",
	"el2h_error",
	"el1_sync_64",
	"el1_irq_64",
	"el1_fiq_64",
	"el1_error_64",
	"el1_sync_32",
	"el1_irq_32",
	"eL1_fiq_32",
	"el1_error_32",
};

static const char *ec_names[EC_MAX] = {
	[ESR_ELx_EC_UNKNOWN]		= "unknown",
	[ESR_ELx_EC_WFx]		= "wfx",
	[0x02]				= "Unallocated_0x02",
	[ESR_ELx_EC_CP15_32]		= "cp15_32",
	[ESR_ELx_EC_CP15_64]		= "cp15_64",
	[ESR_ELx_EC_CP14_MR]		= "cp14_mr",
	[ESR_ELx_EC_CP14_LS]		= "cp14_ls",
	[ESR_ELx_EC_FP_ASIMD]		= "fp_asimd",
	[ESR_ELx_EC_CP10_ID]		= "cp10_id",
	[0x09]				= "Unallocated_0x09",
	[0x0a]				= "Unallocated_0x0a",
	[0x0b]				= "Unallocated_0x0b",
	[ESR_ELx_EC_CP14_64]		= "cp14_64",
	[0x0d]				= "Unallocated_0x0d",
	[ESR_ELx_EC_ILL]		= "ill",
	[0x0f]				= "Unallocated_0x0f",
	[0x10]				= "Unallocated_0x10",
	[ESR_ELx_EC_SVC32]		= "svc32",
	[ESR_ELx_EC_HVC32]		= "hvc32",
	[ESR_ELx_EC_SMC32]		= "smc32",
	[0x14]				= "Unallocated_0x14",
	[ESR_ELx_EC_SVC64]		= "svc64",
	[ESR_ELx_EC_HVC64]		= "hvc64",
	[ESR_ELx_EC_SMC64]		= "smc64",
	[ESR_ELx_EC_SYS64]		= "sys64",
	[0x19]				= "Unallocated_0x19",
	[0x1a]				= "Unallocated_0x1a",
	[0x1b]				= "Unallocated_0x1b",
	[0x1c]				= "Unallocated_0x1c",
	[0x1d]				= "Unallocated_0x1d",
	[0x1e]				= "Unallocated_0x1e",
	[ESR_ELx_EC_IMP_DEF]		= "implementation defined",
	[ESR_ELx_EC_IABT_LOW]		= "iabt lower EL",
	[ESR_ELx_EC_IABT_CUR]		= "iabt current EL",
	[ESR_ELx_EC_PC_ALIGN]		= "PC alignment",
	[0x23]				= "Unallocated_0x23",
	[ESR_ELx_EC_DABT_LOW]		= "dabt lower EL",
	[ESR_ELx_EC_DABT_CUR]		= "dabt current EL",
	[ESR_ELx_EC_SP_ALIGN]		= "SP alignment",
	[0x27]				= "Unallocated_0x27",
	[ESR_ELx_EC_FP_EXC32]		= "fp_exc32",
	[0x29]				= "Unallocated_0x29",
	[0x2a]				= "Unallocated_0x2a",
	[0x2b]				= "Unallocated_0x2b",
	[ESR_ELx_EC_FP_EXC64]		= "fp_exc64",
	[0x2d]				= "Unallocated_0x2d",
	[0x2e]				= "Unallocated_0x2e",
	[ESR_ELx_EC_SERROR]		= "SError",
	[ESR_ELx_EC_BREAKPT_LOW]	= "breakpt lower EL",
	[ESR_ELx_EC_BREAKPT_CUR]	= "breakpt current EL",
	[ESR_ELx_EC_SOFTSTP_LOW]	= "softstp lower EL",
	[ESR_ELx_EC_SOFTSTP_CUR]	= "softstp current EL",
	[ESR_ELx_EC_WATCHPT_LOW]	= "watchpt lower EL",
	[ESR_ELx_EC_WATCHPT_CUR]	= "watchpt current EL",
	[0x36]				= "Unallocated_0x36",
	[0x37]				= "Unallocated_0x37",
	[ESR_ELx_EC_BKPT32]		= "bkpt32",
	[0x39]				= "Unallocated_0x39",
	[ESR_ELx_EC_VECTOR32]		= "vector32",
	[0x3b]				= "Unallocated_0x3b",
	[ESR_ELx_EC_BRK64]		= "bkpt64",
	[0x3d]				= "Unallocated_0x3d",
	[0x3e]				= "Unallocated_0x3e",
	[0x3f]				= "Unallocated_0x3f",
};

void show_regs(struct pt_regs *regs)
{
	int i;

	printf("pc : [<%016lx>] lr : [<%016lx>] pstate: %08lx\n",
			regs->pc, regs->regs[30], regs->pstate);
	printf("sp : %016lx\n", regs->sp);

	for (i = 29; i >= 0; --i) {
		printf("x%-2d: %016lx ", i, regs->regs[i]);
		if (i % 2 == 0)
			printf("\n");
	}
	printf("\n");
}

bool get_far(unsigned int esr, unsigned long *far)
{
	unsigned int ec = esr >> ESR_ELx_EC_SHIFT;

	if (current_level() == CurrentEL_EL2)
		asm volatile("mrs %0, far_el2": "=r" (*far));
	else
		asm volatile("mrs %0, far_el1": "=r" (*far));

	switch (ec) {
	case ESR_ELx_EC_IABT_LOW:
	case ESR_ELx_EC_IABT_CUR:
	case ESR_ELx_EC_PC_ALIGN:
	case ESR_ELx_EC_DABT_LOW:
	case ESR_ELx_EC_DABT_CUR:
	case ESR_ELx_EC_WATCHPT_LOW:
	case ESR_ELx_EC_WATCHPT_CUR:
		if ((esr & 0x3f /* DFSC */) != 0x10
				|| !(esr & 0x400 /* FnV */))
			return true;
	}
	return false;
}

static void bad_exception(enum vector v, struct pt_regs *regs,
			  unsigned int esr, bool esr_valid, bool bad_vector)
{
	unsigned long far;
	bool far_valid = get_far(esr, &far);
	unsigned int ec = esr >> ESR_ELx_EC_SHIFT;

	printf("CurrentEL: EL%d\n", current_level() == CurrentEL_EL1 ? 1 : 2);

	if (bad_vector) {
		if (v < VECTOR_MAX)
			printf("Unhandled vector %d (%s)\n", v,
					vector_names[v]);
		else
			printf("Got bad vector=%d\n", v);
	} else if (esr_valid) {
		if (ec_names[ec])
			printf("Unhandled exception ec=%#x (%s)\n", ec,
					ec_names[ec]);
		else
			printf("Got bad ec=%#x\n", ec);
	}

	printf("Vector: %d (%s)\n", v, vector_names[v]);
	printf("ESR_ELx: %8s%08x, ec=%#x (%s)\n", "", esr, ec, ec_names[ec]);
	printf("FAR_ELx: %016lx (%svalid)\n", far, far_valid ? "" : "not ");
	printf("Exception frame registers:\n");
	show_regs(regs);
	abort();
}

void install_exception_handler(enum vector v, unsigned int ec, exception_fn fn)
{
	struct thread_info *ti = current_thread_info();

	if (v < VECTOR_MAX && ec < EC_MAX)
		ti->exception_handlers[v][ec] = fn;
}

void install_irq_handler(enum vector v, irq_handler_fn fn)
{
	struct thread_info *ti = current_thread_info();

	if (v < VECTOR_MAX)
		ti->exception_handlers[v][0] = (exception_fn)fn;
}

void default_vector_sync_handler(enum vector v, struct pt_regs *regs,
				 unsigned int esr)
{
	struct thread_info *ti = thread_info_sp(regs->sp);
	unsigned int ec = esr >> ESR_ELx_EC_SHIFT;

	if (ti->flags & TIF_USER_MODE) {
		if (ec < EC_MAX && ti->exception_handlers[v][ec]) {
			ti->exception_handlers[v][ec](regs, esr);
			return;
		}
		ti = current_thread_info();
	}

	if (ec < EC_MAX && ti->exception_handlers[v][ec])
		ti->exception_handlers[v][ec](regs, esr);
	else
		bad_exception(v, regs, esr, true, false);
}

void default_vector_irq_handler(enum vector v, struct pt_regs *regs,
				unsigned int esr)
{
	struct thread_info *ti = thread_info_sp(regs->sp);
	irq_handler_fn irq_handler =
		(irq_handler_fn)ti->exception_handlers[v][0];

	if (ti->flags & TIF_USER_MODE) {
		if (irq_handler) {
			irq_handler(regs);
			return;
		}
		ti = current_thread_info();
		irq_handler = (irq_handler_fn)ti->exception_handlers[v][0];
	}

	if (irq_handler)
		irq_handler(regs);
	else
		bad_exception(v, regs, esr, false, false);
}

void vector_handlers_default_init(vector_fn *handlers)
{
	handlers[EL1H_SYNC]	= default_vector_sync_handler;
	handlers[EL1H_IRQ]	= default_vector_irq_handler;
	handlers[EL0_SYNC_64]	= default_vector_sync_handler;
	handlers[EL0_IRQ_64]	= default_vector_irq_handler;
	handlers[EL2H_SYNC]	= default_vector_sync_handler;
	handlers[EL2H_IRQ]	= default_vector_irq_handler;
}

/* Needed to compile with -Wmissing-prototypes */
void do_handle_exception(enum vector v, struct pt_regs *regs, unsigned int esr);

void do_handle_exception(enum vector v, struct pt_regs *regs, unsigned int esr)
{
	struct thread_info *ti = thread_info_sp(regs->sp);

	if (ti->flags & TIF_USER_MODE) {
		if (v < VECTOR_MAX && ti->vector_handlers[v]) {
			ti->vector_handlers[v](v, regs, esr);
			return;
		}
		ti = current_thread_info();
	}

	if (v < VECTOR_MAX && ti->vector_handlers[v])
		ti->vector_handlers[v](v, regs, esr);
	else
		bad_exception(v, regs, esr, true, true);
}

void install_vector_handler(enum vector v, vector_fn fn)
{
	struct thread_info *ti = current_thread_info();

	if (v < VECTOR_MAX)
		ti->vector_handlers[v] = fn;
}

static void __thread_info_init(struct thread_info *ti, unsigned int flags)
{
	memset(ti, 0, sizeof(struct thread_info));
	ti->cpu = mpidr_to_cpu(get_mpidr());
	ti->flags = flags;
}

void thread_info_init(struct thread_info *ti, unsigned int flags)
{
	__thread_info_init(ti, flags);
	vector_handlers_default_init(ti->vector_handlers);
}

void start_usr(void (*func)(void *arg), void *arg, unsigned long sp_usr)
{
	sp_usr &= (~15UL); /* stack ptr needs 16-byte alignment */

	__thread_info_init(thread_info_sp(sp_usr), TIF_USER_MODE);
	thread_info_sp(sp_usr)->pgtable = current_thread_info()->pgtable;

	if (current_level() == CurrentEL_EL1) {
		asm volatile(
		"mov	x0, %0\n"
		"msr	sp_el0, %1\n"
		"msr	elr_el1, %2\n"
		"mov	x3, xzr\n"	/* clear and "set" PSR_MODE_EL0t */
		"msr	spsr_el1, x3\n"
		"eret\n"
		:: "r" (arg), "r" (sp_usr), "r" (func) : "x0", "x3");
	} else {
		asm volatile(
		"mov	x0, %0\n"
		"msr	sp_el0, %1\n"
		"msr	elr_el2, %2\n"
		"mov	x3, xzr\n"	/* clear and "set" PSR_MODE_EL0t */
		"msr	spsr_el2, x3\n"
		"eret\n"
		:: "r" (arg), "r" (sp_usr), "r" (func) : "x0", "x3");
	}
}

bool is_user(void)
{
	return current_thread_info()->flags & TIF_USER_MODE;
}
