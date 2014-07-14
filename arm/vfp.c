/*
 * Test ARMv7 (Cortex A-15) VFPv4
 *
 * Copyright (C) 2014, STanislav Nechutny <stanislav@nechutny.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"
#include "asm/setup.h"
#include "asm/ptrace.h"
#include "asm/asm-offsets.h"
#include "asm/processor.h"

#define TESTGRP "vfc"
#define DOUBLE_PLUS_INF  0x7ff00000
#define DOUBLE_MINUS_INF 0xfff00000

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


/**
 *	Enable VFP 
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438c/CDEDBHDD.html
 */
static inline void enable_vfp()
{
	/*
	 * Maybe is enabling wrong. In manual is for mrc and mcr c1, c1
	 * but it throw errors and don't work. This solution with
	 * c1, c0 work correctly.
	 */
	asm volatile(
		"mrc p15, 0, r0, c1, c0, 2"	"\n\t"
		"orr r0, r0, #(3<<10)"		"\n\t"
		"bic r0, r0, #(3<<14)"		"\n\t"
		"mcr p15, 0, r0, c1, c0, 2"	"\n\t"
		"isb"						"\n\t"

		"mov r0, #0x00F00000"		"\n\t"
		"mcr p15, 0, r0, c1, c0, 2"	"\n\t"
		"isb"						"\n\t"

		"mov r3, #0x40000000"		"\n\t"
		"vmsr fpexc, r3"			"\n\t"
	:
	:
	: "r0","r3"
	);
}


/**
 *	Disable VFP 
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438c/CDEDBHDD.html
 */
static inline void disable_vfp()
{
	asm volatile(
		"mov r3, #(~0x40000000)"	"\n\t"
		"vmsr fpexc, r3"			"\n\t"

		"mov r0, #(~0x00F00000)"	"\n\t"
		"mcr p15, 0, r0, c1, c0, 2"	"\n\t"
		"isb"						"\n\t"
		
		"mrc p15, 0, r0, c1, c0, 2"	"\n\t"
		"bic r0, r0, #(3<<10)"		"\n\t"
		"orr r0, r0, #(3<<14)"		"\n\t"
		"mcr p15, 0, r0, c1, c0, 2"	"\n\t"
		"isb"						"\n\t"
	:
	:
	: "r0","r3"
	);
}


static int test_fabsd()
{
	printf("Testing fabsd\n");
	double volatile result = -1.56473475206407319770818276083446107804775238037109375;

	asm volatile(
		"fabsd %[result], %[result]"	"\n\t"
		: [result]"+w" (result)
		: 
	);

	if (result != 1.56473475206407319770818276083446107804775238037109375)
		return 0;

	return 1;
}


static int test_faddd()
{
	
	printf("Testing faddd 1.328125+(-0.0625)\n");
	double result = 1.328125;

	asm volatile(
		"faddd %[result], %[result], %[num]"	"\n\t"
		: [result]"+w" (result)
		: [num]"w" (-0.0625)
	);

	if (result != 1.265625)
		return 0;


	printf(
			"Testing faddd for maximal precision\n"
			" 1.11000101010011110100011101010110010101110100011000111\n"
			"+1.11111010001110001010011000011000110001000001010011101\n"
	);
	
	result = 1.0;
	
	asm volatile(
		"faddd %[result], %[num1], %[num2]"	"\n\t"
		: [result]"+w" (result)
		: [num1]"w" (1.77074094636852741313504111531074158847332000732421875),
		  [num2]"w" (1.97742689232480339800446245135390199720859527587890625)
	);

	if (result != 3.748167838693330811139503566664643585681915283203125)
		return 0;


	printf("Testing faddd (inf)+(inf)\n");
	
	union {
		double inf;
		unsigned long long input;
	} data;

	union {
		double d;
		unsigned long long input;
	} result2;

	data.input = DOUBLE_PLUS_INF;
	result2.input = 1ULL;
	
	asm volatile(
		"faddd %[result], %[num], %[num]"	"\n\t"
		: [result]"+w" (result2.d)
		: [num]"w" (data.inf)
	);

	if( data.input == result2.input)
		return 0;

	return 1;
}




static int test_fcmpd()
{
	printf("Testing fcmpd for correct NF set\n");
	double result = 0.0;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"blt .found_error_fcmp"		"\n\t"
		: [result]"+w" (result) // only for leaving next if untouched by gcc optimalization
		: [num1]"w" (1.5),
		  [num2]"w" (-1.2)
	);

	// jump destination for errors
	if(result == 1.2) {
		asm volatile(".found_error_fcmp:");
		return 0;
	}
	
	
	printf("Testing fcmpd for correct CF set\n");;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"bgt .found_error_fcmp"		"\n\t"
		:
		: [num1]"w" (1.5),
		  [num2]"w" (2.0)
	);


	printf("Testing fcmpd for correct ZF set\n");
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"beq .found_error_fcmp"		"\n\t"
		:
		: [num1]"w" (-1.5),
		  [num2]"w" (1.5)
	);

	return 1;
}

static int test_fsubd()
{
	printf("Testing fsubd 2.75-0.25\n");
	double volatile result = 2.75;

	asm volatile(
		"fsubd %[result], %[result], %[num]"	"\n\t"
		: [result]"+w" (result)
		: [num]"w" (0.25)
	);

	return (result == 2.5);
}



/**
 *	Test floating point instructins.
 * 	Check N, Z, C.. registers and results
 */
static void check_arithmetic()
{
	enable_vfp();

	report("%s", test_fabsd(), testname);
	report("%s", test_faddd(), testname);
	report("%s", test_fcmpd(), testname);
	report("%s", test_fsubd(), testname);
}


/**
 *	Test floating point instructins.
 * 	Check exceptions
 */
static void check_exception()
{
	report("%s", 1, testname);

}

int main(int argc, char **argv)
{
	testname_set(NULL);
	assert_args(argc, 1);
	testname_set(argv[0]);

	if (strcmp(argv[0], "arithmetic") == 0) {
		check_arithmetic();
	} else if (strcmp(argv[0], "exception") == 0) {
		check_exception();
	}

	return report_summary();
}
