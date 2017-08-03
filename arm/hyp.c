/*
 * hyp-mode tests
 *
 * Copyright (C) 2017, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <libcflat.h>
#include <asm/processor.h>
#include <asm/smp.h>

static cpumask_t started_el2;
static cpumask_t eret_works;
static cpumask_t hvc_works;

static void drop_to_el1(void *data)
{
	int cpu = smp_processor_id();

	asm volatile(
	"	mov	x0, #(1 << 31)\n"	/* HCR_RW: 64-bit EL1 */
	"	msr	hcr_el2, x0\n"
	"	mov	x0, sp\n"
	"	msr	sp_el1, x0\n"
	"	adr	x0, 1f\n"
	"	msr	elr_el2, x0\n"
	"	isb\n"
	"	mov	x0, #" xstr(PSR_MODE_EL1h) "\n"
	"	orr	x0, x0, #" xstr(PSR_F_BIT) "\n"
	"	orr	x0, x0, #" xstr(PSR_I_BIT) "\n"
	"	orr	x0, x0, #" xstr(PSR_A_BIT) "\n"
	"	orr	x0, x0, #" xstr(PSR_D_BIT) "\n"
	"	msr	spsr_el2, x0\n"
	"	eret\n"
	"1:\n" : : : "x0");

	if (current_level() == CurrentEL_EL1)
		cpumask_set_cpu(cpu, &eret_works);
	else
		report_info("CPU%d did not drop to EL1", cpu);
}

static void hvc_handler(struct pt_regs *regs, unsigned int esr)
{
	if ((esr & 0xffff) == 123 && regs->regs[0] == 456)
		cpumask_set_cpu(smp_processor_id(), &hvc_works);
}

static void test_hvc(void *data)
{
	int v = current_level() == CurrentEL_EL2 ? EL2H_SYNC : EL1_SYNC_64;

	cpumask_clear_cpu(smp_processor_id(), &hvc_works);

	install_exception_handler(v, ESR_ELx_EC_HVC64, hvc_handler);
	asm volatile("mov x0, #456; hvc #123");
	install_exception_handler(v, ESR_ELx_EC_HVC64, NULL);
}

static void check_el2(void *data)
{
	int cpu = smp_processor_id();

	if (current_level() == CurrentEL_EL2)
		cpumask_set_cpu(cpu, &started_el2);
	else
		report_info("CPU%d not started in EL2", cpu);
}

int main(void)
{
	int i = 2;

	report_prefix_push("hyp");

	report_info("NR_CPUS=%d", nr_cpus);

	on_cpus(check_el2, NULL);

	if (!cpumask_test_cpu(0, &started_el2)) {
		report_skip("tests must be started in EL2");
		return report_summary();
	}

	report("started EL2", cpumask_full(&started_el2));

	for (; i >= 1; --i) {
		if (i == 1) {
			on_cpus(drop_to_el1, NULL);
			report("eret to EL1", cpumask_full(&eret_works));
			if (!cpumask_full(&eret_works)) {
				report_info("skipping EL1 tests");
				continue;
			}
		}
		report_prefix_pushf("el%d", i);
		on_cpus(test_hvc, NULL);
		report("hvc", cpumask_full(&hvc_works));
		report_prefix_pop();
	}

	return report_summary();
}
