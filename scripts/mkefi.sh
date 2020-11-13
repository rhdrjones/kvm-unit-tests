#!/bin/bash

if [ ! -f config.mak ]; then
	echo "run ./configure --target-efi && make first. See ./configure -h"
	exit 1
fi
source config.mak
source scripts/arch-run.bash
source scripts/common.bash

EFI_TESTS='efi-tests'
EFI_RUN="$EFI_TESTS/run.nsh"
ENV_GUID='97ef3e03-7329-4a6a-b9ba-6c1fdcc5f823'

set_env()
{
	local env val

	[ -f "$KVM_UNIT_TESTS_ENV" ] && export KVM_UNIT_TESTS_ENV_OLD="$KVM_UNIT_TESTS_ENV"
	export KVM_UNIT_TESTS_ENV=$(mktemp)
	env_params
	env_file
	env_errata || exit 2

	while read -r line; do
		env=${line%=*}
		val=${line#*=}
		echo "setvar $env -guid $ENV_GUID -rt =L\"$val\"" >> $EFI_TESTS/startup.nsh
	done < $KVM_UNIT_TESTS_ENV

	rm -f $KVM_UNIT_TESTS_ENV
}

select_test_fn()
{
	local testname=$1
	local kernel=$4
	local opts=$5

	kernel=$(basename $kernel)
	kernel=${kernel%flat}
	kernel=${kernel}efi

	if [ ! -f "$TEST_DIR/$kernel" ]; then
		echo "Can't find $TEST_DIR/$kernel" >&2
		exit 2
	else
		cp -f "$TEST_DIR/$kernel" $EFI_TESTS
	fi

	if [[ $opts =~ append ]]; then
		opts=${opts##*append \'}
		opts=${opts%%\'*}
	fi

	echo "if %1 == $testname then" >> $EFI_RUN
	echo "  echo $kernel $opts" >> $EFI_RUN
	echo "  $kernel $opts" >> $EFI_RUN
	echo "  goto done" >> $EFI_RUN
	echo "endif" >> $EFI_RUN
}

list_test_fn()
{
	local testname=$1
	echo "echo $testname" >> $EFI_RUN
}

#
# Generate run.nsh
#
mkdir -p $EFI_TESTS
echo > $EFI_RUN
echo "@echo -off" >> $EFI_RUN
echo >> $EFI_RUN
for_each_unittest arm/unittests.cfg select_test_fn
echo >> $EFI_RUN
for_each_unittest arm/unittests.cfg list_test_fn
echo >> $EFI_RUN
echo ":done" >> $EFI_RUN

if [ ! -f "$EFI_TESTS/startup.nsh" ]; then
	echo > "$EFI_TESTS/startup.nsh"
	echo "@echo -off" >> "$EFI_TESTS/startup.nsh"
	echo >> "$EFI_TESTS/startup.nsh"
	set_env
	echo >> "$EFI_TESTS/startup.nsh"
	echo "fs0:" >> "$EFI_TESTS/startup.nsh"
fi

[ "$EFI_SMP" ] && EFI_SMP="-smp $EFI_SMP"

if [ "$ARCH" = "arm64" ] && [ ! -f "$EFI_TESTS/dtb" ]; then
	DTBFILE="$EFI_TESTS/dtb" arm/run $EFI_SMP
fi

if [ -f "$TEST_DIR/cmd.efi" ]; then
	cp -f "$TEST_DIR/cmd.efi" $EFI_TESTS
fi
