/*
 * Test ARMv7 (Cortex A-15) VFPv4
 *
 * Copyright (C) 2014, Stanislav Nechutny <stanislav@nechutny.net>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "libcflat.h"
#include "asm/setup.h"
#include "asm/ptrace.h"
#include "asm/asm-offsets.h"
#include "asm/processor.h"

#define TESTGRP "vfc"

#define DOUBLE_PLUS_INF		0x7ff0000000000000
#define DOUBLE_MINUS_INF	0xfff0000000000000
#define DOUBLE_PLUS_NULL	0x0000000000000000
#define DOUBLE_MINUS_NULL	0x8000000000000000

#define DOUBLE_UNION(name)	union { \
								unsigned long long input; \
								double d; \
							} name;

							
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
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438i/CDEDBHDD.html
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
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438i/CDEDBHDD.html
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


static void test_fabsd()
{
	double volatile result = -1.56473475206407319770818276083446107804775238037109375;

	asm volatile(
		"fabsd %[result], %[result]"	"\n\t"
		: [result]"+w" (result)
		: 
	);

	report("%s[%s]", (result == 1.56473475206407319770818276083446107804775238037109375), testname, "-num");


	DOUBLE_UNION(result2);

	result2.input = DOUBLE_MINUS_INF;
	
	asm volatile(
		"fabsd %[result], %[result]"	"\n\t"
		: [result]"+w" (result2.d)
	);

	report("%s[%s]", (result2.input == DOUBLE_PLUS_INF), testname, "-inf");

}


static void test_faddd()
{
	
	double result = 1.328125;

	asm volatile(
		"faddd %[result], %[result], %[num]"	"\n\t"
		: [result]"+w" (result)
		: [num]"w" (-0.0625)
	);
	report("%s[%s]", (result == 1.265625), testname,"num");


	/*
	 * Testing faddd for maximal precision
	 *  1.11000101010011110100011101010110010101110100011000111
	 * +1.11111010001110001010011000011000110001000001010011101
	 */
	
	result = 1.0;
	
	asm volatile(
		"faddd %[result], %[num1], %[num2]"	"\n\t"
		: [result]"+w" (result)
		: [num1]"w" (1.77074094636852741313504111531074158847332000732421875),
		  [num2]"w" (1.97742689232480339800446245135390199720859527587890625)
	);
	report("%s[%s]", (result == 3.748167838693330811139503566664643585681915283203125), testname, "max precision");



	DOUBLE_UNION(data);

	DOUBLE_UNION(result2);

	data.input = DOUBLE_PLUS_INF;
	result2.input = 1ULL;
	
	asm volatile(
		"faddd %[result], %[num], %[num]"	"\n\t"
		: [result]"+w" (result2.d)
		: [num]"w" (data.d)
	);

	report("%s[%s]", ( data.input == result2.input), testname, "(inf)+(inf)");

}




static void test_fcmpd()
{
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
		report("%s", 0, testname);
		return;
	}
	report("%s[%s]", 1, testname,"NF");
	

	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"bgt .found_error_fcmp"		"\n\t"
		:
		: [num1]"w" (1.5),
		  [num2]"w" (2.0)
	);
	report("%s[%s]", 1, testname,"CF");

	
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"beq .found_error_fcmp"		"\n\t"
		:
		: [num1]"w" (-1.5),
		  [num2]"w" (1.5)
	);
	report("%s[%s]", 1, testname,"ZF");


	DOUBLE_UNION(num1);
	DOUBLE_UNION(num2);

	num1.input = DOUBLE_PLUS_NULL;
	num2.input = DOUBLE_MINUS_NULL;
	
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n\t"
		"fmstat"					"\n\t"
		"bne .found_error_fcmp"		"\n\t"
		:
		: [num1]"w" (num1.d),
		  [num2]"w" (num2.d)
	);
	report("%s[%s]", 1, testname,"+0.0,-0.0");
	
}

static void test_fsubd()
{
	double volatile result = 2.75;

	asm volatile(
		"fsubd %[result], %[result], %[num]"	"\n\t"
		: [result]"+w" (result)
		: [num]"w" (0.25)
	);
	report("%s[%s]", (result == 2.5), testname,"num");

	/*
	 * Testing fsubd for maximal precision
	 *  1.11000101110000010100101000011001011010000001010101111
	 * -111.11111010101001100110101110100001011000110011010001
	 */
	
	result = 1.0;
	
	asm volatile(
		"fsubd %[result], %[num1], %[num2]"	"\n\t"
		: [result]"+w" (result)
		: [num1]"w" (1.77248061294820569155916700765374116599559783935546875),
		  [num2]"w" (7.97910187425732519983512247563339769840240478515625)
	);
	report("%s[%s]", (result == -6.20662126130911950827595546797965653240680694580078125), testname,"max precision");

	
	DOUBLE_UNION(data);
	DOUBLE_UNION(result2);

	data.input = DOUBLE_MINUS_INF;
	result2.input = DOUBLE_PLUS_INF;
	
	asm volatile(
		"fsubd %[result], %[num], %[result]"	"\n\t"
		: [result]"+w" (result2.d)
		: [num]"w" (data.d)
	);

	report("%s[%s]", ( data.input == result2.input), testname,"(-inf)-(+inf)");

}


int main(int argc, char **argv)
{
	testname_set(NULL);
	assert_args(argc, 1);
	testname_set(argv[0]);

	enable_vfp();

	if (strcmp(argv[0], "fabsd") == 0) {
		test_fabsd();
	} else if (strcmp(argv[0], "faddd") == 0) {
		test_faddd();
	} else if (strcmp(argv[0], "fcmpd") == 0) {
		test_fcmpd();
	} else if (strcmp(argv[0], "fsubd") == 0) {
		test_fsubd();
	}

	return report_summary();
}
