/*
 * Test the framework itself. These tests confirm that setup works.
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"
#include "asm/setup.h"
#include "asm/ptrace.h"
#include "asm/asm-offsets.h"
#include "asm/processor.h"
#include "asm/exception.h"

#define TESTGRP "selftest"

static char testname[64];

static void testname_set(const char *subtest)
{
	strcpy(testname, TESTGRP);
	if (subtest) {
		strcat(testname, "::");
		strcat(testname, subtest);
	}
}

static void assert_args(int num_args, int needed_args)
{
	if (num_args < needed_args) {
		printf("%s: not enough arguments\n", testname);
		abort();
	}
}

static char *split_var(char *s, long *val)
{
	char *p;

	p = strchr(s, '=');
	if (!p)
		return NULL;

	*val = atol(p+1);
	*p = '\0';

	return s;
}

static void check_setup(int argc, char **argv)
{
	int nr_tests = 0, i;
	char *var;
	long val;

	for (i = 0; i < argc; ++i) {

		var = split_var(argv[i], &val);
		if (!var)
			continue;

		if (strcmp(argv[i], "mem") == 0) {

			phys_addr_t memsize =
					memregions[nr_memregions-1].addr
					+ memregions[nr_memregions-1].size
					- PHYS_OFFSET;
			phys_addr_t expected = ((phys_addr_t)val)*1024*1024;

			report("%s[%s]", memsize == expected, testname, "mem");
			++nr_tests;

		} else if (strcmp(argv[i], "smp") == 0) {

			report("%s[%s]", nr_cpus == (int)val, testname, "smp");
			++nr_tests;
		}
	}

	assert_args(nr_tests, 2);
}

static bool check_regs(struct pt_regs *regs)
{
	unsigned i;

	/* exception handlers should always run in svc mode */
	if (current_mode() != SVC_MODE)
		return false;

	for (i = 0; i < ARRAY_SIZE(regs->uregs); ++i) {
		if (regs->uregs[i] != expected_regs.uregs[i])
			return false;
	}

	return true;
}

static bool und_works;
static void und_handler(struct pt_regs *regs)
{
	und_works = check_regs(regs);
}

static bool check_und(void)
{
	install_exception_handler(EXCPTN_UND, und_handler);

	/* issue an instruction to a coprocessor we don't have */
	test_exception("", "mcr p2, 0, r0, c0, c0", "");

	install_exception_handler(EXCPTN_UND, NULL);

	return und_works;
}

static bool svc_works;
static void svc_handler(struct pt_regs *regs)
{
	u32 svc = *(u32 *)(regs->ARM_pc - 4) & 0xffffff;

	if (processor_mode(regs) == SVC_MODE) {
		/*
		 * When issuing an svc from supervisor mode lr_svc will
		 * get corrupted. So before issuing the svc, callers must
		 * always push it on the stack. We pushed it to offset 4.
		 */
		regs->ARM_lr = *(unsigned long *)(regs->ARM_sp + 4);
	}

	svc_works = check_regs(regs) && svc == 123;
}

static bool check_svc(void)
{
	install_exception_handler(EXCPTN_SVC, svc_handler);

	if (current_mode() == SVC_MODE) {
		/*
		 * An svc from supervisor mode will corrupt lr_svc and
		 * spsr_svc. We need to save/restore them separately.
		 */
		test_exception(
			"mrs	r0, spsr\n"
			"push	{ r0,lr }\n",
			"svc	#123\n",
			"pop	{ r0,lr }\n"
			"msr	spsr_cxsf, r0\n"
		);
	} else {
		test_exception("", "svc #123", "");
	}

	install_exception_handler(EXCPTN_SVC, NULL);

	return svc_works;
}

static void check_vectors(void *arg __unused)
{
	report("%s", check_und() && check_svc(), testname);
	exit(report_summary());
}

int main(int argc, char **argv)
{
	testname_set(NULL);
	assert_args(argc, 1);
	testname_set(argv[0]);

	if (strcmp(argv[0], "setup") == 0) {

		check_setup(argc-1, &argv[1]);

	} else if (strcmp(argv[0], "vectors-svc") == 0) {

		check_vectors(NULL);

	} else if (strcmp(argv[0], "vectors-usr") == 0) {

		phys_start_usr(2*PAGE_SIZE, check_vectors, NULL);
	}

	return report_summary();
}
