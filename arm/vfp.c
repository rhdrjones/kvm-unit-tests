/*
 * Test ARMv7 (Cortex A-15) VFPv4
 *
 * Copyright (C) 2014, Stanislav Nechutny <stanislav@nechutny.net>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include "asm/asm-offsets.h"
#include "asm/processor.h"
#include "asm/exception.h"

#define TESTGRP "vfc"
#define DOUBLE_PLUS_INF		0x7ff0000000000000
#define DOUBLE_MINUS_INF	0xfff0000000000000
#define DOUBLE_PLUS_NULL	0x0000000000000000
#define DOUBLE_MINUS_NULL	0x8000000000000000
#define DOUBLE_PLUS_NAN		0x7ff0000000000020
#define DOUBLE_MINUS_NAN	0xfff0000000000020
#define DOUBLE_UNION(name)			\
	union {					\
		unsigned long long input;	\
		double d;			\
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

int handled;
static void und_handler()
{
	handled = 1;
}

/**
 *	Enable VFP 
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438i/CDEDBHDD.html
 */
static inline void enable_vfp()
{
	asm volatile(
		"mov r0, #0x00F00000"		"\n"
		"mcr p15, 0, r0, c1, c0, 2"	"\n"
		"isb"				"\n"

		"mov r0, #0x40000000"		"\n"
		"vmsr fpexc, r0"
	:
	:
	: "r0"
	);
}

/**
 *	Disable VFP 
 *	http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0438i/CDEDBHDD.html
 */
static inline void disable_vfp()
{
	asm volatile(
		"mov r0, #0"			"\n"
		"vmsr fpexc, r0"		"\n"

		"mov r0, #0"			"\n"
		"mcr p15, 0, r0, c1, c0, 2"	"\n"
		"isb"
	:
	:
	: "r0"
	);
}

static void test_fabsd()
{
	/*
	 * Test maximal precision
	 * IEEE 754 Double is:
	 * 	1 bit for sign
	 * 	11 bits exponent
	 * 	52 bits fract.
	 *
	 * -1.56473475206407319770818276083446107804775238037109375
	 * is
	 * 101111111111100100001001001001110100111010110000011110100101001
	 * so expected is
	 * 001111111111100100001001001001110100111010110000011110100101001
	 * resp. 1.56473475206407319770818276083446107804775238037109375
	 */
	double result = -1.56473475206407319770818276083446107804775238037109375;
	asm volatile(
		"fabsd %[result], %[result]"
		: [result]"+w" (result)
	);
	report("%s[%s]", (result == 1.56473475206407319770818276083446107804775238037109375),
		testname, "-num");
	
	/*
	 * Test -inf
	 * expected is +inf
	 * Inf is stored:
	 * 1 sign bit (+inf/-inf)
	 * 11 exponent bits all set to 1
	 * 52 fract bits all set to 0
	 */
	DOUBLE_UNION(result2);
	result2.input = DOUBLE_MINUS_INF;
	asm volatile(
		"fabsd %[result], %[result]"
		: [result]"+w" (result2.d)
	);
	report("%s[%s]", (result2.input == DOUBLE_PLUS_INF), testname, "-inf");
}

static void test_faddd()
{
	/* Test 2 nums */
	double result = 1.328125;
	asm volatile(
		"faddd %[result], %[result], %[num]"
		: [result]"+w" (result)
		: [num]"w" (-0.0625)
	);
	report("%s[%s]", (result == 1.265625), testname,"num");
	
	/*
	 * Testing faddd for maximal precision
	 *  1.11000101010011110100011101010110010101110100011000111
	 * +1.11111010001110001010011000011000110001000001010011101
	 * 
	 * = 010000000000110111111100001111110110101101111000110110101101100
	 */
	result = 1.0;
	asm volatile(
		"faddd %[result], %[num1], %[num2]"
		: [result]"+w" (result)
		: [num1]"w" (1.77074094636852741313504111531074158847332000732421875),
		  [num2]"w" (1.97742689232480339800446245135390199720859527587890625)
	);
	report("%s[%s]", (result == 3.748167838693330811139503566664643585681915283203125),
		testname, "max precision");

	/*
	 * Test inf+inf
	 * Expected result is +inf as defined in IEEE 754
	 */
	DOUBLE_UNION(data);
	DOUBLE_UNION(result2);
	data.input = DOUBLE_PLUS_INF;
	result2.input = 1ULL;
	asm volatile(
		"faddd %[result], %[num], %[num]"
		: [result]"+w" (result2.d)
		: [num]"w" (data.d)
	);
	report("%s[%s]", (data.input == result2.input), testname, "(inf)+(inf)");


	/*
	 * Test inf+num
	 * Expected result is +inf
	 */
	data.input = DOUBLE_PLUS_INF;
	result2.input = 0x3000000000000500;
	asm volatile(
		"faddd %[result], %[result], %[num]"
		: [result]"+w" (result2.d)
		: [num]"w" (data.d)
	);
	report("%s[%s]", (data.input == result2.input), testname, "inf+num");


	/*
	 * Test (-inf)+(inf)
	 * Canceling two infinities should set IVO
	 */
	data.input = DOUBLE_PLUS_INF;
	result2.input = DOUBLE_MINUS_INF;
	unsigned int ok = 0;
	asm volatile(
		"faddd %[result], %[result], %[num1]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[num2]"			"\n"
		"cmp r0, #1"				"\n"
		"addeq %[ok], %[ok], #1"
		: [result]"+w" (result2.d),
		  [ok]"+r" (ok)
		: [num1]"w" (data.d),
		  [num2]"r" (0x0000ffff)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "(-inf)+(inf)");
}

static void test_fcmpd()
{
	/*
	 * Test NF
	 * Expected is NF=0
	 */
	int result = 1;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n"
		"fmstat"			"\n"
		"submi %[result], %[result], #1"
		: [result]"+r" (result)
		: [num1]"w" (1.5),
		  [num2]"w" (-1.2)
	);
	report("%s[%s]", result, testname, "NF");

	/*
	 * Test CF
	 * Expected is CF=0
	 */
	result = 1;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n"
		"fmstat"			"\n"
		"subcs %[result], %[result], #1"
		: [result]"+r" (result)
		: [num1]"w" (1.5),
		  [num2]"w" (2.0)
	);
	report("%s[%s]", result, testname, "CF");

	/*
	 * Test ZF
	 * Expected is ZF=0
	 */
	result = 1;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n"
		"fmstat"			"\n"
		"subeq %[result], %[result], #1"
		: [result]"+r" (result)
		: [num1]"w" (-1.5),
		  [num2]"w" (1.5)
	);
	report("%s[%s]", 1, testname, "ZF");

	/*
	 * Test +null and -null
	 * 
	 * The IEEE 754 standard specifies equality if one is +0 and the
	 * other is -0.
	 */
	result = 1;
	DOUBLE_UNION(num1);
	DOUBLE_UNION(num2);
	num1.input = DOUBLE_PLUS_NULL;
	num2.input = DOUBLE_MINUS_NULL;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n"
		"fmstat"			"\n"
		"subne %[result], %[result], #1"
		: [result]"+r" (result)
		: [num1]"w" (num1.d),
		  [num2]"w" (num2.d)
	);
	report("%s[%s]", result, testname, "+0.0,-0.0");

	/*
	 * Test (nan), (nan)
	 * Comparing two Not a number values should set IVO bit in FPSCR
	 */
	num1.input = DOUBLE_PLUS_NAN;
	num2.input = DOUBLE_MINUS_NAN;
	unsigned int ok = 0;
	asm volatile(
		"fcmpd %[num1], %[num2]"	"\n"
		"fmrx r0, fpscr"		"\n"
		"and r0, r0, %[mask]"		"\n"
		"cmp r0, #1"			"\n"
		"addeq %[ok], %[ok], #1"
		
		: [ok]"+r" (ok)
		: [num1]"w" (num1.d),
		  [num2]"w" (num2.d),
		  [mask]"r" (0x0000ffff)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "NaN");
}

static void test_fcpyd()
{
	/*
	 * Test num to num
	 * Copy double value from one register to second.
	 * Expected result is same value in num2 as num1
	 */
	DOUBLE_UNION(num1);
	DOUBLE_UNION(num2);
	num1.d = 1.75;
	num2.d = -1.25;
	asm volatile(
		"fcpyd %[num1], %[num2]"
		: [num1]"+w" (num1.d)
		: [num2]"w" (num2.d)
	);
	report("%s[%s]", (num1.input == num2.input), testname, "num");

	/*
	 * Test num to same register
	 * Copy value from register to same register.
	 * Expected result is -1.5
	 */
	num1.d = -1.5;
	num2.d = -1.5;
	asm volatile(
		"fcpyd %[num1], %[num1]"
		: [num1]"+w" (num1.d)
	);
	report("%s[%s]", (num1.input == num2.input), testname, "same reg");

	/*
	 * Test copy NaN
	 * Try copy +NaN from ome register to second.
	 * Expected result is same NaN in second register as in first.
	 */
	num1.d = -1;
	num2.input = DOUBLE_PLUS_NAN;
	asm volatile(
		"fcpyd %[num1], %[num2]"
		: [num1]"+w" (num1.d)
		: [num2]"w" (num2.d)
	);
	report("%s[%s]", (num1.input == DOUBLE_PLUS_NAN), testname, "NaN");
}

,static void test_fdivd()
{
	/*
	 * Test (num)/(-num)
	 * Divide positive number by negative.
	 * Expected result is coresponding negative number.
	 */
	DOUBLE_UNION(num1);
	DOUBLE_UNION(num2);
	num1.d = 0.75;
	num2.d = -0.9375;
	asm volatile(
		"fdivd %[num1], %[num1], %[num2]"
		: [num1]"+w" (num1.d)
		: [num2]"w" (num2.d)
	);
	report("%s[%s]", (num1.d == -0.8), testname, "(num)/(-num)");

	/*
	 * Test (num)/(num)
	 * Divivde two positive numbers.
	 * Expected result is coresponding positive number.
	 */
	num1.d = 0.875;
	num2.d = 0.125;
	asm volatile(
		"fdivd %[num1], %[num1], %[num2]"
		: [num1]"+w" (num1.d)
		: [num2]"w" (num2.d)
	);
	report("%s[%s]", (num1.d == 7.0), testname, "(num)/(-num)");

	/*
	 * Test 0/0
	 * Divide by zero.
	 * Should set IVO and INX bits
	 */
	num1.d = 0.0;
	unsigned int ok = 0;
	asm volatile(
		"fdivd %[result], %[result], %[result]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[mask]"			"\n"
		"cmp r0, #17"				"\n"
		"addeq %[ok], %[ok], r0"
		
		: [result]"+w" (num1.d),
		  [ok]"+r" (ok)
		: [mask]"r" (0x0000ffff)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "0/0");

	/*
	 * Test 0/-0
	 * Divide positive zero by negative zero
	 * Should set IVO and INX bits
	 */
	num1.input = DOUBLE_PLUS_NULL;
	num2.input = DOUBLE_MINUS_NULL;
	ok = 0;
	asm volatile(
		"fdivd %[result], %[result], %[num]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[mask]"			"\n"
		"cmp r0, #17"				"\n"
		"addeq %[ok], %[ok], #1"
		
		: [result]"+w" (num1.d),
		  [ok]"+r" (ok)
		: [mask]"r" (0x0000ffff),
		  [num]"w" (num2.d)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "0/-0");

	/*
	 * Test 1.25/-0
	 * Divide number by negative zero.
	 * Should set IVO, DVZ and INX bits
	 * Expected result is negative infinity
	 */
	num1.d = 1.25;
	num2.input = DOUBLE_MINUS_NULL;
	ok = 0;
	asm volatile(
		"fdivd %[result], %[result], %[num]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[mask]"			"\n"
		"cmp r0, #19"				"\n"
		"addeq %[ok], %[ok], #1"
		
		: [result]"+w" (num1.d),
		  [ok]"+r" (ok)
		: [mask]"r" (0x0000ffff),
		  [num]"w" (num2.d)
		: "r0"
	);
	report("%s[%s]", (ok && num1.input == DOUBLE_MINUS_INF), testname, "num/-0");

	/*
	 * Test -inf/inf
	 * Divide -infinity by +infinity
	 * Should set IVO bit
	 * 
	 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	 * Need recheck, because on RaspberryPi with VFPv3 it set only IVO bit
	 * , but on KVM-ARM and QEMU it set also DVZ and INX bit.
	 * IEEE 754 don't specify this case
	 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	 */
	num1.input = DOUBLE_MINUS_INF;
	num2.input = DOUBLE_PLUS_INF;
	ok = 0;
	asm volatile(
		"fdivd %[result], %[result], %[num]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[mask]"			"\n"
		"cmp r0, #1"				"\n"
		"addeq %[ok], %[ok], #1"
		
		: [result]"+w" (num1.d),
		  [ok]"+r" (ok)
		: [mask]"r" (0x0000ffff),
		  [num]"w" (num2.d)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "(-inf)/(inf)");
}

static void test_fnegd()
{
	/*
	 * -Number
	 * Negate number, expected is 1.375 for -1.375
	 */
	DOUBLE_UNION(num1);
	DOUBLE_UNION(num2);
	num1.d = -1.375;
	num2.d = 1.375;
	asm volatile(
		"fnegd %[result], %[result]"
		: [result]"+w" (num1.d)
	);
	report("%s[%s]", (num1.input == num2.input), testname, "-num");

	/*
	 * +Number
	 * Negate number with maximal fract bit range
	 * 001111111111111111010110110101101111100000011011000011101001101
	 * Expected result is
	 * 101111111111111111010110110101101111100000011011000011101001101
	 */
	num1.d = 1.98995110431943678097610472832457162439823150634765625;
	num2.d = -1.98995110431943678097610472832457162439823150634765625;
	asm volatile(
		"fnegd %[result], %[result]"
		: [result]"+w" (num1.d)
	);
	report("%s[%s]", (num1.input == num2.input), testname, "+num");

	/*
	 * -Inf
	 * Negate -infinity to +infinity
	 */
	num1.input = DOUBLE_MINUS_INF;
	num2.input = DOUBLE_PLUS_INF;
	asm volatile(
		"fnegd %[result], %[result]"
		: [result]"+w" (num1.d)
	);
	report("%s[%s]", (num1.input == num2.input), testname, "-inf");

	/*
	 * +NaN and check status register
	 * Try negate +NaN. Result should be same NaN with changed sign bit
	 * and FPSCR bits 0-15 should be 0
	 */
	num1.input = DOUBLE_MINUS_NAN;
	num2.input = DOUBLE_PLUS_NAN;
	unsigned int ok = 0;
	asm volatile(
		"fnegd %[result], %[result]"		"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[mask]"			"\n"
		"cmp r0, #0"				"\n"
		"addeq %[ok], %[ok], #1"
		
		: [result]"+w" (num1.d),
		  [ok]"+r" (ok)
		: [mask]"r" (0x0000ffff)
		: "r0"
	);
	report("%s[%s]", (num1.input == num2.input && ok == 1), testname, "+NaN");
}

static void test_fsubd()
{
	/*
	 * Test two numbers
	 * Substract one nomber from another.
	 * Expected result is 2.5
	 */
	double volatile result = 2.75;
	asm volatile(
		"fsubd %[result], %[result], %[num]"
		: [result]"+w" (result)
		: [num]"w" (0.25)
	);
	report("%s[%s]", (result == 2.5), testname, "num");

	/*
	 * Testing fsubd for maximal precision
	 *  1.11000101110000010100101000011001011010000001010101111
	 * -111.111110101010011001101011101000010110001100110100010
	 * Expecting result is
	 * 110000000001100011010011100101001000011000011111111011000111101
	 */
	result = 1.0;
	asm volatile(
		"fsubd %[result], %[num1], %[num2]"
		: [result]"+w" (result)
		: [num1]"w" (1.77248061294820569155916700765374116599559783935546875),
		  [num2]"w" (7.97910187425732519983512247563339769840240478515625)
	);
	report("%s[%s]", (result == -6.20662126130911950827595546797965653240680694580078125),
		testname, "max precision");

	/*
	 * Test (-inf)-(+inf)
	 * Substracting positive infinity from negative infinity
	 * Expected result is negative inifnity
	 */
	DOUBLE_UNION(data);
	DOUBLE_UNION(result2);
	data.input = DOUBLE_MINUS_INF;
	result2.input = DOUBLE_PLUS_INF;
	asm volatile(
		"fsubd %[result], %[num], %[result]"
		: [result]"+w" (result2.d)
		: [num]"w" (data.d)
	);
	report("%s[%s]", (data.input == result2.input), testname, "(-inf)-(+inf)");

	/*
	 * Test (inf)-(nan)
	 * Substracting NaN from infinity should set IVO bit to 1
	 */
	data.input = DOUBLE_PLUS_NAN;
	result2.input = DOUBLE_PLUS_INF;
	unsigned int ok = 0;
	asm volatile(
		"faddd %[result], %[result], %[num1]"	"\n"
		"fmrx r0, fpscr"			"\n"
		"and r0, r0, %[num2]"			"\n"
		"cmp r0, #1"				"\n"
		"addeq %[ok], %[ok], #1"
		
		: [result]"+w" (result2.d),
		  [ok]"+r" (ok)
		: [num1]"w" (data.d),
		  [num2]"r" (0x0000ffff)
		: "r0"
	);
	report("%s[%s]", (ok), testname, "(inf)-(NaN)");
}

static void test_disabled(void)
{
	disable_vfp();
	
	/* Fabsd with disabled VFP */
	handled = 0;
	install_exception_handler(EXCPTN_UND, und_handler);
	test_exception("", "fabsd d0, d0", "");
	report("%s[%s]", handled, testname, "fabsd");
	
	/* Fdivd with disabled VFP */
	handled = 0;
	install_exception_handler(EXCPTN_UND, und_handler);
	test_exception("", "fdivd d0, d1, d2", "");
	report("%s[%s]", handled, testname, "fdivd");

	/* Fcmpd with disabled VFP */
	handled = 0;
	install_exception_handler(EXCPTN_UND, und_handler);
	test_exception("", "fcmpd d0, d1", "");
	report("%s[%s]", handled, testname, "fcmpd");
	
	install_exception_handler(EXCPTN_UND, NULL);
}

int main(int argc, char **argv)
{
	testname_set(NULL);
	assert_args(argc, 1);
	testname_set(argv[0]);
	enable_vfp();

	if (strcmp(argv[0], "disabled") == 0)
		test_disabled();
	else if (strcmp(argv[0], "fabsd") == 0)
		test_fabsd();
	else if (strcmp(argv[0], "faddd") == 0)
		test_faddd();
	else if (strcmp(argv[0], "fcmpd") == 0)
		test_fcmpd();
	else if (strcmp(argv[0], "fcpyd") == 0)
		test_fcpyd();
	else if (strcmp(argv[0], "fdivd") == 0)
		test_fdivd();
	else if (strcmp(argv[0], "fnegd") == 0)
		test_fnegd();
	else if (strcmp(argv[0], "fsubd") == 0)
		test_fsubd();
	else 
		printf("Unkown test\n");

	return report_summary();
}
